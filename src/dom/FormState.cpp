#include "litebird/dom/FormState.hpp"
#include "litebird/dom/CustomElementRegistry.hpp"
#include "litebird/dom/Document.hpp"
#include "litebird/dom/TreeWalk.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace litebird {

FormControlState& Element::ensure_form_state() {
    if (!m_form_state) m_form_state = std::make_shared<FormControlState>();
    return *m_form_state;
}

bool Element::is_defined() const {
    if (m_custom_state == CustomElementState::Custom) return true;
    if (is_foreign()) return true;
    return !CustomElementRegistry::is_valid_custom_element_name(m_tag_name);
}

namespace forms {

bool (*pattern_matcher)(const std::string& pattern, const std::string& value) = nullptr;

namespace {

bool is_html(const Element& element, const char* tag) {
    return !element.is_foreign() && element.tag_name() == tag;
}

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

std::string lower(std::string text) {
    for (char& c : text) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}

std::string strip(const std::string& text) {
    std::size_t start = 0, end = text.size();
    while (start < end && is_space(text[start])) ++start;
    while (end > start && is_space(text[end - 1])) --end;
    return text.substr(start, end - start);
}

std::string strip_and_collapse(const std::string& text) {
    std::string out;
    bool pending = false;
    for (char c : text) {
        if (is_space(c)) { pending = !out.empty(); continue; }
        if (pending) { out += ' '; pending = false; }
        out += c;
    }
    return out;
}

std::string strip_newlines(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) if (c != '\n' && c != '\r') out += c;
    return out;
}

// The root of the tree `node` is in. Kept alive by the returned pointer.
std::shared_ptr<const Node> tree_root(const Node& node) {
    std::shared_ptr<const Node> at = node.shared_from_this();
    for (;;) {
        auto parent = at->parent();
        if (!parent) return at;
        at = std::move(parent);
    }
}

std::shared_ptr<Element> shared(const Element& element) {
    return std::static_pointer_cast<Element>(const_cast<Element&>(element).shared_from_this());
}

const Element* ancestor(const Element& element, const char* tag) {
    for (auto at = element.parent(); at; at = at->parent()) {
        if (at->is_element() && is_html(static_cast<const Element&>(*at), tag)) return static_cast<const Element*>(at.get());
    }
    return nullptr;
}

bool is_descendant_of(const Node& node, const Node& ancestor_node) {
    for (auto at = node.parent(); at; at = at->parent()) {
        if (at.get() == &ancestor_node) return true;
    }
    return false;
}

const char* const kInputTypes[] = {
    "hidden", "text", "search", "tel", "url", "email", "password", "date", "month", "week", "time",
    "datetime-local", "number", "range", "color", "checkbox", "radio", "file", "submit", "image", "reset", "button"
};

enum class ValueMode { Value, Default, DefaultOn, Filename };

ValueMode value_mode(const std::string& type) {
    if (type == "hidden" || type == "submit" || type == "image" || type == "reset" || type == "button") return ValueMode::Default;
    if (type == "checkbox" || type == "radio") return ValueMode::DefaultOn;
    if (type == "file") return ValueMode::Filename;
    return ValueMode::Value;
}

bool is_number_type(const std::string& type) { return type == "number" || type == "range"; }

double attr_number(const Element& element, const char* name, double fallback) {
    const std::string* text = element.attribute(name);
    double out = 0;
    return text && parse_number(*text, out) ? out : fallback;
}

bool has_attr_number(const Element& element, const char* name, double& out) {
    const std::string* text = element.attribute(name);
    return text && parse_number(*text, out);
}

// "Allowed value step" of a number or range input; 0 for step="any".
double step_of(const Element& input) {
    const std::string* text = input.attribute("step");
    if (!text) return 1;
    if (lower(*text) == "any") return 0;
    double value = 0;
    if (!parse_number(*text, value) || value <= 0) return 1;
    return value;
}

double step_base(const Element& input) {
    double out = 0;
    if (has_attr_number(input, "min", out)) return out;
    if (has_attr_number(input, "value", out)) return out;
    return input_type(input) == "range" ? 0 : 0;
}

bool is_valid_color(const std::string& text) {
    if (text.size() != 7 || text[0] != '#') return false;
    for (std::size_t i = 1; i < 7; ++i) {
        const char c = text[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

bool all_digits(const std::string& text, std::size_t from, std::size_t count) {
    if (from + count > text.size()) return false;
    for (std::size_t i = from; i < from + count; ++i) if (text[i] < '0' || text[i] > '9') return false;
    return true;
}

bool is_valid_date(const std::string& text) {
    return text.size() == 10 && all_digits(text, 0, 4) && text[4] == '-' && all_digits(text, 5, 2) &&
           text[7] == '-' && all_digits(text, 8, 2);
}

bool is_valid_time(const std::string& text) {
    if (text.size() < 5 || !all_digits(text, 0, 2) || text[2] != ':' || !all_digits(text, 3, 2)) return false;
    if (text.size() == 5) return true;
    if (text[5] != ':' || !all_digits(text, 6, 2)) return false;
    if (text.size() == 8) return true;
    return text[8] == '.' && text.size() > 9 && text.size() <= 12 && all_digits(text, 9, text.size() - 9);
}

std::string sanitize(const Element& input, const std::string& type, const std::string& raw) {
    if (type == "text" || type == "search" || type == "tel" || type == "password") return strip_newlines(raw);
    if (type == "url" || type == "email") return strip(strip_newlines(raw));
    if (type == "number") {
        double ignored = 0;
        return parse_number(raw, ignored) ? raw : std::string();
    }
    if (type == "range") {
        const double min = attr_number(input, "min", 0);
        double max = attr_number(input, "max", 100);
        if (max < min) max = min;
        double value = 0;
        if (!parse_number(raw, value)) value = max < min ? min : min + (max - min) / 2;
        value = std::min(std::max(value, min), max);
        const double step = step_of(input);
        if (step > 0) {
            const double base = min;
            double aligned = base + std::round((value - base) / step) * step;
            if (aligned > max) aligned -= step;
            if (aligned < min) aligned = min;
            value = aligned;
        }
        return number_to_string(value);
    }
    if (type == "color") return is_valid_color(raw) ? lower(raw) : std::string("#000000");
    if (type == "date") return is_valid_date(raw) ? raw : std::string();
    if (type == "time") return is_valid_time(raw) ? raw : std::string();
    if (type == "datetime-local") {
        auto t = raw.find('T');
        if (t == std::string::npos) t = raw.find(' ');
        if (t == std::string::npos || !is_valid_date(raw.substr(0, t)) || !is_valid_time(raw.substr(t + 1))) return std::string();
        return raw.substr(0, t) + "T" + raw.substr(t + 1);
    }
    if (type == "month") {
        return raw.size() == 7 && all_digits(raw, 0, 4) && raw[4] == '-' && all_digits(raw, 5, 2) ? raw : std::string();
    }
    if (type == "week") {
        return raw.size() == 8 && all_digits(raw, 0, 4) && raw[4] == '-' && raw[5] == 'W' && all_digits(raw, 6, 2)
                   ? raw : std::string();
    }
    return raw;
}

// The value of an input in value mode: what the control holds.
std::string input_value(const Element& input, const std::string& type) {
    const FormControlState* state = input.form_state();
    const std::string raw = state && state->value_dirty ? state->value : input.get_attribute("value");
    return sanitize(input, type, raw);
}

std::string normalize_newlines(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            out += '\n';
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
        } else {
            out += text[i];
        }
    }
    return out;
}

bool is_valid_email(const std::string& text) {
    const auto at = text.find('@');
    if (at == std::string::npos || at == 0 || at + 1 >= text.size() || text.find('@', at + 1) != std::string::npos) return false;
    static const std::string local_extra = ".!#$%&'*+/=?^_`{|}~-";
    for (std::size_t i = 0; i < at; ++i) {
        const char c = text[i];
        const bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        if (!alnum && local_extra.find(c) == std::string::npos) return false;
    }
    std::size_t start = at + 1;
    while (start <= text.size()) {
        std::size_t dot = text.find('.', start);
        if (dot == std::string::npos) dot = text.size();
        if (dot == start || dot - start > 63) return false;
        for (std::size_t i = start; i < dot; ++i) {
            const char c = text[i];
            const bool alnum = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
            if (!alnum && !(c == '-' && i != start && i + 1 != dot)) return false;
        }
        start = dot + 1;
        if (dot == text.size()) break;
    }
    return true;
}

bool is_absolute_url(const std::string& text) {
    std::size_t i = 0;
    if (text.empty() || !((text[0] >= 'a' && text[0] <= 'z') || (text[0] >= 'A' && text[0] <= 'Z'))) return false;
    while (i < text.size() && text[i] != ':') {
        const char c = text[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.';
        if (!ok) return false;
        ++i;
    }
    if (i >= text.size()) return false;
    const std::string scheme = lower(text.substr(0, i));
    const bool special = scheme == "http" || scheme == "https" || scheme == "ftp" || scheme == "ws" || scheme == "wss" || scheme == "file";
    if (!special) return true;
    // A special scheme needs a host.
    std::size_t rest = i + 1;
    while (rest < text.size() && (text[rest] == '/' || text[rest] == '\\')) ++rest;
    return scheme == "file" || rest < text.size();
}

// The first <legend> child of a fieldset: controls inside it stay enabled.
const Element* first_legend(const Element& fieldset) {
    for (const auto& child : fieldset.children()) {
        if (child->is_element() && is_html(static_cast<const Element&>(*child), "legend")) {
            return static_cast<const Element*>(child.get());
        }
    }
    return nullptr;
}

void collect_options(const Element& select, std::vector<std::shared_ptr<Element>>& out) {
    for (const auto& child : select.children()) {
        if (!child->is_element()) continue;
        auto element = std::static_pointer_cast<Element>(child);
        if (is_html(*element, "option")) out.push_back(element);
        else if (is_html(*element, "optgroup")) {
            for (const auto& inner : element->children()) {
                if (inner->is_element() && is_html(static_cast<const Element&>(*inner), "option")) {
                    out.push_back(std::static_pointer_cast<Element>(inner));
                }
            }
        }
    }
}

bool option_disabled(const Element& option) {
    if (option.has_attribute("disabled")) return true;
    auto parent = option.parent();
    return parent && parent->is_element() && is_html(static_cast<const Element&>(*parent), "optgroup") &&
           static_cast<const Element&>(*parent).has_attribute("disabled");
}

// What the option itself says, before a single select picks one.
bool raw_selected(const Element& option) {
    const FormControlState* state = option.form_state();
    if (state && state->has_selected) return state->selected;
    return option.has_attribute("selected");
}

// Whether the "nothing selected" a script asked for still stands.
bool none_still_holds(const Element& select, const std::vector<std::shared_ptr<Element>>& options) {
    const FormControlState* state = select.form_state();
    if (!state || !state->select_none) return false;
    if (state->none_options.size() != options.size()) return false;
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (state->none_options[i] != options[i].get()) return false;
    }
    return true;
}

// The index of the one selected option of a single select, or -1.
int single_selection(const Element& select, const std::vector<std::shared_ptr<Element>>& options) {
    int picked = -1;
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (raw_selected(*options[i])) picked = static_cast<int>(i);
    }
    if (picked >= 0 || none_still_holds(select, options)) return picked;
    // A drop-down always shows something: the first option that can be chosen.
    int size = 0;
    if (const std::string* text = select.attribute("size")) size = std::atoi(text->c_str());
    if (size > 1) return -1;
    for (std::size_t i = 0; i < options.size(); ++i) {
        if (!option_disabled(*options[i])) return static_cast<int>(i);
    }
    return -1;
}

void clear_none(Element& select) {
    if (FormControlState* state = select.form_state()) {
        state->select_none = false;
        state->none_options.clear();
    }
}

} // namespace

// --- Classification ---------------------------------------------------------------

bool is_listed(const Element& element) {
    if (element.is_foreign()) return false;
    const std::string& tag = element.tag_name();
    return tag == "input" || tag == "button" || tag == "select" || tag == "textarea" || tag == "fieldset" ||
           tag == "output" || tag == "object";
}

bool is_submittable(const Element& element) {
    if (element.is_foreign()) return false;
    const std::string& tag = element.tag_name();
    return tag == "input" || tag == "button" || tag == "select" || tag == "textarea";
}

bool is_labelable(const Element& element) {
    if (element.is_foreign()) return false;
    const std::string& tag = element.tag_name();
    if (tag == "input") return input_type(element) != "hidden";
    return tag == "button" || tag == "select" || tag == "textarea" || tag == "meter" || tag == "output" || tag == "progress";
}

std::string input_type(const Element& input) {
    const std::string* text = input.attribute("type");
    if (!text) return "text";
    const std::string wanted = lower(*text);
    for (const char* known : kInputTypes) {
        if (wanted == known) return wanted;
    }
    return "text";
}

std::string button_type(const Element& button) {
    const std::string* text = button.attribute("type");
    if (!text) return "submit";
    const std::string wanted = lower(*text);
    return wanted == "reset" || wanted == "button" ? wanted : std::string("submit");
}

bool is_submit_button(const Element& element) {
    if (is_html(element, "button")) return button_type(element) == "submit";
    if (is_html(element, "input")) {
        const std::string type = input_type(element);
        return type == "submit" || type == "image";
    }
    return false;
}

bool has_text_selection(const Element& element) {
    if (is_html(element, "textarea")) return true;
    if (!is_html(element, "input")) return false;
    const std::string type = input_type(element);
    return type == "text" || type == "search" || type == "url" || type == "tel" || type == "password";
}

// --- Ownership --------------------------------------------------------------------

std::shared_ptr<Element> form_owner(const Element& control) {
    if (const std::string* id = control.attribute("form")) {
        if (is_listed(control) || is_html(control, "label") || is_html(control, "legend") || is_html(control, "option")) {
            auto root = tree_root(control);
            auto found = id->empty() ? nullptr : root->descendant_with_id(*id);
            return found && is_html(*found, "form") ? found : nullptr;
        }
    }
    for (auto at = control.parent(); at; at = at->parent()) {
        if (at->is_element() && is_html(static_cast<const Element&>(*at), "form")) {
            return std::static_pointer_cast<Element>(at);
        }
    }
    return nullptr;
}

std::vector<std::shared_ptr<Element>> form_controls(const Element& form) {
    std::vector<std::shared_ptr<Element>> out;
    auto root = tree_root(form);
    for_each_descendant(*root, [&](Node& node) {
        if (!node.is_element()) return true;
        auto& element = static_cast<Element&>(node);
        if (!is_listed(element)) return true;
        if (is_html(element, "input") && input_type(element) == "image") return true;
        // Without a form attribute anywhere near, only what is inside the
        // form can belong to it, and that is settled by one look upwards.
        if (!element.has_attribute("form") && !is_descendant_of(element, form)) return true;
        if (form_owner(element).get() == &form) out.push_back(shared(element));
        return true;
    });
    return out;
}

std::vector<std::shared_ptr<Element>> fieldset_controls(const Element& fieldset) {
    std::vector<std::shared_ptr<Element>> out;
    for_each_descendant(fieldset, [&](Node& node) {
        if (node.is_element() && is_listed(static_cast<Element&>(node))) out.push_back(shared(static_cast<Element&>(node)));
        return true;
    });
    return out;
}

std::shared_ptr<Element> label_control(const Element& label) {
    if (const std::string* id = label.attribute("for")) {
        auto root = tree_root(label);
        auto found = id->empty() ? nullptr : root->descendant_with_id(*id);
        return found && is_labelable(*found) ? found : nullptr;
    }
    std::shared_ptr<Element> found;
    for_each_descendant(label, [&](Node& node) {
        if (node.is_element() && is_labelable(static_cast<Element&>(node))) {
            found = shared(static_cast<Element&>(node));
            return false;
        }
        return true;
    });
    return found;
}

std::vector<std::shared_ptr<Element>> labels_of(const Element& control) {
    std::vector<std::shared_ptr<Element>> out;
    if (!is_labelable(control)) return out;
    auto root = tree_root(control);
    for_each_descendant(*root, [&](Node& node) {
        if (node.is_element() && is_html(static_cast<Element&>(node), "label") &&
            label_control(static_cast<Element&>(node)).get() == &control) {
            out.push_back(shared(static_cast<Element&>(node)));
        }
        return true;
    });
    return out;
}

std::vector<std::shared_ptr<Element>> radio_group(const Element& radio) {
    std::vector<std::shared_ptr<Element>> out;
    const std::string* name = radio.attribute("name");
    if (!name || name->empty()) {
        out.push_back(shared(radio));
        return out;
    }
    auto owner = form_owner(radio);
    auto root = tree_root(radio);
    for_each_descendant(*root, [&](Node& node) {
        if (!node.is_element()) return true;
        auto& element = static_cast<Element&>(node);
        if (!is_html(element, "input") || input_type(element) != "radio") return true;
        const std::string* other = element.attribute("name");
        if (!other || *other != *name) return true;
        if (form_owner(element) == owner) out.push_back(shared(element));
        return true;
    });
    if (out.empty()) out.push_back(shared(radio));       // the root itself is a radio
    return out;
}

// --- Value ------------------------------------------------------------------------

std::string textarea_raw_value(const Element& textarea) {
    const FormControlState* state = textarea.form_state();
    return state && state->value_dirty ? state->value : textarea.text_content();
}

std::string value(const Element& control) {
    if (control.is_foreign()) return control.get_attribute("value");
    const std::string& tag = control.tag_name();
    if (tag == "input") {
        const std::string type = input_type(control);
        switch (value_mode(type)) {
            case ValueMode::Value: return input_value(control, type);
            case ValueMode::Default: return control.get_attribute("value");
            case ValueMode::DefaultOn: return control.has_attribute("value") ? control.get_attribute("value") : std::string("on");
            case ValueMode::Filename: return std::string();
        }
    }
    if (tag == "textarea") return normalize_newlines(textarea_raw_value(control));
    if (tag == "select") {
        auto selected = selected_options(control);
        return selected.empty() ? std::string() : option_value(*selected.front());
    }
    if (tag == "option") return option_value(control);
    if (tag == "output") {
        const FormControlState* state = control.form_state();
        return state && state->value_dirty ? state->value : control.text_content();
    }
    return control.get_attribute("value");
}

bool set_value(Element& control, const std::string& new_value) {
    if (control.is_foreign()) {
        control.set_attribute("value", new_value);
        return true;
    }
    const std::string& tag = control.tag_name();
    if (tag == "input") {
        const std::string type = input_type(control);
        switch (value_mode(type)) {
            case ValueMode::Value: {
                FormControlState& state = control.ensure_form_state();
                state.value = sanitize(control, type, new_value);
                state.value_dirty = true;
                state.selection_set = false;
                control.mark_needs_layout();
                Node::bump_tree_version();
                return true;
            }
            case ValueMode::Default:
            case ValueMode::DefaultOn:
                control.set_attribute("value", new_value);
                return true;
            case ValueMode::Filename:
                return new_value.empty();
        }
    }
    if (tag == "textarea" || tag == "output") {
        FormControlState& state = control.ensure_form_state();
        state.value = new_value;
        state.value_dirty = true;
        state.selection_set = false;
        control.mark_needs_layout();
        Node::bump_tree_version();
        return true;
    }
    if (tag == "select") {
        auto options = select_options(control);
        int found = -1;
        for (std::size_t i = 0; i < options.size(); ++i) {
            if (option_value(*options[i]) == new_value) { found = static_cast<int>(i); break; }
        }
        set_selected_index(control, found);
        return true;
    }
    control.set_attribute("value", new_value);
    return true;
}

std::string default_value(const Element& control) {
    if (is_html(control, "textarea")) return control.text_content();
    if (is_html(control, "output")) {
        // Remembered when the value was first changed; until then the text.
        return control.text_content();
    }
    return control.get_attribute("value");
}

// --- Checkboxes and radio buttons -------------------------------------------------

bool checked(const Element& input) {
    const FormControlState* state = input.form_state();
    if (state && state->has_checked) return state->checked;
    if (!input.has_attribute("checked")) return false;
    if (!is_html(input, "input") || input_type(input) != "radio") return true;
    // Several radio buttons of one group marked `checked` in the markup: the
    // last one is the one that is checked.
    bool after = false;
    for (const auto& member : radio_group(input)) {
        if (member.get() == &input) { after = true; continue; }
        if (!after) continue;
        const FormControlState* other = member->form_state();
        if (other && other->has_checked ? other->checked : member->has_attribute("checked")) return false;
    }
    return true;
}

void set_checked(Element& input, bool new_value, bool make_dirty) {
    const bool radio = is_html(input, "input") && input_type(input) == "radio";
    if (radio && new_value) {
        for (const auto& member : radio_group(input)) {
            if (member.get() == &input) continue;
            FormControlState& other = member->ensure_form_state();
            other.checked = false;
            other.has_checked = true;
        }
    }
    FormControlState& state = input.ensure_form_state();
    state.checked = new_value;
    state.has_checked = true;
    if (make_dirty) state.checked_dirty = true;
    input.mark_needs_layout();
    Node::bump_tree_version();
}

// --- select and option ------------------------------------------------------------

bool is_multiple_select(const Element& select) { return select.has_attribute("multiple"); }

std::vector<std::shared_ptr<Element>> select_options(const Element& select) {
    std::vector<std::shared_ptr<Element>> out;
    collect_options(select, out);
    return out;
}

std::vector<std::shared_ptr<Element>> selected_options(const Element& select) {
    std::vector<std::shared_ptr<Element>> out;
    auto options = select_options(select);
    if (is_multiple_select(select)) {
        for (const auto& option : options) if (raw_selected(*option)) out.push_back(option);
        return out;
    }
    const int index = single_selection(select, options);
    if (index >= 0) out.push_back(options[static_cast<std::size_t>(index)]);
    return out;
}

int selected_index(const Element& select) {
    auto options = select_options(select);
    if (is_multiple_select(select)) {
        for (std::size_t i = 0; i < options.size(); ++i) if (raw_selected(*options[i])) return static_cast<int>(i);
        return -1;
    }
    return single_selection(select, options);
}

void set_selected_index(Element& select, int index) {
    auto options = select_options(select);
    for (std::size_t i = 0; i < options.size(); ++i) {
        FormControlState& state = options[i]->ensure_form_state();
        state.selected = static_cast<int>(i) == index;
        state.has_selected = true;
        if (state.selected) state.selected_dirty = true;
    }
    FormControlState& state = select.ensure_form_state();
    if (index < 0 || index >= static_cast<int>(options.size())) {
        state.select_none = true;
        state.none_options.clear();
        for (const auto& option : options) state.none_options.push_back(option.get());
    } else {
        clear_none(select);
    }
    select.mark_needs_layout();
    Node::bump_tree_version();
}

std::shared_ptr<Element> option_select(const Element& option) {
    auto parent = option.parent();
    if (parent && parent->is_element() && is_html(static_cast<const Element&>(*parent), "optgroup")) parent = parent->parent();
    if (parent && parent->is_element() && is_html(static_cast<const Element&>(*parent), "select")) {
        return std::static_pointer_cast<Element>(parent);
    }
    return nullptr;
}

bool option_selected(const Element& option) {
    auto select = option_select(option);
    if (!select || is_multiple_select(*select)) return raw_selected(option);
    auto options = select_options(*select);
    const int index = single_selection(*select, options);
    return index >= 0 && options[static_cast<std::size_t>(index)].get() == &option;
}

void set_option_selected(Element& option, bool new_value) {
    auto select = option_select(option);
    if (select && !is_multiple_select(*select) && new_value) {
        for (const auto& other : select_options(*select)) {
            if (other.get() == &option) continue;
            FormControlState& state = other->ensure_form_state();
            state.selected = false;
            state.has_selected = true;
        }
    }
    FormControlState& state = option.ensure_form_state();
    state.selected = new_value;
    state.has_selected = true;
    state.selected_dirty = true;
    if (select) {
        // Unselecting the shown option of a drop-down selects the first one
        // again; choosing one ends "nothing selected".
        clear_none(*select);
        select->mark_needs_layout();
    }
    Node::bump_tree_version();
}

std::string option_text(const Element& option) {
    std::string text;
    for_each_descendant(option, [&](Node& node) {
        if (node.is_text()) {
            // Text inside a <script> in the option does not count.
            auto parent = node.parent();
            if (parent && parent->is_element() && static_cast<Element&>(*parent).tag_name() == "script") return true;
            text += node.text_content();
        }
        return true;
    });
    return strip_and_collapse(text);
}

std::string option_value(const Element& option) {
    if (const std::string* text = option.attribute("value")) return *text;
    return option_text(option);
}

std::string option_label(const Element& option) {
    if (const std::string* text = option.attribute("label")) {
        if (!text->empty()) return *text;
    }
    return option_text(option);
}

// --- Selector states --------------------------------------------------------------

bool matches_checked(const Element& element) {
    if (element.is_foreign()) return false;
    if (element.tag_name() == "input") {
        const std::string type = input_type(element);
        return (type == "checkbox" || type == "radio") && checked(element);
    }
    if (element.tag_name() == "option") return option_selected(element);
    return false;
}

bool can_be_disabled(const Element& element) {
    if (element.is_foreign()) return false;
    const std::string& tag = element.tag_name();
    return tag == "button" || tag == "input" || tag == "select" || tag == "textarea" || tag == "optgroup" ||
           tag == "option" || tag == "fieldset";
}

bool is_disabled(const Element& element) {
    if (!can_be_disabled(element)) return false;
    if (element.has_attribute("disabled")) return true;
    const std::string& tag = element.tag_name();
    if (tag == "option") return option_disabled(element);
    if (tag == "optgroup") return false;
    // Inside a disabled fieldset, except inside its first legend.
    const Node* below = &element;
    for (auto at = element.parent(); at; at = at->parent()) {
        if (at->is_element()) {
            const auto& up = static_cast<const Element&>(*at);
            if (is_html(up, "fieldset") && up.has_attribute("disabled")) {
                const Element* legend = first_legend(up);
                if (!(legend && (legend == below || is_descendant_of(element, *legend)))) return true;
            }
        }
        below = at.get();
    }
    return false;
}

bool can_be_required(const Element& element) {
    if (element.is_foreign()) return false;
    const std::string& tag = element.tag_name();
    return tag == "input" || tag == "select" || tag == "textarea";
}

bool is_required(const Element& element) {
    return can_be_required(element) && element.has_attribute("required");
}

bool is_read_write(const Element& element) {
    if (element.is_foreign()) return false;
    const std::string& tag = element.tag_name();
    if (tag == "textarea") return !element.has_attribute("readonly") && !is_disabled(element);
    if (tag == "input") {
        const std::string type = input_type(element);
        if (value_mode(type) != ValueMode::Value || type == "range" || type == "color") return false;
        return !element.has_attribute("readonly") && !is_disabled(element);
    }
    if (const std::string* editable = element.attribute("contenteditable")) {
        return lower(*editable) != "false";
    }
    return false;
}

bool placeholder_shown(const Element& element) {
    if (element.is_foreign() || !element.has_attribute("placeholder")) return false;
    if (element.tag_name() == "textarea") return value(element).empty();
    if (element.tag_name() != "input") return false;
    const std::string type = input_type(element);
    const bool takes_placeholder = type == "text" || type == "search" || type == "url" || type == "tel" ||
                                   type == "email" || type == "password" || type == "number";
    return takes_placeholder && value(element).empty();
}

int validity_for_selector(const Element& element) {
    if (element.is_foreign()) return 0;
    const std::string& tag = element.tag_name();
    if (tag == "form" || tag == "fieldset") return invalid_controls(element).empty() ? 1 : 2;
    if (!will_validate(element)) return 0;
    return validity(element).valid() ? 1 : 2;
}

// --- Validation -------------------------------------------------------------------

bool will_validate(const Element& control) {
    if (!is_submittable(control)) return false;
    if (is_disabled(control)) return false;
    if (ancestor(control, "datalist")) return false;
    const std::string& tag = control.tag_name();
    if (tag == "button") return button_type(control) == "submit";
    if (tag == "input") {
        const std::string type = input_type(control);
        if (type == "hidden" || type == "reset" || type == "button") return false;
        if (control.has_attribute("readonly")) return false;
    }
    if (tag == "textarea" && control.has_attribute("readonly")) return false;
    return true;
}

Validity validity(const Element& control) {
    Validity out;
    if (!is_submittable(control)) return out;
    if (const FormControlState* state = control.form_state()) out.custom_error = !state->custom_validity.empty();
    const std::string& tag = control.tag_name();
    const bool required = control.has_attribute("required");

    if (tag == "select") {
        if (required) {
            auto selected = selected_options(control);
            if (selected.empty()) {
                out.value_missing = true;
            } else if (!is_multiple_select(control)) {
                // The placeholder label option: the first option, a child of
                // the select, with an empty value.
                auto options = select_options(control);
                if (!options.empty() && selected.front() == options.front() &&
                    options.front()->parent().get() == &control && option_value(*options.front()).empty()) {
                    out.value_missing = true;
                }
            }
        }
        return out;
    }
    if (tag == "textarea") {
        out.value_missing = required && value(control).empty();
        return out;
    }
    if (tag != "input") return out;

    const std::string type = input_type(control);
    if (type == "checkbox") {
        out.value_missing = required && !checked(control);
        return out;
    }
    if (type == "radio") {
        bool group_required = false, any_checked = false;
        for (const auto& member : radio_group(control)) {
            group_required = group_required || member->has_attribute("required");
            any_checked = any_checked || checked(*member);
        }
        out.value_missing = group_required && !any_checked;
        return out;
    }
    if (type == "file") {
        out.value_missing = required;
        return out;
    }
    if (value_mode(type) != ValueMode::Value) return out;

    const std::string current = input_value(control, type);
    if (required && current.empty() && type != "range" && type != "color") out.value_missing = true;
    if (current.empty()) return out;

    if (type == "email") {
        if (control.has_attribute("multiple")) {
            std::size_t start = 0;
            while (start <= current.size()) {
                std::size_t comma = current.find(',', start);
                if (comma == std::string::npos) comma = current.size();
                if (!is_valid_email(strip(current.substr(start, comma - start)))) out.type_mismatch = true;
                start = comma + 1;
            }
        } else {
            out.type_mismatch = !is_valid_email(current);
        }
    } else if (type == "url") {
        out.type_mismatch = !is_absolute_url(current);
    }

    const bool takes_pattern = type == "text" || type == "search" || type == "url" || type == "tel" ||
                               type == "email" || type == "password";
    if (takes_pattern && pattern_matcher) {
        if (const std::string* pattern = control.attribute("pattern")) {
            out.pattern_mismatch = !pattern_matcher(*pattern, current);
        }
    }

    if (is_number_type(type)) {
        double number = 0;
        if (parse_number(current, number)) {
            double bound = 0;
            if (has_attr_number(control, "min", bound) && number < bound) out.range_underflow = true;
            if (has_attr_number(control, "max", bound) && number > bound) out.range_overflow = true;
            const double step = step_of(control);
            if (step > 0) {
                const double steps = (number - step_base(control)) / step;
                if (std::fabs(steps - std::round(steps)) > 1e-9 * std::max(1.0, std::fabs(steps))) out.step_mismatch = true;
            }
        }
    }
    return out;
}

std::string validation_message(const Element& control) {
    if (!will_validate(control)) return std::string();
    const Validity v = validity(control);
    if (v.custom_error) return control.form_state()->custom_validity;
    if (v.value_missing) return "Please fill out this field.";
    if (v.type_mismatch) return "Please enter a value of the right kind.";
    if (v.pattern_mismatch) return "Please match the requested format.";
    if (v.range_underflow) return "The value is too small.";
    if (v.range_overflow) return "The value is too large.";
    if (v.step_mismatch) return "The value is not one of the allowed steps.";
    return std::string();
}

std::vector<std::shared_ptr<Element>> invalid_controls(const Element& form_or_fieldset) {
    std::vector<std::shared_ptr<Element>> out;
    auto controls = is_html(form_or_fieldset, "form") ? form_controls(form_or_fieldset) : fieldset_controls(form_or_fieldset);
    for (const auto& control : controls) {
        if (will_validate(*control) && !validity(*control).valid()) out.push_back(control);
    }
    return out;
}

// --- Numbers ----------------------------------------------------------------------

bool parse_number(const std::string& text, double& out) {
    // -?digits(.digits)?([eE][+-]?digits)? or -?.digits...
    std::size_t i = 0;
    const std::size_t n = text.size();
    if (i < n && text[i] == '-') ++i;
    std::size_t digits = 0;
    while (i < n && text[i] >= '0' && text[i] <= '9') { ++i; ++digits; }
    if (i < n && text[i] == '.') {
        ++i;
        std::size_t fraction = 0;
        while (i < n && text[i] >= '0' && text[i] <= '9') { ++i; ++fraction; }
        if (fraction == 0) return false;
        digits += fraction;
    }
    if (digits == 0) return false;
    if (i < n && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        if (i < n && (text[i] == '+' || text[i] == '-')) ++i;
        std::size_t exponent = 0;
        while (i < n && text[i] >= '0' && text[i] <= '9') { ++i; ++exponent; }
        if (exponent == 0) return false;
    }
    if (i != n) return false;
    out = std::strtod(text.c_str(), nullptr);
    return std::isfinite(out);
}

std::string number_to_string(double value) {
    if (value == 0) return "0";
    if (value == std::floor(value) && std::fabs(value) < 1e15) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.0f", value);
        return buffer;
    }
    // The shortest text that reads back as the same number.
    char buffer[40];
    for (int precision = 1; precision <= 17; ++precision) {
        std::snprintf(buffer, sizeof(buffer), "%.*g", precision, value);
        if (std::strtod(buffer, nullptr) == value) break;
    }
    return buffer;
}

bool step(Element& input, int count) {
    if (!is_html(input, "input")) return false;
    const std::string type = input_type(input);
    if (!is_number_type(type)) return false;
    const double step_size = step_of(input);
    if (step_size <= 0) return false;
    double min = 0, max = 0;
    const bool has_min = has_attr_number(input, "min", min) || type == "range";
    const bool has_max = has_attr_number(input, "max", max) || type == "range";
    if (type == "range") {
        min = attr_number(input, "min", 0);
        max = attr_number(input, "max", 100);
    }
    if (has_min && has_max && min > max) return true;
    const double base = step_base(input);
    double number = 0;
    if (!parse_number(input_value(input, type), number)) number = 0;

    const double steps = (number - base) / step_size;
    const bool aligned = std::fabs(steps - std::round(steps)) <= 1e-9 * std::max(1.0, std::fabs(steps));
    if (!aligned) {
        // Not on a step: go to the nearest one in the direction asked for.
        number = base + (count > 0 ? std::ceil(steps) : std::floor(steps)) * step_size;
    } else {
        number += count * step_size;
    }
    if (has_min && number < min) number = base + std::ceil((min - base) / step_size - 1e-9) * step_size;
    if (has_max && number > max) number = base + std::floor((max - base) / step_size + 1e-9) * step_size;
    set_value(input, number_to_string(number));
    return true;
}

// --- Reset and submission ---------------------------------------------------------

void reset_control(Element& control) {
    if (control.is_foreign()) return;
    const std::string& tag = control.tag_name();
    FormControlState* state = control.form_state();
    if (tag == "input" || tag == "textarea" || tag == "output") {
        if (state) {
            state->value.clear();
            state->value_dirty = false;
            state->checked_dirty = false;
            state->has_checked = false;
            state->selection_set = false;
        }
    } else if (tag == "select") {
        for (const auto& option : select_options(control)) {
            if (FormControlState* option_state = option->form_state()) {
                option_state->has_selected = false;
                option_state->selected_dirty = false;
            }
        }
        clear_none(control);
    }
    control.mark_needs_layout();
    Node::bump_tree_version();
}

std::vector<Entry> entry_list(const Element& form, const Element* submitter) {
    std::vector<Entry> out;
    for (const auto& control : form_controls(form)) {
        const Element& field = *control;
        if (ancestor(field, "datalist") || is_disabled(field)) continue;
        const std::string& tag = field.tag_name();
        std::string type;
        if (tag == "input") type = input_type(field);
        const bool is_button = tag == "button" || type == "submit" || type == "reset" || type == "button" || type == "image";
        if (is_button && &field != submitter) continue;
        if ((type == "checkbox" || type == "radio") && !checked(field)) continue;
        if (tag == "fieldset" || tag == "output" || tag == "object") continue;
        const std::string name = field.get_attribute("name");
        if (name.empty()) continue;
        if (tag == "select") {
            for (const auto& option : selected_options(field)) {
                if (!option_disabled(*option)) out.push_back({name, option_value(*option), false});
            }
        } else if (type == "file") {
            out.push_back({name, std::string(), true});
        } else {
            out.push_back({name, value(field), false});
        }
    }
    return out;
}

void attribute_changed(Element& element, const std::string& name, bool had_old, const std::string& old_value) {
    if (element.is_foreign()) return;
    const std::string& tag = element.tag_name();
    FormControlState* state = element.form_state();
    if (tag == "input") {
        if (name == "checked") {
            // The attribute is the default; a box nobody has touched follows it.
            if (state && state->checked_dirty) return;
            if (state) state->has_checked = false;
            if (element.has_attribute("checked") && input_type(element) == "radio") {
                set_checked(element, true, false);
                element.ensure_form_state().has_checked = true;
            }
        } else if (name == "type") {
            // Changing the kind of control: what it holds moves between the
            // attribute and the state as the standard lays down.
            Element before("input");
            if (had_old) before.m_attributes["type"] = old_value;
            const ValueMode old_mode = value_mode(input_type(before));
            const ValueMode new_mode = value_mode(input_type(element));
            if (old_mode == new_mode) return;
            if (old_mode == ValueMode::Value && state && state->value_dirty && !state->value.empty() &&
                (new_mode == ValueMode::Default || new_mode == ValueMode::DefaultOn)) {
                const std::string keep = state->value;
                state->value.clear();
                state->value_dirty = false;
                element.set_attribute("value", keep);
            } else if (state && new_mode != ValueMode::Value) {
                state->value.clear();
                state->value_dirty = false;
            } else if (state && old_mode != ValueMode::Value && new_mode == ValueMode::Value) {
                state->value.clear();
                state->value_dirty = false;
            }
        }
    } else if (tag == "option" && name == "selected") {
        if (state && state->selected_dirty) return;
        if (state) state->has_selected = false;
        if (element.has_attribute("selected")) {
            if (auto select = option_select(element)) {
                clear_none(*select);
                if (!is_multiple_select(*select)) {
                    for (const auto& other : select_options(*select)) {
                        if (other.get() == &element) continue;
                        if (FormControlState* other_state = other->form_state()) {
                            if (other_state->has_selected && other_state->selected) other_state->selected = false;
                        }
                    }
                }
            }
        }
    }
}

} // namespace forms

// --- The shorthands on Element ---------------------------------------------------

std::string Element::value() const { return forms::value(*this); }
void Element::set_value(std::string val) { forms::set_value(*this, val); }
bool Element::checked() const {
    return m_tag_name == "option" ? forms::option_selected(*this) : forms::checked(*this);
}
void Element::set_checked(bool c) { forms::set_checked(*this, c); }
bool Element::disabled() const { return forms::is_disabled(*this); }

} // namespace litebird
