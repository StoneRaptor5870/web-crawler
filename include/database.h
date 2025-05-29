#ifndef DATABASE_H
#define DATABASE_H

#include <sqlite3.h>
#include <time.h>
#include "config.h"

// Database structure
typedef struct
{
    sqlite3 *db;
    sqlite3_stmt *insert_page;
    sqlite3_stmt *insert_url;
    sqlite3_stmt *check_visited;
    sqlite3_stmt *get_queue;
    sqlite3_stmt *update_crawled;
    sqlite3_stmt *get_stats;
} CrawlerDB;

// Statistics structure
typedef struct
{
    int pages_crawled;
    int links_found;
    int errors;
    int skipped_urls;
    time_t start_time;
    int session_id;
} CrawlerStats;

// Global database instance (extern declaration)
extern CrawlerDB crawler_db;
extern CrawlerStats stats;

// Database initialization and management functions
int init_database(void);
void cleanup_database(void);

// Session management
int create_crawl_session(const char *start_url);
int resume_crawl_session(void);
void print_resume_info(void);

// Page and URL management
void save_page_to_db(const char *url, const char *content, size_t content_length,
                     long response_code, int depth);
void add_url_to_queue(const char *url, int depth);
int is_url_visited(const char *url);
int get_next_url(char *url_buffer, int *depth);
void mark_url_crawled(const char *url);
void save_extracted_link(const char *source_url, const char *target_url);

// Statistics
void update_stats_from_db(void);
void print_stats(void);

#endif // DATABASE_H