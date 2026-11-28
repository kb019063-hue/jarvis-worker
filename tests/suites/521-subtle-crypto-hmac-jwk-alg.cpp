TEST(subtle_crypto_hmac_jwk_alg_validation) {
    auto doc = HtmlParser::parse("<!doctype html><html><body></body></html>");
    JsInterpreter interp;
    interp.set_document(doc);

    interp.eval(R"(
        var hs1Alg = null, mismatchErr = null;
        var k = 'AAECAwQFBgcICQoLDA0ODw';
        crypto.subtle.importKey('jwk', { kty: 'oct', k: k, alg: 'HS1', ext: true }, { name: 'HMAC', hash: 'SHA-1' }, true, ['sign'])
            .then(function(key) {
                return crypto.subtle.exportKey('jwk', key);
            })
            .then(function(jwk) {
                hs1Alg = jwk.alg;
                return crypto.subtle.importKey('jwk', { kty: 'oct', k: k, alg: 'HS512', ext: true }, { name: 'HMAC', hash: 'SHA-256' }, true, ['sign']);
            })
            .catch(function(e) {
                mismatchErr = e.name;
            });
    )");
    assert(interp.eval("hs1Alg === 'HS1'").is_truthy());
    assert(interp.eval("mismatchErr === 'DataError'").is_truthy());
}
