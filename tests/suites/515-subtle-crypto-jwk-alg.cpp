TEST(subtle_crypto_jwk_alg_validation) {
    auto doc = HtmlParser::parse("<!doctype html><html><body></body></html>");
    JsInterpreter interp;
    interp.set_document(doc);

    interp.eval(R"(
        var okImport = null, mismatchErr = null;
        var k128 = 'AAECAwQFBgcICQoLDA0ODw';
        crypto.subtle.importKey('jwk', { kty: 'oct', k: k128, alg: 'A128GCM', ext: true }, 'AES-GCM', true, ['encrypt'])
            .then(function(k) {
                okImport = (k.algorithm.name === 'AES-GCM' && k.algorithm.length === 128);
                return crypto.subtle.importKey('jwk', { kty: 'oct', k: k128, alg: 'A128CBC', ext: true }, 'AES-GCM', true, ['encrypt']);
            })
            .catch(function(e) {
                mismatchErr = e.name;
            });
    )");
    assert(interp.eval("okImport === true").is_truthy());
    assert(interp.eval("mismatchErr === 'DataError'").is_truthy());
}
