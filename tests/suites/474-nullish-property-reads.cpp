// Phase 474: ordinary property reads on null and undefined are TypeErrors.
TEST(the_bytecode_vm_rejects_nullish_property_reads) {
    using namespace litebird;
    auto agree = [](const char* source, const char* expected) {
        TestPage tree;
        TestPage bytecode;
        assert(tree.js.eval(source).to_string() == expected);
        assert(bytecode.js.eval_bytecode(source).to_string() == expected);
    };

    // The original disagreement: catch sees a TypeError, not undefined.
    agree("try { null.foo; } catch (e) { e.name; }", "TypeError");
    agree("try { undefined.foo; } catch (e) { e.name; }", "TypeError");
    agree("try { null['foo']; } catch (e) { e.name; }", "TypeError");
    agree("try { undefined['foo']; } catch (e) { e.name; }", "TypeError");
    agree("try { null.foo; } catch (e) { e.message; }", "Cannot read properties of null (reading 'foo')");
    agree("try { undefined['foo']; } catch (e) { e.message; }", "Cannot read properties of undefined (reading 'foo')");
    agree("try { typeof null.foo; } catch (e) { e.name; }", "TypeError");
    agree("try { null.foo(); } catch (e) { e.name; }", "TypeError");
    agree("var o = {foo: 7}; String(o.foo) + '/' + String(o['foo'])", "7/7");
    agree("String('abc'.length) + '/' + String('abc'[1])", "3/b");
    agree("String(({x:undefined}).x)", "undefined");
    // The key expression itself runs before the nullish-base error.
    agree("var s=''; try { null[(s += 'k')]; } catch(e) { s += e.name; } String(s)", "kTypeError");
    // The exception crosses frames and is catchable, rather than becoming a
    // missing-value answer or a host exception.
    agree("function f(){return null.foo;} try{f()}catch(e){e.name}", "TypeError");

    // Node evaluates the key expression, but does NOT convert an object key
    // to a property key when the base is nullish. The tree-walker currently
    // converts it too soon, so this asserts the VM against Node separately.
    TestPage page;
    assert(page.js.eval_bytecode("var s=''; var key={toString:function(){s+='c';return 'x'}}; try{null[key]}catch(e){s+=e.name}String(s)").to_string() == "TypeError");

    // An uncaught read escapes as an actual JavaScript throw.
    TestPage uncaught;
    bool threw = false;
    try { uncaught.js.eval_bytecode("null.foo;"); }
    catch (const JsThrow& error) {
        threw = true;
        assert(js_describe_thrown(error.value) == "TypeError: Cannot read properties of null (reading 'foo')");
    }
    assert(threw);
}
