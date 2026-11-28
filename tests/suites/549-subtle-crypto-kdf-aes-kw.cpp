TEST(subtle_crypto_kdf_derive_aes_kw) {
    auto doc = HtmlParser::parse("<!doctype html><html><body></body></html>");
    JsInterpreter interp;
    interp.set_document(doc);

    interp.eval(R"(
        var ok = null;
        var pw = new Uint8Array([1, 2, 3, 4, 5, 6, 7, 8]);
        var salt = new Uint8Array([9, 10, 11, 12]);
        Promise.all([
            crypto.subtle.importKey('raw', pw, 'PBKDF2', false, ['deriveKey']),
            crypto.subtle.generateKey({ name: 'AES-GCM', length: 128 }, true, ['encrypt'])
        ]).then(function(res) {
            var base = res[0], target = res[1];
            return crypto.subtle.deriveKey(
                { name: 'PBKDF2', salt: salt, iterations: 10, hash: 'SHA-256' },
                base,
                { name: 'AES-KW', length: 128 },
                false,
                ['wrapKey', 'unwrapKey']
            ).then(function(kek) {
                return crypto.subtle.wrapKey('raw', target, kek, { name: 'AES-KW' })
                    .then(function(wrapped) {
                        return crypto.subtle.unwrapKey('raw', wrapped, kek, { name: 'AES-KW' }, 'AES-GCM', true, ['encrypt']);
                    });
            });
        }).then(function(unwrapped) {
            ok = (unwrapped instanceof CryptoKey && unwrapped.algorithm.name === 'AES-GCM' && unwrapped.algorithm.length === 128);
        });
    )");
    assert(interp.eval("ok === true").is_truthy());
}
