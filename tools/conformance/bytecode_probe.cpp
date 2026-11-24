// Runs a script through both engines and prints what each answered.
//
// The bytecode VM (src/js/VM.cpp, src/js/BytecodeCompiler.cpp) exists but is
// not what a page runs on: `JsInterpreter::eval` is the tree-walker, and the
// VM is reachable only through `eval_bytecode`. ROADMAP row 7 says the VM is
// to become the engine with the tree-walker kept as a differential oracle,
// which only works if the two can be compared case by case. Nothing could
// before this, which is why the compiler was free to refuse `var` for as long
// as it did -- nothing called it.
//
// Usage:  bytecode_probe -            read the script from stdin
//         bytecode_probe <file>       read the script from a file
//         bytecode_probe -e <text>    the script is the argument
//
// Output is one line per path, so a checker can compare them: the side, then
// what happened, then the text. The status is always the second word, so no
// message can be mistaken for a prefix -- an earlier form put the side second
// and a compiler message that itself contained "vm " was read twice.
//
//     ast value <text>     the tree-walker's answer
//     vm value <text>      the bytecode VM's answer
//     ast threw <message>  the tree-walker threw
//     vm threw <message>   the VM threw (or refused to compile)
//
// A separate process per path would lose the point: the two have to be given
// the same global object and the same built-ins to be comparable at all.

#include "litebird/js/JsInterpreter.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace litebird;

namespace {

/// Reads the script: stdin, a file, or an argument. Returns false if there is
/// nothing to read.
bool read_source(int argc, char** argv, std::string& source) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-e" && i + 1 < argc) {
            source = argv[++i];
            return true;
        }
    }
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-" || arg == "-e") {
            std::ostringstream buffer;
            buffer << std::cin.rdbuf();
            source = buffer.str();
            return true;
        }
        if (!arg.empty() && arg[0] != '-') {
            std::ifstream file(arg, std::ios::binary);
            if (!file) {
                std::cerr << "bytecode_probe: cannot read " << arg << "\n";
                return false;
            }
            std::ostringstream buffer;
            buffer << file.rdbuf();
            source = buffer.str();
            return true;
        }
    }
    std::ostringstream buffer;
    buffer << std::cin.rdbuf();
    source = buffer.str();
    return true;
}

/// One line, whatever the value holds: a newline in a string must not turn one
/// answer into two lines and confuse the checker.
std::string one_line(std::string text) {
    for (char& c : text) {
        if (c == '\n') c = ' ';
        if (c == '\r') c = ' ';
    }
    return text;
}

} // namespace

int main(int argc, char** argv) {
    std::string source;
    if (!read_source(argc, argv, source)) return 2;

    auto doc = std::make_shared<Document>();
    doc->set_title("bytecode probe");

    // The tree-walker first, on its own interpreter, so that whatever it
    // leaves in the global object cannot be what makes the VM agree.
    {
        JsInterpreter interpreter(doc);
        try {
            const std::string answer = interpreter.eval(source).to_string();
            std::cout << "ast value " << one_line(answer) << "\n";
        } catch (const JsThrow& thrown) {
            std::cout << "ast threw " << one_line(thrown.value.to_string()) << "\n";
        } catch (const JsStackOverflow&) {
            std::cout << "ast threw RangeError: Maximum call stack size exceeded\n";
        } catch (const std::exception& error) {
            std::cout << "ast threw " << one_line(error.what()) << "\n";
        }
    }

    // Then the VM, on a fresh interpreter of its own.
    {
        JsInterpreter interpreter(doc);
        try {
            const std::string answer = interpreter.eval_bytecode(source).to_string();
            std::cout << "vm value " << one_line(answer) << "\n";
        } catch (const JsThrow& thrown) {
            std::cout << "vm threw " << one_line(thrown.value.to_string()) << "\n";
        } catch (const JsStackOverflow&) {
            std::cout << "vm threw RangeError: Maximum call stack size exceeded\n";
        } catch (const std::exception& error) {
            std::cout << "vm threw " << one_line(error.what()) << "\n";
        }
    }
    return 0;
}
