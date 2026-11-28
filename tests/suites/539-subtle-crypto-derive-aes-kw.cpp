TEST(subtle_crypto_derive_aes_kw) {
    auto doc = HtmlParser::parse("<!doctype html><html><body></body></html>");
    JsInterpreter interp;
    interp.set_document(doc);

    interp.eval(R"(
        var ok = null;
        Promise.all([
            crypto.subtle.generateKey({ name: 'ECDH', namedCurve: 'P-256' }, true, ['deriveKey']),
            crypto.subtle.generateKey({ name: 'ECDH', namedCurve: 'P-256' }, true, ['deriveKey']),
            crypto.subtle.generateKey({ name: 'AES-GCM', length: 128 }, true, ['encrypt', 'decrypt'])
        ]).then(function(res) {
            var a = res[0], b = res[1], target = res[2];
            return Promise.all([
                crypto.subtle.deriveKey({ name: 'ECDH', public: b.publicKey }, a.privateKey, { name: 'AES-KW', length: 256 }, false, ['wrapKey']),
                crypto.subtle.deriveKey({ name: 'ECDH', public: a.publicKey }, b.privateKey, { name: 'AES-KW', length: 256 }, false, ['unwrapKey'])
            ]).then(function(keks) {
                return crypto.subtle.wrapKey('raw', target, keks[0], { name: 'AES-KW' })
                    .then(function(wrapped) {
                        return crypto.subtle.unwrapKey('raw', wrapped, keks[1], { name: 'AES-KW' }, 'AES-GCM', true, ['encrypt']);
                    });
            });
        }).then(function(unwrapped) {
            ok = (unwrapped instanceof CryptoKey && unwrapped.algorithm.name === 'AES-GCM' && unwrapped.algorithm.length === 128);
        });
    )");
    assert(interp.eval("ok === true").is_truthy());
}
