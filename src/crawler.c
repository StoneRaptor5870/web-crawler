#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdarg.h>
#include <curl/curl.h>
#include <libxml/HTMLparser.h>
#include <libxml/xpath.h>
#include <libxml/uri.h>
#include <signal.h>
#include "../include/config.h"
#include "../include/crawler.h"
#include "../include/database.h"
#include "../include/threads.h"

ThreadPool *thread_pool = NULL;
pthread_mutex_t db_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t console_mutex = PTHREAD_MUTEX_INITIALIZER;

// Structure to pass URL and depth to worker threads
typedef struct
{
    char *url;
    int depth;
} CrawlTask;

char *my_strdup(const char *s)
{
    if (!s)
        return NULL;

    size_t len = strlen(s) + 1;
    char *dup = malloc(len);

    if (!dup)
        return NULL;

    strcpy(dup, s);
    return dup;
}

void safe_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);

    pthread_mutex_lock(&console_mutex);
    vprintf(format, args);
    fflush(stdout);
    pthread_mutex_unlock(&console_mutex);

    va_end(args);
}

// Database operations
void safe_save_page_to_db(const char *url, const char *content, size_t content_length,
                          long response_code, int depth)
{
    pthread_mutex_lock(&db_mutex);
    save_page_to_db(url, content, content_length, response_code, depth);
    pthread_mutex_unlock(&db_mutex);
}

void safe_add_url_to_queue(const char *url, int depth)
{
    pthread_mutex_lock(&db_mutex);
    add_url_to_queue(url, depth);
    pthread_mutex_unlock(&db_mutex);
}

int safe_is_url_visited(const char *url)
{
    pthread_mutex_lock(&db_mutex);
    int result = is_url_visited(url);
    pthread_mutex_unlock(&db_mutex);
    return result;
}

void safe_mark_url_crawled(const char *url)
{
    pthread_mutex_lock(&db_mutex);
    mark_url_crawled(url);
    pthread_mutex_unlock(&db_mutex);
}

void safe_save_extracted_link(const char *source_url, const char *target_url)
{
    pthread_mutex_lock(&db_mutex);
    save_extracted_link(source_url, target_url);
    pthread_mutex_unlock(&db_mutex);
}

// Stats update
void safe_increment_pages_crawled()
{
    pthread_mutex_lock(&stats_mutex);
    stats.pages_crawled++;
    pthread_mutex_unlock(&stats_mutex);
}

void safe_increment_errors()
{
    pthread_mutex_lock(&stats_mutex);
    stats.errors++;
    pthread_mutex_unlock(&stats_mutex);
}

// Function to create pages directory if it doesn't exist
int create_pages_directory()
{
    struct stat st = {0};

    // Check if pages directory exists
    if (stat("pages", &st) == -1)
    {
        // Directory doesn't exist, create it
        if (mkdir("pages", 0755) == -1)
        {
            perror("Failed to create pages directory");
            return 0;
        }
        printf("Created pages directory\n");
    }
    return 1;
}

// Check if URL should be skipped based on patterns
int should_skip_url(const char *url)
{
    if (!url)
        return 1;

    for (int i = 0; SKIP_URL_PATTERNS[i] != NULL; i++)
    {
        if (strstr(url, SKIP_URL_PATTERNS[i]) != NULL)
        {
            return 1;
        }
    }
    return 0;
}

// Callback function for libcurl to write received data
size_t write_callback(void *contents, size_t size, size_t nmemb, WebPage *page)
{
    size_t real_size = size * nmemb;

    if (!page || !contents)
    {
        return 0;
    }

    // Check if we would exceed maximum page size
    if (page->size + real_size > MAX_PAGE_SIZE)
    {
        fprintf(stderr, "Page size exceeds maximum limit\n");
        return 0; // This will cause curl to abort
    }

    // Expand buffer if needed
    size_t needed_capacity = page->size + real_size + 1;
    if (needed_capacity > page->capacity)
    {
        size_t new_capacity = page->capacity * 2;
        if (new_capacity < needed_capacity)
        {
            new_capacity = needed_capacity;
        }

        char *ptr = realloc(page->data, new_capacity);
        if (!ptr)
        {
            fprintf(stderr, "Memory allocation failed during download\n");
            return 0;
        }
        page->data = ptr;
        page->capacity = new_capacity;
    }

    memcpy(&(page->data[page->size]), contents, real_size);
    page->size += real_size;
    page->data[page->size] = '\0';

    return real_size;
}

// Clean and normalize URL
void normalize_url(char *url)
{
    if (!url)
        return;

    // Remove fragment (#)
    char *fragment = strchr(url, '#');
    if (fragment)
        *fragment = '\0';

    // Remove trailing slash for consistency (but keep for root URLs)
    int len = strlen(url);
    if (len > 1 && url[len - 1] == '/')
    {
        // Don't remove trailing slash if it's just "http://" or "https://"
        char *protocol_end = strstr(url, "://");
        if (protocol_end && (protocol_end + 3) != (url + len - 1))
        {
            url[len - 1] = '\0';
        }
    }
}

// Resolve relative URL to absolute URL
char *resolve_url(const char *base_url, const char *relative_url)
{
    if (!base_url || !relative_url)
        return NULL;

    // If it's already absolute, just return a copy
    if (strncmp(relative_url, "http://", 7) == 0 ||
        strncmp(relative_url, "https://", 8) == 0)
    {
        return my_strdup(relative_url);
    }

    xmlChar *resolved = xmlBuildURI((const xmlChar *)relative_url, (const xmlChar *)base_url);
    if (!resolved)
        return NULL;

    char *result = my_strdup((char *)resolved);
    xmlFree(resolved);
    return result;
}

// Extract links from HTML content
void extract_links(const char *html, const char *base_url, int current_depth)
{
    if (!html || !base_url)
    {
        return;
    }

    // Suppress libxml2 error messages
    xmlSetGenericErrorFunc(NULL, NULL);

    htmlDocPtr doc = htmlReadMemory(html, strlen(html), base_url, NULL,
                                    HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING | HTML_PARSE_RECOVER);
    if (!doc)
    {
        safe_increment_errors();
        return;
    }

    xmlXPathContextPtr context = xmlXPathNewContext(doc);
    if (!context)
    {
        xmlFreeDoc(doc);
        safe_increment_errors();
        return;
    }

    // Look for both <a href> and <link href> tags
    const char *xpath_expressions[] = {
        "//a[@href]",
        "//link[@href]",
        NULL};

    for (int expr_idx = 0; xpath_expressions[expr_idx] != NULL; expr_idx++)
    {
        xmlXPathObjectPtr result = xmlXPathEvalExpression(
            (xmlChar *)xpath_expressions[expr_idx], context);

        if (!result)
            continue;

        xmlNodeSetPtr nodes = result->nodesetval;
        if (nodes)
        {
            for (int i = 0; i < nodes->nodeNr; i++)
            {
                xmlNodePtr node = nodes->nodeTab[i];
                if (!node)
                    continue;

                xmlChar *href = xmlGetProp(node, (xmlChar *)"href");
                if (href)
                {
                    char *absolute_url = resolve_url(base_url, (char *)href);
                    if (absolute_url)
                    {
                        // Only process HTTP/HTTPS URLs
                        if ((strncmp(absolute_url, "http://", 7) == 0 ||
                             strncmp(absolute_url, "https://", 8) == 0) &&
                            strlen(absolute_url) < MAX_URL_LENGTH)
                        {

                            normalize_url(absolute_url);

                            if (!safe_is_url_visited(absolute_url) && !should_skip_url(absolute_url))
                            {
                                safe_add_url_to_queue(absolute_url, current_depth + 1);
                                safe_save_extracted_link(base_url, absolute_url);

                                if (VERBOSE_OUTPUT)
                                {
                                    safe_printf("Found link: %s (depth %d)\n", absolute_url, current_depth + 1);
                                }
                            }
                        }
                        free(absolute_url);
                    }
                    xmlFree(href);
                }
            }
        }
        xmlXPathFreeObject(result);
    }

    xmlXPathFreeContext(context);
    xmlFreeDoc(doc);
}

// Download and process a single URL
int crawl_url(const char *url, int depth)
{
    if (!url)
        return 0;

    safe_printf("Thread %ld crawling: %s (depth %d)\n", (long)pthread_self(), url, depth);

    CURL *curl = curl_easy_init();
    if (!curl)
    {
        safe_printf("Thread %ld: Failed to initialize curl for %s\n", (long)pthread_self(), url);
        safe_increment_errors();
        return 0;
    }

    WebPage page = {0};
    page.capacity = INITIAL_PAGE_SIZE;
    page.data = malloc(page.capacity);
    if (!page.data)
    {
        safe_printf("Thread %ld: Failed to allocate memory for %s\n", (long)pthread_self(), url);
        curl_easy_cleanup(curl);
        safe_increment_errors();
        return 0;
    }

    // Configure curl
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &page);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, MAX_REDIRECTS);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, REQUEST_TIMEOUT);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, SSL_VERIFY_PEER);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, SSL_VERIFY_HOST);

    CURLcode res = curl_easy_perform(curl);
    long response_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

    int success = 0;
    if (res != CURLE_OK)
    {
        safe_printf("Thread %ld: curl failed for %s: %s\n",
                    (long)pthread_self(), url, curl_easy_strerror(res));
        safe_increment_errors();
    }
    else if (response_code == 200 && page.data && page.size > 0)
    {
        safe_printf("Thread %ld: Successfully downloaded %s (%zu bytes)\n",
                    (long)pthread_self(), url, page.size);

        safe_increment_pages_crawled();
        success = 1;

        // Save to database
        safe_save_page_to_db(url, page.data, page.size, response_code, depth);

        // Extract links from the page
        extract_links(page.data, url, depth);

        // Save page content if enabled
        if (SAVE_PAGES)
        {
            static int file_counter = 0;
            static pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

            pthread_mutex_lock(&file_mutex);
            file_counter++;
            char filename[512];
            snprintf(filename, sizeof(filename), "pages/%sthread_%ld_%d.html",
                     PAGE_FILE_PREFIX, (long)pthread_self(), file_counter);
            pthread_mutex_unlock(&file_mutex);

            FILE *f = fopen(filename, "w");
            if (f)
            {
                fwrite(page.data, 1, page.size, f);
                fclose(f);
                safe_printf("Thread %ld: Saved content to %s\n", (long)pthread_self(), filename);
            }
        }
    }
    else
    {
        safe_printf("Thread %ld: HTTP error %ld for %s\n", (long)pthread_self(), response_code, url);
        safe_increment_errors();
    }

    if (page.data)
        free(page.data);
    curl_easy_cleanup(curl);

    return success;
}

// Worker function for thread pool
static void crawl_task_worker(void *arg)
{
    CrawlTask *task = (CrawlTask *)arg;
    if (task)
    {
        crawl_url(task->url, task->depth);
        free(task->url);
        free(task);
    }
}

// Performance monitoring function
void print_performance_stats()
{
    static time_t last_check = 0;
    static int last_pages_crawled = 0;

    time_t current_time = time(NULL);
    if (current_time - last_check >= 60)
    { // Print every 60 seconds
        pthread_mutex_lock(&stats_mutex);
        int current_pages = stats.pages_crawled;
        pthread_mutex_unlock(&stats_mutex);

        if (last_check > 0)
        {
            double rate = (double)(current_pages - last_pages_crawled) / (current_time - last_check);
            safe_printf("Performance: %.2f pages/second (Total: %d pages)\n", rate, current_pages);
        }

        last_check = current_time;
        last_pages_crawled = current_pages;
    }
}

int main(int argc, char *argv[])
{
    int resume_mode = 0;
    char *start_url = NULL;

    // Parse command line arguments
    if (argc == 2)
    {
        if (strcmp(argv[1], "--resume") == 0)
        {
            resume_mode = 1;
        }
        else
        {
            start_url = argv[1];
        }
    }
    else if (argc == 3 && strcmp(argv[1], "--resume") == 0)
    {
        resume_mode = 1;
        stats.session_id = atoi(argv[2]);
    }
    else if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <starting_url>\n", argv[0]);
        fprintf(stderr, "       %s --resume [session_id]\n", argv[0]);
        fprintf(stderr, "Examples:\n");
        fprintf(stderr, "  %s https://example.com\n", argv[0]);
        fprintf(stderr, "  %s --resume\n", argv[0]);
        fprintf(stderr, "  %s --resume 5\n", argv[0]);
        return 1;
    }

    // Validate starting URL if not in resume mode
    if (!resume_mode && start_url)
    {
        if (strncmp(start_url, "http://", 7) != 0 && strncmp(start_url, "https://", 8) != 0)
        {
            fprintf(stderr, "Error: URL must start with http:// or https://\n");
            return 1;
        }
    }

    // Initialize database
    if (!init_database())
    {
        fprintf(stderr, "Failed to initialize database\n");
        return 1;
    }

    // Initialize thread pool
    safe_printf("Creating thread pool with %d threads\n", MAX_THREADS);
    thread_pool = thread_pool_create(MAX_THREADS);
    if (!thread_pool)
    {
        fprintf(stderr, "Failed to create thread pool\n");
        return 1;
    }

    // Handle resume mode
    if (resume_mode)
    {
        if (stats.session_id == 0)
        {
            // Auto-resume latest session
            stats.session_id = resume_crawl_session();
            if (stats.session_id == -1)
            {
                print_resume_info();
                cleanup_database();
                return 1;
            }
        }

        // Verify session exists and is active
        const char *sql = "SELECT start_url, start_time FROM crawl_sessions WHERE id = ? AND status = 'running'";
        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(crawler_db.db, sql, -1, &stmt, NULL) != SQLITE_OK)
        {
            fprintf(stderr, "Database error\n");
            cleanup_database();
            return 1;
        }

        sqlite3_bind_int(stmt, 1, stats.session_id);
        if (sqlite3_step(stmt) != SQLITE_ROW)
        {
            fprintf(stderr, "Session %d not found or not active\n", stats.session_id);
            sqlite3_finalize(stmt);
            print_resume_info();
            cleanup_database();
            return 1;
        }

        const char *db_url = (const char *)sqlite3_column_text(stmt, 0);
        start_url = my_strdup(db_url);
        if (!start_url)
        {
            fprintf(stderr, "Failed to allocate memory for start URL\n");
            sqlite3_finalize(stmt);
            cleanup_database();
            return 1;
        }

        stats.start_time = sqlite3_column_int64(stmt, 1);
        sqlite3_finalize(stmt);

        printf("Resuming crawl session %d\n", stats.session_id);
        printf("Original start URL: %s\n", start_url);
    }
    else
    {
        // Create new session
        stats.session_id = create_crawl_session(start_url);
        if (stats.session_id == -1)
        {
            fprintf(stderr, "Failed to create crawl session\n");
            cleanup_database();
            return 1;
        }

        stats.start_time = time(NULL);

        // Add initial URL to queue
        add_url_to_queue(start_url, 0);

        printf("Starting new crawl session %d\n", stats.session_id);
    }

    // Create pages directory if it doesn't exist
    if (!create_pages_directory())
    {
        fprintf(stderr, "Failed to create pages directory. Continuing without saving pages.\n");
    }

    // Initialize libraries
    curl_global_init(CURL_GLOBAL_DEFAULT);
    xmlInitParser();
    LIBXML_TEST_VERSION;

    // Start/Resume crawling
    printf("=====================================\n");
    printf("Session ID: %d\n", stats.session_id);
    printf("Start URL: %s\n", start_url);
    printf("Max depth: %d\n", MAX_DEPTH);
    printf("Max URLs: %d\n", MAX_URLS);
    printf("Delay between requests: %d seconds\n", DELAY_SECONDS);
    printf("Database: %s\n", DB_NAME);
    printf("=====================================\n\n");

    // Main crawling loop
    char current_url[MAX_URL_LENGTH];
    int current_depth;
    int urls_processed = 0;

    while (stats.pages_crawled < MAX_URLS)
    {
        // Get next URL with proper locking
        pthread_mutex_lock(&db_mutex);
        int has_url = get_next_url(current_url, &current_depth);

        if (!has_url)
        {
            pthread_mutex_unlock(&db_mutex);
            // Wait a bit and check again, or break if no more work
            usleep(500000); // 0.5 second

            // Check if thread pool is idle and no more URLs
            pthread_mutex_lock(&db_mutex);
            int queue_empty = !get_next_url(current_url, &current_depth);
            pthread_mutex_unlock(&db_mutex);

            if (queue_empty && thread_pool->working_count == 0)
            {
                break; // No more work to do
            }
            continue;
        }

        // Check if already visited while holding the lock
        int already_visited = is_url_visited(current_url);
        if (!already_visited)
        {
            // Mark as visited immediately to prevent other threads from picking it up
            mark_url_crawled(current_url);
        }
        pthread_mutex_unlock(&db_mutex);

        if (!already_visited)
        {
            // Create a task for the thread pool
            CrawlTask *task = malloc(sizeof(CrawlTask));
            if (task)
            {
                task->url = my_strdup(current_url);
                task->depth = current_depth;

                if (task->url)
                {
                    thread_pool_add_work(thread_pool, crawl_task_worker, task);
                    urls_processed++;

                    safe_printf("Added URL %d to queue: %s (depth %d)\n",
                                urls_processed, current_url, current_depth);
                }
                else
                {
                    free(task);
                }
            }
        }

        print_performance_stats();

        // Small delay to prevent overwhelming the queue
        usleep(100000); // 0.1 second
    }

    safe_printf("Waiting for all threads to complete...\n");
    thread_pool_wait(thread_pool);
    safe_printf("All threads completed!\n");

    // Cleanup
    print_stats();
    thread_pool_destroy(thread_pool);

    pthread_mutex_destroy(&db_mutex);
    pthread_mutex_destroy(&stats_mutex);
    pthread_mutex_destroy(&console_mutex);

    if (resume_mode && start_url)
    {
        free(start_url);
    }

    cleanup_database();
    xmlCleanupParser();
    curl_global_cleanup();

    safe_printf("\nCrawling completed!\n");
    return 0;
}