# Web Crawler Makefile

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -O2
LIBS = -lcurl -lxml2
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

# Compile source files - Fixed pattern rule
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Install dependencies (Ubuntu/Debian)
install-deps:
	@echo "Installing dependencies..."
	sudo apt-get update
	sudo apt-get install -y libcurl4-openssl-dev libxml2-dev build-essential
	@echo "Dependencies installed!"

# Install dependencies (macOS with Homebrew)
install-deps-mac:
	@echo "Installing dependencies..."
	brew install curl libxml2
	@echo "Dependencies installed!"

# Clean build files
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR) $(PAGES_DIR)
	@echo "Clean complete!"

# Create output directory for crawled pages
setup:
	mkdir -p output
	@echo "Output directory created!"

# Run with example URL
test: $(TARGET)
	./$(TARGET) https://httpbin.org/links/5/0

# Debug build
debug: CFLAGS += -g -DDEBUG
debug: directories $(TARGET)

# Help target
help:
	@echo "Web Crawler Build System"
	@echo "========================"
	@echo ""
	@echo "Targets:"
	@echo "  all            - Build the web crawler (default)"
	@echo "  clean          - Remove build files and crawled pages"
	@echo "  debug          - Build with debug symbols"
	@echo "  test           - Build and run with test URL"
	@echo "  setup          - Create output directory"
	@echo "  help           - Show this help message"
	@echo ""
	@echo "Dependency Installation:"
	@echo "  install-deps     - Install deps on Ubuntu/Debian"
	@echo "  install-deps-rpm - Install deps on CentOS/RHEL/Fedora"
	@echo "  install-deps-mac - Install deps on macOS (requires Homebrew)"
	@echo ""
	@echo "Usage:"
	@echo "  make install-deps  # Install required libraries"
	@echo "  make               # Build the crawler"
	@echo "  ./bin/webcrawler <url> # Run the crawler"

.PHONY: all clean debug test setup help install-deps install-deps-rpm install-deps-mac directories