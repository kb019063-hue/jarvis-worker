// Phase 508, finding 508: Uint8Array base64/hex proposal (tc39.es/proposal-arraybuffer-base64).
TEST(uint8array_base64_hex) {
    TestPage page;
    JsValue result = page.js.eval(R"JS(
        (function(){
            var failures=[];
            function check(name, cond){ if(!cond) failures.push(name); }
            function throws(name, fn, kind){
                try{ fn(); failures.push(name+":no-throw"); }catch(e){ if(kind && !(e instanceof kind)) failures.push(name+":wrong-kind "+e.name); }
            }
            // existence
            check("toHex exists", typeof Uint8Array.prototype.toHex==="function");
            check("toBase64 exists", typeof Uint8Array.prototype.toBase64==="function");
            check("fromHex exists", typeof Uint8Array.fromHex==="function");
            check("fromBase64 exists", typeof Uint8Array.fromBase64==="function");
            check("setFromHex exists", typeof Uint8Array.prototype.setFromHex==="function");
            check("setFromBase64 exists", typeof Uint8Array.prototype.setFromBase64==="function");

            // toHex basics
            check("toHex hello", new Uint8Array([72,101,108,108,111]).toHex()==="48656c6c6f");
            check("toHex empty", new Uint8Array([]).toHex()==="");
            check("toHex ff", new Uint8Array([255]).toHex()==="ff");

            // fromHex round-trip
            check("fromHex hello", Uint8Array.fromHex("48656c6c6f").join(",")==="72,101,108,108,111");
            check("fromHex upper", Uint8Array.fromHex("48656C6C6F").join(",")==="72,101,108,108,111");
            check("fromHex empty", Uint8Array.fromHex("").length===0);
            throws("fromHex odd", function(){ Uint8Array.fromHex("abc"); }, SyntaxError);
            throws("fromHex bad digit", function(){ Uint8Array.fromHex("GG"); }, SyntaxError);
            throws("fromHex non-string", function(){ Uint8Array.fromHex(123); }, TypeError);

            // toBase64 basics
            check("toBase64 hello", new Uint8Array([72,101,108,108,111]).toBase64()==="SGVsbG8=");
            check("toBase64 omitPadding", new Uint8Array([72,101,108,108,111]).toBase64({omitPadding:true})==="SGVsbG8");
            check("toBase64 empty", new Uint8Array([]).toBase64()==="");
            check("toBase64 1 byte", new Uint8Array([0]).toBase64()==="AA==");
            check("toBase64 1 byte omit", new Uint8Array([0]).toBase64({omitPadding:true})==="AA");
            check("toBase64 2 bytes", new Uint8Array([255,255]).toBase64()==="//8=");
            throws("toBase64 bad alphabet", function(){ new Uint8Array([1]).toBase64({alphabet:"bad"}); }, TypeError);
            throws("toBase64 options not object", function(){ new Uint8Array([1]).toBase64("bad"); }, TypeError);

            // fromBase64 basics
            check("fromBase64 hello", Uint8Array.fromBase64("SGVsbG8=").join(",")==="72,101,108,108,111");
            check("fromBase64 omitPadding", Uint8Array.fromBase64("SGVsbG8").join(",")==="72,101,108,108,111");
            check("fromBase64 whitespace", Uint8Array.fromBase64(" S G V s b G 8 = ").join(",")==="72,101,108,108,111");
            check("fromBase64 empty", Uint8Array.fromBase64("").length===0);
            throws("fromBase64 non-string", function(){ Uint8Array.fromBase64(123); }, TypeError);
            throws("fromBase64 bad alphabet", function(){ Uint8Array.fromBase64("AB==",{alphabet:"bad"}); }, TypeError);
            throws("fromBase64 bad lastChunk", function(){ Uint8Array.fromBase64("AB==",{lastChunkHandling:"bad"}); }, TypeError);

            // base64url
            var b = new Uint8Array([251,255,191]);
            check("toBase64 base64", b.toBase64()==="+/+/");
            check("toBase64 base64url", b.toBase64({alphabet:"base64url"})==="-_-_");
            check("fromBase64 base64url roundtrip", Uint8Array.fromBase64(b.toBase64({alphabet:"base64url"}),{alphabet:"base64url"}).join(",")==="251,255,191");
            throws("fromBase64 base64url rejects plus", function(){ Uint8Array.fromBase64("ab/c",{alphabet:"base64url"}); }, SyntaxError);
            check("fromBase64 base64url dash", Uint8Array.fromBase64("-_-_",{alphabet:"base64url"}).join(",")==="251,255,191");

            // lastChunkHandling
            throws("strict rejects single char", function(){ Uint8Array.fromBase64("A",{lastChunkHandling:"strict"}); }, SyntaxError);
            check("loose decodes 2-char", Uint8Array.fromBase64("QQ",{lastChunkHandling:"loose"}).join(",")==="65");
            check("stop-before-partial drops", Uint8Array.fromBase64("SGVsbG8",{lastChunkHandling:"stop-before-partial"}).length===3);
            throws("strict extra bits", function(){ Uint8Array.fromBase64("AB==",{lastChunkHandling:"strict"}); }, SyntaxError);

            // setFromHex
            var dst=new Uint8Array(5);
            var r=dst.setFromHex("48656c6c6f");
            check("setFromHex read", r.read===10);
            check("setFromHex written", r.written===5);
            check("setFromHex bytes", dst.join(",")==="72,101,108,108,111");
            var dst2=new Uint8Array(2);
            var r2=dst2.setFromHex("aabbcc");
            check("setFromHex trunc read", r2.read===4);
            check("setFromHex trunc written", r2.written===2);
            check("setFromHex trunc bytes", dst2.join(",")==="170,187");
            throws("setFromHex odd", function(){ new Uint8Array(2).setFromHex("abc"); }, SyntaxError);
            throws("setFromHex non-string", function(){ new Uint8Array(2).setFromHex(123); }, TypeError);

            // setFromBase64
            var dst3=new Uint8Array(5);
            var r3=dst3.setFromBase64("SGVsbG8=");
            check("setFromBase64 read", r3.read===8);
            check("setFromBase64 written", r3.written===5);
            check("setFromBase64 bytes", dst3.join(",")==="72,101,108,108,111");
            var dst4=new Uint8Array(2);
            var r4=dst4.setFromBase64("AQIDBA==");
            check("setFromBase64 trunc", r4.written===0 || r4.written===2);
            throws("setFromBase64 non-string", function(){ new Uint8Array(2).setFromBase64(123); }, TypeError);

            // brand checks
            throws("toHex on Int8Array", function(){ Uint8Array.prototype.toHex.call(new Int8Array([1])); }, TypeError);
            throws("toBase64 on non-Uint8Array", function(){ Uint8Array.prototype.toBase64.call(new Int8Array([1])); }, TypeError);

            return failures.length ? failures.join("\n") : "ok";
        })()
    )JS");
    assert(!page.js.last_eval_failed());
    if (result.to_string() != "ok") {
        std::fprintf(stderr, "uint8array_base64_hex failures:\n%s\n", result.to_string().c_str());
    }
    assert(result.to_string() == "ok");
}
