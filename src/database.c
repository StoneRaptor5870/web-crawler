#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>
#include "../include/crawler.h"
#include "../include/database.h"

CrawlerStats stats = {0};
CrawlerDB crawler_db = {0};

int init_database(void)
{
    int rc = sqlite3_open(DB_NAME, &crawler_db.db);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(crawler_db.db));
        return 0;
    }

    // Enable WAL mode for better performance
    if (ENABLE_WAL_MODE)
    {
        char *err_msg = 0;
        rc = sqlite3_exec(crawler_db.db, "PRAGMA journal_mode=WAL;", 0, 0, &err_msg);
        if (rc != SQLITE_OK)
        {
            fprintf(stderr, "Failed to enable WAL mode: %s\n", err_msg);
            sqlite3_free(err_msg);
        }
    }

    // Create tables
    const char *create_tables_sql =
        "CREATE TABLE IF NOT EXISTS crawl_sessions ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    start_url TEXT NOT NULL,"
        "    start_time INTEGER NOT NULL,"
        "    end_time INTEGER,"
        "    status TEXT DEFAULT 'running'"
        ");"

        "CREATE TABLE IF NOT EXISTS pages ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    session_id INTEGER,"
        "    url TEXT NOT NULL,"
        "    content TEXT,"
        "    content_length INTEGER,"
        "    response_code INTEGER,"
        "    crawl_time INTEGER,"
        "    depth INTEGER,"
        "    FOREIGN KEY(session_id) REFERENCES crawl_sessions(id),"
        "    UNIQUE(session_id, url)"
        ");"

        "CREATE TABLE IF NOT EXISTS url_queue ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    session_id INTEGER,"
        "    url TEXT NOT NULL,"
        "    depth INTEGER,"
        "    status TEXT DEFAULT 'pending',"
        "    added_time INTEGER,"
        "    crawled_time INTEGER,"
        "    error_count INTEGER DEFAULT 0,"
        "    FOREIGN KEY(session_id) REFERENCES crawl_sessions(id),"
        "    UNIQUE(session_id, url)"
        ");"

        "CREATE TABLE IF NOT EXISTS extracted_links ("
        "    id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "    session_id INTEGER,"
        "    source_url TEXT NOT NULL,"
        "    target_url TEXT NOT NULL,"
        "    discovered_time INTEGER,"
        "    FOREIGN KEY(session_id) REFERENCES crawl_sessions(id)"
        ");"

        "CREATE INDEX IF NOT EXISTS idx_url_queue_status ON url_queue(session_id, status);"
        "CREATE INDEX IF NOT EXISTS idx_pages_url ON pages(session_id, url);"
        "CREATE INDEX IF NOT EXISTS idx_extracted_links_source ON extracted_links(session_id, source_url);";

    char *err_msg = 0;
    rc = sqlite3_exec(crawler_db.db, create_tables_sql, 0, 0, &err_msg);
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        return 0;
    }

    // Prepare statements
    const char *insert_page_sql =
        "INSERT OR REPLACE INTO pages (session_id, url, content, content_length, response_code, crawl_time, depth) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)";

    const char *insert_url_sql =
        "INSERT OR IGNORE INTO url_queue (session_id, url, depth, added_time) VALUES (?, ?, ?, ?)";

    const char *check_visited_sql =
        "SELECT 1 FROM pages WHERE session_id = ? AND url = ? LIMIT 1";

    const char *get_queue_sql =
        "SELECT url, depth FROM url_queue WHERE session_id = ? AND status = 'pending' ORDER BY depth, id LIMIT 1";

    const char *update_crawled_sql =
        "UPDATE url_queue SET status = 'crawled', crawled_time = ? WHERE session_id = ? AND url = ?";

    const char *get_stats_sql =
        "SELECT "
        "    (SELECT COUNT(*) FROM pages WHERE session_id = ?) as pages_crawled,"
        "    (SELECT COUNT(*) FROM extracted_links WHERE session_id = ?) as links_found,"
        "    (SELECT COUNT(*) FROM url_queue WHERE session_id = ? AND status = 'error') as errors,"
        "    (SELECT COUNT(*) FROM url_queue WHERE session_id = ? AND status = 'skipped') as skipped";

    if (sqlite3_prepare_v2(crawler_db.db, insert_page_sql, -1, &crawler_db.insert_page, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(crawler_db.db, insert_url_sql, -1, &crawler_db.insert_url, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(crawler_db.db, check_visited_sql, -1, &crawler_db.check_visited, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(crawler_db.db, get_queue_sql, -1, &crawler_db.get_queue, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(crawler_db.db, update_crawled_sql, -1, &crawler_db.update_crawled, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(crawler_db.db, get_stats_sql, -1, &crawler_db.get_stats, NULL) != SQLITE_OK)
    {

        fprintf(stderr, "Failed to prepare statements: %s\n", sqlite3_errmsg(crawler_db.db));
        return 0;
    }

    return 1;
}

int create_crawl_session(const char *start_url)
{
    const char *sql = "INSERT INTO crawl_sessions (start_url, start_time) VALUES (?, ?)";
    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(crawler_db.db, sql, -1, &stmt, NULL) != SQLITE_OK)
    {
        fprintf(stderr, "Failed to prepare session insert: %s\n", sqlite3_errmsg(crawler_db.db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, start_url, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, time(NULL));

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE)
    {
        fprintf(stderr, "Failed to create session: %s\n", sqlite3_errmsg(crawler_db.db));
        return -1;
    }

    return (int)sqlite3_last_insert_rowid(crawler_db.db);
}

int resume_crawl_session()
{
    const char *sql = "SELECT id FROM crawl_sessions WHERE status = 'running' ORDER BY id DESC LIMIT 1";
    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(crawler_db.db, sql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return -1;
    }

    int session_id = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        session_id = sqlite3_column_int(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return session_id;
}

void save_page_to_db(const char *url, const char *content, size_t content_length,
                     long response_code, int depth)
{
    sqlite3_bind_int(crawler_db.insert_page, 1, stats.session_id);
    sqlite3_bind_text(crawler_db.insert_page, 2, url, -1, SQLITE_STATIC);
    sqlite3_bind_text(crawler_db.insert_page, 3, content, content_length, SQLITE_STATIC);
    sqlite3_bind_int64(crawler_db.insert_page, 4, content_length);
    sqlite3_bind_int64(crawler_db.insert_page, 5, response_code);
    sqlite3_bind_int64(crawler_db.insert_page, 6, time(NULL));
    sqlite3_bind_int(crawler_db.insert_page, 7, depth);

    if (sqlite3_step(crawler_db.insert_page) != SQLITE_DONE)
    {
        fprintf(stderr, "Failed to save page: %s\n", sqlite3_errmsg(crawler_db.db));
    }

    sqlite3_reset(crawler_db.insert_page);
}

void add_url_to_queue(const char *url, int depth)
{
    sqlite3_bind_int(crawler_db.insert_url, 1, stats.session_id);
    sqlite3_bind_text(crawler_db.insert_url, 2, url, -1, SQLITE_STATIC);
    sqlite3_bind_int(crawler_db.insert_url, 3, depth);
    sqlite3_bind_int64(crawler_db.insert_url, 4, time(NULL));

    if (sqlite3_step(crawler_db.insert_url) == SQLITE_DONE)
    {
        stats.links_found++;
    }

    sqlite3_reset(crawler_db.insert_url);
}

int is_url_visited(const char *url)
{
    sqlite3_bind_int(crawler_db.check_visited, 1, stats.session_id);
    sqlite3_bind_text(crawler_db.check_visited, 2, url, -1, SQLITE_STATIC);

    int visited = 0;
    if (sqlite3_step(crawler_db.check_visited) == SQLITE_ROW)
    {
        visited = 1;
    }

    sqlite3_reset(crawler_db.check_visited);
    return visited;
}

int get_next_url(char *url_buffer, int *depth)
{
    sqlite3_bind_int(crawler_db.get_queue, 1, stats.session_id);

    int found = 0;
    if (sqlite3_step(crawler_db.get_queue) == SQLITE_ROW)
    {
        const char *url = (const char *)sqlite3_column_text(crawler_db.get_queue, 0);
        *depth = sqlite3_column_int(crawler_db.get_queue, 1);
        strncpy(url_buffer, url, MAX_URL_LENGTH - 1);
        url_buffer[MAX_URL_LENGTH - 1] = '\0';
        found = 1;
    }

    sqlite3_reset(crawler_db.get_queue);
    return found;
}

void mark_url_crawled(const char *url)
{
    sqlite3_bind_int64(crawler_db.update_crawled, 1, time(NULL));
    sqlite3_bind_int(crawler_db.update_crawled, 2, stats.session_id);
    sqlite3_bind_text(crawler_db.update_crawled, 3, url, -1, SQLITE_STATIC);

    sqlite3_step(crawler_db.update_crawled);
    sqlite3_reset(crawler_db.update_crawled);
}

void save_extracted_link(const char *source_url, const char *target_url)
{
    const char *sql = "INSERT OR IGNORE INTO extracted_links (session_id, source_url, target_url, discovered_time) VALUES (?, ?, ?, ?)";
    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(crawler_db.db, sql, -1, &stmt, NULL) == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, stats.session_id);
        sqlite3_bind_text(stmt, 2, source_url, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, target_url, -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 4, time(NULL));

        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

// Get statistics from database
void update_stats_from_db(void)
{
    sqlite3_bind_int(crawler_db.get_stats, 1, stats.session_id);
    sqlite3_bind_int(crawler_db.get_stats, 2, stats.session_id);
    sqlite3_bind_int(crawler_db.get_stats, 3, stats.session_id);
    sqlite3_bind_int(crawler_db.get_stats, 4, stats.session_id);

    if (sqlite3_step(crawler_db.get_stats) == SQLITE_ROW)
    {
        stats.pages_crawled = sqlite3_column_int(crawler_db.get_stats, 0);
        stats.links_found = sqlite3_column_int(crawler_db.get_stats, 1);
        stats.errors = sqlite3_column_int(crawler_db.get_stats, 2);
        stats.skipped_urls = sqlite3_column_int(crawler_db.get_stats, 3);
    }

    sqlite3_reset(crawler_db.get_stats);
}

// Print crawler statistics
void print_stats(void)
{
    update_stats_from_db();

    time_t end_time = time(NULL);
    double elapsed = difftime(end_time, stats.start_time);

    printf("\n=== Crawler Statistics ===\n");
    printf("Session ID: %d\n", stats.session_id);
    printf("Pages crawled: %d\n", stats.pages_crawled);
    printf("Links found: %d\n", stats.links_found);
    printf("URLs skipped: %d\n", stats.skipped_urls);
    printf("Errors: %d\n", stats.errors);
    printf("Time elapsed: %.2f seconds\n", elapsed);
    if (elapsed > 0)
    {
        printf("Average pages/second: %.2f\n", stats.pages_crawled / elapsed);
    }
}

void cleanup_database(void)
{
    if (crawler_db.insert_page)
        sqlite3_finalize(crawler_db.insert_page);
    if (crawler_db.insert_url)
        sqlite3_finalize(crawler_db.insert_url);
    if (crawler_db.check_visited)
        sqlite3_finalize(crawler_db.check_visited);
    if (crawler_db.get_queue)
        sqlite3_finalize(crawler_db.get_queue);
    if (crawler_db.update_crawled)
        sqlite3_finalize(crawler_db.update_crawled);
    if (crawler_db.get_stats)
        sqlite3_finalize(crawler_db.get_stats);

    if (crawler_db.db)
    {
        // Mark session as completed
        const char *sql = "UPDATE crawl_sessions SET status = 'completed', end_time = ? WHERE id = ?";
        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(crawler_db.db, sql, -1, &stmt, NULL) == SQLITE_OK)
        {
            sqlite3_bind_int64(stmt, 1, time(NULL));
            sqlite3_bind_int(stmt, 2, stats.session_id);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }

        sqlite3_close(crawler_db.db);
    }
}

void print_resume_info(void)
{
    const char *sql =
        "SELECT s.id, s.start_url, s.start_time, "
        "       COUNT(DISTINCT p.url) as pages_crawled, "
        "       COUNT(DISTINCT q.url) as total_urls "
        "FROM crawl_sessions s "
        "LEFT JOIN pages p ON s.id = p.session_id "
        "LEFT JOIN url_queue q ON s.id = q.session_id "
        "WHERE s.status = 'running' "
        "GROUP BY s.id "
        "ORDER BY s.start_time DESC";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(crawler_db.db, sql, -1, &stmt, NULL) != SQLITE_OK)
    {
        return;
    }

    printf("\n=== Available Sessions to Resume ===\n");
    int found_sessions = 0;

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        found_sessions = 1;
        int session_id = sqlite3_column_int(stmt, 0);
        const char *start_url = (const char *)sqlite3_column_text(stmt, 1);
        time_t start_time = sqlite3_column_int64(stmt, 2);
        int pages_crawled = sqlite3_column_int(stmt, 3);
        int total_urls = sqlite3_column_int(stmt, 4);

        char time_str[100];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&start_time));

        printf("Session %d: %s\n", session_id, start_url);
        printf("  Started: %s\n", time_str);
        printf("  Progress: %d pages crawled, %d URLs in queue\n", pages_crawled, total_urls);
        printf("\n");
    }

    if (!found_sessions)
    {
        printf("No active sessions found to resume.\n");
    }

    sqlite3_finalize(stmt);
}