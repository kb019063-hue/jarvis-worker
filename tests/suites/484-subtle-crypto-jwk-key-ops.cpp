TEST(subtle_crypto_jwk_key_ops_and_ext) {
    auto doc = HtmlParser::parse("<!doctype html><html><body></body></html>");
    JsInterpreter interp;
    interp.set_document(doc);

    interp.eval(R"(
        var ops = null, extErr = null;
        crypto.subtle.generateKey({ name: 'AES-GCM', length: 128 }, true, ['encrypt', 'decrypt'])
            .then(function(k) {
                return crypto.subtle.exportKey('jwk', k);
            })
            .then(function(jwk) {
                ops = jwk.key_ops;
                return crypto.subtle.importKey('jwk', { kty: 'oct', k: jwk.k, ext: false }, 'AES-GCM', true, ['encrypt']);
            })
            .catch(function(e) {
                extErr = e.name;
            });
    )");
    assert(interp.eval("Array.isArray(ops) && ops.length === 2 && ops[0] === 'encrypt' && ops[1] === 'decrypt'").is_truthy());
    assert(interp.eval("extErr === 'DataError'").is_truthy());
}
