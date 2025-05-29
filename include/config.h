#ifndef CRAWLER_CONFIG_H
#define CRAWLER_CONFIG_H

#include <stddef.h>

/* Web Crawler Configuration File
 * Modify these settings to customize crawler behavior
 */

// URL and Queue Limits
#define MAX_URL_LENGTH 2048 // Maximum length of a single URL
#define MAX_URLS 10000      // Maximum total URLs to crawl
#define MAX_DEPTH 3         // Maximum crawling depth from start URL
#define HASH_SIZE 10007     // Hash table size for visited URLs (prime number)

// Database Settings
#define DB_NAME "crawler.db"
#define ENABLE_WAL_MODE 1   // Enable WAL mode for better performance

// Network Settings
#define DELAY_SECONDS 5             // Delay between requests (seconds) - be polite!
#define REQUEST_TIMEOUT 30L         // HTTP request timeout (seconds)
#define MAX_REDIRECTS 5L            // Maximum number of redirects to follow
#define USER_AGENT "WebCrawler/1.0" // User agent string

// SSL Settings (for production, set these to 1)
#define SSL_VERIFY_PEER 0L // Verify SSL certificates (0=disabled, 1=enabled)
#define SSL_VERIFY_HOST 0L // Verify SSL hostnames (0=disabled, 1=enabled)

// Output Settings
#define SAVE_PAGES 1             // Save downloaded pages to files (0=no, 1=yes)
#define PAGE_FILE_PREFIX "page_" // Prefix for saved page files
#define VERBOSE_OUTPUT 1         // Print detailed progress (0=quiet, 1=verbose)

// Memory Settings
#define INITIAL_PAGE_SIZE 4096           // Initial buffer size for downloaded pages
#define MAX_PAGE_SIZE (10 * 1024 * 1024) // Maximum page size (10MB)

// Content Filtering
#define CRAWL_HTTP 1            // Crawl HTTP URLs (0=no, 1=yes)
#define CRAWL_HTTPS 1           // Crawl HTTPS URLs (0=no, 1=yes)
#define FOLLOW_EXTERNAL_LINKS 1 // Follow links to other domains (0=no, 1=yes)

// Error Handling
#define MAX_CONSECUTIVE_ERRORS 10 // Stop crawling after this many consecutive errors
#define RETRY_FAILED_REQUESTS 0   // Retry failed requests (0=no, 1=yes)
#define MAX_RETRIES 3             // Maximum number of retries per URL

// Performance Settings
#define ENABLE_COMPRESSION 1   // Enable gzip compression (0=no, 1=yes)
#define DNS_CACHE_TIMEOUT 60   // DNS cache timeout (seconds)
#define CONNECTION_TIMEOUT 10L // Connection timeout (seconds)

// Debug Settings
#ifdef DEBUG
#define ENABLE_DEBUG_OUTPUT 1
#define DEBUG_MEMORY_USAGE 1
#define DEBUG_URL_PROCESSING 1
#else
#define ENABLE_DEBUG_OUTPUT 0
#define DEBUG_MEMORY_USAGE 0
#define DEBUG_URL_PROCESSING 0
#endif

// URL Filtering Patterns (simple substring matching)
// Add URLs that should be skipped
// URL Filtering Patterns
static const char *SKIP_URL_PATTERNS[] __attribute__((unused)) = {
    ".pdf", ".jpg", ".jpeg", ".png", ".gif", ".bmp",
    ".mp3", ".mp4", ".avi", ".mov",
    ".zip", ".rar", ".tar", ".gz",
    ".exe", ".dmg", ".pkg",
    "mailto:", "javascript:", "tel:",
    NULL};

// Domain filtering (if FOLLOW_EXTERNAL_LINKS is 0)
// Only crawl URLs from these domains (NULL = allow all)
static const char *ALLOWED_DOMAINS[] __attribute__((unused)) = {
    // "example.com",
    // "www.example.com",
    NULL};

// Macro for debug printing
#if ENABLE_DEBUG_OUTPUT
#define DEBUG_PRINT(fmt, ...) fprintf(stderr, "[DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define DEBUG_PRINT(fmt, ...)
#endif

// Macro for verbose printing
#if VERBOSE_OUTPUT
#define VERBOSE_PRINT(fmt, ...) printf(fmt "\n", ##__VA_ARGS__)
#else
#define VERBOSE_PRINT(fmt, ...)
#endif

#endif // CRAWLER_CONFIG_H