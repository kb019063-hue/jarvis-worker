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
        auto node = current_node();
        tokenizer.set_allow_cdata(node && node->element_namespace() != ElementNamespace::HTML);
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
    m_tokenizer_pending_tag = tag;

    if (tag == "form") {
        m_form_element = m_context_element;
    }

    HtmlTokenizer tokenizer(html);
    m_tokenizer = &tokenizer;
    if (m_tokenizer_pending != ContentModel::Data) {
        tokenizer.set_content_model(m_tokenizer_pending, m_tokenizer_pending_tag);
    }

    while (true) {
        auto node = current_node();
        tokenizer.set_allow_cdata(node && node->element_namespace() != ElementNamespace::HTML);
        HtmlToken token = tokenizer.next_token();
        if (token.type == HtmlTokenType::Character && token.data.empty()) continue;
        process_token(token);
        if (token.type == HtmlTokenType::EndOfFile) break;
    }
    m_tokenizer = nullptr;

    auto fragment = std::make_shared<Node>(NodeType::DocumentFragment);
    auto children = root->children();
    for (const auto& child : children) {
        root->remove_child(child);
        fragment->append_child(child);
    }
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

void HtmlTreeBuilder::index_open_element(std::size_t index, const std::shared_ptr<Element>& element) {
    if (!element) return;
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
    } else if (element->element_namespace() == ElementNamespace::MathML) {
        if (is_mathml_text_integration_point_tag(name) || name == "annotation-xml") {
            m_scope_boundary_indices[0].push_back(index);
        }
    } else if (element->element_namespace() == ElementNamespace::SVG) {
        if (is_svg_html_integration_point_tag(name)) m_scope_boundary_indices[0].push_back(index);
    }
}

void HtmlTreeBuilder::rebuild_open_element_indexes() {
    m_open_html_tag_indices.clear();
    for (auto& boundaries : m_scope_boundary_indices) boundaries.clear();
    for (std::size_t i = 0; i < m_open_elements.size(); ++i) {
        index_open_element(i, m_open_elements[i]);
    }
}

void HtmlTreeBuilder::push_element(const std::shared_ptr<Element>& element) {
    m_open_elements.push_back(element);
    index_open_element(m_open_elements.size() - 1, element);
}

void HtmlTreeBuilder::pop_current_node() {
    if (m_open_elements.empty()) return;
    const std::size_t index = m_open_elements.size() - 1;
    auto element = m_open_elements.back();
    if (element && element->element_namespace() == ElementNamespace::HTML) {
        auto it = m_open_html_tag_indices.find(element->tag_name());
        if (it != m_open_html_tag_indices.end() && !it->second.empty() && it->second.back() == index) {
            it->second.pop_back();
            if (it->second.empty()) m_open_html_tag_indices.erase(it);
        }
    }
    for (auto& boundaries : m_scope_boundary_indices) {
        if (!boundaries.empty() && boundaries.back() == index) boundaries.pop_back();
    }
    m_open_elements.pop_back();
}

void HtmlTreeBuilder::resize_open_elements(std::size_t size) {
    while (m_open_elements.size() > size) pop_current_node();
}

void HtmlTreeBuilder::erase_open_element(std::size_t index) {
    if (index >= m_open_elements.size()) return;
    m_open_elements.erase(m_open_elements.begin() + static_cast<std::ptrdiff_t>(index));
    rebuild_open_element_indexes();
}

void HtmlTreeBuilder::insert_open_element(std::size_t index, const std::shared_ptr<Element>& element) {
    index = std::min(index, m_open_elements.size());
    m_open_elements.insert(m_open_elements.begin() + static_cast<std::ptrdiff_t>(index), element);
    rebuild_open_element_indexes();
}

void HtmlTreeBuilder::replace_open_element(std::size_t index, const std::shared_ptr<Element>& element) {
    if (index >= m_open_elements.size()) return;
    m_open_elements[index] = element;
    rebuild_open_element_indexes();
}

namespace {
// Tree-construction stack queries are scoped to HTML elements, not foreign
// elements that happen to share a local name.
bool is_html_element(const std::shared_ptr<Element>& element) {
    return element && element->element_namespace() == ElementNamespace::HTML;
}
} // namespace

void HtmlTreeBuilder::pop_until_tag(const std::string& tag) {
    while (!m_open_elements.empty()) {
        auto element = m_open_elements.back();
        pop_current_node();
        if (is_html_element(element) && element->tag_name() == tag) return;
    }
}

void HtmlTreeBuilder::pop_until_one_of(const std::vector<std::string>& tags) {
    while (!m_open_elements.empty()) {
        auto element = m_open_elements.back();
        pop_current_node();
        if (is_html_element(element) &&
            std::find(tags.begin(), tags.end(), element->tag_name()) != tags.end()) return;
    }
}

int HtmlTreeBuilder::element_index_in_stack(const std::string& tag) const {
    for (int i = static_cast<int>(m_open_elements.size()) - 1; i >= 0; --i) {
        const auto& element = m_open_elements[static_cast<std::size_t>(i)];
        if (is_html_element(element) && element->tag_name() == tag) return i;
    }
    return -1;
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
    for (int i = static_cast<int>(m_open_elements.size()) - 1; i >= 0; --i) {
        const auto& element = m_open_elements[static_cast<std::size_t>(i)];
        if (is_html_element(element) && element->tag_name() == tag) return true;
        if (element->element_namespace() == ElementNamespace::HTML &&
            (element->tag_name() == "html" || element->tag_name() == "table" ||
             element->tag_name() == "template")) {
            return false;
        }
    }
    return false;
}

bool HtmlTreeBuilder::has_element_in_select_scope(const std::string& tag) const {
    for (int i = static_cast<int>(m_open_elements.size()) - 1; i >= 0; --i) {
        const auto& element = m_open_elements[static_cast<std::size_t>(i)];
        if (is_html_element(element) && element->tag_name() == tag) return true;
        if (!(element->tag_name() == "optgroup" || element->tag_name() == "option")) return false;
    }
    return false;
}

void HtmlTreeBuilder::generate_implied_end_tags(const std::string& except) {
    while (!m_open_elements.empty()) {
        const std::string& name = m_open_elements.back()->tag_name();
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
        const std::string& name = m_open_elements.back()->tag_name();
        if (name == "table" || name == "template" || name == "html") return;
        pop_current_node();
    }
}

void HtmlTreeBuilder::clear_stack_back_to_table_body_context() {
    while (!m_open_elements.empty()) {
        const std::string& name = m_open_elements.back()->tag_name();
        if (name == "tbody" || name == "tfoot" || name == "thead" || name == "template" || name == "html") return;
        pop_current_node();
    }
}

void HtmlTreeBuilder::clear_stack_back_to_row_context() {
    while (!m_open_elements.empty()) {
        const std::string& name = m_open_elements.back()->tag_name();
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
    int last_template = -1;
    int last_table = -1;
    for (int i = static_cast<int>(m_open_elements.size()) - 1; i >= 0; --i) {
        const auto& element = m_open_elements[static_cast<std::size_t>(i)];
        if (last_template < 0 && element->tag_name() == "template") last_template = i;
        if (last_table < 0 && element->tag_name() == "table") last_table = i;
        if (last_template >= 0 && last_table >= 0) break;
    }

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
        element->set_template_contents(std::make_shared<Node>(NodeType::DocumentFragment));
    }
    return element;
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
    append_node_at(element, appropriate_place_for_inserting_node());
    push_element(element);
    return element;
}

std::shared_ptr<Element> HtmlTreeBuilder::insert_foreign_element(const HtmlToken& token, ElementNamespace ns) {
    auto element = create_element_for_token(token, ns);
    append_node_at(element, appropriate_place_for_inserting_node());
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
    auto place = appropriate_place_for_inserting_node();
    insert_comment_at(token, place.parent ? place.parent : m_document, place.before);
}

// ---------------------------------------------------------------------------
// Active formatting elements
// ---------------------------------------------------------------------------

void HtmlTreeBuilder::push_formatting_element(const std::shared_ptr<Element>& element, const HtmlToken& token) {
    // Noah's Ark clause: at most three identical entries after the last marker.
    int count = 0;
    bool seen_marker = false;
    for (int i = static_cast<int>(m_formatting.size()) - 1; i >= 0; --i) {
        const auto& entry = m_formatting[static_cast<std::size_t>(i)];
        if (entry.marker) { seen_marker = true; break; }
        if (entry.element->tag_name() == element->tag_name() &&
            entry.element->attributes() == element->attributes()) {
            ++count;
        }
    }
    if (count >= 3) {
        for (int i = static_cast<int>(m_formatting.size()) - 1; i >= 0; --i) {
            const auto& entry = m_formatting[static_cast<std::size_t>(i)];
            if (entry.marker) break;
            if (entry.element->tag_name() == element->tag_name() &&
                entry.element->attributes() == element->attributes()) {
                m_formatting.erase(m_formatting.begin() + i);
                break;
            }
        }
    }
    (void)seen_marker;
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

    auto last = m_formatting.back().element;
    bool in_stack = std::find(m_open_elements.begin(), m_open_elements.end(), last) != m_open_elements.end();
    if (in_stack) return;

    int index = static_cast<int>(m_formatting.size()) - 1;
    while (index > 0) {
        const auto& previous = m_formatting[static_cast<std::size_t>(index - 1)];
        if (previous.marker) break;
        if (std::find(m_open_elements.begin(), m_open_elements.end(), previous.element) != m_open_elements.end()) break;
        --index;
    }

    for (; index < static_cast<int>(m_formatting.size()); ++index) {
        auto& entry = m_formatting[static_cast<std::size_t>(index)];
        auto element = create_element_for_token(entry.token, ElementNamespace::HTML);
        append_node_at(element, appropriate_place_for_inserting_node());
        push_element(element);
        entry.element = element;
    }
}

bool HtmlTreeBuilder::adoption_agency(const HtmlToken& token) {
    const std::string subject = token.name;
    auto is_in_stack = [&](const std::shared_ptr<Element>& element) {
        return std::find(m_open_elements.begin(), m_open_elements.end(), element) != m_open_elements.end();
    };
    auto stack_index = [&](const std::shared_ptr<Element>& element) {
        for (std::size_t i = 0; i < m_open_elements.size(); ++i) {
            if (m_open_elements[i] == element) return static_cast<int>(i);
        }
        return -1;
    };

    // Spec step 2.
    if (auto current = current_node()) {
        if (current->tag_name() == subject && formatting_entry_index(current) < 0) {
            pop_current_node();
            return true;
        }
    }

    for (int outer = 0; outer < 8; ++outer) {
        FormattingEntry* formatting_entry = last_formatting_entry_by_tag(subject);
        if (!formatting_entry) return false;  // Act as "any other end tag".

        auto formatting_element = formatting_entry->element;
        // Copy the token now: the formatting list is mutated below and both
        // pointers and indices into it can go stale.
        const HtmlToken formatting_token = formatting_entry->token;
        if (!is_in_stack(formatting_element)) {
            m_formatting.erase(m_formatting.begin() + formatting_entry_index(formatting_element));
            return true;
        }
        if (!has_element_in_scope(subject)) return true;

        const int formatting_stack_index = stack_index(formatting_element);

        // Find the furthest block.
        int furthest_index = -1;
        for (int i = formatting_stack_index + 1; i < static_cast<int>(m_open_elements.size()); ++i) {
            const auto& element = m_open_elements[static_cast<std::size_t>(i)];
            if (element->element_namespace() == ElementNamespace::HTML &&
                special_html_tags().count(element->tag_name())) {
                furthest_index = i;
                break;
            }
            if (element->element_namespace() != ElementNamespace::HTML) {
                furthest_index = i;
                break;
            }
        }

        if (furthest_index < 0) {
            resize_open_elements(static_cast<std::size_t>(formatting_stack_index));
            const int entry_index = formatting_entry_index(formatting_element);
            if (entry_index >= 0) m_formatting.erase(m_formatting.begin() + entry_index);
            return true;
        }

        auto furthest_block = m_open_elements[static_cast<std::size_t>(furthest_index)];
        auto common_ancestor = m_open_elements[static_cast<std::size_t>(formatting_stack_index - 1)];

        int bookmark = formatting_entry_index(formatting_element);
        auto node = furthest_block;
        auto last_node = furthest_block;
        int node_index = furthest_index;

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
                erase_open_element(static_cast<std::size_t>(node_index));
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
        }

        // Insert last_node at the appropriate place for common_ancestor.
        if (auto parent = last_node->parent()) parent->remove_child(last_node);

        append_node_at(last_node, appropriate_place_for_inserting_node(common_ancestor, true));

        bookmark = std::max(0, std::min(bookmark, static_cast<int>(m_formatting.size())));
        auto new_element = create_element_for_token(formatting_token, ElementNamespace::HTML);

        // Move all children of the furthest block into the new element.
        auto children = furthest_block->children();
        for (const auto& child : children) {
            furthest_block->remove_child(child);
            new_element->append_child(child);
        }
        furthest_block->append_child(new_element);

        // Update the formatting list and the stack of open elements.
        const int entry_index = formatting_entry_index(formatting_element);
        if (entry_index >= 0) {
            m_formatting.erase(m_formatting.begin() + entry_index);
            if (entry_index < bookmark) --bookmark;
        }
        bookmark = std::max(0, std::min(bookmark, static_cast<int>(m_formatting.size())));
        m_formatting.insert(m_formatting.begin() + bookmark, FormattingEntry{new_element, formatting_token, false});

        const int old_index = stack_index(formatting_element);
        if (old_index >= 0) erase_open_element(static_cast<std::size_t>(old_index));
        const int furthest_now = stack_index(furthest_block);
        insert_open_element(static_cast<std::size_t>(furthest_now + 1), new_element);
    }
    return true;
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

void HtmlTreeBuilder::acknowledge_self_closing(const HtmlToken& token) {
    if (token.self_closing) pop_current_node();
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
    for (int i = static_cast<int>(m_open_elements.size()) - 1; i >= 0; --i) {
        // Fragment case: the first (root) node is replaced by the context
        // element passed to the fragment parsing algorithm.
        std::shared_ptr<Element> node = m_open_elements[static_cast<std::size_t>(i)];
        if (i == 0 && m_fragment_case && m_context_element) node = m_context_element;
        const std::string& name = node->tag_name();
        const bool last = (i == 0);
        const bool html = node->element_namespace() == ElementNamespace::HTML;

        if (html) {
            if (name == "select") {
                for (int j = i - 1; j > 0; --j) {
                    const auto& ancestor = m_open_elements[static_cast<std::size_t>(j)];
                    if (ancestor->tag_name() == "template") break;
                    if (ancestor->tag_name() == "table") {
                        m_mode = Mode::InSelectInTable;
                        return;
                    }
                }
                m_mode = Mode::InSelect;
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

    if (m_tokenizer) {
        m_tokenizer->set_allow_cdata(!m_open_elements.empty() &&
                                     m_open_elements.back()->element_namespace() != ElementNamespace::HTML);
    }

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
            m_table_text_return_mode = m_mode;
            m_mode = Mode::InTableText;
            m_pending_table_text.clear();
            m_pending_table_text.push_back(c);
            return;
        case Mode::InTableText:
            m_pending_table_text.push_back(c);
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
