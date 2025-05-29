#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <curl/curl.h>
#include <libxml/HTMLparser.h>
#include <libxml/xpath.h>
#include <libxml/uri.h>
#include "../include/config.h"
#include "../include/crawler.h"
#include "../include/database.h"

char *strdup(const char *s)
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
        return strdup(relative_url);
    }

    xmlChar *resolved = xmlBuildURI((const xmlChar *)relative_url, (const xmlChar *)base_url);
    if (!resolved)
        return NULL;

    char *result = strdup((char *)resolved);
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
        stats.errors++;
        return;
    }

    xmlXPathContextPtr context = xmlXPathNewContext(doc);
    if (!context)
    {
        xmlFreeDoc(doc);
        stats.errors++;
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

                            if (!is_url_visited(absolute_url) && !should_skip_url(absolute_url))
                            {
                                add_url_to_queue(absolute_url, current_depth + 1);
                                save_extracted_link(base_url, absolute_url);
                                if (VERBOSE_OUTPUT)
                                {
                                    printf("Found link: %s (depth %d)\n", absolute_url, current_depth + 1);
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

    printf("Crawling: %s (depth %d)\n", url, depth);

    CURL *curl = curl_easy_init();
    if (!curl)
    {
        fprintf(stderr, "Failed to initialize curl\n");
        stats.errors++;
        return 0;
    }

    WebPage page = {0};
    page.capacity = INITIAL_PAGE_SIZE;
    page.data = malloc(page.capacity);
    if (!page.data)
    {
        fprintf(stderr, "Failed to allocate initial page buffer\n");
        curl_easy_cleanup(curl);
        stats.errors++;
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
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        stats.errors++;
    }
    else if (response_code == 200 && page.data && page.size > 0)
    {
        printf("Successfully downloaded %s (%zu bytes)\n", url, page.size);
        stats.pages_crawled++;
        success = 1;

        // Save to database
        save_page_to_db(url, page.data, page.size, response_code, depth);

        // Extract links from the page
        extract_links(page.data, url, depth);

        // Save page content if enabled
        if (SAVE_PAGES)
        {
            char filename[512];
            snprintf(filename, sizeof(filename), "pages/%s%d.html", PAGE_FILE_PREFIX, stats.pages_crawled);
            FILE *f = fopen(filename, "w");
            if (f)
            {
                fwrite(page.data, 1, page.size, f);
                fclose(f);
                printf("Saved content to %s\n", filename);
            }
            else
            {
                fprintf(stderr, "Failed to save file %s\n", filename);
                perror("fopen");
            }
        }
    }
    else
    {
        printf("HTTP error %ld for %s\n", response_code, url);
        stats.errors++;
    }

    // Mark URL as crawled in database
    mark_url_crawled(url);

    if (page.data)
        free(page.data);
    curl_easy_cleanup(curl);

    return success;
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
        start_url = strdup(db_url);
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

    while (get_next_url(current_url, &current_depth) && stats.pages_crawled < MAX_URLS)
    {
        if (!is_url_visited(current_url))
        {
            crawl_url(current_url, current_depth);

            // Add delay between requests
            if (DELAY_SECONDS > 0)
            {
                sleep(DELAY_SECONDS);
            }
        }
        else
        {
            mark_url_crawled(current_url);
        }
    }

    // Cleanup
    print_stats();
    cleanup_database();

    if (resume_mode && start_url)
    {
        free(start_url);
    }

    xmlCleanupParser();
    curl_global_cleanup();

    printf("\nCrawling completed!\n");
    return 0;
}