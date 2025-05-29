#ifndef CRAWLER_H
#define CRAWLER_H

#include <stddef.h>
#include <curl/curl.h>

// Structure to hold downloaded web page content
typedef struct
{
    char *data;
    size_t size;
    size_t capacity;
} WebPage;

// URL utility functions
void normalize_url(char *url);
char *resolve_url(const char *base_url, const char *relative_url);
int should_skip_url(const char *url);

// Web page download functions
size_t write_callback(void *contents, size_t size, size_t nmemb, WebPage *page);
int crawl_url(const char *url, int depth);

// HTML parsing and link extraction
void extract_links(const char *html, const char *base_url, int current_depth);

// File system utilities
int create_pages_directory(void);

// Custom string functions
char *strdup(const char *s);

#endif // CRAWLER_H