#===============================================================================
# Kaissa Chess Engine - Makefile
# WinBoard Protocol Version
# Based on original Turbo-C Kaissa Chess Engine
#===============================================================================

# Compiler and linker settings
CC = clang
CFLAGS =  -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function
CFLAGS += -march=k8 -mtune=core2            # generic SSE3 no-popcount
#CFLAGS += -march=silvermont -mtune=k8        # SSE4 popcount
CFLAGS += -funsafe-math-optimizations -fsee -fomit-frame-pointer 
CFLAGS += -ftree-vectorize -funroll-loops -ffast-math -pipe -fstrict-aliasing 
LDFLAGS = -lm

# Debug build flags
DEBUG_CFLAGS = -g -O0 -DDEBUG

# Release build flags
RELEASE_CFLAGS = -O3 -flto=auto -DNDEBUG

# Target executable name
TARGET = kaissa

# Source files
SRCS = kaissa.c

# Object files
OBJS = $(SRCS:.c=.o)

# Dependency files
DEPS = $(SRCS:.c=.d)

#===============================================================================
# Build targets
#===============================================================================

# Default target - release build
all: release

# Release build
release: CFLAGS += $(RELEASE_CFLAGS)
release: $(TARGET)

# Debug build
debug: CFLAGS += $(DEBUG_CFLAGS)
debug: $(TARGET)

# Development build with warnings
dev: CFLAGS += -g -Wall -Wextra -pedantic -Wshadow -Wconversion
dev: $(TARGET)

# Static build (standalone executable)
static: CFLAGS += -static -static-libgcc
static: $(TARGET)

# Small build (optimized for size)
small: CFLAGS += -Os -s
small: $(TARGET)

# Profile-guided optimization build
profile: CFLAGS += -fprofile-generate
profile: $(TARGET)
	./$(TARGET) --test
	@echo "=== Running profile feedback build ==="
	$(MAKE) clean
	$(MAKE) profile-use

profile-use: CFLAGS += -fprofile-use -O3
profile-use: $(TARGET)

# Link executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "=== Build complete: $(TARGET) ==="
	@ls -lh $(TARGET)

# Compile C source files
%.o: %.c
	$(CC) $(CFLAGS) -MMD -c $< -o $@

#===============================================================================
# Installation targets
#===============================================================================

PREFIX = /usr/local
BINDIR = $(PREFIX)/bin

install: $(TARGET)
	install -d $(BINDIR)
	install -m 755 $(TARGET) $(BINDIR)/
	@echo "=== Installed to $(BINDIR)/$(TARGET) ==="

uninstall:
	rm -f $(BINDIR)/$(TARGET)
	@echo "=== Uninstalled from $(BINDIR)/$(TARGET) ==="

#===============================================================================
# Testing targets
#===============================================================================

test: $(TARGET)
	@echo "=== Running basic tests ==="
	@echo "xboard" | ./$(TARGET)
	@echo "quit" | ./$(TARGET)
	@echo "=== Tests completed ==="

test-perft: $(TARGET)
	@echo "=== Running perft tests (if implemented) ==="
	./$(TARGET) --perft

bench: $(TARGET)
	@echo "=== Running benchmark ==="
	time ./$(TARGET) --bench

#===============================================================================
# Development targets
#===============================================================================

# Run with valgrind memory checker
valgrind: debug
	valgrind --leak-check=full --show-leak-kinds=all ./$(TARGET)

# Run with gdb
gdb: debug
	gdb ./$(TARGET)

# Generate call graph
callgraph: debug
	gprof ./$(TARGET) gmon.out > callgraph.txt
	@echo "=== Call graph saved to callgraph.txt ==="

# Format source code
format:
	@if command -v clang-format >/dev/null 2>&1; then \
		clang-format -i $(SRCS); \
		echo "=== Formatted source code ==="; \
	else \
		echo "clang-format not installed, skipping"; \
	fi

# Count lines of code
cloc:
	@if command -v cloc >/dev/null 2>&1; then \
		cloc $(SRCS); \
	else \
		wc -l $(SRCS); \
	fi

#===============================================================================
# Cleanup targets
#===============================================================================

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET)
	rm -f gmon.out *.gcda *.gcno *.gcov
	rm -f callgraph.txt
	@echo "=== Clean complete ==="

distclean: clean
	rm -f *~ *.bak *.swp
	rm -f kaissa.exe kaissa.exe.stackdump
	@echo "=== Dist clean complete ==="

#===============================================================================
# Windows cross-compilation (MinGW)
#===============================================================================

ifdef WINDOWS
  CC = i686-w64-mingw32-gcc
  TARGET = kaissa.exe
endif

windows: clean
	$(MAKE) CC=i686-w64-mingw32-gcc TARGET=kaissa.exe release

windows64: clean
	$(MAKE) CC=x86_64-w64-mingw32-gcc TARGET=kaissa64.exe release

#===============================================================================
# Package targets
#===============================================================================

# Create source distribution
dist:
	rm -rf kaissa-src
	mkdir -p kaissa-src
	cp $(SRCS) Makefile README.md LICENSE kaissa-src/ 2>/dev/null || true
	tar czf kaissa-src.tar.gz kaissa-src/
	rm -rf kaissa-src
	@echo "=== Source distribution created: kaissa-src.tar.gz ==="

#===============================================================================
# Help target
#===============================================================================

help:
	@echo "Kaissa Chess Engine - Makefile Targets"
	@echo "======================================="
	@echo ""
	@echo "Build targets:"
	@echo "  all         - Build release version (default)"
	@echo "  release     - Build optimized release"
	@echo "  debug       - Build with debug symbols"
	@echo "  dev         - Build with extra warnings for development"
	@echo "  static      - Build statically linked executable"
	@echo "  small       - Build size-optimized executable"
	@echo "  profile     - Build for profile-guided optimization"
	@echo ""
	@echo "Installation:"
	@echo "  install     - Install to $(PREFIX)/bin"
	@echo "  uninstall   - Remove installed executable"
	@echo ""
	@echo "Testing:"
	@echo "  test        - Run basic tests"
	@echo "  test-perft  - Run performance tests"
	@echo "  bench       - Run benchmark"
	@echo ""
	@echo "Development:"
	@echo "  valgrind    - Run with memory checker"
	@echo "  gdb         - Run with debugger"
	@echo "  format      - Format source code"
	@echo "  cloc        - Count lines of code"
	@echo ""
	@echo "Windows cross-compilation:"
	@echo "  windows     - Build for Windows (32-bit)"
	@echo "  windows64   - Build for Windows (64-bit)"
	@echo ""
	@echo "Package:"
	@echo "  dist        - Create source distribution"
	@echo ""
	@echo "Cleanup:"
	@echo "  clean       - Remove build files"
	@echo "  distclean   - Remove all generated files"
	@echo ""

#===============================================================================
# Phony targets
#===============================================================================

.PHONY: all release debug dev static small profile profile-use
.PHONY: install uninstall test test-perft bench
.PHONY: valgrind gdb format cloc
.PHONY: clean distclean windows windows64 dist help

# Include dependency files
-include $(DEPS)