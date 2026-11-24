// Phase 253, finding 257: the media element is in an honest state.
//
// WHATWG HTML keeps a playback state beside every media element, next to the
// attributes a page wrote: the volume, whether it is muted right now (the
// `muted` content attribute is only the default, which `defaultMuted`
// reflects), the current time, the playback rate. The engine had no-op
// setters and a hard-coded getter for all of it, `NETWORK_*`/`HAVE_*` did
// not exist, `TimeRanges`/`MediaError` were absent, `play()` resolved a
// playback nobody would ever run, and `canPlayType` said `""` to everything.
// Each is now what HTML §4.8.11 says, for as far as an engine with no
// network fetch, demux or codec stack honestly can.
TEST(a_media_element_keeps_the_state_it_is_given) {
    TestPage page;
    JsInterpreter& js = page.js;

    // --- the interface constants (§4.8.11.2) ------------------------------
    assert(js.eval("HTMLMediaElement.NETWORK_EMPTY").to_number() == 0.0);
    assert(js.eval("HTMLMediaElement.NETWORK_IDLE").to_number() == 1.0);
    assert(js.eval("HTMLMediaElement.NETWORK_LOADING").to_number() == 2.0);
    assert(js.eval("HTMLMediaElement.NETWORK_NO_SOURCE").to_number() == 3.0);
    assert(js.eval("HTMLMediaElement.HAVE_NOTHING").to_number() == 0.0);
    assert(js.eval("HTMLMediaElement.HAVE_METADATA").to_number() == 1.0);
    assert(js.eval("HTMLMediaElement.HAVE_CURRENT_DATA").to_number() == 2.0);
    assert(js.eval("HTMLMediaElement.HAVE_FUTURE_DATA").to_number() == 3.0);
    assert(js.eval("HTMLMediaElement.HAVE_ENOUGH_DATA").to_number() == 4.0);
    // Constants live on the constructor and on the prototype, and are shared
    // with the subclasses.
    assert(js.eval("HTMLMediaElement.prototype.NETWORK_EMPTY").to_number() == 0.0);
    assert(js.eval("HTMLVideoElement.HAVE_ENOUGH_DATA").to_number() == 4.0);
    assert(js.eval("HTMLAudioElement.HAVE_METADATA").to_number() == 1.0);

    // --- volume holds to [0, 1] (§4.8.11.3) -------------------------------
    js.eval("var a = document.createElement('audio');");
    assert(js.eval("a.volume").to_number() == 1.0);
    js.eval("a.volume = 0.25;");
    assert(js.eval("a.volume").to_number() == 0.25);
    assert(js.eval(
        "(function(){ try { a.volume = -0.1; return 'no'; } catch (e) { return e.name; } })()")
               .to_string() == "IndexSizeError");
    assert(js.eval(
        "(function(){ try { a.volume = 1.5; return 'no'; } catch (e) { return e.name; } })()")
               .to_string() == "IndexSizeError");
    // A refused write does not land.
    assert(js.eval("a.volume").to_number() == 0.25);
    // A non-finite double never crosses a WebIDL `double`.
    assert(js.eval(
        "(function(){ try { a.volume = NaN; return 'no'; } catch (e) { return e.name; } })()")
               .to_string() == "TypeError");
    // Another element keeps its own default.
    js.eval("var b = document.createElement('audio');");
    assert(js.eval("b.volume").to_number() == 1.0);

    // --- muted is the live state; defaultMuted reflects the attribute -----
    js.eval("var m = document.createElement('audio'); m.muted = true;");
    assert(js.eval("m.muted").is_truthy());
    assert(!js.eval("m.hasAttribute('muted')").is_truthy());
    assert(!js.eval("m.defaultMuted").is_truthy());
    // defaultMuted reflects the content attribute, in both directions.
    js.eval("m.defaultMuted = true;");
    assert(js.eval("m.hasAttribute('muted')").is_truthy());
    js.eval("var m2 = document.createElement('audio'); m2.setAttribute('muted', '');");
    assert(js.eval("m2.defaultMuted").is_truthy());

    // --- currentTime and playbackRate keep what they are given ------------
    js.eval("var c = document.createElement('audio'); c.currentTime = 12.5; c.playbackRate = 1.5;");
    assert(js.eval("c.currentTime").to_number() == 12.5);
    assert(js.eval("c.playbackRate").to_number() == 1.5);
    assert(js.eval(
        "(function(){ try { c.currentTime = NaN; return 'no'; } catch (e) { return e.name; } })()")
               .to_string() == "TypeError");
    assert(js.eval(
        "(function(){ try { c.playbackRate = Infinity; return 'no'; } catch (e) { return e.name; } })()")
               .to_string() == "TypeError");
    // Another element keeps its own position and rate.
    assert(js.eval("document.createElement('audio').currentTime").to_number() == 0.0);
    assert(js.eval("document.createElement('audio').playbackRate").to_number() == 1.0);

    // --- TimeRanges (empty: nothing has loaded) ----------------------------
    assert(js.eval("a.buffered.length").to_number() == 0.0);
    assert(js.eval("a.seekable.length").to_number() == 0.0);
    assert(js.eval("a.played.length").to_number() == 0.0);
    assert(js.eval("typeof a.buffered.start").to_string() == "function");
    assert(js.eval("typeof a.buffered.end").to_string() == "function");
    // The index that does not exist in an empty list: the spec's IndexSizeError.
    assert(js.eval(
        "(function(){ try { a.buffered.start(0); return 'no'; } catch (e) { return e.name; } })()")
               .to_string() == "IndexSizeError");
    assert(js.eval(
        "(function(){ try { a.buffered.end(0); return 'no'; } catch (e) { return e.name; } })()")
               .to_string() == "IndexSizeError");
    assert(js.eval("typeof TimeRanges === 'function'").is_truthy());
    // A range list is not constructible.
    assert(js.eval(
        "(function(){ try { new TimeRanges(); return 'no'; } catch (e) { return e.name; } })()")
               .to_string() == "TypeError");

    // --- MediaError (§4.8.11.3): the interface and its constants -----------
    assert(js.eval("typeof MediaError === 'function'").is_truthy());
    assert(js.eval(
        "[MediaError.MEDIA_ERR_ABORTED, MediaError.MEDIA_ERR_NETWORK, MediaError.MEDIA_ERR_DECODE,"
        " MediaError.MEDIA_ERR_SRC_NOT_SUPPORTED].join(',')").to_string() == "1,2,3,4");
    // Nothing ever loads, so there is never an error to report.
    assert(js.eval("a.error === null").is_truthy());

    // --- canPlayType (§4.8.11.3) -------------------------------------------
    // `maybe` is the honest answer for the pairs every browser ships; `""` is
    // the honest answer for everything else, because nothing here decodes.
    assert(js.eval("a.canPlayType('audio/wav; codecs=\"1\"')").to_string() == "maybe");
    assert(js.eval("a.canPlayType('audio/mpeg')").to_string() == "maybe");
    assert(js.eval("a.canPlayType('audio/ogg; codecs=\"vorbis\"')").to_string() == "maybe");
    assert(js.eval("a.canPlayType('video/mp4; codecs=\"avc1.42E01E\"')").to_string() == "maybe");
    assert(js.eval("a.canPlayType('video/webm; codecs=\"vp8\"')").to_string() == "maybe");
    assert(js.eval("a.canPlayType('video/webm; codecs=\"vp9\"')").to_string() == "maybe");
    assert(js.eval("a.canPlayType('video/mp4; codecs=\"avc1\"')").to_string() != "probably");
    assert(js.eval("a.canPlayType('')").to_string() == "");
    assert(js.eval("a.canPlayType('video/x-not-a-real-codec')").to_string() == "");
    assert(js.eval("a.canPlayType('application/octet-stream')").to_string() == "");
    assert(js.eval("a.canPlayType('audio/wav; codecs=\"boguscodec\"')").to_string() == "");
    assert(js.eval("a.canPlayType('video/mp4; codecs=\"mp4a.40.2\"')").to_string() == "maybe");

    // --- play() rejects instead of resolving a playback that never runs ---
    js.eval(
        "window.__playRejection = 'none';"
        "var pa = document.createElement('audio');"
        "pa.play().then(function() { window.__playRejection = 'resolved'; },"
        "              function(e) { window.__playRejection = 'rejected:' + (e && e.name); });");
    EventLoop::default_loop().run_until_idle(std::chrono::seconds(5));
    assert(js.global_object().get_property("__playRejection").to_string() ==
           "rejected:NotSupportedError");

    std::printf("      [media element] constants, state, TimeRanges, MediaError, canPlayType and play() rejection\n");
    std::fflush(stdout);
}
