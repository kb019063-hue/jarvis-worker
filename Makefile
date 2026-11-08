CXX ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Iinclude -MMD -MP
BUILD_DIR ?= build

ASAN_FLAGS = -std=c++20 -O1 -g -Wall -Wextra -Iinclude -MMD -MP \
             -fsanitize=address,undefined -fno-omit-frame-pointer

SRCS = src/dom/Node.cpp \
       src/dom/Document.cpp \
       src/dom/Comment.cpp \
       src/dom/MutationObserver.cpp \
       src/dom/ShadowRoot.cpp \
       src/dom/CustomElementRegistry.cpp \
       src/dom/IntersectionObserver.cpp \
       src/dom/ResizeObserver.cpp \
       src/dom/History.cpp \
       src/html/HtmlTokenizer.cpp \
       src/html/HtmlParser.cpp \
       src/html/TreeDump.cpp \
       src/html/EntityTable.cpp \
       src/html/HtmlTreeBuilder.cpp \
       src/html/HtmlTreeBuilderModes.cpp \
       src/css/CssParser.cpp \
       src/css/StyleResolver.cpp \
       src/layout/LayoutBox.cpp \
       src/layout/LayoutTree.cpp \
       src/layout/FloatTableLayout.cpp \
       src/render/PaintCommand.cpp \
       src/render/ImageDecoder.cpp \
       src/render/Canvas2D.cpp \
       src/render/SvgRenderer.cpp \
       src/render/TerminalRenderer.cpp \
       src/render/BitmapRenderer.cpp \
       src/render/ImageFilter.cpp \
       src/js/JsValue.cpp \
       src/js/JsLexer.cpp \
       src/js/JsParser.cpp \
       src/js/Bytecode.cpp \
       src/js/BytecodeCompiler.cpp \
       src/js/Heap.cpp \
       src/js/VM.cpp \
       src/js/JsInterpreter.cpp \
       src/js/Json.cpp \
       src/js/Promise.cpp \
       src/js/Storage.cpp \
       src/js/EventLoop.cpp \
       src/js/Worker.cpp \
       src/js/Generator.cpp \
       src/net/ResourceLoader.cpp \
       src/net/URL.cpp \
       src/net/Fetch.cpp \
       src/net/WebSocket.cpp \
       src/net/AbortController.cpp \
       src/shell/BrowserShell.cpp

OBJS = $(SRCS:src/%.cpp=$(BUILD_DIR)/%.o)

TARGET = $(BUILD_DIR)/litebird
TEST_TARGET = $(BUILD_DIR)/test_suite
CONFORMANCE = python3 tools/conformance/run_tree_construction.py

.PHONY: all clean test run conformance fuzz asan help

all: $(TARGET)

$(BUILD_DIR)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TARGET): $(OBJS) $(BUILD_DIR)/main.o
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $^ -o $@
	@echo "\033[1;32mBuild complete: $(TARGET)\033[0m"

$(BUILD_DIR)/main.o: src/main.cpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

-include $(OBJS:.o=.d) $(BUILD_DIR)/main.d

$(TEST_TARGET): $(OBJS) tests/test_suite.cpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(OBJS) tests/test_suite.cpp -o $@

test: $(TEST_TARGET)
	@./$(TEST_TARGET)

run: $(TARGET)
	@./$(TARGET) examples/hello.html

## Conformance scoreboard (html5lib / WPT tree-construction corpus).
conformance: $(TARGET)
	@$(CONFORMANCE)

## Parser robustness: mutation fuzzing against a sanitizer build.
fuzz:
	@python3 tools/fuzz/fuzz_parser.py

## Address + UB sanitizer build, then the test suite and a fuzz smoke run.
## LeakSanitizer is disabled: the JS engine has known pre-existing leaks that
## are tracked as a separate cleanup item (see ARCHITECTURE.md, "Known gaps").
asan:
	ASAN_OPTIONS=detect_leaks=0 $(MAKE) BUILD_DIR=build-asan CXXFLAGS="$(ASAN_FLAGS)" all
	ASAN_OPTIONS=detect_leaks=0 $(MAKE) BUILD_DIR=build-asan CXXFLAGS="$(ASAN_FLAGS)" test
	ASAN_OPTIONS=detect_leaks=0 $(MAKE) BUILD_DIR=build-asan fuzz

clean:
	rm -rf build build-asan

help:
	@echo "Targets: all (default), test, run, conformance, fuzz, asan, clean"
