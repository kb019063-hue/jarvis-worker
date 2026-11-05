#pragma once

#include "litebird/js/Bytecode.hpp"
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
    JsValue run();

    std::vector<CallFrame> m_frames;
    std::vector<JsValue> m_stack;
    std::unordered_map<std::string, JsValue> m_globals;
    std::shared_ptr<Document> m_document;
};

} // namespace litebird
