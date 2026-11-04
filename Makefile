CXX ?= g++
CXXFLAGS = -std=c++20 -O2 -Wall -Wextra -Iinclude

SRCS = src/dom/Node.cpp \
       src/dom/Document.cpp \
       src/html/HtmlTokenizer.cpp \
       src/html/HtmlParser.cpp \
       src/css/CssParser.cpp \
       src/css/StyleResolver.cpp \
       src/layout/LayoutBox.cpp \
       src/layout/LayoutTree.cpp \
       src/render/PaintCommand.cpp \
       src/render/TerminalRenderer.cpp \
       src/render/BitmapRenderer.cpp \
       src/js/JsValue.cpp \
       src/js/JsLexer.cpp \
       src/js/JsParser.cpp \
       src/js/Bytecode.cpp \
       src/js/BytecodeCompiler.cpp \
       src/js/VM.cpp \
       src/js/JsInterpreter.cpp

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

$(TEST_TARGET): $(OBJS) tests/test_suite.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(OBJS) tests/test_suite.cpp -o $@

test: $(TEST_TARGET)
	@./$(TEST_TARGET)

run: $(TARGET)
	@./$(TARGET) examples/hello.html

clean:
	rm -rf build
