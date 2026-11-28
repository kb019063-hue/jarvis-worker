// Phase 530, finding 530: delete preserves the optional chain's reference.
TEST(optional_delete_removes_the_property_or_short_circuits) {
    auto answer = [](const char* source, const char* expected) {
        TestPage page;
        assert(page.js.eval(source).to_string() == expected);
    };
    // Do not read the last property: delete acts on the reference itself.
    answer("var o={x:1};String(delete o?.x)+'/'+String('x' in o)", "true/false");
    answer("var o={x:1};String(delete o?.['x'])+'/'+String('x' in o)", "true/false");
    answer("var s='',o={};Object.defineProperty(o,'x',{get:function(){s+='g';return 1},configurable:true});var r=delete o?.x;String(r)+'/'+s+'/'+String('x' in o)", "true//false");
    answer("var s='',o={x:1};var k={toString:function(){s+='c';return 'x'}};var r=delete o?.[(s+='k',k)];String(r)+'/'+s+'/'+String('x' in o)", "true/kc/false");
    // The chain short-circuits before keys, calls, or following links run.
    answer("var s='';var r=delete null?.[(s+='k')];String(r)+'/'+s", "true/");
    answer("var s='';var r=delete undefined?.x[(s+='k')].y;String(r)+'/'+s", "true/");
    answer("var s='',o={x:null};var r=delete o?.x?.[(s+='k')];String(r)+'/'+s", "true/");
    answer("var s='',o=null;var r=delete o?.m(s+='a').x;String(r)+'/'+s", "true/");
    // A non-optional link is not rescued merely because an earlier one was
    // optional, and grouping ends an earlier optional chain.
    answer("var s='',o={x:null};try{delete o?.x[(s+='k')]}catch(e){s+=e.name}String(s)", "kTypeError");
    answer("var s='';try{delete (null?.x)[(s+='k')]}catch(e){s+=e.name}String(s)", "kTypeError");
    answer("var o={x:{y:1}};String(delete o?.x.y)+'/'+String('y' in o.x)", "true/false");
    answer("var o={x:1};String(delete (o?.x))+'/'+String('x' in o)", "true/false");
    answer("var s='',o={v:{x:1},m:function(){s+='m';return this.v}};String(delete o?.m().x)+'/'+s+'/'+String('x' in o.v)", "true/m/false");
    answer("var s='',o={m:null};String(delete o.m?.(s+='a').x)+'/'+s", "true/");
    answer("var s='',o={x:1};String(delete o?.[(null?.z,s+='k','x')])+'/'+s+'/'+String('x' in o)", "true/k/false");
    // A chain ending in a call is not a property reference.
    answer("var s='',o={m:function(){s+='m';return 7}};String(delete o?.m())+'/'+s", "true/m");
    // Existing deletion semantics and strict-mode failure remain intact.
    answer("var o={};Object.defineProperty(o,'x',{value:1,configurable:false});String(delete o?.x)+'/'+String('x' in o)", "false/true");
    answer("'use strict';var o={};Object.defineProperty(o,'x',{value:1,configurable:false});try{delete o?.x}catch(e){e.name}", "TypeError");
    answer("var p={x:1},o=Object.create(p);String(delete o?.x)+'/'+String(o.x)", "true/1");
    answer("var sym=Symbol('x'),o={};o[sym]=1;String(delete o?.[sym])+'/'+String(sym in o)", "true/false");
}
