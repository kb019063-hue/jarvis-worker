// ArrayBuffer.prototype.transferToImmutable moves bytes into a fixed immutable buffer.
// Semantics follow the TC39 Immutable ArrayBuffers proposal (stage 2.7, Nov 2026).

TEST(arraybuffer_transfer_to_immutable_detaches_and_freezes_bytes) {
    using namespace litebird;
    TestPage page;
    JsInterpreter& js = page.js;
    auto run = [&](const char* source) { return js.eval(source).to_string(); };

    assert(run("typeof ArrayBuffer.prototype.transferToImmutable") == "function");
    assert(run("ArrayBuffer.prototype.transferToImmutable.length") == "0");
    assert(run("new ArrayBuffer(1).immutable") == "false");

    // A longer immutable result is zero-filled, never resizable, and detaches
    // the original. The bytes remain visible through every new view.
    assert(run("(function(){var b=new ArrayBuffer(3,{maxByteLength:8}); var u=new Uint8Array(b); u.set([11,22,33]); var i=b.transferToImmutable(5); return [b.detached,b.byteLength,i.immutable,i.resizable,i.byteLength,i.maxByteLength,new Uint8Array(i).join(',')].join('|');})()")
           == "true|0|true|false|5|5|11,22,33,0,0");

    // A shorter result keeps only its prefix. Omitted length preserves size.
    assert(run("(function(){var b=new ArrayBuffer(3); new Uint8Array(b).set([8,9,10]); var i=b.transferToImmutable(2); return new Uint8Array(i).join(',')+'|'+i.immutable+'|'+b.detached;})()")
           == "8,9|true|true");
    assert(run("(function(){var b=new ArrayBuffer(2); var i=b.transferToImmutable(); return i.byteLength+'|'+i.maxByteLength+'|'+i.immutable;})()")
           == "2|2|true");

    // Indexed writes are refused (silently in sloppy code, TypeError in strict
    // code); immutable typed-array indices also report non-writable descriptors.
    assert(run("(function(){var b=new ArrayBuffer(2); new Uint8Array(b).set([4,5]); var i=b.transferToImmutable(); var u=new Uint8Array(i); u[0]=99; var sloppy=u[0]; var strict; try{(function(){'use strict';u[0]=99;})()}catch(e){strict=e.name} var d=Object.getOwnPropertyDescriptor(u,'0'); return [sloppy,strict,d.writable,d.configurable].join('|');})()")
           == "4|TypeError|false|false");
    // Defining an identical value or empty descriptor is harmless; changing
    // that fixed element or re-enabling writability is forbidden.
    assert(run("(function(){var b=new ArrayBuffer(1); new Uint8Array(b)[0]=4; var u=new Uint8Array(b.transferToImmutable()); var a,bv,c; try{Object.defineProperty(u,'0',{});a='ok'}catch(e){a=e.name} try{Object.defineProperty(u,'0',{value:4});bv='ok'}catch(e){bv=e.name} try{Object.defineProperty(u,'0',{value:5});c='ok'}catch(e){c=e.name} return [u[0],a,bv,c].join('|');})()")
           == "4|ok|ok|TypeError");

    // All ordinary mutating typed-array operations and DataView setters refuse
    // the write; reads and freezing the view remain valid.
    assert(run("(function(){var b=new ArrayBuffer(3); new Uint8Array(b).set([3,2,1]); var i=b.transferToImmutable(); var u=new Uint8Array(i); var out=[]; for(var m of ['set','fill','copyWithin','reverse','sort']){try{if(m==='set')u.set([9]);else if(m==='copyWithin')u.copyWithin(0,1);else u[m]();out.push('ok')}catch(e){out.push(e.name)}} var dv=new DataView(i); try{dv.setUint8(0,9);out.push('ok')}catch(e){out.push(e.name)} var converted=false; try{dv.setUint8({valueOf:function(){converted=true;return 0}},9)}catch(e){} try{new TextEncoder().encodeInto('z',u);out.push('ok')}catch(e){out.push(e.name)} var frozen=Object.freeze(u); return out.join(',')+'|'+u.join(',')+'|'+Object.isFrozen(frozen)+'|'+converted;})()")
           == "TypeError,TypeError,TypeError,TypeError,TypeError,TypeError,TypeError|3,2,1|true|false");

    // An immutable buffer cannot be resized or transferred/detached again.
    assert(run("(function(){var i=new ArrayBuffer(1).transferToImmutable(); var out=[]; for(var m of ['resize','transfer','transferToFixedLength','transferToImmutable']){try{if(m==='resize')i.resize(2);else i[m]();out.push('ok')}catch(e){out.push(e.name)}} return out.join(',')+'|'+i.detached+'|'+i.byteLength;})()")
           == "TypeError,TypeError,TypeError,TypeError|false|1");

    assert(run("(function(){try{ArrayBuffer.prototype.transferToImmutable.call({})}catch(e){return e.name}})()") == "TypeError");
    assert(run("(function(){try{new ArrayBuffer(2).transferToImmutable(-1)}catch(e){return e.name}})()") == "RangeError");
}
