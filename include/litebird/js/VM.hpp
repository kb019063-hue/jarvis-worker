#pragma once

#include "litebird/js/Bytecode.hpp"
#include "litebird/js/IterationCursor.hpp"
#include "litebird/js/JsValue.hpp"
#include "litebird/js/Heap.hpp"
#include "litebird/dom/Document.hpp"
#include <vector>
#include <unordered_map>
#include <memory>
#include <string>

namespace litebird {

struct CallFrame {
    const BytecodeFunction* function{nullptr};
    const uint8_t* ip{nullptr};
    size_t slots_offset{0};
};

class VM {
public:
    explicit VM(std::shared_ptr<Document> doc = nullptr);

    JsValue interpret(std::shared_ptr<BytecodeFunction> function);
    bool running() const { return !m_frames.empty(); }
    JsValue call_compiled(const JsValue& function, const std::vector<JsValue>& args,
                          const JsValue* this_val);

    void set_global(const std::string& name, JsValue value);
    JsValue get_global(const std::string& name) const;
    const std::unordered_map<std::string, JsValue>& globals() const { return m_globals; }
    std::unordered_map<std::string, JsValue>& globals() { return m_globals; }
    void set_document(std::shared_ptr<Document> doc);

    void push(JsValue value);
    JsValue pop();
    const JsValue& peek(size_t distance = 0) const;

    GCStats collect_garbage();

private:
    void init_builtins();
    JsValue run(size_t stop_depth = 0);

    std::vector<CallFrame> m_frames;
    std::vector<JsValue> m_stack;
    /// The array-destructuring cursors this script left open, one per
    /// `OpIteratorInit` without its close yet. Nested patterns stack, so a
    /// default expression that destructures again opens its own cursor on
    /// top of the one being served.
    std::vector<std::unique_ptr<IterationCursor>> m_iterators;
    std::unordered_map<std::string, JsValue> m_globals;
    std::shared_ptr<Document> m_document;
};

} // namespace litebird
