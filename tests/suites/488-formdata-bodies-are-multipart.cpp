// Phase 488, finding 488: a FormData body reaches the wire as multipart/form-data
// (the Fetch Standard's "multipart/form-data encoding algorithm"; RFC 7578).
// It used to be the string "[object FormData]" with the text/plain default.
TEST(formdata_bodies_are_multipart_form_data) {
    TestPage page;
    JsInterpreter& js = page.js;
    js.eval(R"JS(
        var failures = [];
        var asyncDone = false;
        function eq(label, got, want) {
            if (got !== want) failures.push(label + ': got ' + JSON.stringify(got) + ', want ' + JSON.stringify(want));
        }
        function ok(label, cond) { if (!cond) failures.push(label + ': not truthy'); }

        var fd = new FormData();
        fd.append('plain', 'value');
        fd.append('a"b\r\nc', 'v');
        fd.append('pct%name', 'p100%');
        fd.append('file', new File(['hello file'], 'greeting.txt', { type: 'text/plain' }));
        fd.append('blob', new Blob(['blob-bytes']));
        fd.append('typed', new Blob(['typed-bytes'], { type: 'image/png' }));
        fd.append('quotefile', new File(['QF'], 'ev"il\r\nname.txt', { type: '' }));

        var req = new Request('https://upload.example/path', { method: 'POST', body: fd });
        var ct = req.headers.get('content-type');
        ok('content-type is multipart', ct.indexOf('multipart/form-data; boundary=') === 0);
        var boundary = ct.slice(ct.indexOf('boundary=') + 'boundary='.length);
        ok('boundary is chosen', boundary.length >= 20 && boundary.indexOf('\r') === -1 && boundary.indexOf('\n') === -1);

        // Two requests must not share a boundary: it is chosen per body, not baked in.
        var req2 = new Request('https://upload.example/other', { method: 'POST', body: fd });
        var boundary2 = req2.headers.get('content-type').slice('multipart/form-data; boundary='.length);
        ok('boundary is per body', boundary !== boundary2);

        req.text().then(function (body) {
            var B = '--' + boundary;
            eq('starts with the boundary', body.slice(0, B.length + 2), B + '\r\n');
            eq('ends with the closing boundary', body.slice(-(B.length + 4)), B + '--\r\n');

            ok('string part', body.indexOf(B + '\r\nContent-Disposition: form-data; name="plain"\r\n\r\nvalue\r\n') !== -1);
            ok('name percent-encoding', body.indexOf('name="a%22b%0D%0Ac"') !== -1);
            ok('percent name', body.indexOf('name="pct%25name"') !== -1);
            ok('file part', body.indexOf(
                'name="file"; filename="greeting.txt"\r\nContent-Type: text/plain\r\n\r\nhello file\r\n') !== -1);
            ok('blob part gets filename=blob and octet-stream', body.indexOf(
                'name="blob"; filename="blob"\r\nContent-Type: application/octet-stream\r\n\r\nblob-bytes\r\n') !== -1);
            ok('blob part keeps its type', body.indexOf(
                'name="typed"; filename="blob"\r\nContent-Type: image/png\r\n\r\ntyped-bytes\r\n') !== -1);
            ok('filename percent-encoding and octet-stream fallback', body.indexOf(
                'name="quotefile"; filename="ev%22il%0D%0Aname.txt"\r\nContent-Type: application/octet-stream\r\n\r\nQF\r\n') !== -1);
            ok('entries are in order', body.indexOf('name="plain"') < body.indexOf('name="a%22b%0D%0Ac"') &&
                body.indexOf('name="a%22b%0D%0Ac"') < body.indexOf('name="pct%25name"') &&
                body.indexOf('name="pct%25name"') < body.indexOf('name="file"') &&
                body.indexOf('name="file"') < body.indexOf('name="blob"') &&
                body.indexOf('name="blob"') < body.indexOf('name="typed"') &&
                body.indexOf('name="typed"') < body.indexOf('name="quotefile"'));

            // A FormData response body serializes the same way (Response constructor).
            var resp = new Response(fd);
            var rct = resp.headers.get('content-type');
            ok('response content-type is multipart', rct.indexOf('multipart/form-data; boundary=') === 0);
            return resp.text();
        }).then(function (body) {
            ok('response body is the serialized form', body.indexOf('Content-Disposition: form-data; name="plain"') !== -1 &&
                body.indexOf('hello file') !== -1);

            // An explicit Content-Type from the script wins over the default.
            var req3 = new Request('https://upload.example/third', {
                method: 'POST',
                headers: { 'Content-Type': 'text/csv' },
                body: fd
            });
            eq('script content-type wins', req3.headers.get('content-type'), 'text/csv');

            // An ordinary string body is unchanged.
            var req4 = new Request('https://upload.example/fourth', { method: 'POST', body: 'plain-body' });
            eq('string body default type', req4.headers.get('content-type'), 'text/plain;charset=UTF-8');

            asyncDone = true;
        }, function (e) {
            failures.push('rejected: ' + e);
            asyncDone = true;
        });
    )JS");
    EventLoop::default_loop().run_microtasks();
    assert(js.eval("asyncDone").is_truthy());
    const std::string failed = js.eval("failures.join('\\n')").to_string();
    if (!failed.empty()) std::cerr << failed << "\n";
    assert(failed.empty());
}
