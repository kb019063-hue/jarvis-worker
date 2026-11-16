// Form controls as scripts see them: the HTML interfaces on top of
// dom/FormState, activation behaviour (what a click does), focus, validation,
// submission and FormData.

#include "litebird/js/DomBindings.hpp"
#include "litebird/dom/FormState.hpp"
#include "litebird/dom/TreeWalk.hpp"
#include "litebird/js/EventLoop.hpp"
#include "litebird/js/JsBuiltins.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace litebird {
namespace dom {

namespace {

LITEBIRD_DOM_TAG(is_form, "form")
LITEBIRD_DOM_TAG(is_input, "input")
LITEBIRD_DOM_TAG(is_select, "select")
LITEBIRD_DOM_TAG(is_option, "option")
LITEBIRD_DOM_TAG(is_optgroup, "optgroup")
LITEBIRD_DOM_TAG(is_textarea, "textarea")
LITEBIRD_DOM_TAG(is_button, "button")
LITEBIRD_DOM_TAG(is_label, "label")
LITEBIRD_DOM_TAG(is_fieldset, "fieldset")
LITEBIRD_DOM_TAG(is_legend, "legend")
LITEBIRD_DOM_TAG(is_output, "output")
LITEBIRD_DOM_TAG(is_datalist, "datalist")
LITEBIRD_DOM_TAG(is_progress, "progress")
LITEBIRD_DOM_TAG(is_meter, "meter")

NodeVector to_nodes(const std::vector<std::shared_ptr<Element>>& elements) {
    return NodeVector(elements.begin(), elements.end());
}

// The pattern attribute is a JavaScript regular expression that must match
// the whole value.
bool pattern_matches(const std::string& pattern, const std::string& value) {
    try {
        JsValue regex = js_regexp_create("^(?:" + pattern + ")$", "u");
        return js_regexp_search(regex, JsString(value)) >= 0;
    } catch (const std::exception& ex) {
        if (js_is_internal_unwind(ex)) throw;
        return true;        // a pattern that is not a valid expression constrains nothing
    }
}

// ---------------------------------------------------------------------------
// form[0], form.name, select[2]
// ---------------------------------------------------------------------------

// The controls of `items` called `name` (by id or by name): one of them as
// its element, several as a RadioNodeList.
bool named_controls(Realm& realm, const std::shared_ptr<Node>& root, const NodeVector& items,
                    const std::string& name, JsValue& out) {
    if (name.empty()) return false;
    NodeVector matches;
    for (const auto& node : items) {
        const auto& element = static_cast<const Element&>(*node);
        const std::string* id = element.attribute("id");
        const std::string* other = element.attribute("name");
        if ((id && *id == name) || (other && *other == name)) matches.push_back(node);
    }
    if (matches.empty()) return false;
    if (matches.size() == 1) {
        out = wrap(realm, matches.front());
        return true;
    }
    out = make_live_list(realm, "RadioNodeList", root, [name](Node& form, NodeVector& list) {
        for (const auto& control : forms::form_controls(static_cast<Element&>(form))) {
            const std::string* id = control->attribute("id");
            const std::string* other = control->attribute("name");
            if ((id && *id == name) || (other && *other == name)) list.push_back(control);
        }
    });
    return true;
}

struct ControlExotic : JsExotic {
    Realm* realm{nullptr};
    /// The element this is the exotic of. The object owns the element, so
    /// the pointer is good for as long as anything can ask.
    Element* element{nullptr};
    bool is_select{false};

    NodeVector items() const {
        return to_nodes(is_select ? forms::select_options(*element) : forms::form_controls(*element));
    }
    bool get(JsObject*, const std::string& name, JsValue& out) override {
        std::size_t index = 0;
        if (!parse_index(name, index) || !realm->alive) return false;
        const NodeVector list = items();
        if (index >= list.size()) return false;
        out = wrap(*realm, list[index]);
        return true;
    }
    bool get_last(JsObject*, const std::string& name, JsValue& out) override {
        if (is_select || !realm->alive || name.empty() || name[0] == '%' || name[0] == '@') return false;
        return named_controls(*realm, element->shared_from_this(), items(), name, out);
    }
    bool has(JsObject*, const std::string& name) override {
        std::size_t index = 0;
        return parse_index(name, index) && index < items().size();
    }
    bool set(JsObject*, const std::string& name, const JsValue& value) override {
        std::size_t index = 0;
        if (!parse_index(name, index)) return false;
        if (!is_select || !realm->alive) return true;
        // select[i] = option replaces (or appends); select[i] = null removes.
        const NodeVector list = items();
        if (value.is_nullish()) {
            if (index < list.size()) remove_node(*realm, *list[index]);
            return true;
        }
        Node* node = node_ptr(value);
        if (!node || !is_html_element(*node, "option")) js_throw_type_error("The value assigned is not an option element.");
        if (index < list.size()) {
            auto parent = list[index]->parent();
            parent->insert_before(node->shared_from_this(), list[index]);
            parent->remove_child(list[index]);
        } else {
            element->append_child(node->shared_from_this());
        }
        return true;
    }
    void keys(JsObject*, std::vector<std::string>& out) override {
        const std::size_t count = items().size();
        for (std::size_t i = 0; i < count; ++i) out.push_back(std::to_string(i));
    }
};

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

struct ValidityObject : JsExotic {
    std::shared_ptr<Element> element;
    void trace(Heap& heap) override { trace_node(heap, element.get()); }
};

forms::Validity this_validity() {
    return forms::validity(*this_state<ValidityObject>("ValidityState").element);
}

void setup_validity_state(Iface& I) {
    I.getter("valueMissing", [](Realm&) { return boolean(this_validity().value_missing); });
    I.getter("typeMismatch", [](Realm&) { return boolean(this_validity().type_mismatch); });
    I.getter("patternMismatch", [](Realm&) { return boolean(this_validity().pattern_mismatch); });
    I.getter("tooLong", [](Realm&) { return boolean(this_validity().too_long); });
    I.getter("tooShort", [](Realm&) { return boolean(this_validity().too_short); });
    I.getter("rangeUnderflow", [](Realm&) { return boolean(this_validity().range_underflow); });
    I.getter("rangeOverflow", [](Realm&) { return boolean(this_validity().range_overflow); });
    I.getter("stepMismatch", [](Realm&) { return boolean(this_validity().step_mismatch); });
    I.getter("badInput", [](Realm&) { return boolean(this_validity().bad_input); });
    I.getter("customError", [](Realm&) { return boolean(this_validity().custom_error); });
    I.getter("valid", [](Realm&) { return boolean(this_validity().valid()); });
}

// checkValidity for one control: an `invalid` event when it fails.
bool check_control(Realm& r, Element& control) {
    if (!forms::will_validate(control) || forms::validity(control).valid()) return true;
    fire(r, control, "invalid", false, true);
    return false;
}

template <bool (*Accepts)(const Node&)>
void define_validation(Iface& I) {
    ElementMembers<Accepts> M(I);
    M.getter("willValidate", [](Realm&, Element& el) { return boolean(forms::will_validate(el)); });
    M.getter("validity", [](Realm& r, Element& el) {
        return cached(wrap(r, el), "validity", [&]() {
            auto state = std::make_shared<ValidityObject>();
            state->element = shared(el);
            return r.make("ValidityState", state);
        });
    });
    M.getter("validationMessage", [](Realm&, Element& el) { return str(forms::validation_message(el)); });
    M.method("checkValidity", 0, [](Realm& r, Element& el, Args) { return boolean(check_control(r, el)); });
    M.method("reportValidity", 0, [](Realm& r, Element& el, Args) { return boolean(check_control(r, el)); });
    M.method("setCustomValidity", 1, [](Realm&, Element& el, Args args) {
        require_args(args, 1, "setCustomValidity");
        el.ensure_form_state().custom_validity = to_str(args[0]);
        Node::bump_tree_version();
        return undefined();
    });
}

template <bool (*Accepts)(const Node&)>
void define_form_owner(Iface& I) {
    ElementMembers<Accepts> M(I);
    M.getter("form", [](Realm& r, Element& el) { return wrap(r, forms::form_owner(el)); });
}

template <bool (*Accepts)(const Node&)>
void define_labels(Iface& I) {
    ElementMembers<Accepts> M(I);
    M.getter("labels", [](Realm& r, Element& el) {
        auto root = el.shared_from_this();
        return cached(wrap(r, el), "labels", [&]() {
            return make_live_list(r, "NodeList", root, [](Node& control, NodeVector& out) {
                out = to_nodes(forms::labels_of(static_cast<Element&>(control)));
            });
        });
    });
}

// ---------------------------------------------------------------------------
// FormData
// ---------------------------------------------------------------------------

struct FormDataState : JsExotic {
    std::vector<std::pair<std::string, JsValue>> entries;
    void trace(Heap& heap) override {
        for (const auto& entry : entries) heap.mark_value(entry.second);
    }
};

FormDataState& this_form_data() {
    return this_state<FormDataState>("FormData");
}

// `form.action` and `button.formAction`: the attribute read as an address.
// Without the attribute, or with an empty one, the form goes to the page it
// is on, so that is the answer: the document's own address, fragment and all.
std::string action_url(Realm& realm, Element& element, const char* attribute) {
    const std::string* action = element.attribute(attribute);
    if (!action || action->empty()) {
        auto document = document_of(realm, element);
        return document ? document->url() : std::string("about:blank");
    }
    return resolve_url(realm, element, *action);
}

JsValue construct_form_data(Realm& realm, const InterfaceDef&, Args args, const JsValue* derived) {
    auto state = std::make_shared<FormDataState>();
    JsValue object = realm.make("FormData", state, derived);
    if (js_arg(args, 0).is_undefined()) return object;
    Node* form = node_ptr(args[0]);
    if (!form || !is_form(*form)) {
        js_throw_type_error("Failed to construct 'FormData': parameter 1 is not of type 'HTMLFormElement'.");
    }
    Element* submitter = nullptr;
    if (!js_arg(args, 1).is_nullish()) {
        Node* node = node_ptr(args[1]);
        if (!node || !node->is_element() || !forms::is_submit_button(static_cast<Element&>(*node))) {
            js_throw_type_error("Failed to construct 'FormData': the specified element is not a submit button.");
        }
        submitter = static_cast<Element*>(node);
        if (forms::form_owner(*submitter).get() != form) {
            throw_dom(realm, "NotFoundError", "The specified element is not owned by this form element.");
        }
    }
    for (const auto& entry : forms::entry_list(static_cast<Element&>(*form), submitter)) {
        // A file input with nothing chosen sends one empty, nameless file.
        state->entries.emplace_back(entry.name, entry.is_file ? make_file(realm, "", "", "application/octet-stream")
                                                              : str(entry.value));
    }
    // The page may still change what is sent.
    JsValue event = make_event(realm, "FormDataEvent", "formdata", true, false, {{"formData", object}});
    dispatch(realm, wrap(realm, *form), event, true);
    return object;
}

bool form_data_entry_at(Realm&, const JsValue& self, std::size_t index, JsValue& out) {
    FormDataState* state = state_of<FormDataState>(self);
    if (!state) throw_illegal_invocation("FormData");
    if (index >= state->entries.size()) return false;
    out = JsValue::make_array({str(state->entries[index].first), state->entries[index].second});
    return true;
}

// The value of an entry given to append() or set(): a string, or a File. A
// Blob that is not a File becomes one called "blob"; a file name given as the
// third argument renames it.
JsValue form_data_value(Realm& realm, Args args) {
    const JsValue& value = args[1];
    if (!is_blob(value)) return str(to_str(value));
    if (args.size() > 2 && !args[2].is_undefined()) return file_from_blob(realm, value, to_str(args[2]));
    return is_file(value) ? value : file_from_blob(realm, value, "blob");
}

void setup_form_data(Iface& I) {
    I.method("append", 2, [](Realm& r, Args args) {
        require_args(args, 2, "FormData.append");
        this_form_data().entries.emplace_back(to_str(args[0]), form_data_value(r, args));
        return undefined();
    });
    I.method("delete", 1, [](Realm&, Args args) {
        require_args(args, 1, "FormData.delete");
        auto& entries = this_form_data().entries;
        const std::string name = to_str(args[0]);
        entries.erase(std::remove_if(entries.begin(), entries.end(),
                                     [&](const auto& entry) { return entry.first == name; }),
                      entries.end());
        return undefined();
    });
    I.method("get", 1, [](Realm&, Args args) {
        require_args(args, 1, "FormData.get");
        const std::string name = to_str(args[0]);
        for (const auto& entry : this_form_data().entries) {
            if (entry.first == name) return entry.second;
        }
        return null();
    });
    I.method("getAll", 1, [](Realm&, Args args) {
        require_args(args, 1, "FormData.getAll");
        const std::string name = to_str(args[0]);
        std::vector<JsValue> out;
        for (const auto& entry : this_form_data().entries) {
            if (entry.first == name) out.push_back(entry.second);
        }
        return JsValue::make_array(out);
    });
    I.method("has", 1, [](Realm&, Args args) {
        require_args(args, 1, "FormData.has");
        const std::string name = to_str(args[0]);
        for (const auto& entry : this_form_data().entries) {
            if (entry.first == name) return boolean(true);
        }
        return boolean(false);
    });
    I.method("set", 2, [](Realm& r, Args args) {
        require_args(args, 2, "FormData.set");
        auto& entries = this_form_data().entries;
        const std::string name = to_str(args[0]);
        const JsValue value = form_data_value(r, args);
        bool placed = false;
        for (auto it = entries.begin(); it != entries.end();) {
            if (it->first != name) {
                ++it;
            } else if (!placed) {
                it->second = value;
                placed = true;
                ++it;
            } else {
                it = entries.erase(it);
            }
        }
        if (!placed) entries.emplace_back(name, value);
        return undefined();
    });
    auto make_iterator = [](int what) {
        return [what](Realm& r, Args) -> JsValue {
            const JsValue self = JsValue::current_this();
            this_form_data();
            auto index = std::make_shared<std::size_t>(0);
            Realm* realm = &r;
            return js_make_iterator([realm, self, index, what](JsValue& value) {
                JsValue pair;
                if (!realm->alive || !form_data_entry_at(*realm, self, *index, pair)) return false;
                ++*index;
                if (what == 0) value = pair;
                else value = pair.array_elements()[static_cast<std::size_t>(what - 1)];
                return true;
            }, {self});
        };
    };
    I.method("entries", 0, make_iterator(0));
    I.method("keys", 0, make_iterator(1));
    I.method("values", 0, make_iterator(2));
    define_method_value(I.proto, kIteratorKey, make_function(I.realm, "entries", 0, make_iterator(0)), PropertyMap::Hidden);
    I.method("forEach", 1, [](Realm&, Args args) {
        require_args(args, 1, "FormData.forEach");
        const JsValue self = JsValue::current_this();
        FormDataState& state = this_form_data();
        if (!args[0].is_function()) js_throw_type_error("FormData.forEach: parameter 1 is not a function.");
        for (std::size_t i = 0; i < state.entries.size(); ++i) {
            js_call(args[0], js_arg(args, 1), {state.entries[i].second, str(state.entries[i].first), self});
        }
        return undefined();
    });
}

} // namespace

std::shared_ptr<JsExotic> element_exotic(Realm& realm, Element& element) {
    if (element.is_foreign()) return nullptr;
    const bool select = element.tag_name() == "select";
    if (!select && element.tag_name() != "form") return nullptr;
    auto exotic = std::make_shared<ControlExotic>();
    exotic->realm = &realm;
    exotic->element = &element;
    exotic->is_select = select;
    return exotic;
}

// ---------------------------------------------------------------------------
// Focus
// ---------------------------------------------------------------------------

bool is_focusable(const Element& element) {
    if (!element.is_connected()) return false;
    if (element.is_foreign()) return element.has_attribute("tabindex");
    const std::string& tag = element.tag_name();
    if (tag == "input") return forms::input_type(element) != "hidden" && !forms::is_disabled(element);
    if (tag == "button" || tag == "select" || tag == "textarea") return !forms::is_disabled(element);
    if (tag == "a" || tag == "area") return element.has_attribute("href") || element.has_attribute("tabindex");
    if (tag == "iframe") return true;
    if (tag == "summary") {
        auto parent = element.parent();
        if (parent && is_html_element(*parent, "details")) return true;
    }
    if (const std::string* editable = element.attribute("contenteditable")) {
        if (ascii_lower(*editable) != "false") return true;
    }
    return element.has_attribute("tabindex");
}

void focus_element(Realm& realm, Element* element, const std::shared_ptr<Document>& blur_in) {
    if (element && !is_focusable(*element)) return;
    // With no element this is a blur: of the given document, or the window's.
    std::shared_ptr<Document> document = element ? document_of(realm, *element)
                                                 : (blur_in ? blur_in : realm.document);
    if (!document) return;
    auto old_focus = document->focused_element();
    if (old_focus && !old_focus->is_connected()) old_focus.reset();
    std::shared_ptr<Element> new_focus = element ? shared(*element) : nullptr;
    if (old_focus == new_focus) return;

    const JsValue old_value = old_focus ? wrap(realm, old_focus) : null();
    const JsValue new_value = new_focus ? wrap(realm, new_focus) : null();
    auto focus_event = [&](const char* type, bool bubbles, const JsValue& related) {
        return make_event(realm, "FocusEvent", type, bubbles, false,
                          {{"relatedTarget", related}, {"view", window_of(realm)}}, true);
    };
    if (old_focus) {
        old_focus->set_focused(false);
        document->set_focused_element(nullptr);
        dispatch(realm, old_value, focus_event("blur", false, new_value), true);
        dispatch(realm, old_value, focus_event("focusout", true, new_value), true);
    }
    if (new_focus && new_focus->is_connected()) {
        new_focus->set_focused(true);
        document->set_focused_element(new_focus);
        dispatch(realm, new_value, focus_event("focus", false, old_value), true);
        dispatch(realm, new_value, focus_event("focusin", true, old_value), true);
    }
}

// ---------------------------------------------------------------------------
// Submitting and resetting
// ---------------------------------------------------------------------------

void reset_form(Realm& realm, Element& form) {
    auto keep = shared(form);
    if (!fire(realm, form, "reset", true, true)) return;
    for (const auto& control : forms::form_controls(form)) forms::reset_control(*control);
}

void submit_form(Realm& realm, Element& form, Element* submitter, bool validate) {
    auto keep = shared(form);
    std::shared_ptr<Element> keep_submitter = submitter ? shared(*submitter) : nullptr;
    if (!form.is_connected()) return;
    if (!validate) return;         // form.submit(): no validation, no event, and nowhere to go
    const bool skip_validation = form.has_attribute("novalidate") ||
                                 (submitter && submitter->has_attribute("formnovalidate"));
    if (!skip_validation) {
        bool all_valid = true;
        for (const auto& control : forms::invalid_controls(form)) {
            all_valid = false;
            fire(realm, *control, "invalid", false, true);
        }
        if (!all_valid) return;
    }
    JsValue event = make_event(realm, "SubmitEvent", "submit", true, true,
                               {{"submitter", submitter ? wrap(realm, *submitter) : null()}});
    dispatch(realm, wrap(realm, form), event, true);
    // A submission that was not cancelled would load another page.
}

// ---------------------------------------------------------------------------
// What a click does
// ---------------------------------------------------------------------------

namespace {

bool has_activation_behavior(const Element& element) {
    if (element.is_foreign()) return false;
    const std::string& tag = element.tag_name();
    if (tag == "input") {
        const std::string type = forms::input_type(element);
        return type == "checkbox" || type == "radio" || type == "submit" || type == "image" || type == "reset";
    }
    if (tag == "button") return forms::button_type(element) != "button";
    if (tag == "a" || tag == "area") return element.has_attribute("href");
    return tag == "label" || tag == "summary";
}

} // namespace

Activation before_click(Realm&, const NodeVector& path) {
    Activation activation;
    for (const auto& node : path) {
        if (!node->is_element()) continue;
        auto& element = static_cast<Element&>(*node);
        if (!has_activation_behavior(element)) continue;
        activation.target = shared(element);
        break;
    }
    if (!activation.target) return activation;
    Element& target = *activation.target;
    if (forms::is_disabled(target)) {
        activation.target.reset();          // a disabled control does nothing when clicked
        return activation;
    }
    if (target.tag_name() != "input") return activation;
    const std::string type = forms::input_type(target);
    if (type == "checkbox") {
        activation.legacy_pre = true;
        activation.checkbox_was_checked = forms::checked(target);
        forms::set_checked(target, !activation.checkbox_was_checked);
    } else if (type == "radio") {
        activation.legacy_pre = true;
        activation.checkbox_was_checked = forms::checked(target);
        for (const auto& member : forms::radio_group(target)) {
            if (member.get() != &target && forms::checked(*member)) activation.radio_previous = member;
        }
        forms::set_checked(target, true);
    }
    return activation;
}

void after_click(Realm& realm, Activation& activation, const JsValue& event, bool cancelled) {
    Element& target = *activation.target;
    const std::string& tag = target.tag_name();
    const std::string type = tag == "input" ? forms::input_type(target) : std::string();

    if (cancelled) {
        // The change made ahead of the listeners is taken back.
        if (type == "checkbox") {
            forms::set_checked(target, activation.checkbox_was_checked);
        } else if (type == "radio") {
            if (activation.radio_previous) forms::set_checked(*activation.radio_previous, true);
            else forms::set_checked(target, activation.checkbox_was_checked);
        }
        return;
    }
    if (type == "checkbox" || type == "radio") {
        if (!target.is_connected()) return;
        if (type == "radio" && activation.checkbox_was_checked) return;     // it was checked already
        JsValue object = wrap(realm, target);
        dispatch(realm, object, make_event(realm, "Event", "input", true, false, {}, true), true);
        dispatch(realm, object, make_event(realm, "Event", "change", true, false), true);
        return;
    }
    if (tag == "input" || tag == "button") {
        auto form = forms::form_owner(target);
        if (!form) return;
        const bool reset = tag == "input" ? type == "reset" : forms::button_type(target) == "reset";
        if (reset) reset_form(realm, *form);
        else submit_form(realm, *form, &target, true);
        return;
    }
    if (tag == "label") {
        auto control = forms::label_control(target);
        if (!control || forms::is_disabled(*control)) return;
        // A click that started on the control itself has done its work already.
        JsValue source = event.get_property("target");
        Node* origin = node_ptr(source);
        for (std::shared_ptr<Node> at = origin ? origin->shared_from_this() : nullptr; at && at.get() != &target; at = at->parent()) {
            if (at == control) return;
            if (at->is_element() && has_activation_behavior(static_cast<Element&>(*at))) return;
        }
        JsValue click = make_event(realm, "PointerEvent", "click", true, true, {{"view", window_of(realm)}}, true);
        dispatch(realm, wrap(realm, control), click, false);
        return;
    }
    if (tag == "a" || tag == "area") {
        const std::string* href = target.attribute("href");
        Url address;
        if (href && target.is_connected() && parse_url(realm, target, *href, address)) navigate(realm, address);
        return;
    }
    if (tag == "summary") {
        auto parent = target.parent();
        if (parent && is_html_element(*parent, "details")) {
            auto& details = static_cast<Element&>(*parent);
            if (details.has_attribute("open")) details.remove_attribute("open");
            else details.set_attribute("open", "");
        }
    }
}

std::shared_ptr<Element> make_option(Realm& realm, Args args) {
    auto option = std::make_shared<Element>("option");
    if (realm.document) option->set_owner_document(realm.document);
    const std::string text = js_arg(args, 0).is_undefined() ? std::string() : to_str(args[0]);
    if (!text.empty()) {
        auto node = std::make_shared<Text>(text);
        if (realm.document) node->set_owner_document(realm.document);
        option->append_child(node);
    }
    if (!js_arg(args, 1).is_undefined()) option->set_attribute("value", to_str(args[1]));
    if (to_bool(js_arg(args, 2))) option->set_attribute("selected", "");
    FormControlState& state = option->ensure_form_state();
    state.selected = to_bool(js_arg(args, 3));
    state.has_selected = true;
    return option;
}

// ---------------------------------------------------------------------------
// The interfaces
// ---------------------------------------------------------------------------

namespace {

// --- Text selection of input and textarea ----------------------------------------

std::size_t value_length(const Element& control) {
    return utf16_length(forms::value(control));
}

void selection_of(const Element& control, std::size_t& start, std::size_t& end, std::string& direction) {
    const std::size_t length = value_length(control);
    const FormControlState* state = control.form_state();
    if (state && state->selection_set) {
        start = std::min<std::size_t>(state->selection_start, length);
        end = std::min<std::size_t>(state->selection_end, length);
        direction = state->selection_direction;
        return;
    }
    // Nothing chosen: the caret stands after the text.
    start = end = (state && state->value_dirty) ? length : 0;
    direction = "none";
}

void set_selection(Realm& r, Element& control, std::size_t start, std::size_t end, const std::string& direction) {
    const std::size_t length = value_length(control);
    FormControlState& state = control.ensure_form_state();
    end = std::min(end, length);
    start = std::min(start, end);
    state.selection_set = true;
    state.selection_start = static_cast<unsigned>(start);
    state.selection_end = static_cast<unsigned>(end);
    state.selection_direction = (direction == "forward" || direction == "backward") ? direction : "none";
    // The select event comes as a task.
    JsValue target = wrap(r, control);
    Realm* realm = &r;
    EventLoop::default_loop().post_task([realm, target]() {
        if (realm->alive) fire(*realm, target, "select", true, false);
    });
}

template <bool (*Accepts)(const Node&)>
void define_text_selection(Iface& I) {
    ElementMembers<Accepts> M(I);
    auto require_selectable = [](Realm& r, Element& el) {
        if (!forms::has_text_selection(el)) {
            throw_dom(r, "InvalidStateError", "The input element's type does not support selection.");
        }
    };
    M.accessor("selectionStart",
        [](Realm&, Element& el) {
            if (!forms::has_text_selection(el)) return null();
            std::size_t start = 0, end = 0;
            std::string direction;
            selection_of(el, start, end, direction);
            return num(static_cast<double>(start));
        },
        [require_selectable](Realm& r, Element& el, const JsValue& value) {
            require_selectable(r, el);
            std::size_t start = 0, end = 0;
            std::string direction;
            selection_of(el, start, end, direction);
            const std::size_t wanted = to_ulong(value);
            set_selection(r, el, wanted, std::max(end, wanted), direction);
        });
    M.accessor("selectionEnd",
        [](Realm&, Element& el) {
            if (!forms::has_text_selection(el)) return null();
            std::size_t start = 0, end = 0;
            std::string direction;
            selection_of(el, start, end, direction);
            return num(static_cast<double>(end));
        },
        [require_selectable](Realm& r, Element& el, const JsValue& value) {
            require_selectable(r, el);
            std::size_t start = 0, end = 0;
            std::string direction;
            selection_of(el, start, end, direction);
            set_selection(r, el, start, to_ulong(value), direction);
        });
    M.accessor("selectionDirection",
        [](Realm&, Element& el) {
            if (!forms::has_text_selection(el)) return null();
            std::size_t start = 0, end = 0;
            std::string direction;
            selection_of(el, start, end, direction);
            return str(direction);
        },
        [require_selectable](Realm& r, Element& el, const JsValue& value) {
            require_selectable(r, el);
            std::size_t start = 0, end = 0;
            std::string direction;
            selection_of(el, start, end, direction);
            set_selection(r, el, start, end, to_str(value));
        });
    M.method("select", 0, [](Realm& r, Element& el, Args) {
        if (forms::has_text_selection(el)) set_selection(r, el, 0, value_length(el), "none");
        return undefined();
    });
    M.method("setSelectionRange", 2, [require_selectable](Realm& r, Element& el, Args args) {
        require_args(args, 2, "setSelectionRange");
        require_selectable(r, el);
        set_selection(r, el, to_ulong(args[0]), to_ulong(args[1]),
                      js_arg(args, 2).is_undefined() ? std::string("none") : to_str(args[2]));
        return undefined();
    });
    M.method("setRangeText", 1, [require_selectable](Realm& r, Element& el, Args args) {
        require_args(args, 1, "setRangeText");
        require_selectable(r, el);
        std::size_t selection_start = 0, selection_end = 0;
        std::string direction;
        selection_of(el, selection_start, selection_end, direction);
        std::size_t start = selection_start, end = selection_end;
        if (args.size() >= 3) {
            start = to_ulong(args[1]);
            end = to_ulong(args[2]);
        } else if (args.size() == 2) {
            js_throw_type_error("Failed to execute 'setRangeText': 3 arguments required, but only 2 present.");
        }
        if (start > end) throw_dom(r, "IndexSizeError", "The start (" + std::to_string(start) + ") is larger than the end (" + std::to_string(end) + ").");
        std::u16string text = js_utf8_to_utf16(forms::value(el));
        start = std::min(start, text.size());
        end = std::min(end, text.size());
        const std::u16string replacement = js_utf8_to_utf16(to_str(args[0]));
        text.replace(start, end - start, replacement);
        forms::set_value(el, js_utf16_to_utf8(text));
        const std::string mode = js_arg(args, 3).is_undefined() ? std::string("preserve") : to_str(args[3]);
        const std::size_t new_end = start + replacement.size();
        if (mode == "select") {
            selection_start = start;
            selection_end = new_end;
        } else if (mode == "start") {
            selection_start = selection_end = start;
        } else if (mode == "end") {
            selection_start = selection_end = new_end;
        } else {
            // "preserve": the selection keeps hold of the text it was on.
            const long delta = static_cast<long>(replacement.size()) - static_cast<long>(end - start);
            if (selection_start > end) selection_start = static_cast<std::size_t>(static_cast<long>(selection_start) + delta);
            else if (selection_start > start) selection_start = start;
            if (selection_end > end) selection_end = static_cast<std::size_t>(static_cast<long>(selection_end) + delta);
            else if (selection_end > start) selection_end = new_end;
        }
        set_selection(r, el, selection_start, selection_end, direction);
        return undefined();
    });
}

template <bool (*Accepts)(const Node&)>
void define_length_limits(Iface& I) {
    ElementMembers<Accepts> M(I);
    for (const char* name : {"maxLength", "minLength"}) {
        const std::string attribute = ascii_lower(name);
        M.accessor(name,
            [attribute](Realm&, Element& el) {
                long value = 0;
                const std::string* text = el.attribute(attribute);
                return num(text && parse_html_integer(*text, value) && value >= 0 ? static_cast<double>(value) : -1.0);
            },
            [attribute](Realm& r, Element& el, const JsValue& value) {
                const long number = to_long(value);
                if (number < 0) throw_dom(r, "IndexSizeError", "The value provided (" + std::to_string(number) + ") is negative.");
                el.set_attribute(attribute, std::to_string(number));
            });
    }
}

// --- input -----------------------------------------------------------------------

void setup_input(Iface& I) {
    ElementMembers<&is_input> M(I);
    reflect_attributes(I, "s:accept s:alt s:autocomplete s:dirName:dirname b:disabled b:formNoValidate:formnovalidate "
                          "s:formTarget:formtarget s:max s:min b:multiple s:name s:pattern s:placeholder "
                          "b:readOnly:readonly b:required s:step r:src s:align s:useMap:usemap "
                          "b:defaultChecked:checked s:defaultValue:value p:size=20 u:width u:height");
    M.accessor("type",
        [](Realm&, Element& el) { return str(forms::input_type(el)); },
        [](Realm&, Element& el, const JsValue& value) { el.set_attribute("type", to_str(value)); });
    M.accessor("value",
        [](Realm&, Element& el) { return str(forms::value(el)); },
        [](Realm& r, Element& el, const JsValue& value) {
            if (!forms::set_value(el, to_str_null_empty(value))) {
                throw_dom(r, "InvalidStateError", "This input element accepts a filename, which may only be programmatically set to the empty string.");
            }
        });
    M.accessor("checked",
        [](Realm&, Element& el) { return boolean(forms::checked(el)); },
        [](Realm&, Element& el, const JsValue& value) { forms::set_checked(el, to_bool(value)); });
    M.accessor("indeterminate",
        [](Realm& r, Element& el) {
            const JsValue* value = el.script_object() ? slot(wrap(r, el), "indeterminate") : nullptr;
            return boolean(value && value->is_truthy());
        },
        [](Realm& r, Element& el, const JsValue& value) { set_slot(wrap(r, el), "indeterminate", boolean(to_bool(value))); });
    M.getter("files", [](Realm& r, Element& el) {
        if (forms::input_type(el) != "file") return null();
        return cached(wrap(r, el), "files", [&]() { return r.make("FileList"); });
    });
    M.getter("list", [](Realm& r, Element& el) {
        const std::string* id = el.attribute("list");
        if (!id || id->empty()) return null();
        auto document = document_of(r, el);
        auto found = document ? document->get_element_by_id(*id) : nullptr;
        return found && is_html_element(*found, "datalist") ? wrap(r, found) : null();
    });
    M.accessor("valueAsNumber",
        [](Realm&, Element& el) {
            return num(forms::value_as_number(el));
        },
        [](Realm& r, Element& el, const JsValue& value) {
            const double number = to_double(value);
            if (std::isinf(number)) js_throw_type_error("The provided double value is non-finite.");
            const std::string type = forms::input_type(el);
            if (!forms::has_value_as_number(type)) {
                throw_dom(r, "InvalidStateError", "This input element does not support Number values.");
            }
            forms::set_value_as_number(el, number);
        });
    M.accessor("valueAsDate",
        [](Realm& r, Element& el) {
            double ms = 0;
            if (!forms::value_as_utc_ms(el, ms)) return null();
            JsObject* cell = Heap::default_heap().allocate_object();
            cell->prototype = r.intrinsics ? r.intrinsics->date_proto : JsIntrinsics::get().date_proto;
            cell->klass = ObjectClass::Date;
            cell->internal = std::make_shared<double>(ms);
            return JsValue::make_object(cell);
        },
        [](Realm& r, Element& el, const JsValue& value) {
            double ms = std::numeric_limits<double>::quiet_NaN();
            bool is_null = value.is_nullish();
            if (!is_null) {
                if (!value.is_object() || !value.object_val || value.object_val->klass != ObjectClass::Date ||
                    !value.object_val->internal) {
                    js_throw_type_error("The provided value is not a Date.");
                }
                ms = *std::static_pointer_cast<double>(value.object_val->internal);
            }
            const std::string type = forms::input_type(el);
            if (!forms::has_value_as_date(type)) {
                throw_dom(r, "InvalidStateError", "This input element does not support Date values.");
            }
            if (is_null) {
                forms::set_value(el, std::string());
                return;
            }
            forms::set_value_as_utc_ms(el, ms);
        });
    M.method("stepUp", 0, [](Realm& r, Element& el, Args args) {
        const int count = js_arg(args, 0).is_undefined() ? 1 : to_long(args[0]);
        if (!forms::step(el, count)) throw_dom(r, "InvalidStateError", "This form element is not steppable.");
        return undefined();
    });
    M.method("stepDown", 0, [](Realm& r, Element& el, Args args) {
        const int count = js_arg(args, 0).is_undefined() ? 1 : to_long(args[0]);
        if (!forms::step(el, -count)) throw_dom(r, "InvalidStateError", "This form element is not steppable.");
        return undefined();
    });
    M.method("showPicker", 0, [](Realm&, Element&, Args) { return undefined(); });
    M.accessor("formAction",
        [](Realm& r, Element& el) {
            return str(action_url(r, el, "formaction"));
        },
        [](Realm&, Element& el, const JsValue& value) { el.set_attribute("formaction", to_str(value)); });
    M.keyword("formMethod", {"get", "post", "dialog"}, "", "get", "formmethod");
    M.keyword("formEnctype", {"application/x-www-form-urlencoded", "multipart/form-data", "text/plain"}, "",
              "application/x-www-form-urlencoded", "formenctype");
    define_length_limits<&is_input>(I);
    define_text_selection<&is_input>(I);
    define_validation<&is_input>(I);
    define_form_owner<&is_input>(I);
    define_labels<&is_input>(I);
}

// --- textarea ----------------------------------------------------------------------

void setup_textarea(Iface& I) {
    ElementMembers<&is_textarea> M(I);
    reflect_attributes(I, "s:autocomplete s:dirName:dirname b:disabled s:name s:placeholder b:readOnly:readonly "
                          "b:required s:wrap p:cols=20 p:rows=2");
    M.getter("type", [](Realm&, Element&) { return str("textarea"); });
    M.accessor("value",
        [](Realm&, Element& el) { return str(forms::value(el)); },
        [](Realm&, Element& el, const JsValue& value) { forms::set_value(el, to_str_null_empty(value)); });
    M.accessor("defaultValue",
        [](Realm&, Element& el) { return str(el.text_content()); },
        [](Realm&, Element& el, const JsValue& value) { set_text_content(el, to_str(value)); });
    M.getter("textLength", [](Realm&, Element& el) { return num(static_cast<double>(value_length(el))); });
    define_length_limits<&is_textarea>(I);
    define_text_selection<&is_textarea>(I);
    define_validation<&is_textarea>(I);
    define_form_owner<&is_textarea>(I);
    define_labels<&is_textarea>(I);
}

// --- select and option ---------------------------------------------------------------

JsValue options_collection(Realm& r, Element& select) {
    auto root = select.shared_from_this();
    return cached(wrap(r, select), "options", [&]() {
        return make_live_list(r, "HTMLOptionsCollection", root, [](Node& node, NodeVector& out) {
            out = to_nodes(forms::select_options(static_cast<Element&>(node)));
        });
    });
}

// select.add(element, before) and options.add(element, before).
void add_option(Realm& r, Element& select, Args args, const char* what) {
    require_args(args, 1, what);
    Node* node = node_ptr(args[0]);
    if (!node || !(is_html_element(*node, "option") || is_html_element(*node, "optgroup"))) {
        js_throw_type_error(std::string("Failed to execute '") + what + "': the element is not an option or an optgroup.");
    }
    if (node->is_host_including_inclusive_ancestor_of(select)) {
        throw_dom(r, "HierarchyRequestError", "The element is an ancestor of the select.");
    }
    std::shared_ptr<Node> reference;
    const JsValue& before = js_arg(args, 1);
    if (Node* given = node_ptr(before)) {
        reference = given->shared_from_this();
    } else if (!before.is_nullish()) {
        const auto options = forms::select_options(select);
        const long index = to_long(before);
        if (index >= 0 && index < static_cast<long>(options.size())) reference = options[static_cast<std::size_t>(index)];
    }
    if (reference && !select.is_host_including_inclusive_ancestor_of(*reference)) {
        throw_dom(r, "NotFoundError", "The element before which the option is to be added is not in the select.");
    }
    if (reference.get() == node) return;
    auto parent = reference ? reference->parent() : select.shared_from_this();
    pre_insert(r, *parent, node->shared_from_this(), reference.get());
}

void set_options_length(Realm& r, Element& select, std::size_t wanted) {
    auto options = forms::select_options(select);
    while (options.size() > wanted) {
        remove_node(r, *options.back());
        options.pop_back();
    }
    while (options.size() < wanted) {
        auto option = std::make_shared<Element>("option");
        if (auto document = document_of(r, select)) option->set_owner_document(document);
        select.append_child(option);
        options.push_back(option);
    }
}

Element& select_of_collection(Realm& r) {
    NodeVector ignored;
    const JsValue& self = JsValue::current_this();
    if (!list_nodes(self, ignored)) throw_illegal_invocation("HTMLOptionsCollection");
    const JsValue* owner = slot(self, "select");
    Node* node = owner ? node_ptr(*owner) : nullptr;
    if (!node) throw_illegal_invocation("HTMLOptionsCollection");
    (void)r;
    return static_cast<Element&>(*node);
}

void setup_options_collection(Iface& I) {
    I.accessor("length",
        [](Realm& r) { return num(static_cast<double>(forms::select_options(select_of_collection(r)).size())); },
        [](Realm& r, const JsValue& value) { set_options_length(r, select_of_collection(r), to_ulong(value)); });
    I.accessor("selectedIndex",
        [](Realm& r) { return num(forms::selected_index(select_of_collection(r))); },
        [](Realm& r, const JsValue& value) { forms::set_selected_index(select_of_collection(r), to_long(value)); });
    I.method("add", 1, [](Realm& r, Args args) {
        add_option(r, select_of_collection(r), args, "HTMLOptionsCollection.add");
        return undefined();
    });
    I.method("remove", 1, [](Realm& r, Args args) {
        Element& select = select_of_collection(r);
        const auto options = forms::select_options(select);
        const long index = to_long(js_arg(args, 0));
        if (index >= 0 && index < static_cast<long>(options.size())) remove_node(r, *options[static_cast<std::size_t>(index)]);
        return undefined();
    });
}

void setup_select(Iface& I) {
    ElementMembers<&is_select> M(I);
    reflect_attributes(I, "s:autocomplete b:disabled b:multiple s:name b:required");
    M.accessor("size",
        [](Realm&, Element& el) {
            unsigned long value = 0;
            const std::string* text = el.attribute("size");
            return num(text && parse_html_non_negative_integer(*text, value) ? static_cast<double>(value) : 0.0);
        },
        [](Realm&, Element& el, const JsValue& value) { el.set_attribute("size", std::to_string(to_ulong(value))); });
    M.getter("type", [](Realm&, Element& el) {
        return str(forms::is_multiple_select(el) ? "select-multiple" : "select-one");
    });
    M.getter("options", [](Realm& r, Element& el) {
        JsValue options = options_collection(r, el);
        if (!slot(options, "select")) set_slot(options, "select", wrap(r, el));
        return options;
    });
    M.accessor("length",
        [](Realm&, Element& el) { return num(static_cast<double>(forms::select_options(el).size())); },
        [](Realm& r, Element& el, const JsValue& value) { set_options_length(r, el, to_ulong(value)); });
    M.method("item", 1, [](Realm& r, Element& el, Args args) {
        require_args(args, 1, "HTMLSelectElement.item");
        const auto options = forms::select_options(el);
        const std::size_t index = to_ulong(args[0]);
        return index < options.size() ? wrap(r, options[index]) : null();
    });
    M.method("namedItem", 1, [](Realm& r, Element& el, Args args) {
        require_args(args, 1, "HTMLSelectElement.namedItem");
        const std::string name = to_str(args[0]);
        if (name.empty()) return null();
        for (const auto& option : forms::select_options(el)) {
            if (option->get_attribute("id") == name || option->get_attribute("name") == name) return wrap(r, option);
        }
        return null();
    });
    M.method("add", 1, [](Realm& r, Element& el, Args args) {
        add_option(r, el, args, "HTMLSelectElement.add");
        return undefined();
    });
    M.method("remove", 0, [](Realm& r, Element& el, Args args) {
        if (args.empty()) {
            remove_node(r, el);         // ChildNode.remove()
            return undefined();
        }
        const auto options = forms::select_options(el);
        const long index = to_long(args[0]);
        if (index >= 0 && index < static_cast<long>(options.size())) remove_node(r, *options[static_cast<std::size_t>(index)]);
        return undefined();
    });
    M.getter("selectedOptions", [](Realm& r, Element& el) {
        auto root = el.shared_from_this();
        return cached(wrap(r, el), "selectedOptions", [&]() {
            return make_live_list(r, "HTMLCollection", root, [](Node& node, NodeVector& out) {
                out = to_nodes(forms::selected_options(static_cast<Element&>(node)));
            });
        });
    });
    M.accessor("selectedIndex",
        [](Realm&, Element& el) { return num(forms::selected_index(el)); },
        [](Realm&, Element& el, const JsValue& value) { forms::set_selected_index(el, to_long(value)); });
    M.accessor("value",
        [](Realm&, Element& el) { return str(forms::value(el)); },
        [](Realm&, Element& el, const JsValue& value) { forms::set_value(el, to_str(value)); });
    M.method("showPicker", 0, [](Realm&, Element&, Args) { return undefined(); });
    define_validation<&is_select>(I);
    define_form_owner<&is_select>(I);
    define_labels<&is_select>(I);
}

void setup_option(Iface& I) {
    ElementMembers<&is_option> M(I);
    reflect_attributes(I, "b:disabled b:defaultSelected:selected");
    M.getter("form", [](Realm& r, Element& el) {
        auto select = forms::option_select(el);
        return wrap(r, select ? forms::form_owner(*select) : nullptr);
    });
    M.accessor("label",
        [](Realm&, Element& el) { return str(forms::option_label(el)); },
        [](Realm&, Element& el, const JsValue& value) { el.set_attribute("label", to_str(value)); });
    M.accessor("selected",
        [](Realm&, Element& el) { return boolean(forms::option_selected(el)); },
        [](Realm&, Element& el, const JsValue& value) { forms::set_option_selected(el, to_bool(value)); });
    M.accessor("value",
        [](Realm&, Element& el) { return str(forms::option_value(el)); },
        [](Realm&, Element& el, const JsValue& value) { el.set_attribute("value", to_str(value)); });
    M.accessor("text",
        [](Realm&, Element& el) { return str(forms::option_text(el)); },
        [](Realm&, Element& el, const JsValue& value) { set_text_content(el, to_str(value)); });
    M.getter("index", [](Realm&, Element& el) {
        auto select = forms::option_select(el);
        if (!select) return num(0);
        const auto options = forms::select_options(*select);
        for (std::size_t i = 0; i < options.size(); ++i) {
            if (options[i].get() == &el) return num(static_cast<double>(i));
        }
        return num(0);
    });
}

void setup_optgroup(Iface& I) {
    reflect_attributes(I, "b:disabled s:label");
}

// --- button, label, fieldset, legend, output, datalist, progress, meter ----------------

void setup_button(Iface& I) {
    ElementMembers<&is_button> M(I);
    reflect_attributes(I, "b:disabled b:formNoValidate:formnovalidate s:formTarget:formtarget s:name s:value");
    M.accessor("type",
        [](Realm&, Element& el) { return str(forms::button_type(el)); },
        [](Realm&, Element& el, const JsValue& value) { el.set_attribute("type", to_str(value)); });
    M.accessor("formAction",
        [](Realm& r, Element& el) {
            return str(action_url(r, el, "formaction"));
        },
        [](Realm&, Element& el, const JsValue& value) { el.set_attribute("formaction", to_str(value)); });
    M.keyword("formMethod", {"get", "post", "dialog"}, "", "get", "formmethod");
    M.keyword("formEnctype", {"application/x-www-form-urlencoded", "multipart/form-data", "text/plain"}, "",
              "application/x-www-form-urlencoded", "formenctype");
    define_validation<&is_button>(I);
    define_form_owner<&is_button>(I);
    define_labels<&is_button>(I);
}

void setup_label(Iface& I) {
    ElementMembers<&is_label> M(I);
    reflect_attributes(I, "s:htmlFor:for");
    M.getter("control", [](Realm& r, Element& el) { return wrap(r, forms::label_control(el)); });
    M.getter("form", [](Realm& r, Element& el) {
        auto control = forms::label_control(el);
        return wrap(r, control ? forms::form_owner(*control) : nullptr);
    });
}

void setup_fieldset(Iface& I) {
    ElementMembers<&is_fieldset> M(I);
    reflect_attributes(I, "b:disabled s:name");
    M.getter("type", [](Realm&, Element&) { return str("fieldset"); });
    M.getter("elements", [](Realm& r, Element& el) {
        auto root = el.shared_from_this();
        return cached(wrap(r, el), "elements", [&]() {
            return make_live_list(r, "HTMLCollection", root, [](Node& node, NodeVector& out) {
                out = to_nodes(forms::fieldset_controls(static_cast<Element&>(node)));
            });
        });
    });
    define_validation<&is_fieldset>(I);
    define_form_owner<&is_fieldset>(I);
}

void setup_legend(Iface& I) {
    ElementMembers<&is_legend> M(I);
    reflect_attributes(I, "s:align");
    M.getter("form", [](Realm& r, Element& el) {
        auto parent = el.parent();
        if (!parent || !is_html_element(*parent, "fieldset")) return null();
        return wrap(r, forms::form_owner(static_cast<Element&>(*parent)));
    });
}

void setup_output(Iface& I) {
    ElementMembers<&is_output> M(I);
    reflect_attributes(I, "s:name");
    M.getter("type", [](Realm&, Element&) { return str("output"); });
    M.token_list("htmlFor", "for");
    M.accessor("value",
        [](Realm&, Element& el) { return str(el.text_content()); },
        [](Realm& r, Element& el, const JsValue& value) {
            // From now on the text is the value, and the default is kept aside.
            JsValue self = wrap(r, el);
            if (!slot(self, "outputDefault")) set_slot(self, "outputDefault", str(el.text_content()));
            set_text_content(el, to_str(value));
        });
    M.accessor("defaultValue",
        [](Realm& r, Element& el) {
            const JsValue* kept = el.script_object() ? slot(wrap(r, el), "outputDefault") : nullptr;
            return kept ? *kept : str(el.text_content());
        },
        [](Realm& r, Element& el, const JsValue& value) {
            JsValue self = wrap(r, el);
            if (slot(self, "outputDefault")) set_slot(self, "outputDefault", str(to_str(value)));
            else set_text_content(el, to_str(value));
        });
    define_validation<&is_output>(I);
    define_form_owner<&is_output>(I);
    define_labels<&is_output>(I);
}

void setup_datalist(Iface& I) {
    ElementMembers<&is_datalist> M(I);
    M.getter("options", [](Realm& r, Element& el) {
        auto root = el.shared_from_this();
        return cached(wrap(r, el), "options", [&]() {
            return make_live_list(r, "HTMLCollection", root, [](Node& node, NodeVector& out) {
                for_each_descendant(node, [&](Node& inner) {
                    if (is_html_element(inner, "option")) out.push_back(inner.shared_from_this());
                    return true;
                });
            });
        });
    });
}

double number_attribute(const Element& element, const char* name, double fallback) {
    double value = 0;
    const std::string* text = element.attribute(name);
    return text && forms::parse_number(strip_ascii_space(*text), value) ? value : fallback;
}

template <bool (*Accepts)(const Node&)>
void define_number(Iface& I, const char* name, double (*read)(const Element&)) {
    ElementMembers<Accepts> M(I);
    const std::string attribute = ascii_lower(name);
    M.accessor(name,
        [read](Realm&, Element& el) { return num(read(el)); },
        [attribute](Realm&, Element& el, const JsValue& value) {
            const double number = to_double(value);
            if (!std::isfinite(number)) js_throw_type_error("The provided double value is non-finite.");
            el.set_attribute(attribute, forms::number_to_string(number));
        });
}

void setup_progress(Iface& I) {
    ElementMembers<&is_progress> M(I);
    define_number<&is_progress>(I, "max", [](const Element& el) {
        const double max = number_attribute(el, "max", 1);
        return max > 0 ? max : 1.0;
    });
    define_number<&is_progress>(I, "value", [](const Element& el) {
        const double max = number_attribute(el, "max", 1);
        const double value = number_attribute(el, "value", 0);
        return std::min(std::max(value, 0.0), max > 0 ? max : 1.0);
    });
    M.getter("position", [](Realm&, Element& el) {
        if (!el.has_attribute("value")) return num(-1);
        double max = number_attribute(el, "max", 1);
        if (!(max > 0)) max = 1;
        return num(std::min(std::max(number_attribute(el, "value", 0), 0.0), max) / max);
    });
    define_labels<&is_progress>(I);
}

void setup_meter(Iface& I) {
    define_number<&is_meter>(I, "min", [](const Element& el) { return number_attribute(el, "min", 0); });
    define_number<&is_meter>(I, "max", [](const Element& el) {
        return std::max(number_attribute(el, "max", 1), number_attribute(el, "min", 0));
    });
    define_number<&is_meter>(I, "value", [](const Element& el) {
        const double min = number_attribute(el, "min", 0);
        const double max = std::max(number_attribute(el, "max", 1), min);
        return std::min(std::max(number_attribute(el, "value", 0), min), max);
    });
    define_number<&is_meter>(I, "low", [](const Element& el) {
        const double min = number_attribute(el, "min", 0);
        const double max = std::max(number_attribute(el, "max", 1), min);
        return std::min(std::max(number_attribute(el, "low", min), min), max);
    });
    define_number<&is_meter>(I, "high", [](const Element& el) {
        const double min = number_attribute(el, "min", 0);
        const double max = std::max(number_attribute(el, "max", 1), min);
        return std::min(std::max(number_attribute(el, "high", max), min), max);
    });
    define_number<&is_meter>(I, "optimum", [](const Element& el) {
        const double min = number_attribute(el, "min", 0);
        const double max = std::max(number_attribute(el, "max", 1), min);
        return std::min(std::max(number_attribute(el, "optimum", (min + max) / 2), min), max);
    });
    define_labels<&is_meter>(I);
}

// --- form ----------------------------------------------------------------------------

void setup_form(Iface& I) {
    ElementMembers<&is_form> M(I);
    reflect_attributes(I, "s:name s:target s:acceptCharset:accept-charset b:noValidate:novalidate s:rel");
    M.getter("elements", [](Realm& r, Element& el) {
        auto root = el.shared_from_this();
        return cached(wrap(r, el), "elements", [&]() {
            JsValue list = make_live_list(r, "HTMLFormControlsCollection", root, [](Node& node, NodeVector& out) {
                out = to_nodes(forms::form_controls(static_cast<Element&>(node)));
            });
            Realm* realm = &r;
            std::weak_ptr<Node> weak = root;
            set_list_named_getter(list, [realm, weak](const NodeVector& items, const std::string& name, JsValue& out) {
                auto form = weak.lock();
                return form && realm->alive && named_controls(*realm, form, items, name, out);
            });
            return list;
        });
    });
    M.getter("length", [](Realm&, Element& el) { return num(static_cast<double>(forms::form_controls(el).size())); });
    M.keyword("method", {"get", "post", "dialog"}, "get", "get");
    M.keyword("enctype", {"application/x-www-form-urlencoded", "multipart/form-data", "text/plain"},
              "application/x-www-form-urlencoded", "application/x-www-form-urlencoded");
    M.keyword("encoding", {"application/x-www-form-urlencoded", "multipart/form-data", "text/plain"},
              "application/x-www-form-urlencoded", "application/x-www-form-urlencoded", "enctype");
    M.keyword("autocomplete", {"on", "off"}, "on", "on");
    M.accessor("action",
        [](Realm& r, Element& el) {
            return str(action_url(r, el, "action"));
        },
        [](Realm&, Element& el, const JsValue& value) { el.set_attribute("action", to_str(value)); });
    M.token_list("relList", "rel", {"noreferrer", "noopener", "opener"});
    M.method("submit", 0, [](Realm& r, Element& el, Args) {
        submit_form(r, el, nullptr, false);
        return undefined();
    });
    M.method("requestSubmit", 0, [](Realm& r, Element& el, Args args) {
        Element* submitter = nullptr;
        if (!js_arg(args, 0).is_nullish()) {
            Node* node = node_ptr(args[0]);
            if (!node || !node->is_element() || !forms::is_submit_button(static_cast<Element&>(*node))) {
                js_throw_type_error("Failed to execute 'requestSubmit' on 'HTMLFormElement': the specified element is not a submit button.");
            }
            submitter = static_cast<Element*>(node);
            if (forms::form_owner(*submitter).get() != &el) {
                throw_dom(r, "NotFoundError", "The specified element is not owned by this form element.");
            }
        }
        submit_form(r, el, submitter, true);
        return undefined();
    });
    M.method("reset", 0, [](Realm& r, Element& el, Args) {
        reset_form(r, el);
        return undefined();
    });
    auto check = [](Realm& r, Element& el, Args) {
        bool all_valid = true;
        for (const auto& control : forms::invalid_controls(el)) {
            all_valid = false;
            fire(r, *control, "invalid", false, true);
        }
        return boolean(all_valid);
    };
    M.method("checkValidity", 0, check);
    M.method("reportValidity", 0, check);
}

void setup_radio_node_list(Iface& I) {
    I.accessor("value",
        [](Realm&) {
            NodeVector nodes;
            if (!list_nodes(JsValue::current_this(), nodes)) throw_illegal_invocation("RadioNodeList");
            for (const auto& node : nodes) {
                auto& element = static_cast<Element&>(*node);
                if (is_input(element) && forms::input_type(element) == "radio" && forms::checked(element)) {
                    return str(forms::value(element));
                }
            }
            return str("");
        },
        [](Realm&, const JsValue& value) {
            NodeVector nodes;
            if (!list_nodes(JsValue::current_this(), nodes)) throw_illegal_invocation("RadioNodeList");
            const std::string wanted = to_str(value);
            for (const auto& node : nodes) {
                auto& element = static_cast<Element&>(*node);
                if (is_input(element) && forms::input_type(element) == "radio" && forms::value(element) == wanted) {
                    forms::set_checked(element, true);
                    return;
                }
            }
        });
}

void setup_file_list(Iface& I) {
    I.getter("length", [](Realm&) { return num(0); });
    I.method("item", 1, [](Realm&, Args) { return null(); });
}

} // namespace

void register_form_interfaces() {
    forms::pattern_matcher = &pattern_matches;
    const auto element_ctor = &construct_custom_element;
    register_interface({"HTMLFormElement", "HTMLElement", setup_form, element_ctor, 0, nullptr});
    register_interface({"HTMLInputElement", "HTMLElement", setup_input, element_ctor, 0, nullptr});
    register_interface({"HTMLTextAreaElement", "HTMLElement", setup_textarea, element_ctor, 0, nullptr});
    register_interface({"HTMLSelectElement", "HTMLElement", setup_select, element_ctor, 0, nullptr});
    register_interface({"HTMLOptionElement", "HTMLElement", setup_option, element_ctor, 0, nullptr});
    register_interface({"HTMLOptGroupElement", "HTMLElement", setup_optgroup, element_ctor, 0, nullptr});
    register_interface({"HTMLButtonElement", "HTMLElement", setup_button, element_ctor, 0, nullptr});
    register_interface({"HTMLLabelElement", "HTMLElement", setup_label, element_ctor, 0, nullptr});
    register_interface({"HTMLFieldSetElement", "HTMLElement", setup_fieldset, element_ctor, 0, nullptr});
    register_interface({"HTMLLegendElement", "HTMLElement", setup_legend, element_ctor, 0, nullptr});
    register_interface({"HTMLOutputElement", "HTMLElement", setup_output, element_ctor, 0, nullptr});
    register_interface({"HTMLDataListElement", "HTMLElement", setup_datalist, element_ctor, 0, nullptr});
    register_interface({"HTMLProgressElement", "HTMLElement", setup_progress, element_ctor, 0, nullptr});
    register_interface({"HTMLMeterElement", "HTMLElement", setup_meter, element_ctor, 0, nullptr});
    register_interface({"HTMLOptionsCollection", "HTMLCollection", setup_options_collection, nullptr, 0, nullptr});
    register_interface({"HTMLFormControlsCollection", "HTMLCollection", nullptr, nullptr, 0, nullptr});
    register_interface({"RadioNodeList", "NodeList", setup_radio_node_list, nullptr, 0, nullptr});
    register_interface({"ValidityState", nullptr, setup_validity_state, nullptr, 0, nullptr});
    register_interface({"FormData", nullptr, setup_form_data, construct_form_data, 0, nullptr});
    register_interface({"FileList", nullptr, setup_file_list, nullptr, 0, nullptr});
}

} // namespace dom
} // namespace litebird
