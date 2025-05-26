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
#include "../include/crawler.h"

char *strdup(const char *s)
{
    size_t len = strlen(s) + 1;
    char *dup = malloc(len);
    if (dup)
    {
        memcpy(dup, s, len);
    }
    return dup;
}

// Structure to hold downloaded web page content
typedef struct
{
    char *data;
    size_t size;
    size_t capacity;
} WebPage;

// URL queue node for BFS crawling
typedef struct URLNode
{
    char url[MAX_URL_LENGTH];
    int depth;
    struct URLNode *next;
} URLNode;

// URL queue structure
typedef struct
{
    URLNode *front;
    URLNode *rear;
    int count;
} URLQueue;

// Hash table for visited URLs
typedef struct HashNode
{
    char url[MAX_URL_LENGTH];
    struct HashNode *next;
} HashNode;

HashNode *visited_urls[HASH_SIZE];

// Statistics
typedef struct
{
    int pages_crawled;
    int links_found;
    int errors;
    int skipped_urls;
    time_t start_time;
} CrawlerStats;

CrawlerStats stats = {0};

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

// Hash function for URL hash table
unsigned int hash_url(const char *url)
{
    if (!url)
        return 0;

    unsigned int hash = 5381;
    int c;
    while ((c = *url++))
    {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % HASH_SIZE;
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

// Check if URL has been visited
int is_visited(const char *url)
{
    if (!url)
        return 1;

    unsigned int index = hash_url(url);
    HashNode *node = visited_urls[index];

    while (node)
    {
        if (strcmp(node->url, url) == 0)
        {
            return 1;
        }
        node = node->next;
    }
    return 0;
}

// Mark URL as visited
void mark_visited(const char *url)
{
    if (!url || is_visited(url))
        return;

    unsigned int index = hash_url(url);
    HashNode *new_node = malloc(sizeof(HashNode));
    if (!new_node)
    {
        fprintf(stderr, "Memory allocation failed for visited URL\n");
        return;
    }

    strncpy(new_node->url, url, MAX_URL_LENGTH - 1);
    new_node->url[MAX_URL_LENGTH - 1] = '\0';
    new_node->next = visited_urls[index];
    visited_urls[index] = new_node;
}

// Initialize URL queue
URLQueue *init_queue()
{
    URLQueue *queue = malloc(sizeof(URLQueue));
    if (!queue)
        return NULL;

    queue->front = NULL;
    queue->rear = NULL;
    queue->count = 0;
    return queue;
}

// Add URL to queue
void enqueue_url(URLQueue *queue, const char *url, int depth)
{
    if (!queue || !url || queue->count >= MAX_URLS || depth > MAX_DEPTH)
    {
        return;
    }

    // Skip if URL should be filtered
    if (should_skip_url(url))
    {
        stats.skipped_urls++;
        if (VERBOSE_OUTPUT)
        {
            printf("Skipped URL: %s\n", url);
        }
        return;
    }

    URLNode *new_node = malloc(sizeof(URLNode));
    if (!new_node)
    {
        fprintf(stderr, "Memory allocation failed for URL queue\n");
        return;
    }

    strncpy(new_node->url, url, MAX_URL_LENGTH - 1);
    new_node->url[MAX_URL_LENGTH - 1] = '\0';
    new_node->depth = depth;
    new_node->next = NULL;

    if (queue->rear)
    {
        queue->rear->next = new_node;
    }
    else
    {
        queue->front = new_node;
    }
    queue->rear = new_node;
    queue->count++;
}

// Remove URL from queue
URLNode *dequeue_url(URLQueue *queue)
{
    if (!queue || !queue->front)
        return NULL;

    URLNode *node = queue->front;
    queue->front = queue->front->next;
    if (!queue->front)
    {
        queue->rear = NULL;
    }
    queue->count--;
    return node;
}

// Callback function for libcurl to write received data
size_t write_callback(void *contents, size_t size, size_t nmemb, WebPage *page)
{
    size_t real_size = size * nmemb;

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
void extract_links(const char *html, const char *base_url, URLQueue *queue, int current_depth)
{
    if (!html || !base_url || !queue)
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

                            if (!is_visited(absolute_url) && !should_skip_url(absolute_url))
                            {
                                enqueue_url(queue, absolute_url, current_depth + 1);
                                stats.links_found++;
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
int crawl_url(const char *url, URLQueue *queue, int depth)
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

        // Extract links from the page
        extract_links(page.data, url, queue, depth);

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

    if (page.data)
        free(page.data);
    curl_easy_cleanup(curl);

    return success;
}

// Clean up visited URLs hash table
void cleanup_visited_urls()
{
    for (int i = 0; i < HASH_SIZE; i++)
    {
        HashNode *node = visited_urls[i];
        while (node)
        {
            HashNode *temp = node;
            node = node->next;
            free(temp);
        }
        visited_urls[i] = NULL;
    }
}

// Clean up URL queue
void cleanup_queue(URLQueue *queue)
{
    if (!queue)
        return;

    while (queue->front)
    {
        URLNode *temp = dequeue_url(queue);
        if (temp)
            free(temp);
    }
    free(queue);
}

// Print crawler statistics
void print_stats()
{
    time_t end_time = time(NULL);
    double elapsed = difftime(end_time, stats.start_time);

    printf("\n=== Crawler Statistics ===\n");
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

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <starting_url>\n", argv[0]);
        fprintf(stderr, "Example: %s https://example.com\n", argv[0]);
        return 1;
    }

    // Validate starting URL
    if (strncmp(argv[1], "http://", 7) != 0 && strncmp(argv[1], "https://", 8) != 0)
    {
        fprintf(stderr, "Error: URL must start with http:// or https://\n");
        return 1;
    }

    // Create pages directory if it doesn't exist
    if (!create_pages_directory())
    {
        fprintf(stderr, "Failed to create pages directory. Continuing without saving pages.\n");
    }

    // Initialize
    stats.start_time = time(NULL);
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // Initialize libxml2
    xmlInitParser();
    LIBXML_TEST_VERSION;

    URLQueue *queue = init_queue();
    if (!queue)
    {
        fprintf(stderr, "Failed to initialize URL queue\n");
        curl_global_cleanup();
        xmlCleanupParser();
        return 1;
    }

    // Start crawling
    printf("Starting web crawler...\n");
    printf("Starting URL: %s\n", argv[1]);
    printf("Max depth: %d\n", MAX_DEPTH);
    printf("Max URLs: %d\n", MAX_URLS);
    printf("Delay between requests: %d seconds\n", DELAY_SECONDS);
    printf("=====================================\n\n");

    enqueue_url(queue, argv[1], 0);

    while (queue->front && stats.pages_crawled < MAX_URLS)
    {
        URLNode *current = dequeue_url(queue);
        if (!current)
            break;

        if (!is_visited(current->url))
        {
            mark_visited(current->url);
            crawl_url(current->url, queue, current->depth);

            // Add delay between requests
            if (DELAY_SECONDS > 0)
            {
                sleep(DELAY_SECONDS);
            }
        }
        free(current);
    }

    // Cleanup
    cleanup_queue(queue);
    cleanup_visited_urls();
    print_stats();

    xmlCleanupParser();
    curl_global_cleanup();

    printf("\nCrawling completed!\n");
    return 0;
}