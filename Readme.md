# Web Crawler in C

A robust web crawler implementation in C that can systematically browse and download web pages while respecting rate limits and avoiding infinite loops.

## Features

- **HTTP/HTTPS Support**: Full support for secure and non-secure web pages
- **Link Extraction**: Automatically extracts and follows links from HTML pages
- **Duplicate Detection**: Hash table-based visited URL tracking to avoid loops
- **Depth Control**: Configurable maximum crawling depth
- **Rate Limiting**: Polite crawling with configurable delays
- **Error Handling**: Robust error handling for network and parsing errors
- **Statistics**: Real-time crawling statistics and performance metrics
- **Content Saving**: Saves downloaded pages to local files
- **Memory Management**: Careful memory management to prevent leaks

## Dependencies

The crawler requires the following libraries:

- **libcurl**: For HTTP/HTTPS requests
- **libxml2**: For HTML parsing and link extraction
- **Standard C libraries**: stdio, stdlib, string, unistd, time

## Installation

### Ubuntu/Debian

```bash
# Install dependencies
make install-deps

# Or manually:
sudo apt-get update
sudo apt-get install -y libcurl4-openssl-dev libxml2-dev build-essential
```

### macOS

```bash
# Install dependencies (requires Homebrew)
make install-deps-mac

# Or manually:
brew install curl libxml2
```

## Building

```bash
# Build the crawler
make

# Or build with debug symbols
make debug

# Clean build files
make clean
```

## Usage

### Basic Usage

```bash
.bin/webcrawler <starting_url>
```

### Examples

```bash
# Crawl a simple website
.bin/webcrawler https://example.com

# Crawl a news website
.bin/webcrawler https://news.ycombinator.com

# Test with a URL that has multiple links
.bin/webcrawler https://httpbin.org/links/10/0
```

### Quick Test

```bash
# Build and run with test URL
make test
```

## Configuration

You can modify these constants in the source code to customize behavior:

```c
#define MAX_URL_LENGTH 2048    // Maximum length of URLs
#define MAX_URLS 10000         // Maximum number of URLs to crawl
#define MAX_DEPTH 3            // Maximum crawling depth
#define DELAY_SECONDS 1        // Delay between requests (be polite!)
```

## Output

The crawler will:

1. **Console Output**: Display real-time crawling progress, found links, and statistics
2. **HTML Files**: Save each crawled page as `page_N.html` where N is the page number in the pages directory
3. **Final Statistics**: Show summary of pages crawled, links found, errors, and performance

### Sample Output

```
Starting web crawler...
Starting URL: https://example.com
Max depth: 3
Max URLs: 10000
Delay between requests: 1 seconds
=====================================

Crawling: https://example.com (depth 0)
Successfully downloaded https://example.com (1256 bytes)
Saved content to page_1.html
Found link: https://www.iana.org/domains/example (depth 1)
Crawling: https://www.iana.org/domains/example (depth 1)
...

=== Crawler Statistics ===
Pages crawled: 15
Links found: 47
Errors: 2
Time elapsed: 23.45 seconds
Average pages/second: 0.64
```

## Architecture

### Core Components

1. **HTTP Client**: Uses libcurl for robust HTTP/HTTPS handling
2. **HTML Parser**: Uses libxml2 for parsing HTML and extracting links
3. **URL Queue**: Breadth-first search implementation for systematic crawling
4. **Hash Table**: Efficient duplicate URL detection
5. **Memory Management**: Careful allocation/deallocation to prevent leaks

### Data Structures

- **WebPage**: Holds downloaded page content
- **URLQueue**: Queue for BFS crawling with depth tracking
- **HashNode**: Hash table for visited URL tracking
- **CrawlerStats**: Performance and statistics tracking

## Politeness Features

The crawler implements several "polite" crawling practices:

- **Rate Limiting**: Configurable delay between requests
- **User Agent**: Identifies itself as "WebCrawler/1.0"
- **Timeout Handling**: 30-second timeout for requests
- **Redirect Limits**: Maximum of 5 redirects per request
- **Depth Limiting**: Prevents infinite crawling

## Error Handling

The crawler handles various error conditions:

- Network timeouts and connection failures
- HTTP error codes (404, 500, etc.)
- Invalid HTML content
- Memory allocation failures
- Malformed URLs

## Limitations

- **Single-threaded**: Currently processes one URL at a time
- **No robots.txt**: Doesn't check robots.txt files (add this for production use)
- **Limited Content Types**: Only processes HTML content
- **No JavaScript**: Cannot handle dynamically generated content
- **Memory Usage**: Keeps all visited URLs in memory

## Troubleshooting

### Common Issues

**Build Errors**

```bash
# Missing libcurl headers
sudo apt-get install libcurl4-openssl-dev

# Missing libxml2 headers
sudo apt-get install libxml2-dev
```

**Runtime Errors**

```bash
# SSL certificate errors
# The crawler disables SSL verification - enable for production

# Memory errors
# Check for memory leaks with valgrind:
valgrind --leak-check=full .bin/webcrawler https://example.com
```

### Debug Mode

```bash
# Build with debug symbols
make debug

# Run with gdb
gdb .bin/webcrawler
(gdb) run https://example.com
```

## Extending the Crawler

### Adding Multi-threading

```c
// Add pthread support for parallel crawling
#include <pthread.h>

// Create worker threads
pthread_t workers[NUM_THREADS];
```

### Adding robots.txt Support

```c
// Check robots.txt before crawling
int check_robots_txt(const char *base_url, const char *user_agent);
```

### Adding Content Type Filtering

```c
// Only crawl specific content types
if (strstr(content_type, "text/html") == NULL) {
    // Skip non-HTML content
    return;
}
```

## Version History

- v1.0: Initial implementation with basic crawling functionality
- v1.1: Database storage with sqlite
- Features planned: Multi-threading, robots.txt support, database storage
