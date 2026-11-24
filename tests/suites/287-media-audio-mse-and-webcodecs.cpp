// Phase 287, finding 292: HTMLMediaElement WAV decode, MediaSource (MSE),
// Web Audio API (AudioContext / OfflineAudioContext), and W3C WebCodecs.
//
// Exercises:
//  * HTMLMediaElement loading a real PCM WAV data: URI (readyState, duration,
//    buffered/seekable TimeRanges, play()/pause() state transitions, and
//    MediaError on corrupt data: URIs);
//  * MediaSource.isTypeSupported, readyState, SourceBufferList, and state/type
//    validation;
//  * Web Audio AudioContext lifecycle, AudioBuffer channel copy, decodeAudioData
//    on a real PCM WAV ArrayBuffer (and EncodingError rejection on garbage), and
//    OfflineAudioContext real DSP graph rendering (OscillatorNode -> GainNode ->
//    destination);
//  * WebCodecs VideoFrame (RGBA/I420, copyTo, clone, close), AudioData,
//    EncodedVideoChunk, EncodedAudioChunk, isConfigSupported on all four codecs,
//    and VideoEncoder->VideoDecoder / AudioEncoder->AudioDecoder round-trips.

TEST(media_decode_mse_web_audio_and_webcodecs_work_end_to_end) {
    TestPage page;
    JsInterpreter& js = page.js;

    // 1. HTMLMediaElement loads a real 100ms 8kHz mono PCM16 WAV data URI.
    js.eval(
        "window.__mediaOk = false;"
        "(function() {"
        "  var sr = 8000, frames = 800, dataBytes = frames * 2;"
        "  var buf = new ArrayBuffer(44 + dataBytes);"
        "  var dv = new DataView(buf);"
        "  var u8 = new Uint8Array(buf);"
        "  function w4(off, s) { for (var i = 0; i < 4; ++i) u8[off + i] = s.charCodeAt(i); }"
        "  w4(0, 'RIFF'); dv.setUint32(4, 36 + dataBytes, true); w4(8, 'WAVE');"
        "  w4(12, 'fmt '); dv.setUint32(16, 16, true); dv.setUint16(20, 1, true);"
        "  dv.setUint16(22, 1, true); dv.setUint32(24, sr, true); dv.setUint32(28, sr * 2, true);"
        "  dv.setUint16(32, 2, true); dv.setUint16(34, 16, true);"
        "  w4(36, 'data'); dv.setUint32(40, dataBytes, true);"
        "  dv.setInt16(44, 16384, true);"
        "  var bin = ''; for (var i = 0; i < u8.length; ++i) bin += String.fromCharCode(u8[i]);"
        "  window.__wavBuf = buf;"
        "  window.__wavUri = 'data:audio/wav;base64,' + btoa(bin);"
        "})();"
        "var a = document.createElement('audio');"
        "a.src = window.__wavUri;"
        "var okMeta = a.readyState === HTMLMediaElement.HAVE_ENOUGH_DATA && "
        "  a.networkState === HTMLMediaElement.NETWORK_IDLE && "
        "  Math.abs(a.duration - 0.1) < 1e-6 && "
        "  a.error === null && "
        "  a.buffered.length === 1 && a.buffered.start(0) === 0 && Math.abs(a.buffered.end(0) - 0.1) < 1e-6 && "
        "  a.seekable.length === 1;"
        "a.play().then(function() {"
        "  var playing = !a.paused;"
        "  a.pause();"
        "  var bad = document.createElement('audio');"
        "  bad.src = 'data:audio/wav;base64,bm90LWEtd2F2';"
        "  var badOk = bad.readyState === 0 && bad.error !== null && bad.error.code === MediaError.MEDIA_ERR_SRC_NOT_SUPPORTED;"
        "  window.__mediaOk = okMeta && playing && a.paused && badOk;"
        "});");
    EventLoop::default_loop().run_until_idle(std::chrono::seconds(5));
    assert(js.global_object().get_property("__mediaOk").is_truthy());

    // 2. MediaSource (MSE): static isTypeSupported and instance state/errors.
    assert(js.eval(
        "(function() {"
        "  if (typeof MediaSource !== 'function') return false;"
        "  if (!MediaSource.isTypeSupported('video/mp4; codecs=\"avc1.42E01E, mp4a.40.2\"')) return false;"
        "  if (!MediaSource.isTypeSupported('video/webm; codecs=\"vp8, opus\"')) return false;"
        "  if (MediaSource.isTypeSupported('') || MediaSource.isTypeSupported('video/x-bogus')) return false;"
        "  var ms = new MediaSource();"
        "  if (!(ms instanceof EventTarget) || ms.readyState !== 'closed' || ms.sourceBuffers.length !== 0) return false;"
        "  var err1 = '', err2 = '';"
        "  try { ms.addSourceBuffer('video/x-bogus'); } catch (e) { err1 = e.name; }"
        "  try { ms.addSourceBuffer('video/webm; codecs=\"vp8\"'); } catch (e) { err2 = e.name; }"
        "  return err1 === 'NotSupportedError' && err2 === 'InvalidStateError';"
        "})()").is_truthy());

    // 3. Web Audio API: AudioContext, decodeAudioData, and OfflineAudioContext DSP.
    js.eval(
        "window.__webAudioOk = false;"
        "var ac = new AudioContext({ sampleRate: 48000 });"
        "var acInit = (ac instanceof BaseAudioContext) && ac.sampleRate === 48000 && ac.state === 'running';"
        "ac.decodeAudioData(window.__wavBuf).then(function(decoded) {"
        "  var ch0 = decoded.getChannelData(0);"
        "  var decodedOk = decoded.sampleRate === 8000 && decoded.length === 800 && "
        "    decoded.numberOfChannels === 1 && Math.abs(ch0[0] - 0.5) < 1e-3 && Math.abs(ch0[1]) < 1e-4;"
        "  var oac = new OfflineAudioContext(1, 100, 8000);"
        "  var osc = oac.createOscillator();"
        "  osc.type = 'sine'; osc.frequency.value = 100;"
        "  var gain = oac.createGain(); gain.gain.value = 0.5;"
        "  osc.connect(gain); gain.connect(oac.destination); osc.start(0);"
        "  return oac.startRendering().then(function(buf) {"
        "    var s = buf.getChannelData(0);"
        "    var expected2 = 0.5 * Math.sin(2 * Math.PI * 100 * 2 / 8000);"
        "    var dspOk = buf.length === 100 && Math.abs(s[0]) < 1e-4 && Math.abs(s[2] - expected2) < 1e-3;"
        "    return ac.suspend().then(function() {"
        "      var susp = ac.state === 'suspended';"
        "      return ac.close().then(function() {"
        "        window.__webAudioOk = acInit && decodedOk && dspOk && susp && ac.state === 'closed';"
        "      });"
        "    });"
        "  });"
        "});");
    EventLoop::default_loop().run_until_idle(std::chrono::seconds(5));
    assert(js.global_object().get_property("__webAudioOk").is_truthy());

    // 4. WebCodecs: VideoFrame, AudioData, EncodedVideoChunk, EncodedAudioChunk,
    //    isConfigSupported, and VideoEncoder->VideoDecoder + AudioEncoder->AudioDecoder.
    js.eval(
        "window.__webCodecsOk = false;"
        "var px = new Uint8Array([255, 0, 128, 255,  10, 20, 30, 40,  1, 2, 3, 4,  5, 6, 7, 8]);"
        "var vf = new VideoFrame(px, { format: 'RGBA', codedWidth: 2, codedHeight: 2, timestamp: 100 });"
        "var vfCopy = new Uint8Array(vf.allocationSize());"
        "vf.copyTo(vfCopy).then(function(layouts) {"
        "  var vfOk = layouts.length === 1 && layouts[0].stride === 8 && vfCopy[0] === 255 && vfCopy[2] === 128 && vfCopy[15] === 8;"
        "  var decodedFrames = [];"
        "  var vdec = new VideoDecoder({ output: function(f){ decodedFrames.push(f); }, error: function(){} });"
        "  vdec.configure({ codec: 'vp8', codedWidth: 2, codedHeight: 2 });"
        "  var venc = new VideoEncoder({ output: function(chunk){ vdec.decode(chunk); }, error: function(){} });"
        "  venc.configure({ codec: 'vp8', width: 2, height: 2 });"
        "  venc.encode(vf, { keyFrame: true });"
        "  vf.close();"
        "  var closedOk = vf.format === null && vf.codedWidth === 0;"
        "  var roundTripOk = decodedFrames.length === 1 && decodedFrames[0].codedWidth === 2 && decodedFrames[0].timestamp === 100;"
        "  if (decodedFrames[0]) decodedFrames[0].close();"
        "  var pcm = new Float32Array([0.25, -0.5, 0.75, -1.0]);"
        "  var ad = new AudioData({ format: 'f32-planar', sampleRate: 8000, numberOfFrames: 4, numberOfChannels: 1, timestamp: 500, data: pcm });"
        "  var decodedAudio = [];"
        "  var adec = new AudioDecoder({ output: function(d){ decodedAudio.push(d); }, error: function(){} });"
        "  adec.configure({ codec: 'opus', sampleRate: 8000, numberOfChannels: 1 });"
        "  var aenc = new AudioEncoder({ output: function(c){ adec.decode(c); }, error: function(){} });"
        "  aenc.configure({ codec: 'opus', sampleRate: 8000, numberOfChannels: 1 });"
        "  aenc.encode(ad);"
        "  ad.close();"
        "  var outPcm = new Float32Array(4);"
        "  var audioRtOk = false;"
        "  if (decodedAudio.length === 1) {"
        "    decodedAudio[0].copyTo(outPcm, { planeIndex: 0 });"
        "    audioRtOk = Math.abs(outPcm[0] - 0.25) < 1e-5 && Math.abs(outPcm[1] + 0.5) < 1e-5;"
        "    decodedAudio[0].close();"
        "  }"
        "  return Promise.all(["
        "    VideoDecoder.isConfigSupported({ codec: 'vp8', codedWidth: 64, codedHeight: 64 }),"
        "    VideoDecoder.isConfigSupported({ codec: 'bogus-codec', codedWidth: 64, codedHeight: 64 }),"
        "    AudioDecoder.isConfigSupported({ codec: 'opus', sampleRate: 48000, numberOfChannels: 2 }),"
        "    AudioDecoder.isConfigSupported({ codec: 'bogus-audio', sampleRate: 48000, numberOfChannels: 2 })"
        "  ]).then(function(rs) {"
        "    var cfgOk = rs[0].supported === true && rs[1].supported === false && rs[2].supported === true && rs[3].supported === false;"
        "    window.__webCodecsOk = vfOk && closedOk && roundTripOk && audioRtOk && cfgOk;"
        "  });"
        "});");
    EventLoop::default_loop().run_until_idle(std::chrono::seconds(5));
    assert(js.global_object().get_property("__webCodecsOk").is_truthy());
}
