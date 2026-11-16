#pragma once

// The state of form controls, apart from their attributes.
//
// HTML keeps two things for every control. The attribute (`value="ann"`,
// `checked`, `selected`) is the default: what the page author wrote, and what
// reset() goes back to. The state is what the control holds now: what was
// typed, whether the box is ticked. They start out equal and part ways the
// first time the user or a script changes the control, and from then on
// changing the attribute no longer changes the state (the control is "dirty").
//
// The engine used to have only the attribute: `input.value = "x"` rewrote
// `value="x"`, `checkbox.checked = false` removed the attribute, and so
// reset() had nothing to go back to, `defaultValue` was always the current
// value, and innerHTML showed what was typed.
//
// Everything here is plain C++ on the tree, with no script engine in it: the
// selector matcher needs it (`:checked`, `:disabled`, `:invalid`) and so will
// layout. The script bindings (DomFormBindings.cpp) put the HTML interfaces
// on top.

#include "litebird/dom/Element.hpp"

#include <memory>
#include <string>
#include <vector>

namespace litebird {

/// What a control holds beyond its attributes. Allocated on first need.
struct FormControlState {
    // input, textarea: the value once it is dirty.
    std::string value;
    bool value_dirty{false};
    // checkbox, radio. `has_checked` says `checked` is the answer; without it
    // the attribute is.
    bool checked{false};
    bool has_checked{false};
    bool checked_dirty{false};
    // option: likewise.
    bool selected{false};
    bool has_selected{false};
    bool selected_dirty{false};
    // select: a script chose "no option" (selectedIndex = -1). Holds until
    // the list of options changes; `none_options` is the list it was said of.
    bool select_none{false};
    std::vector<const Element*> none_options;
    std::string custom_validity;
    // Text selection of an input or a textarea.
    bool selection_set{false};
    unsigned selection_start{0};
    unsigned selection_end{0};
    std::string selection_direction{"none"};
};

namespace forms {

// --- What kind of control -------------------------------------------------------
/// button, fieldset, input, object, output, select, textarea: what
/// form.elements lists (an image button excepted, which the caller checks).
bool is_listed(const Element& element);
/// button, input, select, textarea: what can contribute to a submission.
bool is_submittable(const Element& element);
/// What a <label> can label.
bool is_labelable(const Element& element);
/// An input's type: one of the known keywords in lower case, "text" for
/// anything else or nothing.
std::string input_type(const Element& input);
/// A button element's type: "submit", "reset" or "button".
std::string button_type(const Element& button);
/// True for a button or input that submits its form when activated.
bool is_submit_button(const Element& element);
/// Input types whose value is a line of text with a selection.
bool has_text_selection(const Element& element);

// --- Who belongs to whom --------------------------------------------------------
/// The form a control belongs to: the one its `form` attribute names, else
/// the nearest <form> above it. Null when there is none.
std::shared_ptr<Element> form_owner(const Element& control);
/// The listed controls of a form, in tree order.
std::vector<std::shared_ptr<Element>> form_controls(const Element& form);
/// The listed controls inside a fieldset.
std::vector<std::shared_ptr<Element>> fieldset_controls(const Element& fieldset);
/// The control a label labels: the one its `for` names, else the first
/// labelable element inside it.
std::shared_ptr<Element> label_control(const Element& label);
/// The labels of a control, in tree order.
std::vector<std::shared_ptr<Element>> labels_of(const Element& control);
/// The radio buttons that share a group with `radio`, itself included.
std::vector<std::shared_ptr<Element>> radio_group(const Element& radio);

// --- Value ----------------------------------------------------------------------
/// The value of an input, textarea, select, button, option or output, as the
/// `value` property gives it.
std::string value(const Element& control);
/// Sets it. False when the control refuses (a file input given a file name).
bool set_value(Element& control, const std::string& value);
/// The default: the attribute, or a textarea's text.
std::string default_value(const Element& control);
/// A textarea's value before line breaks are normalised.
std::string textarea_raw_value(const Element& textarea);

// --- Checkboxes and radio buttons ----------------------------------------------
bool checked(const Element& input);
/// Sets the state and marks it dirty; a radio button unchecks its group.
void set_checked(Element& input, bool value, bool make_dirty = true);

// --- select and option ----------------------------------------------------------
std::vector<std::shared_ptr<Element>> select_options(const Element& select);
std::vector<std::shared_ptr<Element>> selected_options(const Element& select);
int selected_index(const Element& select);
void set_selected_index(Element& select, int index);
bool option_selected(const Element& option);
void set_option_selected(Element& option, bool value);
std::shared_ptr<Element> option_select(const Element& option);
std::string option_text(const Element& option);
std::string option_value(const Element& option);
std::string option_label(const Element& option);
bool is_multiple_select(const Element& select);

// --- States the selector matcher asks about ---------------------------------------
bool matches_checked(const Element& element);
bool is_disabled(const Element& element);
bool can_be_disabled(const Element& element);
bool is_required(const Element& element);
bool can_be_required(const Element& element);
bool is_read_write(const Element& element);
bool placeholder_shown(const Element& element);
/// 0: not something that is valid or invalid. 1: valid. 2: invalid.
int validity_for_selector(const Element& element);

// --- Validation -----------------------------------------------------------------
struct Validity {
    bool value_missing{false};
    bool type_mismatch{false};
    bool pattern_mismatch{false};
    bool too_long{false};
    bool too_short{false};
    bool range_underflow{false};
    bool range_overflow{false};
    bool step_mismatch{false};
    bool bad_input{false};
    bool custom_error{false};
    bool valid() const {
        return !(value_missing || type_mismatch || pattern_mismatch || too_long || too_short ||
                 range_underflow || range_overflow || step_mismatch || bad_input || custom_error);
    }
};
/// Whether the control takes part in validation at all.
bool will_validate(const Element& control);
Validity validity(const Element& control);
std::string validation_message(const Element& control);
/// The controls of `form` (or inside `fieldset`) that fail validation.
std::vector<std::shared_ptr<Element>> invalid_controls(const Element& form_or_fieldset);

// --- Numbers & Date/Time ----------------------------------------------------------
/// HTML's "valid floating-point number".
bool parse_number(const std::string& text, double& out);
std::string number_to_string(double value);
bool has_value_as_number(const std::string& type);
bool has_value_as_date(const std::string& type);
double value_as_number(const Element& input);
bool set_value_as_number(Element& input, double value);
bool value_as_utc_ms(const Element& input, double& out);
bool set_value_as_utc_ms(Element& input, double ms);
/// stepUp(n) / stepDown(n) with n negative. False when the type has no steps.
bool step(Element& input, int count);

// --- Reset and submission ---------------------------------------------------------
void reset_control(Element& control);
struct Entry {
    std::string name;
    std::string value;
    bool is_file{false};
};
/// The name/value pairs a form would submit, `submitter` being the button
/// that was used (null for none).
std::vector<Entry> entry_list(const Element& form, const Element* submitter);

/// Called by Element when an attribute of a control changes, so that state
/// which still follows the attribute does.
void attribute_changed(Element& element, const std::string& name, bool had_old, const std::string& old_value);

/// Matches `value` against a `pattern` attribute. Set by the script bindings,
/// which have a regular expression engine; without it nothing mismatches.
extern bool (*pattern_matcher)(const std::string& pattern, const std::string& value);

} // namespace forms
} // namespace litebird
