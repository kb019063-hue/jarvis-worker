#pragma once

#include <string>
#include <memory>

namespace litebird {

class Node;

enum class EventPhase : uint8_t {
    None = 0,
    Capturing = 1,
    AtTarget = 2,
    Bubbling = 3
};

class Event {
public:
    explicit Event(std::string type, bool bubbles = true, bool cancelable = true)
        : m_type(std::move(type)), m_bubbles(bubbles), m_cancelable(cancelable) {}

    virtual ~Event() = default;

    const std::string& type() const { return m_type; }
    std::shared_ptr<Node> target() const { return m_target; }
    std::shared_ptr<Node> current_target() const { return m_current_target; }
    bool bubbles() const { return m_bubbles; }
    bool cancelable() const { return m_cancelable; }
    bool default_prevented() const { return m_default_prevented; }
    bool propagation_stopped() const { return m_propagation_stopped; }
    EventPhase event_phase() const { return m_phase; }

    void set_target(std::shared_ptr<Node> t) { m_target = std::move(t); }
    void set_current_target(std::shared_ptr<Node> ct) { m_current_target = std::move(ct); }
    void set_event_phase(EventPhase p) { m_phase = p; }

    void prevent_default() {
        if (m_cancelable) m_default_prevented = true;
    }

    void stop_propagation() {
        m_propagation_stopped = true;
    }

    std::string data;
    std::string key;
    std::string code;

private:
    std::string m_type;
    std::shared_ptr<Node> m_target;
    std::shared_ptr<Node> m_current_target;
    bool m_bubbles{true};
    bool m_cancelable{true};
    bool m_default_prevented{false};
    bool m_propagation_stopped{false};
    EventPhase m_phase{EventPhase::None};
};

} // namespace litebird
