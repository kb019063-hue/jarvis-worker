// Document, DOMImplementation, DOMParser and XMLSerializer.

#include "litebird/js/DomBindings.hpp"
#include "litebird/dom/TreeWalk.hpp"
#include "litebird/html/HtmlParser.hpp"
#include "litebird/js/JsBuiltins.hpp"
#include "litebird/js/Storage.hpp"

#include <ctime>

namespace litebird {
namespace dom {

namespace {

bool is_document_node(const Node& node) { return node.is_document(); }

Document& as_document(Node& node) { return static_cast<Document&>(node); }

std::shared_ptr<Document> shared_document(Node& node) {
    return std::static_pointer_cast<Document>(node.shared_from_this());
}

std::string collapse_whitespace(const std::string& text) {
    std::string out;
    bool pending = false;
    for (char c : text) {
        if (is_ascii_space(c)) {
            pending = !out.empty();
            continue;
        }
        if (pending) {
            out += ' ';
            pending = false;
        }
        out += c;
    }
    return out;
}

std::shared_ptr<Element> title_element(const Document& document) {
    std::shared_ptr<Element> found;
    for_each_descendant(document, [&](Node& node) {
        if (is_html_element(node, "title")) {
            found = shared(static_cast<Element&>(node));
            return false;
        }
        return true;
    });
    return found;
}

// A live collection of the elements below the document that `wanted` accepts.
JsValue document_collection(Realm& realm, Node& document, const char* slot_name, bool (*wanted)(const Element&)) {
    JsValue owner = wrap(realm, document);
    auto root = document.shared_from_this();
    return cached(owner, slot_name, [&]() {
        return make_live_list(realm, "HTMLCollection", root, [wanted](Node& node, NodeVector& out) {
            for_each_descendant(node, [&](Node& inner) {
                if (inner.is_element() && wanted(static_cast<Element&>(inner))) out.push_back(inner.shared_from_this());
                return true;
            });
        });
    });
}

bool namespace_from_uri(const std::string& uri, ElementNamespace& out) {
    if (uri == "http://www.w3.org/1999/xhtml") out = ElementNamespace::HTML;
    else if (uri == "http://www.w3.org/2000/svg") out = ElementNamespace::SVG;
    else if (uri == "http://www.w3.org/1998/Math/MathML") out = ElementNamespace::MathML;
    else return false;
    return true;
}

std::string two_digits(int value) {
    return (value < 10 ? "0" : "") + std::to_string(value);
}

void set_owner_of_tree(const std::shared_ptr<Document>& document) {
    for (const auto& child : document->children()) child->set_owner_document(document);
}

// ---------------------------------------------------------------------------
// DOMImplementation
// ---------------------------------------------------------------------------

struct ImplementationState : JsExotic {
    std::shared_ptr<Document> document;
    void trace(Heap& heap) override { trace_node(heap, document.get()); }
};

void setup_implementation(Iface& I) {
    I.method("createHTMLDocument", 0, [](Realm& r, Args args) {
        this_state<ImplementationState>("DOMImplementation");
        if (js_arg(args, 0).is_undefined()) return wrap(r, make_document(r, true, nullptr));
        const std::string title = to_str(args[0]);
        return wrap(r, make_document(r, true, &title));
    });
    I.method("createDocument", 2, [](Realm& r, Args args) {
        this_state<ImplementationState>("DOMImplementation");
        require_args(args, 2, "DOMImplementation.createDocument");
        auto document = make_document(r, false, nullptr);
        const std::string name = args[1].is_nullish() ? std::string() : to_str(args[1]);
        if (!name.empty()) document->append_child(create_element_ns(r, *document, args[0], name));
        return wrap(r, document);
    });
    I.method("createDocumentType", 3, [](Realm& r, Args args) {
        ImplementationState& state = this_state<ImplementationState>("DOMImplementation");
        require_args(args, 3, "DOMImplementation.createDocumentType");
        const std::string name = to_str(args[0]);
        validate_name(r, name);
        auto type = std::make_shared<DocumentTypeNode>(name, to_str(args[1]), to_str(args[2]));
        type->set_owner_document(state.document);
        return wrap(r, type);
    });
    I.method("hasFeature", 0, [](Realm&, Args) { return boolean(true); });
}

// ---------------------------------------------------------------------------
// Document
// ---------------------------------------------------------------------------

void setup_document(Iface& I) {
    const auto doc = &is_document_node;

    I.node_getter(doc, "implementation", [](Realm& r, Node& self) {
        auto document = shared_document(self);
        return cached(wrap(r, self), "implementation", [&]() {
            auto state = std::make_shared<ImplementationState>();
            state->document = document;
            return r.make("DOMImplementation", state);
        });
    });
    I.node_getter(doc, "URL", [](Realm&, Node& self) { return str(as_document(self).url()); });
    I.node_getter(doc, "documentURI", [](Realm&, Node& self) { return str(as_document(self).url()); });
    I.node_getter(doc, "compatMode", [](Realm&, Node& self) {
        return str(as_document(self).quirks_mode() == QuirksMode::Quirks ? "BackCompat" : "CSS1Compat");
    });
    for (const char* name : {"characterSet", "charset", "inputEncoding"}) {
        I.node_getter(doc, name, [](Realm&, Node&) { return str("UTF-8"); });
    }
    I.node_getter(doc, "contentType", [](Realm&, Node&) { return str("text/html"); });
    I.node_getter(doc, "doctype", [](Realm& r, Node& self) { return wrap(r, as_document(self).doctype_node()); });
    I.node_getter(doc, "documentElement", [](Realm& r, Node& self) { return wrap(r, as_document(self).document_element()); });
    I.node_getter(doc, "head", [](Realm& r, Node& self) { return wrap(r, as_document(self).head()); });
    I.node_accessor(doc, "body",
        [](Realm& r, Node& self) { return wrap(r, as_document(self).body()); },
        [](Realm& r, Node& self, const JsValue& value) {
            Node* node = node_ptr(value);
            if (!node || !(is_html_element(*node, "body") || is_html_element(*node, "frameset"))) {
                throw_dom(r, "HierarchyRequestError", "The new body element must be a 'body' or 'frameset' element.");
            }
            auto root = as_document(self).document_element();
            if (!root) throw_dom(r, "HierarchyRequestError", "The document has no document element.");
            auto old = as_document(self).body();
            if (old.get() == node) return;
            auto keep = node->shared_from_this();
            if (old) {
                ChildListBatch batch(r, *root);
                auto next = old->next_sibling();
                auto previous = old->previous_sibling();
                root->remove_child(old);
                if (next) root->insert_before(keep, next);
                else root->append_child(keep);
                batch.commit({keep}, {old}, previous, next);
            } else {
                root->append_child(keep);
            }
        });
    I.node_accessor(doc, "title",
        [](Realm&, Node& self) {
            auto title = title_element(as_document(self));
            return str(title ? collapse_whitespace(title->text_content()) : std::string());
        },
        [](Realm& r, Node& self, const JsValue& value) {
            Document& document = as_document(self);
            auto title = title_element(document);
            if (!title) {
                auto head = document.head();
                if (!head) return;
                title = create_element(r, document, "title");
                head->append_child(title);
            }
            set_text_content(*title, to_str(value));
        });
    I.node_accessor(doc, "dir",
        [](Realm&, Node& self) {
            auto root = as_document(self).document_element();
            const std::string value = root ? ascii_lower(root->get_attribute("dir")) : std::string();
            return str(value == "ltr" || value == "rtl" || value == "auto" ? value : std::string());
        },
        [](Realm&, Node& self, const JsValue& value) {
            if (auto root = as_document(self).document_element()) root->set_attribute("dir", to_str(value));
        });
    I.node_getter(doc, "readyState", [](Realm&, Node& self) { return str(as_document(self).ready_state()); });
    I.node_getter(doc, "referrer", [](Realm&, Node&) { return str(""); });
    I.node_getter(doc, "lastModified", [](Realm&, Node&) {
        const std::time_t now = static_cast<std::time_t>(js_current_time_ms() / 1000);
        std::tm parts{};
        localtime_r(&now, &parts);
        return str(two_digits(parts.tm_mon + 1) + "/" + two_digits(parts.tm_mday) + "/" +
                   std::to_string(parts.tm_year + 1900) + " " + two_digits(parts.tm_hour) + ":" +
                   two_digits(parts.tm_min) + ":" + two_digits(parts.tm_sec));
    });
    I.node_accessor(doc, "cookie",
        [](Realm& r, Node& self) {
            if (&self != r.document.get()) return str("");
            return str(CookieJar::default_jar().get_document_cookie());
        },
        [](Realm& r, Node& self, const JsValue& value) {
            if (&self == r.document.get()) CookieJar::default_jar().set_document_cookie(to_str(value));
        });
    I.node_getter(doc, "defaultView", [](Realm& r, Node& self) {
        return &self == r.document.get() ? window_of(r) : null();
    });
    I.node_accessor(doc, "location",
        [](Realm& r, Node& self) {
            if (&self != r.document.get() || !r.window.is_object()) return null();
            return r.window.get_property("location");
        },
        [](Realm& r, Node& self, const JsValue& value) {
            if (&self == r.document.get()) navigate_to(r, value);
        });
    I.node_getter(doc, "currentScript", [](Realm& r, Node& self) { return wrap(r, as_document(self).current_script()); });
    I.node_getter(doc, "activeElement", [](Realm& r, Node& self) {
        Document& document = as_document(self);
        auto focused = document.focused_element();
        if (focused && focused->is_connected() && document_of(r, *focused).get() == &document) return wrap(r, focused);
        if (auto body = document.body()) return wrap(r, body);
        return wrap(r, document.document_element());
    });
    I.node_method(doc, "hasFocus", 0, [](Realm& r, Node& self, Args) { return boolean(&self == r.document.get()); });
    I.node_getter(doc, "hidden", [](Realm&, Node&) { return boolean(false); });
    I.node_getter(doc, "visibilityState", [](Realm&, Node&) { return str("visible"); });
    I.node_accessor(doc, "designMode",
        [](Realm&, Node&) { return str("off"); },
        [](Realm&, Node&, const JsValue&) {});
    I.node_getter(doc, "fullscreenElement", [](Realm&, Node&) { return null(); });
    I.node_getter(doc, "pointerLockElement", [](Realm&, Node&) { return null(); });
    I.node_getter(doc, "scrollingElement", [](Realm& r, Node& self) { return wrap(r, as_document(self).document_element()); });

    // --- Finding elements -----------------------------------------------------------
    I.node_method(doc, "getElementById", 1, [](Realm& r, Node& self, Args args) {
        require_args(args, 1, "Document.getElementById");
        const std::string id = to_str(args[0]);
        return wrap(r, id.empty() ? nullptr : self.descendant_with_id(id));
    });
    I.node_method(doc, "getElementsByName", 1, [](Realm& r, Node& self, Args args) {
        require_args(args, 1, "Document.getElementsByName");
        const std::string name = to_str(args[0]);
        return make_live_list(r, "NodeList", self.shared_from_this(), [name](Node& root, NodeVector& out) {
            for_each_descendant(root, [&](Node& node) {
                if (node.is_element()) {
                    const std::string* value = static_cast<Element&>(node).attribute("name");
                    if (value && *value == name) out.push_back(node.shared_from_this());
                }
                return true;
            });
        });
    });
    I.node_method(doc, "getElementsByTagName", 1, [](Realm& r, Node& self, Args args) {
        require_args(args, 1, "Document.getElementsByTagName");
        const std::string name = to_str(args[0]);
        const std::string lower = ascii_lower(name);
        return make_live_list(r, "HTMLCollection", self.shared_from_this(), [name, lower](Node& root, NodeVector& out) {
            for_each_descendant(root, [&](Node& node) {
                if (!node.is_element()) return true;
                const auto& element = static_cast<Element&>(node);
                if (name == "*" || element.tag_name() == (element.is_foreign() ? name : lower)) out.push_back(node.shared_from_this());
                return true;
            });
        });
    });
    I.node_method(doc, "getElementsByTagNameNS", 2, [](Realm& r, Node& self, Args args) {
        require_args(args, 2, "Document.getElementsByTagNameNS");
        const std::string ns = args[0].is_nullish() ? std::string() : to_str(args[0]);
        const std::string name = to_str(args[1]);
        return make_live_list(r, "HTMLCollection", self.shared_from_this(), [ns, name](Node& root, NodeVector& out) {
            const bool any = ns == "*";
            for_each_descendant(root, [&](Node& node) {
                if (!node.is_element()) return true;
                const auto& element = static_cast<Element&>(node);
                if ((any || namespace_uri_of(element) == ns) && (name == "*" || element.tag_name() == name)) {
                    out.push_back(node.shared_from_this());
                }
                return true;
            });
        });
    });
    I.node_method(doc, "getElementsByClassName", 1, [](Realm& r, Node& self, Args args) {
        require_args(args, 1, "Document.getElementsByClassName");
        const std::vector<std::string> wanted = split_tokens(to_str(args[0]));
        return make_live_list(r, "HTMLCollection", self.shared_from_this(), [wanted](Node& root, NodeVector& out) {
            if (wanted.empty()) return;
            for_each_descendant(root, [&](Node& node) {
                if (!node.is_element()) return true;
                const auto& element = static_cast<Element&>(node);
                for (const auto& name : wanted) {
                    if (!element.has_class(name)) return true;
                }
                out.push_back(node.shared_from_this());
                return true;
            });
        });
    });
    I.node_getter(doc, "forms", [](Realm& r, Node& self) {
        return document_collection(r, self, "forms", [](const Element& el) { return is_html_element(el, "form"); });
    });
    I.node_getter(doc, "images", [](Realm& r, Node& self) {
        return document_collection(r, self, "images", [](const Element& el) { return is_html_element(el, "img"); });
    });
    I.node_getter(doc, "scripts", [](Realm& r, Node& self) {
        return document_collection(r, self, "scripts", [](const Element& el) { return is_html_element(el, "script"); });
    });
    I.node_getter(doc, "links", [](Realm& r, Node& self) {
        return document_collection(r, self, "links", [](const Element& el) {
            return (is_html_element(el, "a") || is_html_element(el, "area")) && el.has_attribute("href");
        });
    });
    I.node_getter(doc, "anchors", [](Realm& r, Node& self) {
        return document_collection(r, self, "anchors", [](const Element& el) {
            return is_html_element(el, "a") && el.has_attribute("name");
        });
    });
    I.node_getter(doc, "embeds", [](Realm& r, Node& self) {
        return document_collection(r, self, "embeds", [](const Element& el) { return is_html_element(el, "embed"); });
    });
    I.alias("plugins", "embeds");
    I.node_getter(doc, "styleSheets", [](Realm& r, Node& self) {
        NodeVector sheets;
        for_each_descendant(self, [&](Node& node) {
            if (is_html_element(node, "style")) sheets.push_back(node.shared_from_this());
            return true;
        });
        std::vector<JsValue> out;
        for (const auto& node : sheets) {
            JsValue sheet = JsValue::make_object();
            sheet.set_property("ownerNode", wrap(r, node));
            sheet.set_property("disabled", boolean(false));
            sheet.set_property("type", str("text/css"));
            out.push_back(sheet);
        }
        return JsValue::make_array(out);
    });

    // --- Creating nodes -----------------------------------------------------------
    I.node_method(doc, "createElement", 1, [](Realm& r, Node& self, Args args) {
        require_args(args, 1, "Document.createElement");
        const std::string name = to_str(args[0]);
        validate_name(r, name);
        // createElement('button', {is: 'fancy-button'})
        std::string is;
        if (args.size() > 1 && args[1].is_object_like()) {
            const JsValue value = member(args[1], "is");
            if (!value.is_undefined()) is = to_str(value);
        }
        auto element = create_element(r, as_document(self), ascii_lower(name), ElementNamespace::HTML, true, is);
        JsValue object = wrap(r, element);
        // A script made by a script runs when it is put into the document;
        // one that came from markup assigned to innerHTML never does.
        if (element->tag_name() == "script") set_slot(object, "runnable", boolean(true));
        return object;
    });
    I.node_method(doc, "createElementNS", 2, [](Realm& r, Node& self, Args args) {
        require_args(args, 2, "Document.createElementNS");
        auto element = create_element_ns(r, as_document(self), args[0], to_str(args[1]));
        JsValue object = wrap(r, element);
        if (!element->is_foreign() && element->tag_name() == "script") set_slot(object, "runnable", boolean(true));
        return object;
    });
    I.node_method(doc, "createDocumentFragment", 0, [](Realm& r, Node& self, Args) {
        auto fragment = std::make_shared<Node>(NodeType::DocumentFragment);
        fragment->set_owner_document(shared_document(self));
        return wrap(r, fragment);
    });
    I.node_method(doc, "createTextNode", 1, [](Realm& r, Node& self, Args args) {
        require_args(args, 1, "Document.createTextNode");
        auto text = std::make_shared<Text>(to_str(args[0]));
        text->set_owner_document(shared_document(self));
        return wrap(r, text);
    });
    I.node_method(doc, "createComment", 1, [](Realm& r, Node& self, Args args) {
        require_args(args, 1, "Document.createComment");
        auto comment = std::make_shared<Comment>(to_str(args[0]));
        comment->set_owner_document(shared_document(self));
        return wrap(r, comment);
    });
    I.node_method(doc, "createCDATASection", 1, [](Realm& r, Node&, Args) -> JsValue {
        throw_dom(r, "NotSupportedError", "This operation is not supported for HTML documents.");
    });
    I.node_method(doc, "createAttribute", 1, [](Realm& r, Node&, Args args) {
        require_args(args, 1, "Document.createAttribute");
        const std::string name = to_str(args[0]);
        validate_name(r, name);
        return create_attribute(r, ascii_lower(name));
    });
    I.node_method(doc, "createAttributeNS", 2, [](Realm& r, Node&, Args args) {
        require_args(args, 2, "Document.createAttributeNS");
        const std::string name = to_str(args[1]);
        validate_name(r, name);
        return create_attribute(r, name);
    });
    I.node_method(doc, "createEvent", 1, [](Realm& r, Node&, Args args) {
        require_args(args, 1, "Document.createEvent");
        JsValue event = create_legacy_event(r, to_str(args[0]));
        if (event.is_undefined()) {
            throw_dom(r, "NotSupportedError", "The provided event type ('" + to_str(args[0]) + "') is invalid.");
        }
        return event;
    });
    I.node_method(doc, "importNode", 1, [](Realm& r, Node& self, Args args) {
        require_args(args, 1, "Document.importNode");
        Node& node = node_argument(args, 0, "Document.importNode");
        if (node.is_document()) throw_dom(r, "NotSupportedError", "The node provided is a document, which may not be imported.");
        if (dynamic_cast<ShadowRoot*>(&node)) throw_dom(r, "NotSupportedError", "The node provided is a shadow root, which may not be imported.");
        return wrap(r, clone_node(r, node, to_bool(js_arg(args, 1)), shared_document(self)));
    });
    I.node_method(doc, "adoptNode", 1, [](Realm& r, Node& self, Args args) {
        require_args(args, 1, "Document.adoptNode");
        Node& node = node_argument(args, 0, "Document.adoptNode");
        if (node.is_document()) throw_dom(r, "NotSupportedError", "The node provided is of type '#document', which may not be adopted.");
        if (dynamic_cast<ShadowRoot*>(&node)) throw_dom(r, "HierarchyRequestError", "The node provided is a shadow root, which may not be adopted.");
        auto keep = node.shared_from_this();
        adopt_node(r, node, shared_document(self));
        return args[0];
    });

    I.node_method(doc, "createRange", 0, [](Realm& r, Node& self, Args) {
        return create_range(r, self.shared_from_this());
    });
    I.node_method(doc, "createTreeWalker", 1, [](Realm& r, Node&, Args args) {
        require_args(args, 1, "Document.createTreeWalker");
        Node& root = node_argument(args, 0, "Document.createTreeWalker");
        const uint32_t show = js_arg(args, 1).is_undefined() ? 0xFFFFFFFFu : to_ulong(args[1]);
        return create_traversal(r, false, root.shared_from_this(), show, js_arg(args, 2));
    });
    I.node_method(doc, "createNodeIterator", 1, [](Realm& r, Node&, Args args) {
        require_args(args, 1, "Document.createNodeIterator");
        Node& root = node_argument(args, 0, "Document.createNodeIterator");
        const uint32_t show = js_arg(args, 1).is_undefined() ? 0xFFFFFFFFu : to_ulong(args[1]);
        return create_traversal(r, true, root.shared_from_this(), show, js_arg(args, 2));
    });

    // --- Writing into the parser: there is no parser to write into any more -------
    for (const char* name : {"open", "close", "write", "writeln", "clear", "captureEvents", "releaseEvents"}) {
        I.node_method(doc, name, 0, [](Realm&, Node&, Args) { return undefined(); });
    }
    for (const char* name : {"execCommand", "queryCommandEnabled", "queryCommandState", "queryCommandSupported"}) {
        I.node_method(doc, name, 1, [](Realm&, Node&, Args) { return boolean(false); });
    }
    I.node_method(doc, "elementFromPoint", 2, [](Realm&, Node&, Args) { return null(); });
    I.node_method(doc, "elementsFromPoint", 2, [](Realm&, Node&, Args) { return JsValue::make_array(); });
    I.node_method(doc, "getSelection", 0, [](Realm& r, Node& self, Args) {
        return &self == r.document.get() ? selection_object(r) : null();
    });

    define_parent_node(I, doc);
    define_event_handlers(I, false);
}

// ---------------------------------------------------------------------------
// DOMParser and XMLSerializer
// ---------------------------------------------------------------------------

JsValue construct_plain(Realm& realm, const InterfaceDef& def, Args, const JsValue* derived) {
    JsValue object = realm.make(def.name, nullptr, derived);
    set_slot(object, "brand", str(def.name));
    return object;
}

void require_brand(const char* name) {
    const JsValue* brand = slot(JsValue::current_this(), "brand");
    if (!brand || brand->to_string() != name) throw_illegal_invocation(name);
}

void setup_dom_parser(Iface& I) {
    I.method("parseFromString", 2, [](Realm& r, Args args) {
        require_brand("DOMParser");
        require_args(args, 2, "DOMParser.parseFromString");
        const std::string markup = to_str(args[0]);
        const std::string type = to_str(args[1]);
        static const char* const kTypes[] = {"text/html", "text/xml", "application/xml", "application/xhtml+xml", "image/svg+xml"};
        bool known = false;
        for (const char* candidate : kTypes) known = known || type == candidate;
        if (!known) {
            js_throw_type_error("Failed to execute 'parseFromString' on 'DOMParser': The provided value '" + type +
                                "' is not a valid enum value of type SupportedType.");
        }
        // Scripts in a document made this way never run.
        auto document = HtmlParser::parse(markup, false);
        document->set_url(r.document ? r.document->url() : std::string("about:blank"));
        document->set_ready_state("complete");
        set_owner_of_tree(document);
        return wrap(r, document);
    });
}

void setup_xml_serializer(Iface& I) {
    I.method("serializeToString", 1, [](Realm&, Args args) {
        require_brand("XMLSerializer");
        require_args(args, 1, "XMLSerializer.serializeToString");
        Node& node = node_argument(args, 0, "XMLSerializer.serializeToString");
        if (node.is_document()) {
            std::string out;
            for (const auto& child : script_children(node)) out += child->outer_markup(true);
            return str(out);
        }
        return str(node.outer_markup(true));
    });
}

JsValue construct_document(Realm& realm, const InterfaceDef&, Args, const JsValue*) {
    return wrap(realm, make_document(realm, false, nullptr));
}

} // namespace

std::shared_ptr<Document> make_document(Realm& realm, bool with_skeleton, const std::string* title) {
    auto document = std::make_shared<Document>();
    document->set_url("about:blank");
    document->set_ready_state("complete");
    if (with_skeleton) {
        Doctype type;
        type.present = true;
        type.name = "html";
        document->set_doctype(type);
        auto html = create_element(realm, *document, "html");
        auto head = create_element(realm, *document, "head");
        auto body = create_element(realm, *document, "body");
        document->append_child(html);
        html->append_child(head);
        if (title) {
            auto title_el = create_element(realm, *document, "title");
            head->append_child(title_el);
            if (!title->empty()) title_el->append_child(document->create_text_node(*title));
        }
        html->append_child(body);
    }
    return document;
}

namespace {

// `document.search` is the form called "search". The names are those of
// forms, images, embeds, objects and iframes, and the id of an object or of
// an image that also has a name. Not every id: that is the window's rule.
//
// One difference from browsers, on purpose: there a name shadows the
// document's own members, so `<form name="getElementById">` breaks the page.
// Here a name is only looked for when the document has no member of that
// name.
struct DocumentExotic : JsExotic {
    Realm* realm{nullptr};
    Document* document{nullptr};       // the object this is part of keeps it alive
    uint64_t version{~uint64_t{0}};
    std::unordered_map<std::string, std::weak_ptr<Element>> named;

    std::shared_ptr<Element> find(const std::string& name) {
        if (!realm->alive || name.empty() || name[0] == '%' || name[0] == '@') return nullptr;
        if (version != Node::tree_version()) {
            named.clear();
            for_each_descendant(*document, [&](Node& node) {
                if (!node.is_element()) return true;
                auto& element = static_cast<Element&>(node);
                if (element.is_foreign()) return true;
                const std::string& tag = element.tag_name();
                const bool object = tag == "object";
                const bool image = tag == "img";
                if (!object && !image && tag != "form" && tag != "embed" && tag != "iframe") return true;
                const std::string* name_attribute = element.attribute("name");
                const bool has_name = name_attribute && !name_attribute->empty();
                if (has_name) named.emplace(*name_attribute, shared(element));
                if (object || (image && has_name)) {
                    const std::string* id = element.attribute("id");
                    if (id && !id->empty()) named.emplace(*id, shared(element));
                }
                return true;
            });
            version = Node::tree_version();
        }
        auto found = named.find(name);
        return found == named.end() ? nullptr : found->second.lock();
    }
    bool get_last(JsObject*, const std::string& name, JsValue& out) override {
        auto element = find(name);
        if (!element) return false;
        out = wrap(*realm, element);
        return true;
    }
    bool has(JsObject*, const std::string& name) override { return find(name) != nullptr; }
};

} // namespace

std::shared_ptr<JsExotic> document_exotic(Realm& realm, Document& document) {
    auto exotic = std::make_shared<DocumentExotic>();
    exotic->realm = &realm;
    exotic->document = &document;
    return exotic;
}

std::shared_ptr<Element> create_element_ns(Realm& realm, Document& document, const JsValue& ns_value,
                                           const std::string& qualified) {
    validate_name(realm, qualified);
    const std::string uri = ns_value.is_nullish() ? std::string() : to_str(ns_value);
    const auto colon = qualified.find(':');
    const std::string prefix = colon == std::string::npos ? std::string() : qualified.substr(0, colon);
    const std::string local = colon == std::string::npos ? qualified : qualified.substr(colon + 1);
    static const char* const kXml = "http://www.w3.org/XML/1998/namespace";
    static const char* const kXmlns = "http://www.w3.org/2000/xmlns/";
    if (!prefix.empty() && uri.empty()) throw_dom(realm, "NamespaceError", "A prefix was given without a namespace.");
    if (prefix == "xml" && uri != kXml) throw_dom(realm, "NamespaceError", "The prefix 'xml' belongs to the XML namespace.");
    if ((qualified == "xmlns" || prefix == "xmlns") != (uri == kXmlns)) {
        throw_dom(realm, "NamespaceError", "'xmlns' and the XMLNS namespace go together.");
    }

    ElementNamespace ns = ElementNamespace::Other;
    namespace_from_uri(uri, ns);
    // The name is kept as written: in the HTML namespace too, where "DIV" is
    // not the div element.
    const bool plain_html = ns == ElementNamespace::HTML && local == ascii_lower(local) && prefix.empty();
    std::shared_ptr<Element> element;
    if (plain_html) {
        element = create_element(realm, document, local, ns);
    } else {
        element = create_element(realm, document, local, ns, false);
        element->set_tag_name_verbatim(local);
        element->set_prefix(prefix);
        if (ns == ElementNamespace::Other) element->set_other_namespace_uri(uri);
    }
    return element;
}

std::shared_ptr<Element> create_element(Realm& realm, Document& document, const std::string& local_name,
                                        ElementNamespace ns, bool synchronous_custom, const std::string& is) {
    bool failed = false;
    if (ns == ElementNamespace::HTML && synchronous_custom) {
        // A defined custom element is made by its own constructor.
        if (auto made = create_custom_element(realm, document, local_name, is, failed)) return made;
    }
    auto element = std::make_shared<Element>(local_name, ns);
    auto owner = std::static_pointer_cast<Document>(document.shared_from_this());
    element->set_owner_document(owner);
    if (ns == ElementNamespace::HTML) {
        if (element->tag_name() == "template") {
            auto contents = std::make_shared<Node>(NodeType::DocumentFragment);
            contents->set_owner_document(owner->template_document());
            element->set_template_contents(contents);
        }
        if (!is.empty()) element->set_is_value(is);
        // The constructor threw: a plain element that will not be tried again.
        if (failed) element->set_custom_state(CustomElementState::Failed);
    }
    return element;
}

void register_document_interfaces() {
    register_interface({"Document", "Node", setup_document, construct_document, 0, "HTMLDocument"});
    register_interface({"DOMImplementation", nullptr, setup_implementation, nullptr, 0, nullptr});
    register_interface({"DOMParser", nullptr, setup_dom_parser, construct_plain, 0, nullptr});
    register_interface({"XMLSerializer", nullptr, setup_xml_serializer, construct_plain, 0, nullptr});
}

} // namespace dom
} // namespace litebird
