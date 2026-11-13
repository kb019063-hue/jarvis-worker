#include "litebird/html/HtmlTreeBuilder.hpp"

#include "litebird/dom/Comment.hpp"
#include "litebird/dom/Text.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace litebird {

namespace {

const std::unordered_set<std::string>& special_html_tags() {
    static const std::unordered_set<std::string> tags = {
        "address", "applet", "area", "article", "aside", "base", "basefont",
        "bgsound", "blockquote", "body", "br", "button", "caption", "center",
        "col", "colgroup", "dd", "details", "dir", "div", "dl", "dt", "embed",
        "fieldset", "figcaption", "figure", "footer", "form", "frame",
        "frameset", "h1", "h2", "h3", "h4", "h5", "h6", "head", "header",
        "hgroup", "hr", "html", "iframe", "img", "input", "keygen", "li",
        "link", "listing", "main", "marquee", "menu", "meta", "nav", "noembed",
        "noframes", "noscript", "object", "ol", "p", "param", "plaintext",
        "pre", "script", "search", "section", "select", "source", "style",
        "summary", "table", "tbody", "td", "template", "textarea", "tfoot",
        "th", "thead", "title", "tr", "track", "ul", "wbr", "xmp"};
    return tags;
}

const std::unordered_set<std::string>& formatting_tags() {
    static const std::unordered_set<std::string> tags = {
        "a", "b", "big", "code", "em", "font", "i", "nobr", "s", "small",
        "strike", "strong", "tt", "u"};
    return tags;
}

const std::unordered_set<std::string>& implied_end_tags() {
    static const std::unordered_set<std::string> tags = {
        "dd", "dt", "li", "optgroup", "option", "p", "rb", "rp", "rt", "rtc"};
    return tags;
}

bool is_mathml_text_integration_point_tag(const std::string& tag) {
    return tag == "mi" || tag == "mo" || tag == "mn" || tag == "ms" || tag == "mtext";
}

bool is_svg_html_integration_point_tag(const std::string& tag) {
    return tag == "foreignObject" || tag == "desc" || tag == "title";
}

/// The elements of the "special" category that are not HTML elements: MathML
/// mi, mo, mn, ms, mtext and annotation-xml, and SVG foreignObject, desc and
/// title. The same nine are the SVG and MathML elements that bound a scope.
bool is_special_foreign_element(const Element& element) {
    const std::string& name = element.tag_name();
    if (element.element_namespace() == ElementNamespace::MathML) {
        return is_mathml_text_integration_point_tag(name) || name == "annotation-xml";
    }
    if (element.element_namespace() == ElementNamespace::SVG) return is_svg_html_integration_point_tag(name);
    return false;
}

/// SVG element names that need case adjustment (WHATWG HTML, "adjust SVG
/// attributes/tag names"). Only the names the parser can actually produce are
/// listed; anything else keeps its lower-cased spelling.
const char* adjust_svg_tag_name(const std::string& lower) {
    static const std::unordered_map<std::string, const char*> names = {
        {"altglyph", "altGlyph"}, {"altglyphdef", "altGlyphDef"},
        {"altglyphitem", "altGlyphItem"}, {"animatecolor", "animateColor"},
        {"animatemotion", "animateMotion"}, {"animatetransform", "animateTransform"},
        {"clippath", "clipPath"}, {"feblend", "feBlend"},
        {"fecolormatrix", "feColorMatrix"}, {"fecomponenttransfer", "feComponentTransfer"},
        {"fecomposite", "feComposite"}, {"feconvolvematrix", "feConvolveMatrix"},
        {"fediffuselighting", "feDiffuseLighting"}, {"fedisplacementmap", "feDisplacementMap"},
        {"fedistantlight", "feDistantLight"}, {"fedropshadow", "feDropShadow"},
        {"feflood", "feFlood"}, {"fefunca", "feFuncA"}, {"fefuncb", "feFuncB"},
        {"fefuncg", "feFuncG"}, {"fefuncr", "feFuncR"},
        {"fegaussianblur", "feGaussianBlur"}, {"feimage", "feImage"},
        {"femerge", "feMerge"}, {"femergenode", "feMergeNode"},
        {"femorphology", "feMorphology"}, {"feoffset", "feOffset"},
        {"fepointlight", "fePointLight"}, {"fespecularlighting", "feSpecularLighting"},
        {"fespotlight", "feSpotLight"}, {"fetile", "feTile"},
        {"feturbulence", "feTurbulence"}, {"foreignobject", "foreignObject"},
        {"glyphref", "glyphRef"}, {"lineargradient", "linearGradient"},
        {"radialgradient", "radialGradient"}, {"textpath", "textPath"}};
    auto it = names.find(lower);
    return it == names.end() ? nullptr : it->second;
}

/// Attributes that must be written with an explicit namespace prefix when
/// dumped (the tree dump uses "prefix localname" spelling).
std::string adjust_foreign_attribute_name(const std::string& name, ElementNamespace ns) {
    static const std::unordered_map<std::string, const char*> xlink = {
        {"xlink:actuate", "xlink actuate"}, {"xlink:arcrole", "xlink arcrole"},
        {"xlink:href", "xlink href"}, {"xlink:role", "xlink role"},
        {"xlink:show", "xlink show"}, {"xlink:title", "xlink title"},
        {"xlink:type", "xlink type"}};
    static const std::unordered_map<std::string, const char*> xml = {
        {"xml:lang", "xml lang"}, {"xml:space", "xml space"}};

    if (name == "xmlns") return "xmlns xmlns";
    if (name.rfind("xmlns:", 0) == 0) return "xmlns " + name.substr(6);
    auto x = xlink.find(name);
    if (x != xlink.end()) return x->second;
    auto m = xml.find(name);
    if (m != xml.end()) return m->second;
    if (ns == ElementNamespace::MathML && name == "definitionurl") return "definitionURL";

    // "Adjust SVG attributes": the tokenizer lowercases attribute names, but
    // these SVG attributes are spelled with camel case in the DOM.
    if (ns == ElementNamespace::SVG) {
        static const std::unordered_map<std::string, const char*> svg_attributes = {
            {"attributename", "attributeName"}, {"attributetype", "attributeType"},
            {"basefrequency", "baseFrequency"}, {"baseprofile", "baseProfile"},
            {"calcmode", "calcMode"}, {"clippathunits", "clipPathUnits"},
            {"diffuseconstant", "diffuseConstant"}, {"edgemode", "edgeMode"},
            {"filterunits", "filterUnits"}, {"glyphref", "glyphRef"},
            {"gradienttransform", "gradientTransform"}, {"gradientunits", "gradientUnits"},
            {"kernelmatrix", "kernelMatrix"}, {"kernelunitlength", "kernelUnitLength"},
            {"keypoints", "keyPoints"}, {"keysplines", "keySplines"},
            {"keytimes", "keyTimes"}, {"lengthadjust", "lengthAdjust"},
            {"limitingconeangle", "limitingConeAngle"}, {"markerheight", "markerHeight"},
            {"markerunits", "markerUnits"}, {"markerwidth", "markerWidth"},
            {"maskcontentunits", "maskContentUnits"}, {"maskunits", "maskUnits"},
            {"numoctaves", "numOctaves"}, {"pathlength", "pathLength"},
            {"patterncontentunits", "patternContentUnits"},
            {"patterntransform", "patternTransform"}, {"patternunits", "patternUnits"},
            {"pointsatx", "pointsAtX"}, {"pointsaty", "pointsAtY"}, {"pointsatz", "pointsAtZ"},
            {"preservealpha", "preserveAlpha"}, {"preserveaspectratio", "preserveAspectRatio"},
            {"primitiveunits", "primitiveUnits"}, {"refx", "refX"}, {"refy", "refY"},
            {"repeatcount", "repeatCount"}, {"repeatdur", "repeatDur"},
            {"requiredextensions", "requiredExtensions"},
            {"requiredfeatures", "requiredFeatures"},
            {"specularconstant", "specularConstant"},
            {"specularexponent", "specularExponent"}, {"spreadmethod", "spreadMethod"},
            {"startoffset", "startOffset"}, {"stddeviation", "stdDeviation"},
            {"stitchtiles", "stitchTiles"}, {"surfacescale", "surfaceScale"},
            {"systemlanguage", "systemLanguage"}, {"tablevalues", "tableValues"},
            {"targetx", "targetX"}, {"targety", "targetY"}, {"textlength", "textLength"},
            {"viewbox", "viewBox"}, {"viewtarget", "viewTarget"},
            {"xchannelselector", "xChannelSelector"},
            {"ychannelselector", "yChannelSelector"}, {"zoomandpan", "zoomAndPan"},
        };
        auto svg = svg_attributes.find(name);
        if (svg != svg_attributes.end()) return svg->second;
    }
    return name;
}

bool is_whitespace_only(std::string_view text) {
    return std::all_of(text.begin(), text.end(), [](char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r';
    });
}

/// Tags that break out of foreign content back into HTML.
bool is_foreign_breakout_tag(const std::string& tag) {
    static const std::unordered_set<std::string> tags = {
        "b", "big", "blockquote", "body", "br", "center", "code", "dd", "div",
        "dl", "dt", "em", "embed", "h1", "h2", "h3", "h4", "h5", "h6", "head",
        "hr", "i", "img", "li", "listing", "menu", "meta", "nobr", "ol", "p",
        "pre", "ruby", "s", "small", "span", "strong", "strike", "sub", "sup",
        "table", "tt", "u", "ul", "var"};
    return tags.find(tag) != tags.end();
}

} // namespace

// ---------------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------------

std::shared_ptr<Document> HtmlTreeBuilder::parse(std::string_view html) {
    m_document = std::make_shared<Document>();
    HtmlTokenizer tokenizer(html);
    m_tokenizer = &tokenizer;

    while (true) {
        allow_cdata_in_foreign_content();
        HtmlToken token = tokenizer.next_token();
        if (token.type == HtmlTokenType::Character && token.data.empty()) continue;
        process_token(token);
        if (token.type == HtmlTokenType::EndOfFile) break;
    }

    // Mirror the document's title so the shell and tests can find it.
    for (const auto& el : m_document->get_elements_by_tag_name("title")) {
        m_document->set_title(el->text_content());
        break;
    }

    m_tokenizer = nullptr;
    return m_document;
}

std::shared_ptr<Node> HtmlTreeBuilder::parse_fragment(std::string_view html,
                                                      const std::string& context_tag,
                                                      ElementNamespace context_namespace) {
    m_fragment_case = true;
    m_fragment_document = std::make_shared<Document>();
    m_document = m_fragment_document;

    m_context_element = std::make_shared<Element>(context_tag, context_namespace);

    auto root = std::make_shared<Element>("html");
    m_document->append_child(root);
    push_element(root);

    // The content of a template starts in the "in template" mode, which
    // replaces the current template insertion mode at the first start tag:
    // there has to be one. Without it that was the last element of an empty
    // vector, and any fragment parsed for a template ended the process.
    if (html_name(m_context_element) == "template") m_template_modes.push_back(Mode::InTemplate);

    reset_insertion_mode();

    // Context-sensitive tokenizer state. A foreign (SVG/MathML) context
    // element never switches the tokenizer: an <svg title> context parses its
    // children as markup, unlike an HTML <title>.
    const std::string& tag =
        (m_context_element->element_namespace() == ElementNamespace::HTML)
            ? m_context_element->tag_name()
            : std::string("\x01");
    if (tag == "title" || tag == "textarea") {
        m_tokenizer_pending = ContentModel::Rcdata;
    } else if (tag == "style" || tag == "xmp" || tag == "iframe" || tag == "noembed" || tag == "noframes") {
        m_tokenizer_pending = ContentModel::Rawtext;
    } else if (tag == "script") {
        m_tokenizer_pending = ContentModel::ScriptData;
    } else if (tag == "noscript" && m_scripting) {
        m_tokenizer_pending = ContentModel::Rawtext;
    } else if (tag == "plaintext") {
        m_tokenizer_pending = ContentModel::Plaintext;
    }
    // No start tag has been seen, so no end tag is the "appropriate" one
    // that ends this text: a </script> in the fragment of a script is text.
    m_tokenizer_pending_tag.clear();

    if (tag == "form") {
        m_form_element = m_context_element;
    }

    HtmlTokenizer tokenizer(html);
    m_tokenizer = &tokenizer;
    if (m_tokenizer_pending != ContentModel::Data) {
        tokenizer.set_content_model(m_tokenizer_pending, m_tokenizer_pending_tag);
    }

    while (true) {
        allow_cdata_in_foreign_content();
        HtmlToken token = tokenizer.next_token();
        if (token.type == HtmlTokenType::Character && token.data.empty()) continue;
        process_token(token);
        if (token.type == HtmlTokenType::EndOfFile) break;
    }
    m_tokenizer = nullptr;

    // All of them at once: removing them from the front one at a time moves
    // every later sibling each time.
    auto fragment = std::make_shared<Node>(NodeType::DocumentFragment);
    for (const auto& child : root->take_children()) fragment->append_child(child);
    return fragment;
}

// ---------------------------------------------------------------------------
// Stack of open elements
// ---------------------------------------------------------------------------

std::shared_ptr<Element> HtmlTreeBuilder::current_node() const {
    return m_open_elements.empty() ? nullptr : m_open_elements.back();
}

std::shared_ptr<Element> HtmlTreeBuilder::adjusted_current_node() const {
    if (m_fragment_case && m_open_elements.size() == 1) return m_context_element;
    return current_node();
}

// "<![CDATA[" opens a CDATA section where the adjusted current node is an SVG
// or MathML element, and a comment anywhere else. Asked before each token.
// The adjusted node, so that the top level of a fragment parsed for an <svg>
// counts as being inside it.
void HtmlTreeBuilder::allow_cdata_in_foreign_content() {
    if (!m_tokenizer) return;
    const auto node = adjusted_current_node();
    m_tokenizer->set_allow_cdata(node && node->element_namespace() != ElementNamespace::HTML);
}

void HtmlTreeBuilder::index_open_element(std::size_t index, const std::shared_ptr<Element>& element) {
    if (!element) return;
    m_open_positions[element.get()] = index;
    const std::string& name = element->tag_name();
    if (element->element_namespace() == ElementNamespace::HTML) {
        m_open_html_tag_indices[name].push_back(index);
        if (name == "applet" || name == "caption" || name == "html" || name == "table" ||
            name == "td" || name == "th" || name == "marquee" || name == "object" ||
            name == "template") {
            m_scope_boundary_indices[0].push_back(index);
        }
        if (name == "ol" || name == "ul") m_scope_boundary_indices[1].push_back(index);
        if (name == "button") m_scope_boundary_indices[2].push_back(index);
        if (special_html_tags().count(name)) {
            m_scope_boundary_indices[3].push_back(index);
            if (name != "address" && name != "div" && name != "p") m_scope_boundary_indices[4].push_back(index);
        }
        return;
    }
    // Of the SVG and MathML elements nine are special, and the same nine
    // bound a scope. Every other one is an ordinary element whatever it is
    // called: <svg><td> makes an SVG td, which is not a cell, not special and
    // not the end of any scope.
    if (is_special_foreign_element(*element)) {
        m_scope_boundary_indices[0].push_back(index);
        m_scope_boundary_indices[3].push_back(index);
        m_scope_boundary_indices[4].push_back(index);
    }
}

// Undoes index_open_element for the entry at `index`, which must be the
// highest one still indexed: every list is in stack order, so what belongs to
// that entry is at the end of each.
void HtmlTreeBuilder::unindex_open_element(std::size_t index, const std::shared_ptr<Element>& element) {
    if (!element) return;
    m_open_positions.erase(element.get());
    if (element->element_namespace() == ElementNamespace::HTML) {
        auto it = m_open_html_tag_indices.find(element->tag_name());
        if (it != m_open_html_tag_indices.end() && !it->second.empty() && it->second.back() == index) {
            it->second.pop_back();
            if (it->second.empty()) m_open_html_tag_indices.erase(it);
        }
    }
    for (auto& boundaries : m_scope_boundary_indices) {
        if (!boundaries.empty() && boundaries.back() == index) boundaries.pop_back();
    }
}

void HtmlTreeBuilder::unindex_open_elements_from(std::size_t index) {
    for (std::size_t i = m_open_elements.size(); i-- > index;) unindex_open_element(i, m_open_elements[i]);
}

void HtmlTreeBuilder::index_open_elements_from(std::size_t index) {
    for (std::size_t i = index; i < m_open_elements.size(); ++i) index_open_element(i, m_open_elements[i]);
}

int HtmlTreeBuilder::open_element_position(const std::shared_ptr<Element>& element) const {
    auto found = m_open_positions.find(element.get());
    return found == m_open_positions.end() ? -1 : static_cast<int>(found->second);
}

void HtmlTreeBuilder::push_element(const std::shared_ptr<Element>& element) {
    m_open_elements.push_back(element);
    m_open_depths.push_back(0);
    index_open_element(m_open_elements.size() - 1, element);
}

void HtmlTreeBuilder::pop_current_node() {
    if (m_open_elements.empty()) return;
    unindex_open_element(m_open_elements.size() - 1, m_open_elements.back());
    m_open_elements.pop_back();
    m_open_depths.pop_back();
}

void HtmlTreeBuilder::resize_open_elements(std::size_t size) {
    while (m_open_elements.size() > size) pop_current_node();
}

void HtmlTreeBuilder::erase_open_element(std::size_t index, bool descendants_moved) {
    if (index >= m_open_elements.size()) return;
    if (!descendants_moved && index + 1 < m_open_elements.size()) ++m_depth_beyond_stack;
    unindex_open_elements_from(index);
    m_open_elements.erase(m_open_elements.begin() + static_cast<std::ptrdiff_t>(index));
    m_open_depths.erase(m_open_depths.begin() + static_cast<std::ptrdiff_t>(index));
    index_open_elements_from(index);
}

void HtmlTreeBuilder::insert_open_element(std::size_t index, const std::shared_ptr<Element>& element) {
    index = std::min(index, m_open_elements.size());
    unindex_open_elements_from(index);
    m_open_elements.insert(m_open_elements.begin() + static_cast<std::ptrdiff_t>(index), element);
    m_open_depths.insert(m_open_depths.begin() + static_cast<std::ptrdiff_t>(index), 0);
    index_open_elements_from(index);
}

void HtmlTreeBuilder::replace_open_element(std::size_t index, const std::shared_ptr<Element>& element) {
    if (index >= m_open_elements.size()) return;
    unindex_open_elements_from(index);
    m_open_elements[index] = element;
    m_open_depths[index] = 0;
    index_open_elements_from(index);
}

namespace {
// Tree-construction stack queries are scoped to HTML elements, not foreign
// elements that happen to share a local name.
bool is_html_element(const std::shared_ptr<Element>& element) {
    return element && element->element_namespace() == ElementNamespace::HTML;
}
} // namespace

const std::string& HtmlTreeBuilder::html_name(const Element& element) {
    static const std::string none;
    return element.element_namespace() == ElementNamespace::HTML ? element.tag_name() : none;
}

const std::string& HtmlTreeBuilder::html_name(const std::shared_ptr<Element>& element) {
    static const std::string none;
    return element ? html_name(*element) : none;
}

// "Pop elements from the stack of open elements until an X element has been
// popped." The standard only says this where an X is known to be open. Here
// that has not always been true (rules for <select> content that close the
// <select> without leaving its insertion mode), and popping until an element
// that is not there empties the stack: <html> and <body> are gone, the rest of
// the page is parsed with nothing open, and the adoption agency algorithm then
// read the element "above" the first one. So: no such element, nothing popped.
void HtmlTreeBuilder::pop_until_tag(const std::string& tag) {
    if (element_index_in_stack(tag) < 0) return;
    while (!m_open_elements.empty()) {
        auto element = m_open_elements.back();
        pop_current_node();
        if (is_html_element(element) && element->tag_name() == tag) return;
    }
}

void HtmlTreeBuilder::pop_until_one_of(const std::vector<std::string>& tags) {
    bool open = false;
    for (const auto& tag : tags) open = open || element_index_in_stack(tag) >= 0;
    if (!open) return;
    while (!m_open_elements.empty()) {
        auto element = m_open_elements.back();
        pop_current_node();
        if (is_html_element(element) &&
            std::find(tags.begin(), tags.end(), element->tag_name()) != tags.end()) return;
    }
}

int HtmlTreeBuilder::element_index_in_stack(const std::string& tag) const {
    auto found = m_open_html_tag_indices.find(tag);
    if (found == m_open_html_tag_indices.end() || found->second.empty()) return -1;
    return static_cast<int>(found->second.back());
}

// The standard answers the next two questions by walking down the stack until
// a special element stops the walk. Where it stops is the highest special
// element, which the index knows, and the stack can be 200,000 elements long
// with none of them special (<span>) or with all of them skipped (<div>, for
// a list item).
//
// Both are about HTML elements: "an li element" is an HTML li, and the names
// are looked up among the open HTML elements only. Which elements are special
// depends on the namespace as well (see index_open_element).
std::string HtmlTreeBuilder::open_list_item_closed_by(const std::string& name) const {
    const auto& stops = m_scope_boundary_indices[4];
    if (stops.empty()) return {};
    // li, dd and dt are special themselves, so the walk ends at the highest
    // special element that is not address, div or p: on one of them, or on
    // something that hides them.
    const std::string& open = html_name(m_open_elements[stops.back()]);
    const bool closes = name == "li" ? open == "li" : (open == "dd" || open == "dt");
    return closes ? open : std::string();
}

int HtmlTreeBuilder::open_element_closed_by_end_tag(const std::string& name) const {
    const int target = element_index_in_stack(name);
    if (target < 0) return -1;
    const auto& special = m_scope_boundary_indices[3];
    // The name is tested before the category at each element, so an element
    // that is itself special is still closed by its own tag.
    if (!special.empty() && static_cast<int>(special.back()) > target) return -1;
    return target;
}

// "Any other end tag" of the "in body" rules.
void HtmlTreeBuilder::any_other_end_tag(const std::string& name) {
    const int closed = open_element_closed_by_end_tag(name);
    if (closed < 0) return;
    generate_implied_end_tags(name);
    resize_open_elements(static_cast<std::size_t>(closed));
}

bool HtmlTreeBuilder::has_element_in_scope(const std::string& tag, Scope scope) const {
    return has_element_in_scope_where({tag}, scope);
}

bool HtmlTreeBuilder::has_element_in_scope_where(const std::vector<std::string>& tags, Scope scope) const {
    if (scope == Scope::Select) return has_element_in_select_scope(tags.empty() ? "" : tags.front());
    if (tags.empty()) return false;

    std::ptrdiff_t match_index = -1;
    for (const auto& tag : tags) {
        auto it = m_open_html_tag_indices.find(tag);
        if (it != m_open_html_tag_indices.end() && !it->second.empty()) {
            match_index = std::max(match_index, static_cast<std::ptrdiff_t>(it->second.back()));
        }
    }
    if (match_index < 0) return false;

    std::ptrdiff_t boundary_index = -1;
    const auto& default_boundaries = m_scope_boundary_indices[0];
    if (!default_boundaries.empty()) {
        boundary_index = static_cast<std::ptrdiff_t>(default_boundaries.back());
    }
    if (scope == Scope::ListItem && !m_scope_boundary_indices[1].empty()) {
        boundary_index = std::max(boundary_index,
            static_cast<std::ptrdiff_t>(m_scope_boundary_indices[1].back()));
    } else if (scope == Scope::Button && !m_scope_boundary_indices[2].empty()) {
        boundary_index = std::max(boundary_index,
            static_cast<std::ptrdiff_t>(m_scope_boundary_indices[2].back()));
    }

    // The spec tests for a target before testing the boundary at each stack
    // entry, so a target that is itself the boundary is in scope.
    return match_index >= boundary_index;
}

bool HtmlTreeBuilder::has_element_in_table_scope(const std::string& tag) const {
    // Going down the stack, the element is looked for before the boundary is
    // tested, so an element that is itself a boundary is in scope.
    const int target = element_index_in_stack(tag);
    if (target < 0) return false;
    const int boundary = std::max({element_index_in_stack("html"), element_index_in_stack("table"),
                                   element_index_in_stack("template")});
    return target >= boundary;
}

bool HtmlTreeBuilder::has_element_in_select_scope(const std::string& tag) const {
    for (int i = static_cast<int>(m_open_elements.size()) - 1; i >= 0; --i) {
        const auto& element = m_open_elements[static_cast<std::size_t>(i)];
        const std::string& name = html_name(element);
        if (name == tag) return true;
        if (!(name == "optgroup" || name == "option")) return false;
    }
    return false;
}

void HtmlTreeBuilder::generate_implied_end_tags(const std::string& except) {
    while (!m_open_elements.empty()) {
        // HTML elements only: an SVG <rt> has no implied end tag.
        const std::string& name = html_name(m_open_elements.back());
        if (implied_end_tags().count(name) && name != except) {
            pop_current_node();
        } else {
            break;
        }
    }
}

void HtmlTreeBuilder::generate_all_implied_end_tags() {
    generate_implied_end_tags();
}

void HtmlTreeBuilder::clear_stack_back_to_table_context() {
    while (!m_open_elements.empty()) {
        const std::string& name = html_name(m_open_elements.back());
        if (name == "table" || name == "template" || name == "html") return;
        pop_current_node();
    }
}

void HtmlTreeBuilder::clear_stack_back_to_table_body_context() {
    while (!m_open_elements.empty()) {
        const std::string& name = html_name(m_open_elements.back());
        if (name == "tbody" || name == "tfoot" || name == "thead" || name == "template" || name == "html") return;
        pop_current_node();
    }
}

void HtmlTreeBuilder::clear_stack_back_to_row_context() {
    while (!m_open_elements.empty()) {
        const std::string& name = html_name(m_open_elements.back());
        if (name == "tr" || name == "template" || name == "html") return;
        pop_current_node();
    }
}

// ---------------------------------------------------------------------------
// Insertion
// ---------------------------------------------------------------------------

HtmlTreeBuilder::InsertionPlace HtmlTreeBuilder::appropriate_place_for_inserting_node(
    std::shared_ptr<Element> override_target, bool force_foster) {
    auto target = override_target ? override_target : current_node();
    InsertionPlace place{target, nullptr};
    if (!target) {
        place.parent = m_document;
        return place;
    }

    // A template element's children live in its template contents fragment.
    if (target->element_namespace() == ElementNamespace::HTML && target->tag_name() == "template" &&
        target->template_contents()) {
        place.parent = target->template_contents();
        return place;
    }

    const std::string& target_name = target->tag_name();
    const bool target_is_table_ish = target->element_namespace() == ElementNamespace::HTML &&
                                    (target_name == "table" || target_name == "tbody" ||
                                     target_name == "tfoot" || target_name == "thead" ||
                                     target_name == "tr");

    const bool foster = (force_foster || m_foster_parenting) && target_is_table_ish;
    if (!foster) {
        return place;
    }

    // Foster parenting: insert before the nearest table, or into its parent.
    // Looked up in the index of open HTML elements: a walk down the stack
    // for each fostered node is one step per open element, and the stack is
    // as long as the markup is deep.
    const int last_template = element_index_in_stack("template");
    const int last_table = element_index_in_stack("table");

    if (last_template >= 0 && (last_table < 0 || last_template > last_table)) {
        place.parent = m_open_elements[static_cast<std::size_t>(last_template)]->template_contents();
        return place;
    }

    if (last_table < 0) {
        place.parent = m_open_elements.front();
        return place;
    }

    auto table = m_open_elements[static_cast<std::size_t>(last_table)];
    if (auto table_parent = table->parent()) {
        place.parent = table_parent;
        place.before = table;
        return place;
    }
    place.parent = m_open_elements[static_cast<std::size_t>(last_table - 1)];
    return place;
}

std::shared_ptr<Element> HtmlTreeBuilder::create_element_for_token(const HtmlToken& token, ElementNamespace ns) {
    std::string name = token.name;
    if (ns == ElementNamespace::SVG) {
        if (const char* adjusted = adjust_svg_tag_name(name)) name = adjusted;
    }
    auto element = std::make_shared<Element>(name, ns);
    for (const auto& [key, value] : token.attributes) {
        const std::string attr_name =
            (ns == ElementNamespace::HTML) ? key : adjust_foreign_attribute_name(key, ns);
        if (!element->has_attribute(attr_name)) element->set_attribute(attr_name, value);
    }
    if (ns == ElementNamespace::HTML && token.name == "template") {
        auto contents = std::make_shared<Node>(NodeType::DocumentFragment);
        m_template_of_contents[contents.get()] = element;
        element->set_template_contents(std::move(contents));
    }
    return element;
}

// The nesting limit.
//
// Markup can nest without end, and the parser used to follow it: 300,000
// <div> start tags made a tree 300,000 levels deep. Everything that looks at
// an element's ancestors then costs 300,000 steps per element (selector
// matching, the indentation of a dump), and everything recursive runs out of
// stack.
//
// No element is attached deeper than kMaxTreeDepth. One that would be is
// attached to the nearest ancestor that can still take it, and so becomes a
// sibling of the element it was written inside. Nothing else changes: the new
// element is pushed onto the stack of open elements like any other, later
// text goes into it, and its end tag closes it. The stack therefore still
// grows with the markup, as the standard says it does, and every rule that
// reads the stack (scopes, insertion modes, the adoption agency algorithm)
// works on the state the standard describes.
//
// An earlier version popped the current node at the limit instead, which is
// what WebKit does. That keeps the stack short, but the insertion mode goes
// on assuming that the element it was set for is still open: "in select"
// without a <select>, "in cell" without a cell. Random pages at the limit
// found the first "pop until a select has been popped" that emptied the stack
// within minutes.
//
// Depth is the number of elements from the root down to and including the
// node; what is inside a template counts as inside the template.
std::shared_ptr<Node> HtmlTreeBuilder::node_above(const Node& node) const {
    if (auto parent = node.parent()) return parent;
    if (!node.is_document_fragment()) return nullptr;
    auto found = m_template_of_contents.find(&node);
    return found == m_template_of_contents.end() ? nullptr : found->second.lock();
}

std::size_t HtmlTreeBuilder::element_depth(const Node& node) const {
    // Up to the root, or to the first open element whose depth is already
    // known. What is learned on the way is kept for the open elements passed,
    // so at the limit, where every insertion asks, the walk is a step or two
    // and not 512.
    struct Passed {
        std::size_t position;       // on the stack of open elements
        std::size_t below;          // elements counted before this one
    };
    std::vector<Passed> passed;
    std::size_t counted = 0;
    std::size_t depth = 0;
    std::shared_ptr<Node> keep;
    for (const Node* at = &node;;) {
        if (at->is_element()) {
            auto open = m_open_positions.find(static_cast<const Element*>(at));
            if (open != m_open_positions.end()) {
                if (const std::size_t known = m_open_depths[open->second]) {
                    depth = counted + known;
                    break;
                }
                passed.push_back({open->second, counted});
            }
            ++counted;
        }
        keep = node_above(*at);
        if (!keep) {
            depth = counted;
            break;
        }
        at = keep.get();
    }
    for (const Passed& open : passed) m_open_depths[open.position] = depth - open.below;
    return depth;
}

HtmlTreeBuilder::InsertionPlace HtmlTreeBuilder::within_depth_limit(InsertionPlace place,
                                                                    std::size_t levels_below,
                                                                    std::size_t ceiling) const {
    // An open element is no deeper in the tree than its place on the stack,
    // plus one for every element that was taken out of the middle of the
    // stack with its descendants left inside it: each element was attached
    // to one opened before it, or next to one. So with few elements open
    // nothing can be too deep, and an ordinary page pays one comparison here.
    if (!place.parent || !near_depth_limit(levels_below)) return place;

    std::size_t depth = element_depth(*place.parent);
    // The node goes one level below its parent and brings levels_below more.
    ceiling = std::min(ceiling, kMaxTreeDepth);
    const std::size_t deepest_parent = ceiling > levels_below + 1 ? ceiling - 1 - levels_below : 0;
    if (depth <= deepest_parent) return place;

    // Up, until the parent is shallow enough. Stepping from the contents of
    // a template to the template does not change the depth, so the walk never
    // stops on a template element: what belongs in a template goes into its
    // contents, never under the element itself.
    std::shared_ptr<Node> at = place.parent;
    while (depth > deepest_parent) {
        std::shared_ptr<Node> above = node_above(*at);
        if (!above) break;
        if (at->is_element()) --depth;
        at = std::move(above);
    }
    place.parent = std::move(at);
    place.before = nullptr;     // `before` was a child of the old parent
    return place;
}

HtmlTreeBuilder::InsertionPlace HtmlTreeBuilder::place_for_new_node() {
    return within_depth_limit(appropriate_place_for_inserting_node());
}

void HtmlTreeBuilder::append_node_at(const std::shared_ptr<Node>& node, InsertionPlace place) {
    if (!place.parent) place.parent = m_document;
    if (place.parent == node) return;  // never build a cycle
    if (place.before) {
        place.parent->insert_before(node, place.before);
    } else {
        place.parent->append_child(node);
    }
}

std::shared_ptr<Element> HtmlTreeBuilder::insert_html_element(const HtmlToken& token) {
    auto element = create_element_for_token(token, ElementNamespace::HTML);
    append_node_at(element, place_for_new_node());
    push_element(element);
    return element;
}

std::shared_ptr<Element> HtmlTreeBuilder::insert_foreign_element(const HtmlToken& token, ElementNamespace ns) {
    auto element = create_element_for_token(token, ns);
    append_node_at(element, place_for_new_node());
    push_element(element);
    return element;
}

void HtmlTreeBuilder::insert_character(char c) {
    auto place = appropriate_place_for_inserting_node();
    if (!place.parent) place.parent = m_document;

    // Merge with a preceding text node, per "insert a character".
    std::shared_ptr<Node> previous;
    if (place.before) {
        const auto& siblings = place.parent->children();
        for (std::size_t i = 0; i < siblings.size(); ++i) {
            if (siblings[i] == place.before) {
                if (i > 0) previous = siblings[i - 1];
                break;
            }
        }
    } else if (!place.parent->children().empty()) {
        previous = place.parent->children().back();
    }

    if (previous && previous->is_text()) {
        std::static_pointer_cast<Text>(previous)->append_text(c);
        return;
    }

    auto text = std::make_shared<Text>(std::string(1, c));
    append_node_at(text, place);
}

void HtmlTreeBuilder::insert_text(const std::string& text) {
    for (char c : text) insert_character(c);
}

void HtmlTreeBuilder::insert_comment_at(const HtmlToken& token, const std::shared_ptr<Node>& parent,
                                        const std::shared_ptr<Node>& before) {
    auto comment = std::make_shared<Comment>(token.data);
    if (before) parent->insert_before(comment, before);
    else if (parent) parent->append_child(comment);
}

void HtmlTreeBuilder::insert_comment(const HtmlToken& token) {
    auto place = place_for_new_node();
    insert_comment_at(token, place.parent ? place.parent : m_document, place.before);
}

// ---------------------------------------------------------------------------
// Active formatting elements
// ---------------------------------------------------------------------------

void HtmlTreeBuilder::push_formatting_element(const std::shared_ptr<Element>& element, const HtmlToken& token) {
    // The "Noah's Ark" clause: of the entries after the last marker, at most
    // three may have this tag name and these attributes. When a fourth
    // arrives, the earliest of the three goes. (The entries are HTML
    // elements, each made from its token, so comparing the elements compares
    // what the standard says to compare.)
    std::size_t after_marker = m_formatting.size();
    while (after_marker > 0 && !m_formatting[after_marker - 1].marker) --after_marker;
    auto same = [&](const FormattingEntry& entry) {
        return entry.element->tag_name() == element->tag_name() &&
               entry.element->attributes() == element->attributes();
    };
    std::size_t alike = 0;
    for (std::size_t i = after_marker; i < m_formatting.size(); ++i) alike += same(m_formatting[i]);
    if (alike >= 3) {
        for (std::size_t i = after_marker; i < m_formatting.size(); ++i) {
            if (!same(m_formatting[i])) continue;
            m_formatting.erase(m_formatting.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    }
    m_formatting.push_back(FormattingEntry{element, token, false});
}

void HtmlTreeBuilder::push_formatting_marker() {
    m_formatting.push_back(FormattingEntry{nullptr, HtmlToken{}, true});
}

int HtmlTreeBuilder::formatting_entry_index(const std::shared_ptr<Element>& element) const {
    for (int i = static_cast<int>(m_formatting.size()) - 1; i >= 0; --i) {
        const auto& entry = m_formatting[static_cast<std::size_t>(i)];
        if (!entry.marker && entry.element == element) return i;
    }
    return -1;
}

HtmlTreeBuilder::FormattingEntry* HtmlTreeBuilder::last_formatting_entry_by_tag(const std::string& tag) {
    for (int i = static_cast<int>(m_formatting.size()) - 1; i >= 0; --i) {
        auto& entry = m_formatting[static_cast<std::size_t>(i)];
        if (entry.marker) return nullptr;
        if (entry.element && entry.element->tag_name() == tag) return &entry;
    }
    return nullptr;
}

void HtmlTreeBuilder::clear_formatting_to_marker() {
    while (!m_formatting.empty()) {
        const bool marker = m_formatting.back().marker;
        m_formatting.pop_back();
        if (marker) return;
    }
}

void HtmlTreeBuilder::reconstruct_formatting_elements() {
    if (m_formatting.empty()) return;
    if (m_formatting.back().marker) return;

    // Asked for every start tag and every run of text in the body, so it
    // must not walk the stack.
    if (open_element_position(m_formatting.back().element) >= 0) return;

    int index = static_cast<int>(m_formatting.size()) - 1;
    while (index > 0) {
        const auto& previous = m_formatting[static_cast<std::size_t>(index - 1)];
        if (previous.marker) break;
        if (open_element_position(previous.element) >= 0) break;
        --index;
    }

    for (; index < static_cast<int>(m_formatting.size()); ++index) {
        auto& entry = m_formatting[static_cast<std::size_t>(index)];
        auto element = create_element_for_token(entry.token, ElementNamespace::HTML);
        append_node_at(element, place_for_new_node());
        push_element(element);
        entry.element = element;
    }
}

void HtmlTreeBuilder::adoption_agency(const HtmlToken& token) {
    const std::string subject = token.name;
    auto is_in_stack = [&](const std::shared_ptr<Element>& element) {
        return open_element_position(element) >= 0;
    };
    auto stack_index = [&](const std::shared_ptr<Element>& element) {
        return open_element_position(element);
    };

    // Spec step 2.
    if (auto current = current_node()) {
        if (html_name(current) == subject && formatting_entry_index(current) < 0) {
            pop_current_node();
            return;
        }
    }

    for (int outer = 0; outer < 8; ++outer) {
        FormattingEntry* formatting_entry = last_formatting_entry_by_tag(subject);
        if (!formatting_entry) {
            // "Act as described in the 'any other end tag' entry", whoever
            // asked: an end tag, or the start tag <nobr>, which runs this
            // algorithm to close the <nobr> that is open.
            any_other_end_tag(subject);
            return;
        }

        auto formatting_element = formatting_entry->element;
        // Copy the token now: the formatting list is mutated below and both
        // pointers and indices into it can go stale.
        const HtmlToken formatting_token = formatting_entry->token;
        if (!is_in_stack(formatting_element)) {
            m_formatting.erase(m_formatting.begin() + formatting_entry_index(formatting_element));
            return;
        }
        const int formatting_stack_index = stack_index(formatting_element);
        // "In scope" is asked of this element, not of its name: nothing that
        // bounds a scope may have been opened after it.
        if (!m_scope_boundary_indices[0].empty() &&
            static_cast<int>(m_scope_boundary_indices[0].back()) > formatting_stack_index) {
            return;
        }
        if (formatting_stack_index < 1) {
            // Nothing is open above the formatting element, so there is no
            // common ancestor to move anything to. Cannot happen while <html>
            // is open; it used to, after the stack had been emptied (see
            // pop_until_tag), and then read one element before the first.
            m_formatting.erase(m_formatting.begin() + formatting_entry_index(formatting_element));
            return;
        }

        // The furthest block: the first special element opened after the
        // formatting element. An <svg> or a <math> is not special, so
        // <a><svg></a> has none and the </a> closes both.
        int furthest_index = -1;
        {
            const auto& special = m_scope_boundary_indices[3];
            const auto found = std::upper_bound(special.begin(), special.end(),
                                                static_cast<std::size_t>(formatting_stack_index));
            if (found != special.end()) furthest_index = static_cast<int>(*found);
        }

        if (furthest_index < 0) {
            resize_open_elements(static_cast<std::size_t>(formatting_stack_index));
            const int entry_index = formatting_entry_index(formatting_element);
            if (entry_index >= 0) m_formatting.erase(m_formatting.begin() + entry_index);
            return;
        }

        auto furthest_block = m_open_elements[static_cast<std::size_t>(furthest_index)];
        auto common_ancestor = m_open_elements[static_cast<std::size_t>(formatting_stack_index - 1)];
        // Only measured near the nesting limit; see the new element below.
        const bool at_limit = near_depth_limit();
        const std::size_t furthest_depth_before = at_limit ? element_depth(*furthest_block) : 0;
        // From here on the formatting element, the furthest block and what
        // is open inside them move about in the tree. Their depths are
        // measured again when next asked for.
        std::fill(m_open_depths.begin() + formatting_stack_index, m_open_depths.end(), 0);

        int bookmark = formatting_entry_index(formatting_element);
        auto node = furthest_block;
        auto last_node = furthest_block;
        int node_index = furthest_index;
        std::size_t clones = 0;     // levels the furthest block ends up below last_node

        for (int inner = 1; ; ++inner) {
            --node_index;
            if (node_index < 0) break;
            node = m_open_elements[static_cast<std::size_t>(node_index)];
            if (node == formatting_element) break;

            int node_entry_index = formatting_entry_index(node);
            if (inner > 3 && node_entry_index >= 0) {
                m_formatting.erase(m_formatting.begin() + node_entry_index);
                if (node_entry_index < bookmark) --bookmark;
                node_entry_index = -1;
            }
            if (node_entry_index < 0) {
                erase_open_element(static_cast<std::size_t>(node_index), /*descendants_moved=*/true);
                continue;
            }

            if (node_entry_index < 0 || node_entry_index >= static_cast<int>(m_formatting.size())) continue;
            auto replacement = create_element_for_token(m_formatting[static_cast<std::size_t>(node_entry_index)].token,
                                                        ElementNamespace::HTML);
            m_formatting[static_cast<std::size_t>(node_entry_index)].element = replacement;
            replace_open_element(static_cast<std::size_t>(node_index), replacement);
            node = replacement;

            if (last_node == furthest_block) bookmark = node_entry_index + 1;

            if (auto parent = last_node->parent()) parent->remove_child(last_node);
            node->append_child(last_node);
            last_node = node;
            ++clones;
        }

        // Insert last_node at the appropriate place for common_ancestor.
        if (auto parent = last_node->parent()) parent->remove_child(last_node);

        // Both attachments below are subject to the nesting limit like any
        // other. Without that, <b><div>x</b> repeated at the limit nests one
        // level deeper each time: every <div> is moved under the one before.
        //
        // The furthest block brings its whole subtree along, so at the limit
        // it must not end up deeper than it was. Normally it cannot: it comes
        // out of the formatting element, which is more than the clones put
        // back. At the limit it may have been attached next to all of them.
        append_node_at(last_node,
                       within_depth_limit(appropriate_place_for_inserting_node(common_ancestor, true), clones,
                                          at_limit ? furthest_depth_before : kMaxTreeDepth));

        bookmark = std::max(0, std::min(bookmark, static_cast<int>(m_formatting.size())));
        auto new_element = create_element_for_token(formatting_token, ElementNamespace::HTML);

        // Move all children of the furthest block into the new element, and
        // the new element into the furthest block. That puts the children
        // one level further down. Normally the furthest block has just come
        // up at least one level, out of the formatting element, so nothing
        // ends up deeper than it was. At the nesting limit it may never have
        // been inside the formatting element (it was attached next to it),
        // and then each run of this algorithm would push its children one
        // level deeper, without end. There the new element goes next to the
        // furthest block instead of into it.
        InsertionPlace for_new_element{furthest_block, nullptr};
        if (at_limit && element_depth(*furthest_block) >= furthest_depth_before) {
            if (auto beside = furthest_block->parent()) for_new_element.parent = std::move(beside);
        }
        for (const auto& child : furthest_block->take_children()) new_element->append_child(child);
        append_node_at(new_element, within_depth_limit(std::move(for_new_element)));

        // Update the formatting list and the stack of open elements.
        const int entry_index = formatting_entry_index(formatting_element);
        if (entry_index >= 0) {
            m_formatting.erase(m_formatting.begin() + entry_index);
            if (entry_index < bookmark) --bookmark;
        }
        bookmark = std::max(0, std::min(bookmark, static_cast<int>(m_formatting.size())));
        m_formatting.insert(m_formatting.begin() + bookmark, FormattingEntry{new_element, formatting_token, false});

        const int old_index = stack_index(formatting_element);
        if (old_index >= 0) erase_open_element(static_cast<std::size_t>(old_index), /*descendants_moved=*/true);
        const int furthest_now = stack_index(furthest_block);
        insert_open_element(static_cast<std::size_t>(furthest_now + 1), new_element);
    }
}

// ---------------------------------------------------------------------------
// Mode helpers
// ---------------------------------------------------------------------------

void HtmlTreeBuilder::set_tokenizer_rcdata(const std::string& tag) {
    if (m_tokenizer) m_tokenizer->set_content_model(ContentModel::Rcdata, tag);
}

void HtmlTreeBuilder::set_tokenizer_rawtext(const std::string& tag) {
    if (m_tokenizer) m_tokenizer->set_content_model(ContentModel::Rawtext, tag);
}

void HtmlTreeBuilder::set_tokenizer_script_data(const std::string& tag) {
    if (m_tokenizer) m_tokenizer->set_content_model(ContentModel::ScriptData, tag);
}

void HtmlTreeBuilder::switch_to_text_mode(const std::string& tag, ContentModel model) {
    m_original_mode = m_mode;
    m_mode = Mode::Text;
    if (m_tokenizer) m_tokenizer->set_content_model(model, tag);
}

bool HtmlTreeBuilder::parsing_template_contents() const {
    return element_index_in_stack("template") >= 0 || html_name(m_context_element) == "template";
}

void HtmlTreeBuilder::replace_template_mode(Mode mode) {
    if (!m_template_modes.empty()) m_template_modes.pop_back();
    m_template_modes.push_back(mode);
    m_mode = mode;
}

void HtmlTreeBuilder::close_p_element() {
    generate_implied_end_tags("p");
    if (!m_open_elements.empty() && m_open_elements.back()->tag_name() != "p") {
        // Parse error in the spec; recovery is to pop anyway.
    }
    pop_until_tag("p");
}

bool HtmlTreeBuilder::is_mathml_text_integration_point(const Element& element) const {
    if (element.element_namespace() != ElementNamespace::MathML) return false;
    return is_mathml_text_integration_point_tag(element.tag_name());
}

bool HtmlTreeBuilder::is_html_integration_point(const Element& element) const {
    if (element.element_namespace() == ElementNamespace::SVG) {
        return is_svg_html_integration_point_tag(element.tag_name());
    }
    if (element.element_namespace() == ElementNamespace::MathML) {
        if (element.tag_name() != "annotation-xml") return false;
        std::string encoding = element.get_attribute("encoding");
        std::transform(encoding.begin(), encoding.end(), encoding.begin(), ::tolower);
        return encoding == "text/html" || encoding == "application/xhtml+xml";
    }
    return false;
}

void HtmlTreeBuilder::stop_parsing() {
    // Nothing to tear down: the loop ends when the tokenizer reports EOF.
}

QuirksMode HtmlTreeBuilder::quirks_mode_for(const Doctype& doctype, bool force_quirks) {
    if (force_quirks) return QuirksMode::Quirks;
    if (!doctype.present) return QuirksMode::Quirks;

    std::string name = doctype.name;
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    if (name != "html") return QuirksMode::Quirks;

    std::string public_id = doctype.public_id;
    std::transform(public_id.begin(), public_id.end(), public_id.begin(), ::tolower);
    std::string system_id = doctype.system_id;
    std::transform(system_id.begin(), system_id.end(), system_id.begin(), ::tolower);

    static const std::vector<std::string> quirks_public_prefixes = {
        "+//silmaril//dtd html pro v0r11 19970101//", "-//as//dtd html 3.0 aswedit + extensions//",
        "-//advasoft ltd//dtd html 3.0 aswedit + extensions//", "-//ietf//dtd html 2.0 level 1//",
        "-//ietf//dtd html 2.0 level 2//", "-//ietf//dtd html 2.0 strict level 1//",
        "-//ietf//dtd html 2.0 strict level 2//", "-//ietf//dtd html 2.0 strict//",
        "-//ietf//dtd html 2.0//", "-//ietf//dtd html 2.1e//",
        "-//ietf//dtd html 3.0//", "-//ietf//dtd html 3.2 final//",
        "-//ietf//dtd html 3.2//", "-//ietf//dtd html 3//",
        "-//ietf//dtd html level 0//", "-//ietf//dtd html level 1//",
        "-//ietf//dtd html level 2//", "-//ietf//dtd html level 3//",
        "-//ietf//dtd html strict level 0//", "-//ietf//dtd html strict level 1//",
        "-//ietf//dtd html strict level 2//", "-//ietf//dtd html strict level 3//",
        "-//ietf//dtd html strict//", "-//ietf//dtd html//",
        "-//metrius//dtd metrius presentational//",
        "-//microsoft//dtd internet explorer 2.0 html strict//",
        "-//microsoft//dtd internet explorer 2.0 html//",
        "-//microsoft//dtd internet explorer 2.0 tables//",
        "-//microsoft//dtd internet explorer 3.0 html strict//",
        "-//microsoft//dtd internet explorer 3.0 html//",
        "-//microsoft//dtd internet explorer 3.0 tables//",
        "-//netscape comm. corp.//dtd html//", "-//netscape comm. corp.//dtd strict html//",
        "-//o'reilly and associates//dtd html 2.0//", "-//o'reilly and associates//dtd html extended 1.0//",
        "-//o'reilly and associates//dtd html extended relaxed 1.0//",
        "-//sq//dtd html 2.0 hotmetal + extensions//",
        "-//softquad software//dtd hotmetal pro 6.0::19990601::extensions to html 4.0//",
        "-//softquad//dtd hotmetal pro 4.0::19971010::extensions to html 4.0//",
        "-//spyglass//dtd html 2.0 extended//", "-//sun microsystems corp.//dtd hotjava html//",
        "-//sun microsystems corp.//dtd hotjava strict html//",
        "-//w3c//dtd html 3 1995-03-24//", "-//w3c//dtd html 3.2 draft//",
        "-//w3c//dtd html 3.2 final//", "-//w3c//dtd html 3.2//",
        "-//w3c//dtd html 3.2s draft//", "-//w3c//dtd html 4.0 frameset//",
        "-//w3c//dtd html 4.0 transitional//", "-//w3c//dtd html experimental 19960712//",
        "-//w3c//dtd html experimental 970421//", "-//w3c//dtd w3 html//",
        "-//w3o//dtd w3 html 3.0//", "-//webtechs//dtd mozilla html 2.0//",
        "-//webtechs//dtd mozilla html//"};

    for (const auto& prefix : quirks_public_prefixes) {
        if (public_id.rfind(prefix, 0) == 0) return QuirksMode::Quirks;
    }
    if (public_id == "-//w3o//dtd w3 html strict 3.0//en//" ||
        public_id == "-/w3c/dtd html 4.0 transitional/en" || public_id == "html") {
        return QuirksMode::Quirks;
    }
    if (system_id == "http://www.ibm.com/data/dtd/v11/ibmxhtml1-transitional.dtd") {
        return QuirksMode::Quirks;
    }
    if (doctype.system_id.empty()) {
        if (public_id.rfind("-//w3c//dtd html 4.01 frameset//", 0) == 0 ||
            public_id.rfind("-//w3c//dtd html 4.01 transitional//", 0) == 0) {
            return QuirksMode::Quirks;
        }
    }
    if (public_id.rfind("-//w3c//dtd xhtml 1.0 frameset//", 0) == 0 ||
        public_id.rfind("-//w3c//dtd xhtml 1.0 transitional//", 0) == 0) {
        return QuirksMode::LimitedQuirks;
    }
    if (!doctype.system_id.empty() &&
        (public_id.rfind("-//w3c//dtd html 4.01 frameset//", 0) == 0 ||
         public_id.rfind("-//w3c//dtd html 4.01 transitional//", 0) == 0)) {
        return QuirksMode::LimitedQuirks;
    }
    return QuirksMode::NoQuirks;
}

void HtmlTreeBuilder::reset_insertion_mode() {
    // The standard walks down the stack to the first element that decides
    // the mode. All of those are HTML elements with one of these names, so
    // the walk can start at the highest of them: everything above it would
    // be skipped anyway, and there can be 200,000 elements above it.
    static const char* const deciding[] = {"select", "td", "th", "tr", "tbody", "thead", "tfoot", "caption",
                                           "colgroup", "table", "template", "head", "body", "frameset", "html"};
    int start = 0;
    for (const char* name : deciding) start = std::max(start, element_index_in_stack(name));
    start = std::min(start, static_cast<int>(m_open_elements.size()) - 1);

    for (int i = start; i >= 0; --i) {
        // Fragment case: the first (root) node is replaced by the context
        // element passed to the fragment parsing algorithm.
        std::shared_ptr<Element> node = m_open_elements[static_cast<std::size_t>(i)];
        if (i == 0 && m_fragment_case && m_context_element) node = m_context_element;
        const std::string& name = node->tag_name();
        const bool last = (i == 0);
        const bool html = node->element_namespace() == ElementNamespace::HTML;

        if (html) {
            if (name == "select") {
                // Below the select: a table before any template means "in
                // select in table". Nothing that decides the mode is above
                // the select, so the highest table and the highest template
                // on the stack are the nearest ones below it.
                const int table = element_index_in_stack("table");
                const int template_index = element_index_in_stack("template");
                m_mode = (table > 0 && table < i && table > template_index) ? Mode::InSelectInTable
                                                                            : Mode::InSelect;
                return;
            }
            if (name == "td" || name == "th") { m_mode = Mode::InCell; return; }
            if (name == "tr") { m_mode = Mode::InRow; return; }
            if (name == "tbody" || name == "thead" || name == "tfoot") { m_mode = Mode::InTableBody; return; }
            if (name == "caption") { m_mode = Mode::InCaption; return; }
            if (name == "colgroup") { m_mode = Mode::InColumnGroup; return; }
            if (name == "table") { m_mode = Mode::InTable; return; }
            if (name == "template") { m_mode = m_template_modes.empty() ? Mode::InTemplate : m_template_modes.back(); return; }
            if (name == "head" && !last) { m_mode = Mode::InHead; return; }
            if (name == "body") { m_mode = Mode::InBody; return; }
            if (name == "frameset") { m_mode = Mode::InFrameset; return; }
            if (name == "html") {
                m_mode = m_head_element ? Mode::AfterHead : Mode::BeforeHead;
                return;
            }
        }
        if (last) { m_mode = Mode::InBody; return; }
    }
    m_mode = Mode::InBody;
}

// ---------------------------------------------------------------------------
// Dispatcher
// ---------------------------------------------------------------------------

void HtmlTreeBuilder::process_token(const HtmlToken& token) {
    if (m_mode == Mode::InTableText && token.type != HtmlTokenType::Character) {
        process_in_table_text(token);  // flushes pending text, reprocesses token
        return;
    }

    // Foreign content dispatch.
    if (!m_open_elements.empty() && token.type != HtmlTokenType::EndOfFile) {
        auto node = adjusted_current_node();
        bool use_foreign = false;
        if (node) {
            if (node->element_namespace() != ElementNamespace::HTML) {
                if (is_mathml_text_integration_point(*node)) {
                    const bool text_like = token.type == HtmlTokenType::Character ||
                                           (token.type == HtmlTokenType::StartTag &&
                                            token.name != "mglyph" && token.name != "malignmark");
                    use_foreign = !text_like;
                } else if (node->element_namespace() == ElementNamespace::MathML &&
                           node->tag_name() == "annotation-xml" &&
                           token.is_start_tag("svg")) {
                    use_foreign = false;
                } else if (is_html_integration_point(*node) &&
                           (token.type == HtmlTokenType::Character ||
                            token.type == HtmlTokenType::StartTag)) {
                    use_foreign = false;
                } else {
                    use_foreign = true;
                }
            }
        }
        if (use_foreign) {
            process_foreign_content(token);
            return;
        }
    }

    if (token.type == HtmlTokenType::Character) {
        // "If the next token is a U+000A LINE FEED character token, then
        // ignore that token" -- the authoring convenience for <pre>,
        // <listing> and <textarea>, whose first newline is dropped.
        std::string data = token.data;
        if (m_skip_next_newline) {
            m_skip_next_newline = false;
            if (!data.empty() && data.front() == '\n') {
                data.erase(data.begin());
            }
        }
        process_characters(data);
        return;
    }

    // Any other token consumes the one-shot newline skip.
    m_skip_next_newline = false;

    process_token_in_mode(m_mode, token);
}

void HtmlTreeBuilder::process_characters(const std::string& data) {
    for (char c : data) process_character(c);
}

void HtmlTreeBuilder::process_character(char c) {
    HtmlToken token;
    token.type = HtmlTokenType::Character;
    token.data = std::string(1, c);

    switch (m_mode) {
        case Mode::Initial:
        case Mode::BeforeHtml:
        case Mode::BeforeHead: {
            const bool whitespace = (c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r');
            if (!whitespace) {
                process_token_in_mode(m_mode, token);
            } else if (m_mode == Mode::BeforeHtml || m_mode == Mode::BeforeHead) {
                process_token_in_mode(m_mode, token);
            }
            return;
        }
        case Mode::InHead:
        case Mode::InHeadNoscript:
        case Mode::AfterHead:
            process_token_in_mode(m_mode, token);
            return;
        case Mode::InBody:
        case Mode::InCaption:
        case Mode::InCell:
        case Mode::InTableBody:
        case Mode::InRow:
            process_token_in_mode(m_mode, token);
            return;
        case Mode::Text:
            insert_character(c);
            return;
        case Mode::InTable:
        case Mode::InTableText:
            process_token_in_mode(m_mode, token);
            return;
        case Mode::InColumnGroup:
            if (c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r') {
                insert_character(c);
                return;
            }
            process_token_in_mode(m_mode, token);
            return;
        case Mode::InSelect:
            // "A character token that is U+0000 NULL: parse error, ignore."
            if (c == '\0') return;
            insert_character(c);
            return;
        case Mode::InSelectInTable:
            process_token_in_mode(m_mode, token);
            return;
        case Mode::InTemplate:
            process_token_in_mode(m_mode, token);
            return;
        case Mode::AfterBody:
        case Mode::AfterAfterBody:
        case Mode::AfterFrameset:
        case Mode::AfterAfterFrameset:
        case Mode::InFrameset:
            process_token_in_mode(m_mode, token);
            return;
    }
}

void HtmlTreeBuilder::process_token_in_mode(Mode mode, const HtmlToken& token) {
    switch (mode) {
        case Mode::Initial: process_initial(token); break;
        case Mode::BeforeHtml: process_before_html(token); break;
        case Mode::BeforeHead: process_before_head(token); break;
        case Mode::InHead: process_in_head(token); break;
        case Mode::InHeadNoscript: process_in_head_noscript(token); break;
        case Mode::AfterHead: process_after_head(token); break;
        case Mode::InBody: process_in_body(token); break;
        case Mode::Text: process_text(token); break;
        case Mode::InTable: process_in_table(token); break;
        case Mode::InTableText: process_in_table_text(token); break;
        case Mode::InCaption: process_in_caption(token); break;
        case Mode::InColumnGroup: process_in_column_group(token); break;
        case Mode::InTableBody: process_in_table_body(token); break;
        case Mode::InRow: process_in_row(token); break;
        case Mode::InCell: process_in_cell(token); break;
        case Mode::InSelect: process_in_select(token); break;
        case Mode::InSelectInTable: process_in_select_in_table(token); break;
        case Mode::InTemplate: process_in_template(token); break;
        case Mode::AfterBody: process_after_body(token); break;
        case Mode::InFrameset: process_in_frameset(token); break;
        case Mode::AfterFrameset: process_after_frameset(token); break;
        case Mode::AfterAfterBody: process_after_after_body(token); break;
        case Mode::AfterAfterFrameset: process_after_after_frameset(token); break;
    }
}

} // namespace litebird
