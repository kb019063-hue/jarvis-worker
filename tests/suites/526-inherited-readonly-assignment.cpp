// Phase 526, finding 526: An inherited read-only property stops an assignment.
TEST(inherited_readonly_property_stops_assignment) {
    TestPage page;
    auto must = [&](const char* src, const char* expected) {
        JsValue result = page.js.eval(src);
        std::string got = result.to_string();
        if (got != expected) {
            std::cerr << "FAIL: " << src << "\n  expected: " << expected << "\n  got:      " << got << "\n";
        }
        assert(got == expected);
    };

    // Base case from prototypes.dat: plain object with non-writable prototype property
    must("var p={};Object.defineProperty(p,'ro',{value:1});var o=Object.create(p);o.ro=2;String(o.ro)+','+o.hasOwnProperty('ro')", "1,false");
    must("var p={};Object.defineProperty(p,'ro',{value:1});var o=Object.create(p);var r=(function(){'use strict';try{o.ro=2;return 'assigned';}catch(e){return e.name}})();String(r)+','+String(o.ro)+','+o.hasOwnProperty('ro')", "TypeError,1,false");
    // writable prototype should allow shadowing
    must("var p={};Object.defineProperty(p,'ro',{value:1,writable:true});var o=Object.create(p);o.ro=2;String(o.ro)+','+o.hasOwnProperty('ro')+','+String(p.ro)", "2,true,1");
    // simple prototype with writable data (normal property) should shadow
    must("var p={ro:1};var o=Object.create(p);o.ro=2;String(o.ro)+','+o.hasOwnProperty('ro')+','+String(p.ro)", "2,true,1");
    // accessor with setter should invoke setter on receiver
    must("var p={};var log='';Object.defineProperty(p,'x',{get(){return 1},set(v){log='setter:'+v}});var o=Object.create(p);o.x=2;String(log)+','+String(o.x)+','+o.hasOwnProperty('x')", "setter:2,1,false");
    // accessor without setter should refuse
    must("var p={};Object.defineProperty(p,'x',{get(){return 1}});var o=Object.create(p);o.x=2;String(o.x)+','+o.hasOwnProperty('x')", "1,false");
    must("var p={};Object.defineProperty(p,'x',{get(){return 1}});var o=Object.create(p);var r=(function(){'use strict';try{o.x=2;return 'assigned';}catch(e){return e.name}})();String(r)", "TypeError");
    // two-level chain
    must("var a={};Object.defineProperty(a,'ro',{value:1});var b=Object.create(a);var c=Object.create(b);c.ro=2;String(c.ro)+','+c.hasOwnProperty('ro')", "1,false");
    must("var a={};Object.defineProperty(a,'ro',{value:1,writable:true});var b=Object.create(a);var c=Object.create(b);c.ro=2;String(c.ro)+','+c.hasOwnProperty('ro')", "2,true");
    // class inheritance
    must("class A{}Object.defineProperty(A,'ro',{value:1,writable:false});class B extends A{}B.ro=2;String(B.ro)+','+B.hasOwnProperty('ro')", "1,false");
    must("class A{}Object.defineProperty(A,'ro',{value:1,writable:false});class B extends A{}var r=(function(){'use strict';try{B.ro=2;return 'a';}catch(e){return e.name}})();String(r)+','+String(B.ro)", "TypeError,1");
    must("class A{}Object.defineProperty(A,'ro',{value:1,writable:true});class B extends A{}B.ro=2;String(B.ro)+','+B.hasOwnProperty('ro')", "2,true");
    // own property writable false should still refuse (not prototype but own)
    must("var o={};Object.defineProperty(o,'ro',{value:1,writable:false});o.ro=2;String(o.ro)", "1");
    // ensure non-existent prototype still creates own
    must("var p={};var o=Object.create(p);o.x=5;String(o.x)+','+o.hasOwnProperty('x')", "5,true");
}
