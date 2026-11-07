CXX ?= g++
CXXFLAGS = -std=c++20 -O2 -Wall -Wextra -Iinclude -MMD -MP

SRCS = src/dom/Node.cpp \
       src/dom/Document.cpp \
       src/dom/MutationObserver.cpp \
       src/dom/ShadowRoot.cpp \
       src/dom/CustomElementRegistry.cpp \
       src/dom/IntersectionObserver.cpp \
       src/dom/ResizeObserver.cpp \
       src/dom/History.cpp \
       src/html/HtmlTokenizer.cpp \
       src/html/HtmlParser.cpp \
       src/css/CssParser.cpp \
       src/css/StyleResolver.cpp \
       src/layout/LayoutBox.cpp \
       src/layout/LayoutTree.cpp \
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

OBJS = $(SRCS:src/%.cpp=build/%.o)

TARGET = build/litebird
TEST_TARGET = build/test_suite

.PHONY: all clean test run

all: $(TARGET)

build/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TARGET): $(OBJS) build/main.o
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $^ -o $@
	@echo "\033[1;32mBuild complete: $(TARGET)\033[0m"

build/main.o: src/main.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c $< -o $@

-include $(OBJS:.o=.d) build/main.d

$(TEST_TARGET): $(OBJS) tests/test_suite.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(OBJS) tests/test_suite.cpp -o $@

test: $(TEST_TARGET)
	@./$(TEST_TARGET)

run: $(TARGET)
	@./$(TARGET) examples/hello.html

clean:
	rm -rf build
