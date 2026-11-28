// Phase 544, finding 544: Object built-ins array-index, ToPropertyKey and prototype invariants.
TEST(object_defineproperty_and_prototype_invariants) {
    TestPage page;
    auto must = [&](const char* src, const char* expected) {
        JsValue r = page.js.eval(src);
        std::string got = r.to_string();
        if (got != expected) {
            std::cerr << "FAIL: " << src << "\n  expected: " << expected << "\n  got:      " << got << "\n";
        }
        assert(got == expected);
    };
    // 4294967295 is NOT an array index
    must("var a=[]; Object.defineProperty(a,'4294967295',{value:100}); JSON.stringify([a.hasOwnProperty('4294967295'), a.length, a['4294967295']])", "[true,0,100]");
    // 4294967294 IS max array index, no bad_alloc, length 4294967295
    must("var a=[]; Object.defineProperty(a,'4294967294',{value:7}); JSON.stringify([a.length, a['4294967294']])", "[4294967295,7]");
    // length growth on defineProperty past the end
    must("var arr=[]; arr.length=3; Object.defineProperties(arr,{'3':{value:26}}); JSON.stringify([arr.length, arr[3]])", "[4,26]");
    // plain defaults: new index with only value should be non-writable
    must("var a=[]; Object.defineProperty(a,'0',{value:9}); var d=Object.getOwnPropertyDescriptor(a,'0'); JSON.stringify(d.writable)", "false");
    // existing index keeps writability when descriptor omits it
    must("var a=[1]; var d0=Object.getOwnPropertyDescriptor(a,'0'); Object.defineProperty(a,'0',{value:9}); var d=Object.getOwnPropertyDescriptor(a,'0'); JSON.stringify([d0.writable, d.writable, a[0]])", "[true,true,9]");
    // Symbol ToPropertyKey via @@toPrimitive
    must("var obj={}; var sym=Symbol(); obj[sym]=1; var w={}; w[Symbol.toPrimitive]=function(){return sym;}; JSON.stringify(Object.hasOwn(obj,w))", "true");
    // ToObject before ToPropertyKey: null with throwing key must throw TypeError, not evil
    must("var c=0; var evil={get toString(){c++; throw new Error('evil')}}; try{ Object.hasOwn(null, evil); }catch(e){ JSON.stringify([e.name, c]) }", "[\"TypeError\",0]");
    // hasOwn null/undefined throw
    must("var a=0; try{ Object.hasOwn(null,'x')}catch(e){ a+=1; } try{ Object.hasOwn(undefined,'x')}catch(e){ a+=10; } JSON.stringify(a)", "11");
    // getOwnPropertySymbols nullish throw
    must("var a=0; try{ Object.getOwnPropertySymbols(null)}catch(e){ a+=1; } try{ Object.getOwnPropertySymbols(undefined)}catch(e){ a+=10; } JSON.stringify(a)", "11");
    // getOwnPropertyDescriptors nullish throw
    must("var a=0; try{ Object.getOwnPropertyDescriptors(null)}catch(e){ a+=1; } JSON.stringify(a)", "1");
    // groupBy non-callable throw
    must("try{ Object.groupBy([1], null); }catch(e){ e.name }", "TypeError");
    // immutable Object.prototype
    must("JSON.stringify([Reflect.setPrototypeOf(Object.prototype, Object.create(null)), Reflect.setPrototypeOf(Object.prototype, null), Object.setPrototypeOf(Object.prototype, null)===Object.prototype])", "[false,true,true]");
    must("try{ Object.setPrototypeOf(Object.prototype, Object.create(null)); 'no' }catch(e){ e.name }", "TypeError");
    // Object.create second arg ToObject
    must("try{ Object.create(null, null); 'no' }catch(e){ e.name }", "TypeError");
}
