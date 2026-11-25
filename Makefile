CXX ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Iinclude -MMD -MP
BUILD_DIR ?= build

## -fno-var-tracking: with -g and optimisation GCC works out, for a
## debugger, where every variable lives at every instruction. Under the
## sanitizers that one pass was more than half of the compile time of the
## two files it was measured on here: 54 seconds of src/layout/LayoutBox.cpp
## against 22 without it, 87 of src/css/StyleResolver.cpp against 37 (24
## November 2026, g++ 12, one core). In CI, where the whole engine was
## compiled without it once (run 45471158220), a job's build took 2.3 to
## 3.0 seconds for each object it had to compile, against 3.5 and 4.3 in
## the two lots measured with it (other objects, so the two are not the
## same work). Nothing a sanitizer reports comes from the pass: a report
## names functions, files and lines, and those stay. A debugger on this
## build may say "optimized out" of a variable where it used to show it.
ASAN_FLAGS = -std=c++20 -O1 -g -fno-var-tracking -Wall -Wextra -Iinclude -MMD -MP \
             -fsanitize=address,undefined -fno-omit-frame-pointer

SRCS = src/common/Color.cpp \
       src/common/TerminalText.cpp \
       src/dom/Node.cpp \
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
       src/xml/XmlParser.cpp \
       src/xml/XmlTools.cpp \
       src/xml/XmlDom.cpp \
       src/css/CssParser.cpp \
       src/css/DocumentStyles.cpp \
       src/css/StyleResolver.cpp \
       src/layout/LayoutBox.cpp \
       src/layout/LayoutTree.cpp \
       src/layout/FloatTableLayout.cpp \
       src/render/PaintCommand.cpp \
       src/render/ImageDecoder.cpp \
       src/render/AvifContainer.cpp \
       src/render/FontRenderer.cpp \
       src/render/JpegDecoder.cpp \
       src/render/GifDecoder.cpp \
       src/render/Vp8Decoder.cpp src/render/Vp8Tables.cpp src/render/WebPDecoder.cpp \
       src/render/Canvas2D.cpp \
       src/render/MediaAudioCodecs.cpp \
       src/render/WebGL.cpp \
       src/render/SvgRenderer.cpp \
       src/render/TerminalRenderer.cpp \
       src/render/TerminalWidthTables.cpp \
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
       src/js/TypedArray.cpp \
       src/js/TextCodec.cpp \
       src/js/JsEval.cpp \
       src/js/JsOps.cpp \
       src/js/BuiltinsObject.cpp \
       src/js/BuiltinsArray.cpp \
       src/js/BuiltinsString.cpp \
       src/js/StringUnicodeTables.cpp \
       src/js/BuiltinsNumber.cpp \
       src/js/BuiltinsCollections.cpp \
       src/js/BuiltinsJson.cpp \
       src/js/BuiltinsRegExp.cpp \
       src/js/RegExpUnicodeTables.cpp \
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
       src/js/DomMediaBindings.cpp \
       src/js/DomFormBindings.cpp \
       src/js/DomFileBindings.cpp \
       src/js/DomCryptoBindings.cpp \
       src/js/DomUrlBindings.cpp \
       src/js/DomObserverBindings.cpp \
       src/js/DomWindowBindings.cpp \
       src/js/Generator.cpp \
       src/net/ResourceLoader.cpp \
       src/net/Brotli.cpp \
       src/net/BrotliTables.cpp \
       src/net/Deflate.cpp \
       src/net/UrlParser.cpp \
       src/net/DataUrl.cpp \
       src/net/TextEncoding.cpp \
       src/net/TextEncodingTables.cpp \
       src/net/Idna.cpp \
       src/net/IdnaTables.cpp \
       src/net/Fetch.cpp \
       src/net/WebSocket.cpp \
       src/net/AbortController.cpp \
       src/shell/BrowserShell.cpp \
       src/shell/WebBrowserServer.cpp \
       src/text/Bidi.cpp \
       src/text/BidiTables.cpp

OBJS = $(SRCS:src/%.cpp=$(BUILD_DIR)/%.o)

TARGET = $(BUILD_DIR)/litebird
TEST_TARGET = $(BUILD_DIR)/test_suite
CROSS_ORIGIN_TARGET = $(BUILD_DIR)/cross_origin_test
MEDIA_CHECK_TARGET = $(BUILD_DIR)/media_check
COLOR_CHECK_TARGET = $(BUILD_DIR)/color_check
BROTLI_CHECK_TARGET = $(BUILD_DIR)/brotli_check
BIDI_CHECK_TARGET = $(BUILD_DIR)/bidi_check
CONFORMANCE = python3 tools/conformance/run_tree_construction.py
JS_CONFORMANCE = python3 tools/conformance/run_js_conformance.py
DOM_CONFORMANCE = python3 tools/conformance/run_dom_conformance.py
XML_CONFORMANCE = python3 tools/conformance/run_xml_conformance.py
WPT = python3 tools/conformance/run_wpt.py
# Documents of the W3C XML suite that must be read as the suite says: every
# one that applies to a parser of this kind (tests/xml/README.md says which).
XML_FLOOR ?= 1718
# Subtests of the web-platform-tests in tests/wpt that must pass. Each of the
# others is listed with its reason in tests/wpt/expected-failures.txt; the
# runner also fails on one that fails and is not listed.
WPT_FLOOR ?= 9570
# Generated documents for `make diff-xml`, and trees for its round trip; the
# sanitizer build runs fewer of each.
XML_DIFF_COUNT ?= 4000
XML_ROUNDTRIP_COUNT ?= 3000
XML_ASAN_DIFF_COUNT ?= 1000
XML_ASAN_ROUNDTRIP_COUNT ?= 1000
# URL cases that must pass (tests/url). The UTS #46 host handling makes all
# of them pass.
URL_FLOOR ?= 1188
# Cases that must pass in the sanitizer build. One below what the sanitizer build
# scores: a sanitizer build's stack frames are too large for one deep-recursion
# case that the normal build passes. With Phases 37, 43, 44, 45, 55, 59, and 60
# together, 1019 of 1034 pass under ASan and 1020 in Release (which is what
# the CI floor requires). Leave one case of runner headroom. Phase 63 added
# iteration.dat: 65 of its 78 cases pass, under ASan as in Release. Phase 65
# added eval.dat (37 cases, all passing) and fixed two cases construct.dat
# recorded as failing, so 1122 of 1149 pass under ASan and 1123 in Release.
# Phase 81 made `delete a[1]` leave a hole, which is what arrays.dat's *delete
# leaves a hole* had always expected: 1 more, so leave one case of headroom
# under 1129 (one deep-recursion case passes in Release and not under ASan).
JS_ASAN_FLOOR ?= 1122
# The DOM corpus must pass whole in the sanitizer build: none of its cases
# is about depth.
DOM_ASAN_FLOOR ?= 678

# The string type stands alone, so its stress tests can be built without the
# rest of the engine and run under sanitizers the whole engine cannot use yet.
STRING_STRESS_SRCS = src/js/JsString.cpp tests/js_string_stress_main.cpp
STRING_STRESS_FLAGS = -std=c++20 -O1 -g -Wall -Wextra -Iinclude -fno-omit-frame-pointer

# How many results of the string grid may differ from Node's. Phase 55 removed
# the 24 \p{L} RegExp mismatches (95 -> 71); Phase 60 landed Unicode 17.0
# normalization, full/locale case mapping, and CLDR 48 root collation, bringing
# all 50,580 results to 0 diffs against Node.js.
STRING_DIFF_CEILING ?= 0

# Random pages at the parser's nesting limit, on top of the fixed deep pages:
# how many `make deep` runs, and how many the sanitizer build runs. The
# sanitizer build also nests its pages less deep (see asan-deep).
DEEP_RANDOM ?= 300
DEEP_ASAN_RANDOM ?= 150
DEEP_ASAN_DEPTH ?= 10000

.PHONY: all clean test test-in-stretches run conformance js-conformance test262 dom-conformance url-conformance xml-conformance wpt diff-strings verify-regexp verify-string-unicode diff-trees diff-xml fuzz fuzz-media verify-media verify-avif fuzz-color verify-color verify-brotli fuzz-brotli verify-gc-stress verify-gc-stress-full gc-oracle deep asan asan-core asan-deep tsan bench bench-floats bench-collections bench-styles check-numbers metrics metrics-suites bench-css bench-attributes bench-tokenizer-attributes bench-surrogates bench-js-core verify-promise-iterators verify-collection-iterators security-fetch security-https security-worker security-websocket check-suites help

all: $(TARGET)

## Compare query/fragment-only resolution with Node's URL implementation.
.PHONY: verify-relative-navigation
.PHONY: verify-bytecode
verify-relative-navigation: $(BUILD_DIR)/relative_navigation_fixture
	@python3 tools/conformance/verify_relative_navigation.py --binary $(BUILD_DIR)/relative_navigation_fixture

## Compare path-bearing URL references against Node's URL implementation.
.PHONY: verify-path-resolution
verify-path-resolution: $(BUILD_DIR)/relative_navigation_fixture
	@python3 tools/conformance/verify_path_resolution.py --binary $(BUILD_DIR)/relative_navigation_fixture

$(BUILD_DIR)/relative_navigation_fixture: $(OBJS) tools/conformance/relative_navigation_fixture.cpp
	$(CXX) $(CXXFLAGS) $(OBJS) tools/conformance/relative_navigation_fixture.cpp -o $@

## Compare TextDecoder and TextEncoder with Node, case by case.
.PHONY: verify-text-codec
verify-text-codec: $(TARGET)
	@python3 tools/conformance/verify_text_codec.py $(TARGET)

## Compare the bytecode VM's answer with the tree-walker's, case by case.
.PHONY: verify-bytecode
verify-bytecode: $(BUILD_DIR)/bytecode_probe
	@python3 tools/conformance/verify_bytecode_agreement.py $(BUILD_DIR)/bytecode_probe

$(BUILD_DIR)/bytecode_probe: $(OBJS) tools/conformance/bytecode_probe.cpp
	$(CXX) $(CXXFLAGS) $(OBJS) tools/conformance/bytecode_probe.cpp -o $@

## The original offline driver, for rechecking exposed holdout-02 tasks.
$(BUILD_DIR)/holdout_02_driver: $(OBJS) tools/real_pages/holdout-02/drive.cpp
	$(CXX) $(CXXFLAGS) $(OBJS) tools/real_pages/holdout-02/drive.cpp -o $@

## Check untrusted shell labels without writing attack bytes to a terminal.
.PHONY: verify-terminal-shell-safety
verify-terminal-shell-safety: $(TARGET)
	@python3 tools/conformance/verify_terminal_shell_safety.py --binary $(TARGET)

## Check unhandled worker diagnostics without printing attack bytes.
.PHONY: verify-worker-terminal-safety
verify-worker-terminal-safety: $(TARGET)
	@python3 tools/security/verify_worker_error_terminal.py --binary $(TARGET)

## Check uncaught JS diagnostics without printing attack bytes.
.PHONY: verify-js-error-terminal-safety
verify-js-error-terminal-safety: $(TARGET)
	@python3 tools/security/verify_js_error_terminal.py --binary $(TARGET)

## Check CLI file and navigation error output without displaying attack bytes.
.PHONY: verify-cli-error-terminal-safety
verify-cli-error-terminal-safety: $(TARGET)
	@python3 tools/security/verify_cli_error_terminal.py --binary $(TARGET)

## Check that a page's own text cannot command the terminal through a dump.
.PHONY: verify-page-text-terminal-safety
verify-page-text-terminal-safety: $(TARGET)
	@python3 tools/security/verify_page_text_terminal.py --binary $(TARGET)

## Check CSS/paint dumps and the shared UTF-8 terminal boundary.
.PHONY: verify-css-paint-terminal-safety
verify-css-paint-terminal-safety: $(TARGET) $(BUILD_DIR)/terminal_text_probe
	@python3 tools/security/verify_css_paint_terminal.py --binary $(TARGET) --probe $(BUILD_DIR)/terminal_text_probe

## Capture console attacks without displaying them on a terminal.
.PHONY: verify-console-terminal-safety
verify-console-terminal-safety: $(TARGET)
	@python3 tools/security/verify_console_terminal.py --binary $(TARGET)

## Capture bytecode dump and compiler-diagnostic attacks safely.
.PHONY: verify-bytecode-terminal-safety
verify-bytecode-terminal-safety: $(TARGET)
	@python3 tools/security/verify_bytecode_terminal.py --binary $(TARGET)

## Capture REPL results and CLI echo attacks safely.
.PHONY: verify-cli-echo-terminal-safety
verify-cli-echo-terminal-safety: $(TARGET)
	@python3 tools/security/verify_cli_echo_terminal.py --binary $(TARGET)

## Capture page-controlled layout dump attacks without displaying them.
.PHONY: verify-layout-terminal-safety
verify-layout-terminal-safety: $(TARGET)
	@python3 tools/security/verify_layout_terminal.py --binary $(TARGET)

$(BUILD_DIR)/terminal_text_probe: tools/security/terminal_text_probe.cpp $(BUILD_DIR)/common/TerminalText.o
	$(CXX) $(CXXFLAGS) $^ -o $@

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

## The suites that are files of their own (tests/suites/README.md). A new suite
## is one new file there and no line anywhere else, so that no two pull requests
## change the same place for it. tests/test_suite.cpp reads them through a list
## that is written here from the names of the files: for each a namespace named
## after the file, and inside it the file. The list is written at every build
## and replaced only when it would say something else, so a build with nothing
## new compiles nothing, and a file that was added, removed or renamed is seen.
SUITE_FILES := $(sort $(wildcard tests/suites/*.cpp))
SUITE_LIST := $(BUILD_DIR)/suite_files.inc

.PHONY: FORCE
FORCE:

$(SUITE_LIST): FORCE
	@mkdir -p $(BUILD_DIR)
	@{ echo '// Written by the Makefile from the names in tests/suites/. Not kept in git.'; \
	   for f in $(SUITE_FILES); do \
	     n=$$(basename "$$f" .cpp | tr -c 'A-Za-z0-9\n' '_'); \
	     printf 'namespace suite_%s {\n#include "%s"\n}\n' "$$n" "$$f"; \
	   done; } > $@.new
	@cmp -s $@.new $@ && rm -f $@.new || mv $@.new $@

## The suites are one translation unit: tests/test_suite.cpp and, through
## the list above, every file of tests/suites/, 43,000 lines in all. One
## compiler took 64 seconds for it here at -O2 and takes 147 in the
## sanitizer build in CI, which is most of `make test` after any change.
## It is compiled to an object of its own and linked afterwards. Until 24
## November 2026 one command did both with the engine's objects on its
## line, so it could only begin when the last of them was there; now
## `make -j` compiles it beside the engine (`make -j2 test` from nothing is
## the longer of the two, not their sum). ccache is told to leave this one
## alone: nearly every change adds a suite or touches a header it includes,
## so the object would be kept and almost never asked for again, in a cache
## that is full of the engine's.
##
## In the plain build the suites are compiled without optimisation
## ($(TEST_OPT)): 21 seconds here on 25 November 2026 where -O2 had taken 64
## the day before, and the 462 suites then ran for 51 seconds where they had
## run for 44, because what a suite spends its time in is the engine, whose
## objects are compiled as they always were. That is half a minute less of
## every `make test` here, and in CI the step went from 86 to 106 seconds
## to 65 (run 45544470867), which made "Build & unit tests" 2.1 minutes
## where it had been 2.6 to 3.0. A build with a sanitizer keeps the
## optimisation its own flags name: its suites are several times slower
## already, and some fifty places in them measure time.
TEST_OPT ?= $(if $(findstring -fsanitize,$(CXXFLAGS)),,-O0)
TEST_OBJ := $(BUILD_DIR)/test_suite.o
$(TEST_OBJ): tests/test_suite.cpp tests/js_string_stress.hpp $(SUITE_FILES) $(SUITE_LIST)
	@mkdir -p $(BUILD_DIR)
	CCACHE_DISABLE=1 $(CXX) $(CXXFLAGS) $(TEST_OPT) -iquote . -iquote $(BUILD_DIR) -c tests/test_suite.cpp -o $@

-include $(TEST_OBJ:.o=.d)

$(TEST_TARGET): $(OBJS) $(TEST_OBJ)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(OBJS) $(TEST_OBJ) -o $@

$(CROSS_ORIGIN_TARGET): $(OBJS) tests/cross_origin_windows.cpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(OBJS) tests/cross_origin_windows.cpp -o $@

$(MEDIA_CHECK_TARGET): $(OBJS) tools/media_check.cpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(OBJS) tools/media_check.cpp -o $@

$(COLOR_CHECK_TARGET): $(BUILD_DIR)/common/Color.o tools/color_check.cpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(BUILD_DIR)/common/Color.o tools/color_check.cpp -o $@

## The Unicode Bidirectional Algorithm's conformance suites (tests/bidi), run
## against the two generated tables and the algorithm alone.
$(BIDI_CHECK_TARGET): $(BUILD_DIR)/text/Bidi.o $(BUILD_DIR)/text/BidiTables.o tools/conformance/bidi_check.cpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(BUILD_DIR)/text/Bidi.o $(BUILD_DIR)/text/BidiTables.o tools/conformance/bidi_check.cpp -o $@

$(BROTLI_CHECK_TARGET): $(BUILD_DIR)/net/Brotli.o $(BUILD_DIR)/net/BrotliTables.o tools/brotli_check.cpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(BUILD_DIR)/net/Brotli.o $(BUILD_DIR)/net/BrotliTables.o tools/brotli_check.cpp -o $@

test: $(TEST_TARGET) $(CROSS_ORIGIN_TARGET)
	@./$(TEST_TARGET)
	@./$(CROSS_ORIGIN_TARGET)

## `make test` with the suites as $(STRETCHES) processes side by side: every
## suite is run once, by one of them (tools/ci/run_stretches.py). STRETCHES
## is a number of equal stretches, or `balanced`: the stretches that begin
## at the start of the order and at the suites UNIT_SUITE_CUTS names.
## Equal stretches are equal in suites, not in time: in the sanitizer
## build of 24 November 2026 three of them took 22, 322 and 24 seconds,
## because the suites that nest pages 200,000 deep stand together in the
## middle of the order (79, 37 and 30 seconds of the 322). The cuts put the
## first of those and the next two into stretches of their own, and those
## two stretches (UNIT_SUITE_LATE) are started when the others have ended:
## some fifty places in the suites measure time, and one of them failed in
## its sixth second beside a process that was turning over 5 GB. When a run
## says the stretches have drifted apart (its last lines, and in CI a notice
## on the job, give the time of each and name the slowest suites), move a
## cut: a name here must be a suite of tests/test_suite.cpp's own list, in
## the order of that list, or every stretch fails.
##
## The five names make six stretches. The second and the fourth cut out the
## deep suites (stretches 2 and 3, the late ones); the first and the last
## halve what stands before and after them, at the suites a run named as
## the middle of each ("half of it over when ... began", run 45471158220):
## four stretches side by side on the four cores of CI's machines, then the
## two deep ones. In the sanitizer build that was 117 seconds (run
## 45473629683): 61, 56, 64 and 55 for the four, then 53 beside 35. As four
## stretches, two and then two, it had been 140: 77 beside 85, then 55
## beside 36. Twice the processes were not twice as fast: the four took 236
## seconds between them for what the two had done in 162, so a seventh
## stretch would buy little, and the deep ones are now the larger part.
## The machine counts for as much as the cuts: in the next run (45475379777)
## the same six took 89, 79, 69 and 75, then 87 beside 63, and one of the
## four failed a bound of five seconds beside the others and passed when it
## was run again by itself (run_stretches.py does that): 245 seconds.
##
## UNIT_SUITE_ONLY=early runs the stretches that are not late, and
## UNIT_SUITE_ONLY=late the late ones and nothing else: two calls for two
## machines, which is what CI's two jobs for the unit suites make since 25
## November 2026. There the deep ones had been 72 to 87 seconds that began
## when the other four had taken 102 to 127, in the longest job of a run
## (5.8 to 6.2 minutes where the next was 4.2 to 4.8); the binary takes
## 90 to 147 seconds to compile in each job, which the second one spends
## while the first does. The cross-origin test binary is the first call's.
## Without UNIT_SUITE_ONLY everything is run, as before.
STRETCHES ?= 3
UNIT_SUITE_CUTS ?= inner_html_of_an_xml_document_is_xml,deep_nesting_never_exhausts_the_native_stack,dom_trees_of_any_depth_can_be_walked_and_released,html_parser_void_element_with_a_slash_closes_nothing_else,svg_real_world_paths_arcs_gradients_use_clip_and_guards
UNIT_SUITE_LATE ?= 2,3
UNIT_SUITE_ONLY ?=
ifneq ($(filter-out early late,$(UNIT_SUITE_ONLY)),)
$(error UNIT_SUITE_ONLY is "$(UNIT_SUITE_ONLY)": it is early, late, or nothing)
endif
test-in-stretches: $(TEST_TARGET) $(if $(filter late,$(UNIT_SUITE_ONLY)),,$(CROSS_ORIGIN_TARGET))
	@$(if $(UNIT_SUITE_ONLY),$(if $(filter balanced,$(STRETCHES)),true,echo 'UNIT_SUITE_ONLY=$(UNIT_SUITE_ONLY) means something only with STRETCHES=balanced: with STRETCHES=$(STRETCHES) no stretch is late' >&2; exit 2),true)
	@python3 tools/ci/run_stretches.py ./$(TEST_TARGET) $(if $(filter balanced,$(STRETCHES)),$(UNIT_SUITE_CUTS) $(UNIT_SUITE_LATE) $(UNIT_SUITE_ONLY),$(STRETCHES))
	@$(if $(filter late,$(UNIT_SUITE_ONLY)),true,./$(CROSS_ORIGIN_TARGET))

## The numbers the documents publish: every "A / B (P%)" must divide, the two
## corpus totals must equal the cases on disk, and every figure a document marks
## with `<!--metrics:key/shape-->` must equal docs/metrics.json or the tree.
## The suite count ("214 suites") is reported when README.md is behind and fails
## nothing: no pull request edits it (AGENTS.md, "Working together").
## See tools/ci/check_numbers.py.
check-numbers:
	@python3 tools/ci/check_numbers.py

## How the suites in tests/test_suite.cpp begin. A suite that writes the six-line
## page prologue itself merges badly with another new one: git sees the shared
## lines as context between two differences and produces two conflict regions
## with the prologue outside both, so either side of "ours"/"theirs" yields a
## function carrying the other branch's body. Through `TestPage` the same merge
## is one conflict enclosing the whole function. The count of suites still
## writing it themselves is reported, not failed on. See tools/ci/check_suites.py.
check-suites:
	@python3 tools/ci/check_suites.py

## Refresh every marked figure from docs/metrics.json and the tree, and raise the
## `--min-pass` floors in ci.yml to the scores the tree reaches. This is what to
## run when a pull request has moved a score or a corpus.
## It never lowers a floor: that is a regression, and it is reported instead.
## It leaves the suite count of README.md as it is written: see `metrics-suites`.
metrics:
	@python3 tools/ci/check_numbers.py --write

## The same, and the suite count of README.md with it. For the landing session,
## as the last act of a watch (AGENTS.md, "Working together"): a pull request
## that rewrote the count would be in conflict with every other one that did.
metrics-suites:
	@python3 tools/ci/check_numbers.py --write --suites

## Float-index speed guard: the old sweep needs >5 s for 10k packed floats
## and >12 s for mixed sides; the new index needs ~0.1 s per page locally.
bench-floats: $(TARGET)
	@python3 tools/bench/bench_sized_floats.py $(TARGET) --out $(BUILD_DIR)/bench-floats --sizes 1000 10000 --max-seconds 4

## Completed Map/Set iterators must not make a later equal batch slower. The
## runner has its own checksums and makes both batches in one process, without gc().
bench-collections: $(TARGET)
	@python3 tools/bench/bench_collection_iterators.py $(TARGET) --rounds 8192 --reps 3 --max-growth 2.25 --slack-ms 40

## The tree-walker's own speed: six scripts (a loop, a property loop, a call, a
## recursion, an array, a method) run inside a function, against Node.js on the
## same box. Medians of interleaved runs, so a busy machine does not decide the
## answer. `BENCH_JS_MAX_RATIO` turns it into a floor for CI; leave it empty to
## only report. Row 7 of ROADMAP.md's Tier 2 is this number.
BENCH_JS_MAX_RATIO ?=
bench-js-core: $(TARGET)
	@python3 tools/bench/bench_js_core.py $(TARGET) $(if $(BENCH_JS_MAX_RATIO),--max-ratio $(BENCH_JS_MAX_RATIO),) \
		$(if $(BENCH_JS_BASELINE),--baseline $(BENCH_JS_BASELINE),)

## Explicit font required; install tools/shaping/requirements.txt in a venv.
SHAPING_PYTHON ?= python3
SHAPING_FONT ?=
.PHONY: shaping-oracle shaping-oracle-self-test
$(BUILD_DIR)/shaping-probe: tools/shaping/probe.cpp src/render/FontRenderer.cpp src/js/StringUnicodeTables.cpp include/litebird/render/FontRenderer.hpp include/litebird/js/StringUnicodeTables.hpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) tools/shaping/probe.cpp src/render/FontRenderer.cpp src/js/StringUnicodeTables.cpp -o $@

shaping-oracle: $(BUILD_DIR)/shaping-probe
	$(SHAPING_PYTHON) tools/shaping/compare.py --probe $(BUILD_DIR)/shaping-probe --font "$(SHAPING_FONT)" --output $(BUILD_DIR)/shaping-report.json

shaping-oracle-self-test:
	python3 tools/shaping/compare.py --self-test

## GC oracle: the collector's answer through the CLI, the way a page meets it.
## Cycles must be reclaimed, repeating churn must not grow the live set, what
## a global names must stay intact, and collected_cells must end above zero.
gc-oracle: $(TARGET)
	@python3 tools/bench/gc_oracle.py $(TARGET)

## Style-heavy inline pages share deduplicated ComputedStyle instances across
## boxes and inline fragments; 100k `word <b>bold</b>` took ~1,382 MiB before.
bench-styles: $(TARGET)
	@python3 tools/bench/bench_shared_style.py $(TARGET) --max-rss-mb 500 --max-seconds 10

run: $(TARGET)
	@./$(TARGET) examples/hello.html

## Compare full RGBA screenshots from two builds (strict equality by default).
## Example: make diff-render BEFORE=/tmp/old/litebird AFTER=build/litebird
## Pass DIFF_RENDER_ARGS="--suite tests/render/body-origin.json" for another corpus.
.PHONY: diff-render diff-render-self-test
diff-render:
	@python3 tools/conformance/diff_render.py --before "$(BEFORE)" --after "$(AFTER)" $(DIFF_RENDER_ARGS)

diff-render-self-test:
	@python3 tools/conformance/test_diff_render.py

## Conformance scoreboard (html5lib / WPT tree-construction corpus).
conformance: $(TARGET)
	@$(CONFORMANCE)

## JavaScript scoreboard (tests/js-conformance; expectations taken from Node.js).
js-conformance: $(TARGET)
	@$(JS_CONFORMANCE)

## The four Promise combinators must resolve each yielded element before next().
## The hostile iterators in async.dat never end normally; old code skipped
## resolve(), overran the iterator or timed out. All 34 cases must pass.
verify-promise-iterators: $(TARGET)
	@$(JS_CONFORMANCE) --filter async --quiet --timeout 3 --min-pass 34

## Map/Set compaction, clear, insertion order, and done-latching are recorded
## against Node. The two other cases in this file require absent Uint8Array and Proxy.
verify-collection-iterators: $(TARGET)
	@$(JS_CONFORMANCE) --filter collections-iterators --quiet --min-pass 32

## DOM scoreboard (tests/dom-conformance; expectations recorded from jsdom).
## Running it needs nothing but the binary. Checking or recording the
## expectations (--reference, --record) needs Node.js and, the first time, npm
## and the network: jsdom is installed into build/jsdom.
dom-conformance: $(TARGET)
	@$(DOM_CONFORMANCE)

## What the page actually looks like. `ROADMAP.md` Tier 1 row 8 said nothing
## measured a pixel; this reads the PNG back with its own decoder and compares
## it with what each reftest asserts.
render-conformance: $(TARGET)
	@python3 tools/conformance/render_check.py --binary $(TARGET)
	@python3 tools/conformance/render_check.py --binary $(TARGET) --suite tests/render/body-origin.json --no-self-test
	@python3 tools/conformance/canvas_to_data_url.py --binary $(TARGET)

## Twenty-one directories of test262, the suite of the language's standard:
## what a change to functions, classes, prototypes or property definition
## can move. Downloaded the first time into build/test262, at a pinned
## commit; needs the network once. It is not a scoreboard and has no floor
## (about four fifths pass). It is for the difference between two builds,
##   python3 tools/conformance/run_test262.py --binary before=PATH --binary now=build/litebird
## which lists every test that passes in the first and not in the second,
## and fails if there is one. About three minutes for two builds.
test262: $(TARGET)
	@python3 tools/conformance/run_test262.py

## URL scoreboard: the web-platform-tests URL data (tests/url), run through
## `new URL()` and the setters of the object. Needs nothing but the binary.
url-conformance: $(TARGET)
	@python3 tools/conformance/run_url_conformance.py --min-pass $(URL_FLOOR)

## XML scoreboard: the W3C XML Conformance Test Suite through the XML parser
## alone (litebird --xml-canonical): what must be accepted, what must be
## refused, and the canonical form of 261 documents. The suite is not in the
## repository (tests/xml/README.md says why): the first run downloads it
## into build/xml-conformance and uses it only if its SHA-256 is the pinned
## one. With no network and no file the target fails; it never passes with
## the suite not run.
xml-conformance: $(TARGET)
	@$(XML_CONFORMANCE) --min-pass $(XML_FLOOR)

## The Unicode Bidirectional Algorithm against Unicode's own conformance
## suites: BidiTest.txt (sequences of Bidi_Class values, 770,241 cases over
## the paragraph directions each line allows) and BidiCharacterTest.txt (code
## points, so bracket pairs and mirroring too, 91,707 cases). Both are
## vendored gzipped in tests/bidi, byte for byte as the Unicode Consortium
## publishes them; tests/bidi/README.md records where from. This target needs
## the algorithm and the two generated tables, not the engine.
.PHONY: bidi-conformance
bidi-conformance: $(BIDI_CHECK_TARGET)
	@mkdir -p $(BUILD_DIR)/bidi
	@gzip -dc tests/bidi/BidiTest.txt.gz > $(BUILD_DIR)/bidi/BidiTest.txt
	@gzip -dc tests/bidi/BidiCharacterTest.txt.gz > $(BUILD_DIR)/bidi/BidiCharacterTest.txt
	@$(BIDI_CHECK_TARGET) $(BUILD_DIR)/bidi/BidiTest.txt $(BUILD_DIR)/bidi/BidiCharacterTest.txt

## The web-platform-tests in tests/wpt (the DOM suites, and the CSS Grid and
## Flexbox parsing suites), the tests browsers are tested with, run with their
## own testharness.js. Needs nothing but the binary. Fails when fewer than
## the floor pass, when a subtest fails that tests/wpt/expected-failures.txt
## does not list, when a listed one passes, and when a file differs from
## tests/wpt/MANIFEST.txt.
wpt: $(TARGET)
	@$(WPT) --min-pass $(WPT_FLOOR)

## Documents nobody wrote, half of them damaged, read by Litebird, by Expat
## and by libxml2: no document the two read alike may be read otherwise.
## Then trees with colliding prefixes and declarations, written by
## XMLSerializer and read back by Litebird and by Expat: every one must come
## back the same tree. Needs lxml for libxml2 (pip install lxml).
diff-xml: $(TARGET)
	@python3 tools/fuzz/diff_xml.py --binary $(TARGET) --count $(XML_DIFF_COUNT)
	@python3 tools/fuzz/xml_roundtrip.py --binary $(TARGET) --count $(XML_ROUNDTRIP_COUNT)

## A grid of string expressions (subjects x methods, operators and regular
## expressions) evaluated by Litebird and by Node.js, results compared.
## Needs Node.js; there are no expectations to maintain.
diff-strings: $(TARGET)
	@python3 tools/conformance/diff_strings.py --show 5 --max-differ $(STRING_DIFF_CEILING)

## Every decoder held to the index table it reads: 27 single-byte tables both
## ways, all 23,940 euc-kr / 19,782 big5 / 11,280 shift_jis pointers, and every
## gb18030 range. Then the same tables against Node's TextDecoder, which is a
## second implementation and is wrong on three encodings this tool names. Needs
## Node.js for the cross-check (`--no-node` skips it).
verify-encoding-tables: $(TARGET)
	@python3 tools/conformance/verify_encoding_tables.py --build

## Every table held to the standard that publishes it, not to this engine.
## The tool above asks each decoder whether it agrees with the table it reads,
## and a decoder and its table can be wrong together. This one fetches
## `whatwg/encoding`'s own index files at a pinned digest and compares all
## 91,441 positions with `src/net/TextEncodingTables.cpp`. Needs the network
## unless `build/whatwg-encoding-index/` is already populated (`--offline`).
verify-encoding-index: $(TARGET)
	@python3 tools/conformance/verify_encoding_index.py

## Differential + seeded fuzz verification of RegExp Unicode 17.0 properties,
## v (unicodeSets) set operations, d (hasIndices), and ES2025 modifiers,
## duplicate named capturing groups, and RegExp.escape().
verify-regexp: $(TARGET)
	@python3 tools/conformance/verify_regexp.py --binary $(TARGET)

## Exhaustive all-1,114,112 code-point sweep + curated/seeded differential fuzz
## verification of Unicode 17.0 String normalize(), full/locale case mapping,
## and CLDR 48 root UCA localeCompare() against Node.js.
verify-string-unicode: $(TARGET)
	@python3 tools/conformance/verify_string_unicode.py --binary $(TARGET)

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

## Parser, layout, bitmap-renderer, and image-header mutation fuzzing, and
## the same for XML: documents of the W3C suite (when `make xml-conformance`
## has downloaded it) with bytes changed, cut short and pasted over, read as
## bytes and through XMLHttpRequest, DOMParser and XMLSerializer.
##
## Six parts that share nothing but the binaries they are given: each makes
## its inputs from a seed of its own and keeps what it writes in a directory
## of its own. `make fuzz` runs them one after another, in this order, as it
## always has. `make -j4 fuzz` runs four at a time: until 25 November 2026
## the six were six lines of one recipe, so CI's job for them, which has
## four cores, used one of them for 193 to 266 seconds, and was the second
## longest job of a run.
FUZZ_PARTS := fuzz-pages fuzz-pipeline fuzz-xml fuzz-media fuzz-color fuzz-brotli
.PHONY: fuzz $(FUZZ_PARTS)
fuzz: $(FUZZ_PARTS)

fuzz-pages:
	@python3 tools/fuzz/fuzz_parser.py

fuzz-pipeline:
	@python3 tools/fuzz/fuzz_pipeline.py

fuzz-xml:
	@python3 tools/fuzz/fuzz_xml.py

## Pages that are hostile by size, not by content: one construct nested
## 200,000 levels (markup of every kind, trees built by script, CSS values),
## bitmaps of absurd dimensions, and 100,000 tokens after 200,000 elements
## left open. The mutation fuzzers above start from small pages and never get
## near any of this. Each run is a child process with an 8 MB stack, 2 GB of
## memory and 60 seconds; it fails on a signal, on a limit, or when the text
## after the deep part is missing from the tree, the layout or the paint list.
## Then random pages that keep the parser at its nesting limit. About ten
## minutes on two cores.
##
## CI runs it as three jobs side by side, `make deep DEEP_KINDS="--runs
## dumps"`, `"--runs terminal"` and `"--not-runs dumps,terminal"`: every
## page is given to the engine three ways (dumps, terminal, html5lib), each
## a run of its own, and the dumps were 530 of 1,270 seconds of work on 24
## November 2026, when the one job was the third longest of a run. As two
## jobs, the dumps and the rest, the rest was the longest job of a run the
## next day whenever no slower machine made another one longer: 244 to 247
## seconds of pages on four cores, 448 seconds of work for the terminal
## and 526 for html5lib, beside 137 to 169 for the dumps. "Every kind but
## those two" and not the third name, so that a kind added to
## tools/fuzz/deep_pages.py is run.
DEEP_KINDS ?=
deep: $(TARGET)
	@python3 tools/fuzz/deep_pages.py --binary $(TARGET) --quiet --random $(DEEP_RANDOM) $(DEEP_KINDS)

## Address + UB sanitizer build, then the test suite, the JavaScript, DOM and
## URL corpora, the XML suite, the web-platform-tests, generated XML, a fuzz
## smoke run (asan-core) and the deep pages (asan-deep).
## LeakSanitizer is disabled: the JS engine has known pre-existing leaks that
## are tracked as a separate cleanup item (see ARCHITECTURE.md, "Known gaps").
asan: asan-core
	$(MAKE) asan-deep

## What the sanitizer build is put through, as parts. `make asan-core` runs
## them one after another in this order, which is what it did when all of it
## was one recipe: `make -n asan-core` prints the commands it printed then.
##
## The parts are here for CI. As one step this took 19 to 24 minutes of a run
## of 24 to 31, and every other job was done in six. CI now gives the parts
## to jobs that run side by side (.github/workflows/ci.yml, the jobs named
## "Sanitizers: ..."), each of which makes its own `asan-build` first. Locally
## a part can be run alone the same way: `make asan-build asan-js`.
##
## A new check goes into the part it belongs with, and then CI runs it. A new
## part goes into this recipe and into a job: tools/ci/check_ci_parts.py,
## which CI runs before anything is built, fails for a part that no job runs.
##
## (The parts are declared here and not on the long .PHONY line above, which
## every pull request that adds a target changes, and which two of them then
## change at once.)
.PHONY: asan-build asan-unit asan-verify asan-js asan-dom asan-xml asan-wpt asan-xml-generated asan-fuzz
asan-core:
	$(MAKE) asan-build
	$(MAKE) asan-unit
	$(MAKE) asan-verify
	$(MAKE) asan-js
	$(MAKE) asan-dom
	$(MAKE) asan-xml
	$(MAKE) asan-wpt
	$(MAKE) asan-xml-generated
	$(MAKE) asan-fuzz

## The sanitizer build itself: build-asan/litebird, which every other part and
## asan-deep run.
##
## ASAN_BUILD_ALSO names more to build in the same `make`, beside the engine:
## CI's job for the unit suites gives it build-asan/test_suite, so that the
## one compiler the suites need (147 seconds) works while the others build
## the engine, where it used to start when they had finished. (Not in the
## run that only keeps the objects: ccache is not given that one, see
## TEST_OBJ.)
ASAN_BUILD_ALSO ?=
asan-build:
	ASAN_OPTIONS=detect_leaks=0 $(MAKE) BUILD_DIR=build-asan CXXFLAGS="$(ASAN_FLAGS)" all $(ASAN_BUILD_ALSO)

## The unit suites, built and run with the sanitizers. ASAN_UNIT_STRETCHES=3
## runs them as three processes side by side (tests/test_suite.cpp,
## tools/ci/run_stretches.py), and ASAN_UNIT_STRETCHES=balanced as the six
## of `test-in-stretches` above, which is what CI does: one process took 8
## min 52 s there on 24 November 2026, on a machine with four cores and 16
## GB, and that was the length of a whole CI run. Here it is one process
## unless you say otherwise: several of them in the sanitizer build want more
## memory than a machine with 4 GB has (one stretch alone held 5 GB).
ASAN_UNIT_STRETCHES ?= 1
asan-unit:
	ASAN_OPTIONS=detect_leaks=0 $(MAKE) BUILD_DIR=build-asan CXXFLAGS="$(ASAN_FLAGS)" $(if $(filter 1,$(ASAN_UNIT_STRETCHES)),test,test-in-stretches STRETCHES=$(ASAN_UNIT_STRETCHES))

## The checks that have a verifier of their own: media, colour, Brotli, regular
## expressions, string operations, and the three security ones (HTTPS, worker
## policy, WebSocket).
asan-verify:
	ASAN_OPTIONS=detect_leaks=0 $(MAKE) BUILD_DIR=build-asan CXXFLAGS="$(ASAN_FLAGS)" verify-media
	ASAN_OPTIONS=detect_leaks=0 $(MAKE) BUILD_DIR=build-asan CXXFLAGS="$(ASAN_FLAGS)" verify-avif
	ASAN_OPTIONS=detect_leaks=0 python3 tools/conformance/verify_media_webgl_codecs.py --binary build-asan/litebird
	ASAN_OPTIONS=detect_leaks=0 $(MAKE) BUILD_DIR=build-asan CXXFLAGS="$(ASAN_FLAGS)" verify-color
	ASAN_OPTIONS=detect_leaks=0 $(MAKE) BUILD_DIR=build-asan CXXFLAGS="$(ASAN_FLAGS)" verify-brotli BROTLI_VERIFY_FLAGS=--skip-node
	ASAN_OPTIONS=detect_leaks=0 python3 tools/conformance/verify_regexp.py --binary build-asan/litebird --fuzz-count 200 --skip-node
	ASAN_OPTIONS=detect_leaks=0 python3 tools/conformance/verify_string_unicode.py --binary build-asan/litebird --fuzz 400 --skip-node --skip-full-sweep
	ASAN_OPTIONS=detect_leaks=0 $(MAKE) BUILD_DIR=build-asan CXXFLAGS="$(ASAN_FLAGS)" bidi-conformance
	ASAN_OPTIONS=detect_leaks=0 python3 tools/security/verify_https.py --binary build-asan/litebird
	ASAN_OPTIONS=detect_leaks=0 python3 tools/security/verify_worker_policy.py --binary build-asan/litebird --stress-runs 20
	ASAN_OPTIONS=detect_leaks=0 python3 tools/security/verify_websocket.py --binary build-asan/litebird --stress-runs 20

## The corpus is run for what the sanitizers see while the scripts execute, not
## for its score: a sanitizer report makes a case fail, and --min-pass turns a
## batch of those into a failed build.
asan-js:
	ASAN_OPTIONS=detect_leaks=0 $(JS_CONFORMANCE) --binary build-asan/litebird --quiet --timeout 60 --min-pass $(JS_ASAN_FLOOR)

## The script DOM corpus and the URL cases, for the same reason.
asan-dom:
	ASAN_OPTIONS=detect_leaks=0 $(DOM_CONFORMANCE) --binary build-asan/litebird --quiet --timeout 60 --min-pass $(DOM_ASAN_FLOOR)
	ASAN_OPTIONS=detect_leaks=0 python3 tools/conformance/run_url_conformance.py --binary build-asan/litebird --timeout 600 --min-pass $(URL_FLOOR)

## The W3C XML suite.
asan-xml:
	ASAN_OPTIONS=detect_leaks=0 $(XML_CONFORMANCE) --binary build-asan/litebird --quiet --timeout 120 --min-pass $(XML_FLOOR)

## The web-platform-tests.
asan-wpt:
	ASAN_OPTIONS=detect_leaks=0 $(WPT) --binary build-asan/litebird --quiet --timeout 600 --min-pass $(WPT_FLOOR)

## Generated XML against Expat, and the serializer's round trip.
asan-xml-generated:
	ASAN_OPTIONS=detect_leaks=0 python3 tools/fuzz/diff_xml.py --binary build-asan/litebird --count $(XML_ASAN_DIFF_COUNT) --timeout 120 --expat-only
	ASAN_OPTIONS=detect_leaks=0 python3 tools/fuzz/xml_roundtrip.py --binary build-asan/litebird --count $(XML_ASAN_ROUNDTRIP_COUNT) --timeout 900

## The fuzz smoke run: mutated pages, XML documents, media, colours and Brotli
## streams. With `make -j` its six parts run side by side (see `fuzz`), so
## what each prints is kept together and shown when it ends
## (--output-sync), and one that fails does not stop the others
## (--keep-going): the step then names every part that failed, not the
## first.
asan-fuzz:
	ASAN_OPTIONS=detect_leaks=0 $(MAKE) --output-sync=target --keep-going BUILD_DIR=build-asan CXXFLAGS="$(ASAN_FLAGS)" fuzz BROTLI_VERIFY_FLAGS=--skip-node

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

## CSS whitespace runs through the page pipeline. Each padded page must paint
## like its compact form; the 300,000-space cases used to take seconds.
bench-css: $(TARGET)
	@python3 tools/bench/bench_css_whitespace.py --sizes 30000,300000 --reps 3 --max-ms 3000

## Attribute-list end removals through the DOM. Each case must print the
## expected checksum and stay under the limit; the old vector front erase
## shifted both tails and took seconds at 50,000 attributes.
bench-attributes: $(TARGET)
	@python3 tools/bench/bench_attributes.py --reps 3 --max-ms 3000

## The HTML tokenizer's repeated attribute names. Seven page shapes at 50,000
## attribute occurrences each; every tree dump must equal the tree the markup
## means, and no case may take a second. Comparing each name with every
## accepted attribute took 3.5 s on the wide cases.
bench-tokenizer-attributes: $(TARGET)
	@python3 tools/bench/bench_tokenizer_attributes.py --reps 3 --max-ms 1000

## A string built half a surrogate pair at a time across six cases (including
## reading, indexOf, and slicing from substring between halves). Under the
## old UTF-8 storage, writing a surrogate half settled or left a 3-byte UTF-8
## tail inside the block so every pair copied (5.6 s / 23 s at 20k/40k in
## Phase 41; 893 ms at 20k when read between halves). Phase 53 stores non-ASCII
## text in UTF-16 so all six cases complete in place well within the one-second
## guard. Results face Node.js.
bench-surrogates: $(TARGET)
	@python3 tools/bench/bench_strings.py --only surrogate --reps 3 --max-ms 1000

## Page fetch() must not read arbitrary local files. Generates a marker file
## and checks web, file:-base-smuggled, and local file: documents.
security-fetch: $(TARGET)
	@python3 tools/security/check_fetch_origin.py

## Certificate trust and DNS/IP identity over real local TLS connections.
security-https: $(TARGET)
	@python3 tools/security/verify_https.py --binary $(TARGET)

## Worker script MIME/static-module policy, EventTarget error/message dispatch,
## classic importScripts boundary checks, and 60x concurrent startup/exit stress runs.
security-worker: $(TARGET)
	@python3 tools/security/verify_worker_policy.py --binary $(TARGET) --stress-runs 60

## WebSocket wss:// TLS (X.509/SAN/IP/CA), Fetch Origin/mixed-content policy,
## relative URL resolution, WHATWG validation/close semantics, and 60x exit stress.
security-websocket: $(TARGET)
	@python3 tools/security/verify_websocket.py --binary $(TARGET) --stress-runs 60

## Verifies JPEG (baseline, subsampled, progressive, grayscale, CMYK, restart
## markers) and GIF (87a/89a, transparent, interlaced, multi-frame disposal)
## decoding pixel-by-pixel against ImageMagick/libjpeg when available, plus
## real-world SVG path/arc/transform/gradient rendering.
verify-media: $(MEDIA_CHECK_TARGET)
	@python3 tools/conformance/verify_media_render.py --binary $(MEDIA_CHECK_TARGET)

## Verifies AVIF container parsing, AV1 OBU payload extraction, hostile/malformed
## refusals, deterministic mutation fuzzing, and libheif-backed AV1 picture pixel
## comparison (known unimplemented until an AV1 intra-frame decoder lands).
verify-avif: $(MEDIA_CHECK_TARGET)
	@python3 tools/conformance/verify_avif.py --binary $(MEDIA_CHECK_TARGET)

## Spec-backed conformance oracle for HTMLMediaElement (<audio>/<video>),
## MediaSource/Web Audio, WebCodecs, and WebGL 1/2 (25 cases with KNOWN/FIXED
## gating and 5 built-in negative controls).
.PHONY: verify-media-webgl-codecs
verify-media-webgl-codecs: $(TARGET)
	@python3 tools/conformance/verify_media_webgl_codecs.py --binary $(TARGET)

## Mutation fuzzing of the JPEG, GIF, and SVG decoders/renderer.
fuzz-media: $(MEDIA_CHECK_TARGET)
	@python3 tools/fuzz/fuzz_media.py --binary $(MEDIA_CHECK_TARGET)

## Verifies CSS Color 4/5 space conversions (all 15 spaces against an
## independent exact-rational Python oracle), §13.2 OKLCH gamut mapping,
## and upstream WPT CSS Color 4/5 valid/invalid computed vectors.
verify-color: $(COLOR_CHECK_TARGET)
	@python3 tools/verify_color.py --binary $(COLOR_CHECK_TARGET)

## Adversarial grammar + mutation fuzzing of the CSS Color 4/5 parser, math
## evaluator, color-mix(), relative colors, and space conversions.
fuzz-color: $(COLOR_CHECK_TARGET)
	@python3 tools/verify_color.py --binary $(COLOR_CHECK_TARGET) --fuzz 5000

## Verifies pure C++20 RFC 7932 Brotli decoding across RFC edge vectors,
## bomb/cap boundaries, and Node.js zlib.brotliCompressSync differential streams.
verify-brotli: $(BROTLI_CHECK_TARGET)
	@python3 tools/conformance/verify_brotli.py --binary $(BROTLI_CHECK_TARGET) --fuzz 0 $(BROTLI_VERIFY_FLAGS)

## Collection stress: every case must print what it prints when nothing is
## collected. The negative control runs first (a harness that cannot fail is
## not evidence), then the corpus written for this check, then the DOM corpus,
## where a binding that holds a raw node pointer across a collection shows up.
verify-gc-stress: $(BINARY)
	@python3 tools/conformance/verify_gc_stress.py --self-test
	@python3 tools/conformance/verify_gc_stress.py --seeds 1,2,3
	@python3 tools/conformance/verify_gc_stress.py --dom --interval 2 --seeds 1,2 --jobs 4

## The same at interval 1, over the whole JavaScript corpus and the DOM corpus.
## Slow (a collection is O(heap)) and not in CI; run it when the collector or
## the bindings change.
verify-gc-stress-full: $(BINARY)
	@python3 tools/conformance/verify_gc_stress.py --self-test
	@python3 tools/conformance/verify_gc_stress.py --full --interval 1 --seeds 1 --jobs 4
	@python3 tools/conformance/verify_gc_stress.py --dom --interval 1 --seeds 1 --jobs 4

## Seeded truncation, bit-flip, random-byte, and bomb-cap fuzzing of Brotli.
fuzz-brotli: $(BROTLI_CHECK_TARGET)
	@python3 tools/conformance/verify_brotli.py --binary $(BROTLI_CHECK_TARGET) --fuzz 2000 $(BROTLI_VERIFY_FLAGS)

clean:
	rm -rf build build-asan build-tsan

help:
	@echo "Promise iterator check: make verify-promise-iterators (34 async cases, 3-second limit)"
	@echo "Targets: all (default), test, run, bench-floats, bench-collections, bench-styles, conformance, js-conformance, test262, dom-conformance, url-conformance, xml-conformance, wpt, diff-strings, verify-regexp, diff-trees, diff-xml, fuzz, fuzz-media, verify-media, verify-avif, fuzz-color, verify-color, verify-brotli, fuzz-brotli, verify-gc-stress (verify-gc-stress-full), gc-oracle, deep, asan (asan-core, asan-deep), tsan, bench, bench-css, bench-attributes, bench-tokenizer-attributes, bench-surrogates, bench-js-core, verify-promise-iterators, verify-collection-iterators, security-fetch, security-https, security-worker, security-websocket, check-numbers, check-suites, clean"
	@echo "Parts of asan-core, which CI runs as jobs side by side: asan-build, asan-unit, asan-verify, asan-js, asan-dom, asan-xml, asan-wpt, asan-xml-generated, asan-fuzz"
