# Web Crawler Makefile

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2
LIBS = -lcurl -lxml2 -lsqlite3
INCLUDES = -I/usr/include/libxml2
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
PAGES_DIR = pages

# Target executable
TARGET = $(BIN_DIR)/webcrawler

# Source files
SOURCES = $(wildcard $(SRC_DIR)/*.c)

# Object files
OBJECTS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SOURCES))

# Default target
all: directories $(TARGET)

# Create directories if they don't exist
directories:
	mkdir -p $(OBJ_DIR)
	mkdir -p $(BIN_DIR)

# Build the main executable
$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LIBS)
	@echo "Build complete! Run with: ./$(TARGET) <url>"
	@echo "Example: ./$(TARGET) https://example.com"
	@echo "Resume:  ./$(TARGET) --resume"
	@echo "Resume:  ./$(TARGET) --resume sessionId"

# Compile source files - Fixed pattern rule
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Install dependencies (Ubuntu/Debian)
install-deps:
	@echo "Installing dependencies..."
	sudo apt-get update
	sudo apt-get install -y libcurl4-openssl-dev libxml2-dev libsqlite3-dev build-essential pkg-config
	@echo "Dependencies installed!"
	@echo "Verify installation:"
	@pkg-config --exists libxml-2.0 && echo "✓ libxml2 found" || echo "✗ libxml2 missing"
	@pkg-config --exists sqlite3 && echo "✓ sqlite3 found" || echo "✗ sqlite3 missing"
	@curl-config --version > /dev/null 2>&1 && echo "✓ curl found" || echo "✗ curl missing"

# Install dependencies (macOS with Homebrew)
install-deps-mac:
	@echo "Installing dependencies..."
	brew install curl libxml2 sqlite3
	@echo "Dependencies installed!"

# Check if all dependencies are available
check-deps:
	@echo "Checking dependencies..."
	@echo -n "Checking for pkg-config... "
	@command -v pkg-config > /dev/null 2>&1 && echo "✓" || echo "✗ (install pkg-config)"
	@echo -n "Checking for gcc... "
	@command -v gcc > /dev/null 2>&1 && echo "✓" || echo "✗ (install gcc)"
	@echo -n "Checking libcurl... "
	@curl-config --version > /dev/null 2>&1 && echo "✓" || echo "✗ (install libcurl-dev)"
	@echo -n "Checking libxml2... "
	@pkg-config --exists libxml-2.0 && echo "✓" || echo "✗ (install libxml2-dev)"
	@echo -n "Checking sqlite3... "
	@pkg-config --exists sqlite3 && echo "✓" || echo "✗ (install libsqlite3-dev)"

# Clean build files
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR) $(PAGES_DIR)
	@echo "Clean complete!"

# Clean everything including database
clean-all: clean
	rm -f crawler.db crawler.db-shm crawler.db-wal
	@echo "Complete cleanup done!"

# Run with example URL
test: $(TARGET)
	./$(TARGET) https://httpbin.org/links/5/0

# Test database functionality
test-db: $(TARGET)
	@echo "Testing database functionality..."
	./$(TARGET) https://httpbin.org/links/3/0
	@echo "Checking database contents..."
	@if command -v sqlite3 > /dev/null 2>&1; then \
		echo "Sessions in database:"; \
		sqlite3 crawler.db "SELECT id, start_url, start_time, status FROM crawl_sessions;"; \
		echo "Pages crawled:"; \
		sqlite3 crawler.db "SELECT COUNT(*) as page_count FROM pages;"; \
	else \
		echo "sqlite3 command not found. Install sqlite3 to inspect database."; \
	fi

# Debug build
debug: CFLAGS += -g -DDEBUG
debug: directories $(TARGET)

# Install sqlite3 command line tool
install-sqlite3:
	@echo "Installing SQLite3 command line tool..."
	@if command -v apt-get > /dev/null 2>&1; then \
		sudo apt-get install -y sqlite3; \
	elif command -v brew > /dev/null 2>&1; then \
		brew install sqlite; \
	else \
		echo "Could not determine package manager. Please install sqlite3 manually."; \
	fi

# Show database schema
show-schema: install-sqlite3
	@echo "Database Schema:"
	@sqlite3 crawler.db ".schema" 2>/dev/null || echo "Database not found. Run crawler first."

# Interactive database shell
db-shell: install-sqlite3
	@echo "Opening SQLite shell for crawler.db..."
	@echo "Useful commands:"
	@echo "  .tables                    - List all tables"
	@echo "  .schema                    - Show database schema"
	@echo "  SELECT * FROM crawl_sessions; - Show crawl sessions"
	@echo "  SELECT * FROM pages LIMIT 5;  - Show first 5 pages"
	@echo "  .quit                      - Exit"
	@sqlite3 crawler.db

# Help target
help:
	@echo "Web Crawler Build System with SQLite Support"
	@echo "============================================="
	@echo ""
	@echo "Build Targets:"
	@echo "  all              - Build the web crawler (default)"
	@echo "  debug            - Build with debug symbols and DEBUG flag"
	@echo "  analyze          - Build with extra warnings for static analysis"
	@echo "  clean            - Remove build files and crawled pages"
	@echo "  clean-all        - Remove everything including database files"
	@echo ""
	@echo "Dependency Management:"
	@echo "  install-deps     - Install dependencies on Ubuntu/Debian"
	@echo "  install-deps-mac - Install dependencies on macOS (requires Homebrew)"
	@echo "  check-deps       - Check if all dependencies are installed"
	@echo "  install-sqlite3  - Install SQLite3 command line tool"
	@echo ""
	@echo "Testing:"
	@echo "  test             - Build and run with test URL"
	@echo "  test-db          - Test database functionality"
	@echo ""
	@echo "Database Management:"
	@echo "  show-schema      - Display database schema"
	@echo "  db-shell         - Open interactive SQLite shell"
	@echo ""
	@echo "Usage Examples:"
	@echo "  make install-deps    # Install required libraries"
	@echo "  make setup          # Setup project structure"
	@echo "  make                # Build the crawler"
	@echo "  ./bin/webcrawler https://example.com"
	@echo "  ./bin/webcrawler --resume"
	@echo "  make db-shell       # Explore crawled data"
	@echo ""
	@echo "SQLite Libraries Required:"
	@echo "  - libsqlite3-dev (Ubuntu/Debian)"
	@echo "  - sqlite3 (macOS via Homebrew)"

.PHONY: all clean clean-all debug test test-db help check-deps \
        install-deps install-deps-mac directories install-sqlite3 \
		show-schema db-shell