TEST(subtle_crypto_ecdsa_jwk_alg_es256) {
    auto doc = HtmlParser::parse("<!doctype html><html><body></body></html>");
    JsInterpreter interp;
    interp.set_document(doc);

    interp.eval(R"(
        var exportedAlg = null, mismatchErr = null;
        crypto.subtle.generateKey({ name: 'ECDSA', namedCurve: 'P-256' }, true, ['sign', 'verify'])
            .then(function(pair) {
                return crypto.subtle.exportKey('jwk', pair.publicKey);
            })
            .then(function(jwk) {
                exportedAlg = jwk.alg;
                return crypto.subtle.importKey('jwk', { kty: 'EC', crv: 'P-256', x: jwk.x, y: jwk.y, alg: 'ES384', ext: true }, { name: 'ECDSA', namedCurve: 'P-256' }, true, ['verify']);
            })
            .catch(function(e) {
                mismatchErr = e.name;
            });
    )");
    assert(interp.eval("exportedAlg === 'ES256'").is_truthy());
    assert(interp.eval("mismatchErr === 'DataError'").is_truthy());
}
