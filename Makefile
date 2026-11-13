CXX ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Iinclude -MMD -MP
BUILD_DIR ?= build

ASAN_FLAGS = -std=c++20 -O1 -g -Wall -Wextra -Iinclude -MMD -MP \
             -fsanitize=address,undefined -fno-omit-frame-pointer

SRCS = src/dom/Node.cpp \
       src/dom/Selector.cpp \
       src/dom/FormState.cpp \
       src/dom/Document.cpp \
       src/dom/Comment.cpp \
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
       src/js/JsString.cpp \
       src/js/JsLexer.cpp \
       src/js/JsParser.cpp \
       src/js/Bytecode.cpp \
       src/js/BytecodeCompiler.cpp \
       src/js/Heap.cpp \
       src/js/VM.cpp \
       src/js/JsInterpreter.cpp \
       src/js/JsEval.cpp \
       src/js/JsOps.cpp \
       src/js/BuiltinsObject.cpp \
       src/js/BuiltinsArray.cpp \
       src/js/BuiltinsString.cpp \
       src/js/BuiltinsNumber.cpp \
       src/js/BuiltinsCollections.cpp \
       src/js/BuiltinsJson.cpp \
       src/js/BuiltinsRegExp.cpp \
       src/js/BuiltinsDate.cpp \
       src/js/Json.cpp \
       src/js/Promise.cpp \
       src/js/Storage.cpp \
       src/js/EventLoop.cpp \
       src/js/Worker.cpp \
       src/js/DomBindings.cpp \
       src/js/DomEventBindings.cpp \
       src/js/DomNodeBindings.cpp \
       src/js/DomCollectionBindings.cpp \
       src/js/DomElementBindings.cpp \
       src/js/DomStyleBindings.cpp \
       src/js/DomDocumentBindings.cpp \
       src/js/DomTraversalBindings.cpp \
       src/js/DomHtmlBindings.cpp \
       src/js/DomFormBindings.cpp \
       src/js/DomFileBindings.cpp \
       src/js/DomUrlBindings.cpp \
       src/js/DomObserverBindings.cpp \
       src/js/DomWindowBindings.cpp \
       src/js/Generator.cpp \
       src/net/ResourceLoader.cpp \
       src/net/UrlParser.cpp \
       src/net/Fetch.cpp \
       src/net/WebSocket.cpp \
       src/net/AbortController.cpp \
       src/shell/BrowserShell.cpp

OBJS = $(SRCS:src/%.cpp=$(BUILD_DIR)/%.o)

TARGET = $(BUILD_DIR)/litebird
TEST_TARGET = $(BUILD_DIR)/test_suite
CONFORMANCE = python3 tools/conformance/run_tree_construction.py
JS_CONFORMANCE = python3 tools/conformance/run_js_conformance.py
DOM_CONFORMANCE = python3 tools/conformance/run_dom_conformance.py
# URL cases that must pass (tests/url). The 38 that do not are host names
# outside ASCII.
URL_FLOOR ?= 1150
# Cases that must pass in the sanitizer build. One below the CI scoreboard
# floor: a sanitizer build's stack frames are too large for one deep-recursion
# case that the normal build passes.
JS_ASAN_FLOOR ?= 833
# The DOM corpus must pass whole in the sanitizer build: none of its cases
# is about depth.
DOM_ASAN_FLOOR ?= 557

# The string type stands alone, so its stress tests can be built without the
# rest of the engine and run under sanitizers the whole engine cannot use yet.
STRING_STRESS_SRCS = src/js/JsString.cpp tests/js_string_stress_main.cpp
STRING_STRESS_FLAGS = -std=c++20 -O1 -g -Wall -Wextra -Iinclude -fno-omit-frame-pointer

# How many results of the string grid may differ from Node's. Every one of them
# today is Unicode data the engine does not have (collation, normalization,
# some case mappings, part of \p{L}); lower this as that data arrives.
STRING_DIFF_CEILING ?= 95

# Random pages at the parser's nesting limit, on top of the fixed deep pages:
# how many `make deep` runs, and how many the sanitizer build runs. The
# sanitizer build also nests its pages less deep (see asan-deep).
DEEP_RANDOM ?= 300
DEEP_ASAN_RANDOM ?= 150
DEEP_ASAN_DEPTH ?= 10000

.PHONY: all clean test run conformance js-conformance dom-conformance url-conformance diff-strings diff-trees fuzz deep asan asan-core asan-deep tsan bench help

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

$(TEST_TARGET): $(OBJS) tests/test_suite.cpp tests/js_string_stress.hpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(OBJS) tests/test_suite.cpp -o $@

test: $(TEST_TARGET)
	@./$(TEST_TARGET)

run: $(TARGET)
	@./$(TARGET) examples/hello.html

## Conformance scoreboard (html5lib / WPT tree-construction corpus).
conformance: $(TARGET)
	@$(CONFORMANCE)

## JavaScript scoreboard (tests/js-conformance; expectations taken from Node.js).
js-conformance: $(TARGET)
	@$(JS_CONFORMANCE)

## DOM scoreboard (tests/dom-conformance; expectations recorded from jsdom).
## Running it needs nothing but the binary. Checking or recording the
## expectations (--reference, --record) needs Node.js and, the first time, npm
## and the network: jsdom is installed into build/jsdom.
dom-conformance: $(TARGET)
	@$(DOM_CONFORMANCE)

## URL scoreboard: the web-platform-tests URL data (tests/url), run through
## `new URL()` and the setters of the object. Needs nothing but the binary.
url-conformance: $(TARGET)
	@python3 tools/conformance/run_url_conformance.py --min-pass $(URL_FLOOR)

## A grid of string expressions (subjects x methods, operators and regular
## expressions) evaluated by Litebird and by Node.js, results compared.
## Needs Node.js; there are no expectations to maintain.
diff-strings: $(TARGET)
	@python3 tools/conformance/diff_strings.py --show 5 --max-differ $(STRING_DIFF_CEILING)

## Random pages, a few dozen misnested tags each, parsed by Litebird and by
## parse5 (an independent HTML parser), trees compared: 1,500 pages of each of
## four kinds (plain; with <br/>, type=hidden, CDATA, U+0000 and the like; those
## as fragments; those with scripting disabled). The comparison is with a copy
## of parse5 in which the nine rules where it is not the standard are
## corrected (tools/conformance/diff_trees.py lists them), so no page may
## differ; each line also says how many pages equal parse5 as published. Needs
## Node.js and, the first time, npm and the network: parse5 is installed into
## build/parse5.
DIFF_TREES = python3 tools/conformance/diff_trees.py --binary $(TARGET) --corrected --show 0 --reduce --max-differ 0
diff-trees: $(TARGET)
	@$(DIFF_TREES)
	@$(DIFF_TREES) --extras
	@$(DIFF_TREES) --extras --fragments
	@$(DIFF_TREES) --extras --fragments --no-scripting

## Parser, layout, bitmap-renderer, and image-header mutation fuzzing.
fuzz:
	@python3 tools/fuzz/fuzz_parser.py
	@python3 tools/fuzz/fuzz_pipeline.py

## Pages that are hostile by size, not by content: one construct nested
## 200,000 levels (markup of every kind, trees built by script, CSS values),
## bitmaps of absurd dimensions, and 100,000 tokens after 200,000 elements
## left open. The mutation fuzzers above start from small pages and never get
## near any of this. Each run is a child process with an 8 MB stack, 2 GB of
## memory and 60 seconds; it fails on a signal, on a limit, or when the text
## after the deep part is missing from the tree, the layout or the paint list.
## Then random pages that keep the parser at its nesting limit. About ten
## minutes on two cores.
deep: $(TARGET)
	@python3 tools/fuzz/deep_pages.py --binary $(TARGET) --quiet --random $(DEEP_RANDOM)

## Address + UB sanitizer build, then the test suite, the JavaScript, DOM and
## URL corpora, a fuzz smoke run (asan-core) and the deep pages (asan-deep).
## LeakSanitizer is disabled: the JS engine has known pre-existing leaks that
## are tracked as a separate cleanup item (see ARCHITECTURE.md, "Known gaps").
asan: asan-core
	$(MAKE) asan-deep

## The corpus is run for what the sanitizers see while the scripts execute, not
## for its score: a sanitizer report makes a case fail, and --min-pass turns a
## batch of those into a failed build.
asan-core:
	ASAN_OPTIONS=detect_leaks=0 $(MAKE) BUILD_DIR=build-asan CXXFLAGS="$(ASAN_FLAGS)" all
	ASAN_OPTIONS=detect_leaks=0 $(MAKE) BUILD_DIR=build-asan CXXFLAGS="$(ASAN_FLAGS)" test
	ASAN_OPTIONS=detect_leaks=0 $(JS_CONFORMANCE) --binary build-asan/litebird --quiet --timeout 60 --min-pass $(JS_ASAN_FLOOR)
	ASAN_OPTIONS=detect_leaks=0 $(DOM_CONFORMANCE) --binary build-asan/litebird --quiet --timeout 60 --min-pass $(DOM_ASAN_FLOOR)
	ASAN_OPTIONS=detect_leaks=0 python3 tools/conformance/run_url_conformance.py --binary build-asan/litebird --timeout 600 --min-pass $(URL_FLOOR)
	ASAN_OPTIONS=detect_leaks=0 $(MAKE) BUILD_DIR=build-asan fuzz

## The deep pages in the sanitizer build made by asan-core. This run is for
## memory errors on those pages, not for recursion: that is found at full
## depth by `make deep`, and by a unit test that walks a chain of a million
## nodes. So the pages are a twentieth as deep here, which keeps a build that
## is several times slower to a few minutes. The stack is a quarter of the
## usual one, because what a sanitizer build can still get wrong is a walk
## with a fixed limit: its frames are ten times the size. For the same reason
## layout is not required to reach the bottom of a nest: it stops when the
## stack is nearly used up.
asan-deep:
	ASAN_OPTIONS=detect_leaks=0 python3 tools/fuzz/deep_pages.py --binary build-asan/litebird --quiet \
		--depth $(DEEP_ASAN_DEPTH) --stack-mb 2 --no-layout-check --timeout 300 --random $(DEEP_ASAN_RANDOM)

## The JavaScript string type under ThreadSanitizer, then under AddressSanitizer
## with leak checking on. Strings are shared between threads without locks
## (suspended async functions and workers run on their own threads), and that
## is a claim only ThreadSanitizer can check. Takes about a minute.
tsan:
	@mkdir -p build-tsan
	$(CXX) $(STRING_STRESS_FLAGS) -fsanitize=thread $(STRING_STRESS_SRCS) -o build-tsan/js_string_tsan
	./build-tsan/js_string_tsan
	$(CXX) $(STRING_STRESS_FLAGS) -fsanitize=address,undefined -fno-sanitize-recover=undefined $(STRING_STRESS_SRCS) -o build-tsan/js_string_asan
	./build-tsan/js_string_asan

## Times string-heavy scripts against Node.js when it is installed. Fails if a
## case takes seconds: each one does work that grows in step with its input.
bench: $(TARGET)
	@python3 tools/bench/bench_strings.py --max-ms 3000

clean:
	rm -rf build build-asan build-tsan

help:
	@echo "Targets: all (default), test, run, conformance, js-conformance, dom-conformance, url-conformance, diff-strings, diff-trees, fuzz, deep, asan (asan-core, asan-deep), tsan, bench, clean"
