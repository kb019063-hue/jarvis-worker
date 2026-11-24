// Phase 251, finding 255: label activation follows the HTML association.
TEST(a_label_activates_its_control_but_not_an_interactive_descendant) {
    using namespace litebird;

    auto page_url = [](const std::string& html) { return std::string("data:text/html,") + html; };
    auto click_center = [](BrowserShell& shell, const std::shared_ptr<Element>& element) {
        assert(element);
        const Rect bounds = element->bounding_box();
        assert(bounds.width > 0 && bounds.height > 0);
        const float x = bounds.x + bounds.width / 2;
        const float y = bounds.y + bounds.height / 2;
        assert(shell.element_at(x, y));
        return shell.click_at(x, y);
    };

    // A `for` label forwards to the control identified by that attribute, not
    // merely to any checkbox nearby. The pointer path must use the shell's real
    // hit test and the same activation behavior as a page click.
    {
        BrowserShell shell(400, 300);
        assert(shell.navigate_to(page_url(
            "<!doctype html><body style='margin:0'>"
            "<label id='label' for='second'>Toggle second</label> "
            "<input id='first' type='checkbox' style='width:20px;height:20px'>"
            "<input id='second' type='checkbox' style='width:20px;height:20px'>"
            "<script>window.log=[];"
            "label.addEventListener('click',function(){log.push('label')});"
            "second.addEventListener('click',function(){log.push('control:'+second.checked)});"
            "second.addEventListener('change',function(){log.push('change:'+second.checked)});"
            "</script></body>")));
        auto label = shell.document()->get_element_by_id("label");
        assert(click_center(shell, label));
        assert(shell.document()->get_element_by_id("first")->checked() == false);
        assert(shell.document()->get_element_by_id("second")->checked());
        assert(shell.js_engine()->eval("log.join(',')").to_string() ==
               "label,control:true,change:true");
    }

    // The interactive-content exception is independent of whether that
    // descendant itself has a default action. A type=button has none, but is
    // still interactive: clicking it must not activate the label's checkbox.
    {
        BrowserShell shell(400, 300);
        assert(shell.navigate_to(page_url(
            "<!doctype html><body style='margin:0'>"
            "<label id='label' for='check' style='display:block;width:180px'>"
            "Choose <button id='button' type='button' style='display:block;width:80px;height:24px'>Help</button>"
            "</label>"
            "<input id='check' type='checkbox' style='width:20px;height:20px'>"
            "</body>")));
        auto button = shell.document()->get_element_by_id("button");
        const Rect bounds = button->bounding_box();
        assert(bounds.width > 0 && bounds.height > 0);
        const float x = bounds.x + bounds.width / 2;
        const float y = bounds.y + bounds.height / 2;
        auto hit = shell.element_at(x, y);
        assert(hit == button);
        assert(shell.click_at(x, y));
        assert(!shell.document()->get_element_by_id("check")->checked());
    }

    // The implicit association uses the first labelable descendant. Clicking
    // the label text, rather than the control itself, activates that control.
    {
        BrowserShell shell(400, 300);
        assert(shell.navigate_to(page_url(
            "<!doctype html><body style='margin:0'>"
            "<label id='label'>Choose one<br>"
            "<input id='check' type='checkbox' style='width:20px;height:20px'>"
            "</label></body>")));
        auto label = shell.document()->get_element_by_id("label");
        const Rect bounds = label->bounding_box();
        assert(bounds.width > 0 && bounds.height > 0);
        const float x = bounds.x + 2;
        const float y = bounds.y + 2;
        auto hit = shell.element_at(x, y);
        assert(hit == label);
        assert(shell.click_at(x, y));
        auto control = shell.document()->get_element_by_id("check");
        assert(control->checked());
        // Clicking the control itself already activates it; the enclosing
        // label must not cause a second click or toggle it back.
        assert(click_center(shell, control));
        assert(!control->checked());
    }

    // A disabled labeled control receives no forwarded activation.
    {
        BrowserShell shell(400, 300);
        assert(shell.navigate_to(page_url(
            "<!doctype html><body style='margin:0'>"
            "<label id='label' for='check'>Disabled choice</label>"
            "<input id='check' type='checkbox' disabled style='width:20px;height:20px'>"
            "<script>window.clicks=0;"
            "check.addEventListener('click',function(){clicks++});"
            "</script></body>")));
        assert(click_center(shell, shell.document()->get_element_by_id("label")));
        assert(!shell.document()->get_element_by_id("check")->checked());
        assert(shell.js_engine()->eval("String(clicks)").to_string() == "0");
    }

    // The TodoMVC template's adjacent label has no `for` and contains no
    // control. It therefore has no labeled control to activate; do not invent
    // an association to whichever control happens to be nearby.
    {
        BrowserShell shell(400, 300);
        assert(shell.navigate_to(page_url(
            "<!doctype html><body style='margin:0'>"
            "<input id='check' type='checkbox' style='width:20px;height:20px'>"
            "<label id='label'>An unassociated label</label>"
            "</body>")));
        assert(click_center(shell, shell.document()->get_element_by_id("label")));
        assert(!shell.document()->get_element_by_id("check")->checked());
    }
}
