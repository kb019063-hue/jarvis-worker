#include "litebird/js/DomBindings.hpp"
#include "litebird/js/TypedArray.hpp"
#include "litebird/render/MediaAudioCodecs.hpp"
#include "litebird/render/RasterBudget.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace litebird {
namespace dom {

// Defined in DomHtmlBindings.cpp so HTMLMediaElement and SourceBuffer share TimeRanges.
JsValue make_time_ranges_with(Realm& realm, std::vector<std::pair<double, double>> ranges);

namespace {

// --- Promise helpers --------------------------------------------------------

JsValue make_resolved_promise(Realm& r, const JsValue& value) {
    JsValue promise = r.window.is_object() ? r.window.get_property("Promise") : JsValue();
    if (!promise.is_function()) return undefined();
    return js_call(promise.get_property("resolve"), promise, {value});
}

JsValue make_rejected_promise(Realm& r, const JsValue& err) {
    JsValue promise = r.window.is_object() ? r.window.get_property("Promise") : JsValue();
    if (!promise.is_function()) return undefined();
    return js_call(promise.get_property("reject"), promise, {err});
}

JsValue make_rejected_dom_promise(Realm& r, const std::string& name, const std::string& msg) {
    return make_rejected_promise(r, make_dom_exception(r, name, msg));
}

JsValue make_type_error_obj(Realm& r, const std::string& msg) {
    JsValue ctor = r.window.is_object() ? r.window.get_property("TypeError") : JsValue();
    if (ctor.is_function()) return js_construct(ctor, {str(msg)});
    return make_dom_exception(r, "TypeError", msg);
}

JsValue make_float32_array_from_floats(Realm& r, const std::vector<float>& samples) {
    std::string bytes(samples.size() * sizeof(float), '\0');
    if (!samples.empty()) {
        std::memcpy(bytes.data(), samples.data(), bytes.size());
    }
    JsValue ab = array_buffer_from_bytes(bytes);
    JsValue ctor = r.window.is_object() ? r.window.get_property("Float32Array") : JsValue();
    if (ctor.is_function()) return js_construct(ctor, {ab});
    return ab;
}

bool read_bytes_from_buffer_source(const JsValue& v, std::vector<uint8_t>& out) {
    if (!is_buffer_source(v)) return false;
    std::string raw;
    if (!typed_array_bytes(v, raw)) return false;
    out.assign(reinterpret_cast<const uint8_t*>(raw.data()),
               reinterpret_cast<const uint8_t*>(raw.data()) + raw.size());
    return true;
}

bool write_bytes_to_buffer_source(const JsValue& dest, const uint8_t* src, std::size_t count) {
    if (!is_buffer_source(dest)) return false;
    BufferViewInfo info;
    if (!buffer_view_info(dest, info)) return false;
    if (info.byte_length < count) return false;
    if (info.kind == 2) {
        return typed_array_write(dest, src, count);
    }
    if (info.kind == 1) {
        // ArrayBuffer: construct a temporary Uint8Array view over it and write.
        JsValue view = typed_array_view_over(1, dest, 0, count, false);
        return typed_array_write(view, src, count);
    }
    if (info.kind == 3) {
        // DataView: obtain its underlying buffer and write at its byteOffset.
        JsValue buf = dest.get_property("buffer");
        JsValue view = typed_array_view_over(1, buf, info.byte_offset, count, false);
        return typed_array_write(view, src, count);
    }
    return false;
}

// ============================================================================
// 1. Media Source Extensions (W3C MSE §§3–5)
// ============================================================================

struct SourceBufferState;
struct MediaSourceState;

struct SourceBufferListState : JsExotic {
    std::vector<JsValue> items;

    void trace(Heap& heap) override {
        for (auto& v : items) heap.mark_value(v);
    }

    bool get(JsObject*, const std::string& key, JsValue& out) override {
        if (key.empty() || key[0] < '0' || key[0] > '9') return false;
        char* end = nullptr;
        const unsigned long idx = std::strtoul(key.c_str(), &end, 10);
        if (!end || *end != '\0' || idx >= items.size()) return false;
        out = items[idx];
        return true;
    }

    bool has(JsObject*, const std::string& key) override {
        if (key.empty() || key[0] < '0' || key[0] > '9') return false;
        char* end = nullptr;
        const unsigned long idx = std::strtoul(key.c_str(), &end, 10);
        return end && *end == '\0' && idx < items.size();
    }

    void keys(JsObject*, std::vector<std::string>& out) override {
        for (std::size_t i = 0; i < items.size(); ++i) out.push_back(std::to_string(i));
    }
};

struct SourceBufferState : JsExotic {
    std::string mime_type;
    std::string mode{"segments"};
    bool updating{false};
    double timestamp_offset{0.0};
    double append_window_start{0.0};
    double append_window_end{std::numeric_limits<double>::infinity()};
    std::vector<uint8_t> appended_bytes;
    std::vector<std::pair<double, double>> buffered_ranges;
    bool removed{false};
};

struct MediaSourceState : JsExotic {
    std::string ready_state{"closed"};
    double duration{std::numeric_limits<double>::quiet_NaN()};
    std::shared_ptr<SourceBufferListState> source_buffers_state;
    std::shared_ptr<SourceBufferListState> active_buffers_state;
    JsValue source_buffers_obj{JsValue::make_undefined()};
    JsValue active_buffers_obj{JsValue::make_undefined()};

    void trace(Heap& heap) override {
        heap.mark_value(source_buffers_obj);
        heap.mark_value(active_buffers_obj);
    }
};

void setup_source_buffer_list(Iface& I) {
    I.getter("length", [](Realm&) {
        auto& st = this_state<SourceBufferListState>("SourceBufferList");
        return num(static_cast<double>(st.items.size()));
    });
}

void setup_source_buffer(Iface& I) {
    I.accessor("mode",
        [](Realm&) { return str(this_state<SourceBufferState>("SourceBuffer").mode); },
        [](Realm& r, const JsValue& v) {
            auto& st = this_state<SourceBufferState>("SourceBuffer");
            const std::string m = to_str(v);
            if (m != "segments" && m != "sequence") {
                js_throw_type_error("Invalid SourceBuffer.mode: " + m);
            }
            if (st.updating) throw_dom(r, "InvalidStateError", "SourceBuffer is updating");
            st.mode = m;
        });
    I.getter("updating", [](Realm&) {
        return boolean(this_state<SourceBufferState>("SourceBuffer").updating);
    });
    I.getter("buffered", [](Realm& r) {
        auto& st = this_state<SourceBufferState>("SourceBuffer");
        return make_time_ranges_with(r, st.buffered_ranges);
    });
    I.accessor("timestampOffset",
        [](Realm&) { return num(this_state<SourceBufferState>("SourceBuffer").timestamp_offset); },
        [](Realm&, const JsValue& v) {
            const double d = to_double(v);
            if (!std::isfinite(d)) js_throw_type_error("timestampOffset must be finite");
            this_state<SourceBufferState>("SourceBuffer").timestamp_offset = d;
        });
    I.accessor("appendWindowStart",
        [](Realm&) { return num(this_state<SourceBufferState>("SourceBuffer").append_window_start); },
        [](Realm& r, const JsValue& v) {
            const double d = to_double(v);
            if (!std::isfinite(d) || d < 0.0) js_throw_type_error("Invalid appendWindowStart");
            auto& st = this_state<SourceBufferState>("SourceBuffer");
            if (d >= st.append_window_end) throw_dom(r, "InvalidAccessError", "appendWindowStart >= appendWindowEnd");
            st.append_window_start = d;
        });
    I.accessor("appendWindowEnd",
        [](Realm&) { return num(this_state<SourceBufferState>("SourceBuffer").append_window_end); },
        [](Realm& r, const JsValue& v) {
            const double d = to_double(v);
            if (std::isnan(d)) js_throw_type_error("appendWindowEnd must not be NaN");
            auto& st = this_state<SourceBufferState>("SourceBuffer");
            if (d <= st.append_window_start) throw_dom(r, "InvalidAccessError", "appendWindowEnd <= appendWindowStart");
            st.append_window_end = d;
        });
    I.method("appendBuffer", 1, [](Realm& r, Args args) {
        require_args(args, 1, "SourceBuffer.appendBuffer");
        auto& st = this_state<SourceBufferState>("SourceBuffer");
        if (st.removed) throw_dom(r, "InvalidStateError", "SourceBuffer has been removed");
        std::vector<uint8_t> bytes;
        if (!read_bytes_from_buffer_source(args[0], bytes)) {
            js_throw_type_error("SourceBuffer.appendBuffer requires a BufferSource");
        }
        // Cap total appended bytes to 64 MiB.
        if (st.appended_bytes.size() + bytes.size() > 64u * 1024u * 1024u) {
            throw_dom(r, "QuotaExceededError", "SourceBuffer quota exceeded");
        }
        st.appended_bytes.insert(st.appended_bytes.end(), bytes.begin(), bytes.end());
        // If WAV data was appended, extract exact duration into buffered TimeRanges.
        WavAudio wav;
        std::string err;
        if (decode_wav(st.appended_bytes.data(), st.appended_bytes.size(), wav, err) && wav.duration_seconds > 0.0) {
            st.buffered_ranges = {{st.timestamp_offset, st.timestamp_offset + wav.duration_seconds}};
        }
        return undefined();
    });
    I.method("abort", 0, [](Realm& r, Args) {
        auto& st = this_state<SourceBufferState>("SourceBuffer");
        if (st.removed) throw_dom(r, "InvalidStateError", "SourceBuffer has been removed");
        st.updating = false;
        return undefined();
    });
    I.method("remove", 2, [](Realm& r, Args args) {
        require_args(args, 2, "SourceBuffer.remove");
        auto& st = this_state<SourceBufferState>("SourceBuffer");
        if (st.removed) throw_dom(r, "InvalidStateError", "SourceBuffer has been removed");
        const double s = to_double(args[0]);
        const double e = to_double(args[1]);
        if (!std::isfinite(s) || s < 0.0 || std::isnan(e) || e <= s) {
            js_throw_type_error("Invalid SourceBuffer.remove range");
        }
        st.buffered_ranges.clear();
        return undefined();
    });
}

JsValue construct_media_source(Realm& r, const InterfaceDef&, Args, const JsValue* derived) {
    auto st = std::make_shared<MediaSourceState>();
    st->source_buffers_state = std::make_shared<SourceBufferListState>();
    st->active_buffers_state = std::make_shared<SourceBufferListState>();
    st->source_buffers_obj = r.make("SourceBufferList", st->source_buffers_state);
    st->active_buffers_obj = r.make("SourceBufferList", st->active_buffers_state);
    return r.make("MediaSource", st, derived);
}

void setup_media_source(Iface& I) {
    I.static_method("isTypeSupported", 1, [](Realm&, Args args) {
        if (args.empty() || args[0].is_undefined()) return boolean(false);
        return boolean(media_source_is_type_supported(to_str(args[0])));
    });
    I.getter("readyState", [](Realm&) {
        return str(this_state<MediaSourceState>("MediaSource").ready_state);
    });
    I.accessor("duration",
        [](Realm&) { return num(this_state<MediaSourceState>("MediaSource").duration); },
        [](Realm& r, const JsValue& v) {
            auto& st = this_state<MediaSourceState>("MediaSource");
            const double d = to_double(v);
            if (d < 0.0 || std::isnan(d)) js_throw_type_error("MediaSource.duration must be non-negative");
            if (st.ready_state != "open") throw_dom(r, "InvalidStateError", "MediaSource is not open");
            st.duration = d;
        });
    I.getter("sourceBuffers", [](Realm&) {
        return this_state<MediaSourceState>("MediaSource").source_buffers_obj;
    });
    I.getter("activeSourceBuffers", [](Realm&) {
        return this_state<MediaSourceState>("MediaSource").active_buffers_obj;
    });
    I.method("addSourceBuffer", 1, [](Realm& r, Args args) {
        require_args(args, 1, "MediaSource.addSourceBuffer");
        auto& st = this_state<MediaSourceState>("MediaSource");
        const std::string mime = to_str(args[0]);
        if (mime.empty()) js_throw_type_error("MediaSource.addSourceBuffer requires a non-empty MIME type");
        if (!media_source_is_type_supported(mime)) {
            throw_dom(r, "NotSupportedError", "Unsupported SourceBuffer MIME type: " + mime);
        }
        if (st.ready_state != "open") {
            throw_dom(r, "InvalidStateError", "MediaSource is not in 'open' state");
        }
        auto sb_st = std::make_shared<SourceBufferState>();
        sb_st->mime_type = mime;
        JsValue sb = r.make("SourceBuffer", sb_st);
        st.source_buffers_state->items.push_back(sb);
        st.active_buffers_state->items.push_back(sb);
        return sb;
    });
    I.method("removeSourceBuffer", 1, [](Realm& r, Args args) {
        require_args(args, 1, "MediaSource.removeSourceBuffer");
        auto& st = this_state<MediaSourceState>("MediaSource");
        auto* sb = state_of<SourceBufferState>(args[0]);
        if (!sb) js_throw_type_error("Expected a SourceBuffer");
        auto& items = st.source_buffers_state->items;
        bool found = false;
        for (auto it = items.begin(); it != items.end(); ++it) {
            if (state_of<SourceBufferState>(*it) == sb) {
                items.erase(it);
                found = true;
                break;
            }
        }
        if (!found) throw_dom(r, "NotFoundError", "SourceBuffer does not belong to this MediaSource");
        sb->removed = true;
        auto& aitems = st.active_buffers_state->items;
        for (auto it = aitems.begin(); it != aitems.end(); ++it) {
            if (state_of<SourceBufferState>(*it) == sb) { aitems.erase(it); break; }
        }
        return undefined();
    });
    I.method("endOfStream", 0, [](Realm& r, Args args) {
        auto& st = this_state<MediaSourceState>("MediaSource");
        if (st.ready_state != "open") throw_dom(r, "InvalidStateError", "MediaSource is not open");
        if (!args.empty() && !args[0].is_undefined()) {
            const std::string e = to_str(args[0]);
            if (e != "network" && e != "decode") js_throw_type_error("Invalid EndOfStreamError: " + e);
        }
        st.ready_state = "ended";
        return undefined();
    });
    I.method("setLiveSeekableRange", 2, [](Realm& r, Args args) {
        require_args(args, 2, "MediaSource.setLiveSeekableRange");
        auto& st = this_state<MediaSourceState>("MediaSource");
        if (st.ready_state != "open") throw_dom(r, "InvalidStateError", "MediaSource is not open");
        const double s = to_double(args[0]), e = to_double(args[1]);
        if (!std::isfinite(s) || !std::isfinite(e) || s < 0.0 || s > e) {
            js_throw_type_error("Invalid live seekable range");
        }
        return undefined();
    });
    I.method("clearLiveSeekableRange", 0, [](Realm& r, Args) {
        auto& st = this_state<MediaSourceState>("MediaSource");
        if (st.ready_state != "open") throw_dom(r, "InvalidStateError", "MediaSource is not open");
        return undefined();
    });
}

// ============================================================================
// 2. Web Audio API (W3C Web Audio §1)
// ============================================================================

struct AudioParamState : JsExotic {
    double value{0.0};
    double default_value{0.0};
    double min_value{-3.4028234663852886e+38};
    double max_value{3.4028234663852886e+38};
};

struct AudioBufferState : JsExotic {
    OfflineAudioBufferData data;
};

struct AudioNodeState;

struct BaseAudioContextState : JsExotic {
    bool is_offline{false};
    int number_of_channels{2};
    std::size_t offline_length{0};
    double sample_rate{44100.0};
    double current_time{0.0};
    std::string state{"running"};
    bool rendering_started{false};
    JsValue destination_obj{JsValue::make_undefined()};
    std::vector<std::weak_ptr<AudioNodeState>> nodes;

    void trace(Heap& heap) override {
        heap.mark_value(destination_obj);
    }
};

enum class AudioNodeKind {
    Destination,
    Gain,
    Oscillator,
    BufferSource,
    Analyser,
    BiquadFilter
};

struct AudioNodeState : JsExotic {
    AudioNodeKind kind{AudioNodeKind::Gain};
    std::weak_ptr<BaseAudioContextState> ctx;
    JsValue context_obj{JsValue::make_undefined()};
    int number_of_inputs{1};
    int number_of_outputs{1};
    int channel_count{2};
    std::string channel_count_mode{"max"};
    std::string channel_interpretation{"speakers"};
    std::vector<std::weak_ptr<AudioNodeState>> outputs;

    // GainNode
    std::shared_ptr<AudioParamState> gain_param;
    JsValue gain_obj{JsValue::make_undefined()};

    // OscillatorNode
    std::string osc_type{"sine"};
    std::shared_ptr<AudioParamState> freq_param;
    std::shared_ptr<AudioParamState> detune_param;
    JsValue freq_obj{JsValue::make_undefined()};
    JsValue detune_obj{JsValue::make_undefined()};
    bool started{false};
    double start_time{0.0};
    double stop_time{-1.0};

    // AudioBufferSourceNode
    std::shared_ptr<AudioBufferState> buffer_state;
    JsValue buffer_obj{JsValue::make_null()};
    std::shared_ptr<AudioParamState> playback_rate_param;
    JsValue playback_rate_obj{JsValue::make_undefined()};
    bool loop{false};
    double loop_start{0.0};
    double loop_end{0.0};

    // AnalyserNode
    uint32_t fft_size{2048};
    double min_decibels{-100.0};
    double max_decibels{-30.0};
    double smoothing{0.8};

    // BiquadFilterNode
    std::string filter_type{"lowpass"};
    std::shared_ptr<AudioParamState> q_param;
    JsValue q_obj{JsValue::make_undefined()};

    void trace(Heap& heap) override {
        heap.mark_value(context_obj);
        heap.mark_value(gain_obj);
        heap.mark_value(freq_obj);
        heap.mark_value(detune_obj);
        heap.mark_value(buffer_obj);
        heap.mark_value(playback_rate_obj);
        heap.mark_value(q_obj);
    }
};

JsValue make_audio_param(Realm& r, double def_val, std::shared_ptr<AudioParamState>& out_st,
                         double min_v = -3.4028234663852886e+38,
                         double max_v = 3.4028234663852886e+38) {
    out_st = std::make_shared<AudioParamState>();
    out_st->value = def_val;
    out_st->default_value = def_val;
    out_st->min_value = min_v;
    out_st->max_value = max_v;
    return r.make("AudioParam", out_st);
}

void setup_audio_param(Iface& I) {
    I.accessor("value",
        [](Realm&) { return num(this_state<AudioParamState>("AudioParam").value); },
        [](Realm&, const JsValue& v) {
            const double d = to_double(v);
            if (std::isfinite(d)) this_state<AudioParamState>("AudioParam").value = d;
        });
    I.getter("defaultValue", [](Realm&) { return num(this_state<AudioParamState>("AudioParam").default_value); });
    I.getter("minValue", [](Realm&) { return num(this_state<AudioParamState>("AudioParam").min_value); });
    I.getter("maxValue", [](Realm&) { return num(this_state<AudioParamState>("AudioParam").max_value); });
    auto sched = [](Realm&, Args args) {
        if (!args.empty()) {
            const double d = to_double(args[0]);
            if (std::isfinite(d)) this_state<AudioParamState>("AudioParam").value = d;
        }
        return JsValue::current_this();
    };
    I.method("setValueAtTime", 2, sched);
    I.method("linearRampToValueAtTime", 2, sched);
    I.method("exponentialRampToValueAtTime", 2, sched);
    I.method("setTargetAtTime", 3, sched);
    I.method("cancelScheduledValues", 1, [](Realm&, Args) { return JsValue::current_this(); });
}

JsValue construct_audio_buffer(Realm& r, const InterfaceDef&, Args args, const JsValue* derived) {
    require_args(args, 1, "AudioBuffer");
    if (!args[0].is_object_like()) js_throw_type_error("AudioBuffer requires an options dictionary");
    const JsValue& opts = args[0];
    const JsValue ch_v = opts.get_property("numberOfChannels");
    const int num_ch = ch_v.is_undefined() ? 1 : static_cast<int>(to_double(ch_v));
    const double len_d = to_double(opts.get_property("length"));
    const double sr = to_double(opts.get_property("sampleRate"));
    if (num_ch < 1 || num_ch > 32) throw_dom(r, "NotSupportedError", "Invalid AudioBuffer numberOfChannels");
    if (!std::isfinite(len_d) || len_d < 1.0 || len_d > 64.0 * 1024.0 * 1024.0) {
        throw_dom(r, "NotSupportedError", "Invalid AudioBuffer length");
    }
    if (!std::isfinite(sr) || sr < 3000.0 || sr > 768000.0) {
        throw_dom(r, "NotSupportedError", "Invalid AudioBuffer sampleRate");
    }
    auto st = std::make_shared<AudioBufferState>();
    st->data.number_of_channels = num_ch;
    st->data.length = static_cast<std::size_t>(len_d);
    st->data.sample_rate = sr;
    st->data.channels.assign(static_cast<std::size_t>(num_ch), std::vector<float>(st->data.length, 0.0f));
    return r.make("AudioBuffer", st, derived);
}

void setup_audio_buffer(Iface& I) {
    I.getter("sampleRate", [](Realm&) { return num(this_state<AudioBufferState>("AudioBuffer").data.sample_rate); });
    I.getter("length", [](Realm&) { return num(static_cast<double>(this_state<AudioBufferState>("AudioBuffer").data.length)); });
    I.getter("numberOfChannels", [](Realm&) { return num(this_state<AudioBufferState>("AudioBuffer").data.number_of_channels); });
    I.getter("duration", [](Realm&) {
        auto& d = this_state<AudioBufferState>("AudioBuffer").data;
        return num(d.sample_rate > 0.0 ? static_cast<double>(d.length) / d.sample_rate : 0.0);
    });
    I.method("getChannelData", 1, [](Realm& r, Args args) {
        require_args(args, 1, "AudioBuffer.getChannelData");
        auto& st = this_state<AudioBufferState>("AudioBuffer");
        const int ch = static_cast<int>(to_double(args[0]));
        if (ch < 0 || ch >= st.data.number_of_channels) {
            throw_dom(r, "IndexSizeError", "Channel index out of bounds");
        }
        return make_float32_array_from_floats(r, st.data.channels[static_cast<std::size_t>(ch)]);
    });
    I.method("copyFromChannel", 2, [](Realm& r, Args args) {
        require_args(args, 2, "AudioBuffer.copyFromChannel");
        auto& st = this_state<AudioBufferState>("AudioBuffer");
        const int ch = static_cast<int>(to_double(args[1]));
        const std::size_t start = args.size() >= 3 ? static_cast<std::size_t>(std::max(0.0, to_double(args[2]))) : 0u;
        if (ch < 0 || ch >= st.data.number_of_channels) throw_dom(r, "IndexSizeError", "Channel index out of bounds");
        BufferViewInfo info;
        if (!buffer_view_info(args[0], info)) js_throw_type_error("Expected Float32Array destination");
        const auto& src = st.data.channels[static_cast<std::size_t>(ch)];
        const std::size_t dest_floats = info.byte_length / sizeof(float);
        const std::size_t avail = start < src.size() ? (src.size() - start) : 0u;
        const std::size_t count = std::min(dest_floats, avail);
        if (count > 0) {
            write_bytes_to_buffer_source(args[0], reinterpret_cast<const uint8_t*>(src.data() + start), count * sizeof(float));
        }
        return undefined();
    });
    I.method("copyToChannel", 2, [](Realm& r, Args args) {
        require_args(args, 2, "AudioBuffer.copyToChannel");
        auto& st = this_state<AudioBufferState>("AudioBuffer");
        const int ch = static_cast<int>(to_double(args[1]));
        const std::size_t start = args.size() >= 3 ? static_cast<std::size_t>(std::max(0.0, to_double(args[2]))) : 0u;
        if (ch < 0 || ch >= st.data.number_of_channels) throw_dom(r, "IndexSizeError", "Channel index out of bounds");
        std::vector<uint8_t> bytes;
        if (!read_bytes_from_buffer_source(args[0], bytes)) js_throw_type_error("Expected Float32Array source");
        auto& dst = st.data.channels[static_cast<std::size_t>(ch)];
        const std::size_t src_floats = bytes.size() / sizeof(float);
        const std::size_t avail = start < dst.size() ? (dst.size() - start) : 0u;
        const std::size_t count = std::min(src_floats, avail);
        if (count > 0) {
            std::memcpy(dst.data() + start, bytes.data(), count * sizeof(float));
        }
        return undefined();
    });
}

void setup_audio_node(Iface& I) {
    I.getter("context", [](Realm&) { return this_state<AudioNodeState>("AudioNode").context_obj; });
    I.getter("numberOfInputs", [](Realm&) { return num(this_state<AudioNodeState>("AudioNode").number_of_inputs); });
    I.getter("numberOfOutputs", [](Realm&) { return num(this_state<AudioNodeState>("AudioNode").number_of_outputs); });
    I.accessor("channelCount",
        [](Realm&) { return num(this_state<AudioNodeState>("AudioNode").channel_count); },
        [](Realm& r, const JsValue& v) {
            const int c = static_cast<int>(to_double(v));
            if (c < 1 || c > 32) throw_dom(r, "NotSupportedError", "Invalid channelCount");
            this_state<AudioNodeState>("AudioNode").channel_count = c;
        });
    I.accessor("channelCountMode",
        [](Realm&) { return str(this_state<AudioNodeState>("AudioNode").channel_count_mode); },
        [](Realm&, const JsValue& v) { this_state<AudioNodeState>("AudioNode").channel_count_mode = to_str(v); });
    I.accessor("channelInterpretation",
        [](Realm&) { return str(this_state<AudioNodeState>("AudioNode").channel_interpretation); },
        [](Realm&, const JsValue& v) { this_state<AudioNodeState>("AudioNode").channel_interpretation = to_str(v); });
    I.method("connect", 1, [](Realm&, Args args) {
        require_args(args, 1, "AudioNode.connect");
        auto& self = this_state<AudioNodeState>("AudioNode");
        if (args[0].is_object() && args[0].object_val && args[0].object_val->exotic) {
            if (auto target = std::dynamic_pointer_cast<AudioNodeState>(args[0].object_val->exotic)) {
                self.outputs.push_back(target);
                return args[0];
            }
        }
        return args[0];
    });
    I.method("disconnect", 0, [](Realm&, Args) {
        this_state<AudioNodeState>("AudioNode").outputs.clear();
        return undefined();
    });
}

void setup_audio_destination_node(Iface& I) {
    I.getter("maxChannelCount", [](Realm&) {
        auto& st = this_state<AudioNodeState>("AudioDestinationNode");
        if (auto ctx = st.ctx.lock()) return num(ctx->number_of_channels);
        return num(2);
    });
}

void setup_gain_node(Iface& I) {
    I.getter("gain", [](Realm&) { return this_state<AudioNodeState>("GainNode").gain_obj; });
}

void setup_oscillator_node(Iface& I) {
    I.accessor("type",
        [](Realm&) { return str(this_state<AudioNodeState>("OscillatorNode").osc_type); },
        [](Realm&, const JsValue& v) {
            const std::string t = to_str(v);
            if (t != "sine" && t != "square" && t != "sawtooth" && t != "triangle") {
                js_throw_type_error("Invalid OscillatorType: " + t);
            }
            this_state<AudioNodeState>("OscillatorNode").osc_type = t;
        });
    I.getter("frequency", [](Realm&) { return this_state<AudioNodeState>("OscillatorNode").freq_obj; });
    I.getter("detune", [](Realm&) { return this_state<AudioNodeState>("OscillatorNode").detune_obj; });
    I.method("start", 0, [](Realm&, Args args) {
        auto& st = this_state<AudioNodeState>("OscillatorNode");
        st.started = true;
        st.start_time = (!args.empty() && !args[0].is_undefined()) ? std::max(0.0, to_double(args[0])) : 0.0;
        return undefined();
    });
    I.method("stop", 0, [](Realm&, Args args) {
        auto& st = this_state<AudioNodeState>("OscillatorNode");
        st.stop_time = (!args.empty() && !args[0].is_undefined()) ? std::max(0.0, to_double(args[0])) : 0.0;
        return undefined();
    });
}

void setup_buffer_source_node(Iface& I) {
    I.accessor("buffer",
        [](Realm&) { return this_state<AudioNodeState>("AudioBufferSourceNode").buffer_obj; },
        [](Realm&, const JsValue& v) {
            auto& st = this_state<AudioNodeState>("AudioBufferSourceNode");
            if (v.is_nullish()) {
                st.buffer_state.reset();
                st.buffer_obj = null();
                return;
            }
            if (v.is_object() && v.object_val && v.object_val->exotic) {
                if (auto b = std::dynamic_pointer_cast<AudioBufferState>(v.object_val->exotic)) {
                    st.buffer_state = b;
                    st.buffer_obj = v;
                    return;
                }
            }
            js_throw_type_error("AudioBufferSourceNode.buffer must be an AudioBuffer or null");
        });
    I.getter("playbackRate", [](Realm&) { return this_state<AudioNodeState>("AudioBufferSourceNode").playback_rate_obj; });
    I.getter("detune", [](Realm&) { return this_state<AudioNodeState>("AudioBufferSourceNode").detune_obj; });
    I.accessor("loop",
        [](Realm&) { return boolean(this_state<AudioNodeState>("AudioBufferSourceNode").loop); },
        [](Realm&, const JsValue& v) { this_state<AudioNodeState>("AudioBufferSourceNode").loop = to_bool(v); });
    I.accessor("loopStart",
        [](Realm&) { return num(this_state<AudioNodeState>("AudioBufferSourceNode").loop_start); },
        [](Realm&, const JsValue& v) { this_state<AudioNodeState>("AudioBufferSourceNode").loop_start = to_double(v); });
    I.accessor("loopEnd",
        [](Realm&) { return num(this_state<AudioNodeState>("AudioBufferSourceNode").loop_end); },
        [](Realm&, const JsValue& v) { this_state<AudioNodeState>("AudioBufferSourceNode").loop_end = to_double(v); });
    I.method("start", 0, [](Realm&, Args args) {
        auto& st = this_state<AudioNodeState>("AudioBufferSourceNode");
        st.started = true;
        st.start_time = (!args.empty() && !args[0].is_undefined()) ? std::max(0.0, to_double(args[0])) : 0.0;
        return undefined();
    });
    I.method("stop", 0, [](Realm&, Args args) {
        auto& st = this_state<AudioNodeState>("AudioBufferSourceNode");
        st.stop_time = (!args.empty() && !args[0].is_undefined()) ? std::max(0.0, to_double(args[0])) : 0.0;
        return undefined();
    });
}

void setup_analyser_node(Iface& I) {
    I.accessor("fftSize",
        [](Realm&) { return num(this_state<AudioNodeState>("AnalyserNode").fft_size); },
        [](Realm& r, const JsValue& v) {
            const uint32_t sz = to_ulong(v);
            if (sz < 32 || sz > 32768 || (sz & (sz - 1)) != 0) {
                throw_dom(r, "IndexSizeError", "fftSize must be a power of 2 in [32, 32768]");
            }
            this_state<AudioNodeState>("AnalyserNode").fft_size = sz;
        });
    I.getter("frequencyBinCount", [](Realm&) { return num(this_state<AudioNodeState>("AnalyserNode").fft_size / 2u); });
    I.accessor("minDecibels",
        [](Realm&) { return num(this_state<AudioNodeState>("AnalyserNode").min_decibels); },
        [](Realm&, const JsValue& v) { this_state<AudioNodeState>("AnalyserNode").min_decibels = to_double(v); });
    I.accessor("maxDecibels",
        [](Realm&) { return num(this_state<AudioNodeState>("AnalyserNode").max_decibels); },
        [](Realm&, const JsValue& v) { this_state<AudioNodeState>("AnalyserNode").max_decibels = to_double(v); });
    I.accessor("smoothingTimeConstant",
        [](Realm&) { return num(this_state<AudioNodeState>("AnalyserNode").smoothing); },
        [](Realm&, const JsValue& v) { this_state<AudioNodeState>("AnalyserNode").smoothing = to_double(v); });
    auto fill_zero = [](Realm&, Args) { return undefined(); };
    I.method("getByteFrequencyData", 1, fill_zero);
    I.method("getFloatFrequencyData", 1, fill_zero);
    I.method("getByteTimeDomainData", 1, fill_zero);
    I.method("getFloatTimeDomainData", 1, fill_zero);
}

void setup_biquad_filter_node(Iface& I) {
    I.accessor("type",
        [](Realm&) { return str(this_state<AudioNodeState>("BiquadFilterNode").filter_type); },
        [](Realm&, const JsValue& v) { this_state<AudioNodeState>("BiquadFilterNode").filter_type = to_str(v); });
    I.getter("frequency", [](Realm&) { return this_state<AudioNodeState>("BiquadFilterNode").freq_obj; });
    I.getter("detune", [](Realm&) { return this_state<AudioNodeState>("BiquadFilterNode").detune_obj; });
    I.getter("Q", [](Realm&) { return this_state<AudioNodeState>("BiquadFilterNode").q_obj; });
    I.getter("gain", [](Realm&) { return this_state<AudioNodeState>("BiquadFilterNode").gain_obj; });
}

std::shared_ptr<BaseAudioContextState> this_audio_ctx_ptr() {
    JsValue self = JsValue::current_this();
    if (!self.is_object() || !self.object_val || !self.object_val->exotic) {
        js_throw_type_error("Illegal invocation on BaseAudioContext");
    }
    auto ptr = std::dynamic_pointer_cast<BaseAudioContextState>(self.object_val->exotic);
    if (!ptr) js_throw_type_error("Illegal invocation on BaseAudioContext");
    return ptr;
}

void setup_base_audio_context(Iface& I) {
    I.getter("destination", [](Realm&) { return this_audio_ctx_ptr()->destination_obj; });
    I.getter("sampleRate", [](Realm&) { return num(this_audio_ctx_ptr()->sample_rate); });
    I.getter("currentTime", [](Realm&) { return num(this_audio_ctx_ptr()->current_time); });
    I.getter("state", [](Realm&) { return str(this_audio_ctx_ptr()->state); });

    I.method("createBuffer", 3, [](Realm& r, Args args) {
        require_args(args, 3, "BaseAudioContext.createBuffer");
        const int num_ch = static_cast<int>(to_double(args[0]));
        const double len_d = to_double(args[1]);
        const double sr = to_double(args[2]);
        if (num_ch < 1 || num_ch > 32) throw_dom(r, "NotSupportedError", "Invalid numberOfChannels");
        if (!std::isfinite(len_d) || len_d < 1.0 || len_d > 64.0 * 1024.0 * 1024.0) {
            throw_dom(r, "NotSupportedError", "Invalid length");
        }
        if (!std::isfinite(sr) || sr < 3000.0 || sr > 768000.0) {
            throw_dom(r, "NotSupportedError", "Invalid sampleRate");
        }
        auto st = std::make_shared<AudioBufferState>();
        st->data.number_of_channels = num_ch;
        st->data.length = static_cast<std::size_t>(len_d);
        st->data.sample_rate = sr;
        st->data.channels.assign(static_cast<std::size_t>(num_ch), std::vector<float>(st->data.length, 0.0f));
        return r.make("AudioBuffer", st);
    });

    I.method("createGain", 0, [](Realm& r, Args) {
        auto ctx = this_audio_ctx_ptr();
        auto st = std::make_shared<AudioNodeState>();
        st->kind = AudioNodeKind::Gain;
        st->ctx = ctx;
        st->context_obj = JsValue::current_this();
        st->gain_obj = make_audio_param(r, 1.0, st->gain_param);
        ctx->nodes.push_back(st);
        return r.make("GainNode", st);
    });

    I.method("createOscillator", 0, [](Realm& r, Args) {
        auto ctx = this_audio_ctx_ptr();
        auto st = std::make_shared<AudioNodeState>();
        st->kind = AudioNodeKind::Oscillator;
        st->ctx = ctx;
        st->context_obj = JsValue::current_this();
        st->number_of_inputs = 0;
        st->freq_obj = make_audio_param(r, 440.0, st->freq_param);
        st->detune_obj = make_audio_param(r, 0.0, st->detune_param);
        ctx->nodes.push_back(st);
        return r.make("OscillatorNode", st);
    });

    I.method("createBufferSource", 0, [](Realm& r, Args) {
        auto ctx = this_audio_ctx_ptr();
        auto st = std::make_shared<AudioNodeState>();
        st->kind = AudioNodeKind::BufferSource;
        st->ctx = ctx;
        st->context_obj = JsValue::current_this();
        st->number_of_inputs = 0;
        st->playback_rate_obj = make_audio_param(r, 1.0, st->playback_rate_param);
        st->detune_obj = make_audio_param(r, 0.0, st->detune_param);
        ctx->nodes.push_back(st);
        return r.make("AudioBufferSourceNode", st);
    });

    I.method("createAnalyser", 0, [](Realm& r, Args) {
        auto ctx = this_audio_ctx_ptr();
        auto st = std::make_shared<AudioNodeState>();
        st->kind = AudioNodeKind::Analyser;
        st->ctx = ctx;
        st->context_obj = JsValue::current_this();
        ctx->nodes.push_back(st);
        return r.make("AnalyserNode", st);
    });

    I.method("createBiquadFilter", 0, [](Realm& r, Args) {
        auto ctx = this_audio_ctx_ptr();
        auto st = std::make_shared<AudioNodeState>();
        st->kind = AudioNodeKind::BiquadFilter;
        st->ctx = ctx;
        st->context_obj = JsValue::current_this();
        st->freq_obj = make_audio_param(r, 350.0, st->freq_param);
        st->detune_obj = make_audio_param(r, 0.0, st->detune_param);
        st->q_obj = make_audio_param(r, 1.0, st->q_param);
        st->gain_obj = make_audio_param(r, 0.0, st->gain_param);
        ctx->nodes.push_back(st);
        return r.make("BiquadFilterNode", st);
    });

    I.method("decodeAudioData", 1, [](Realm& r, Args args) {
        require_args(args, 1, "BaseAudioContext.decodeAudioData");
        std::vector<uint8_t> bytes;
        if (!read_bytes_from_buffer_source(args[0], bytes)) {
            return make_rejected_promise(r, make_type_error_obj(r, "decodeAudioData requires an ArrayBuffer"));
        }
        WavAudio wav;
        std::string err;
        if (!decode_wav(bytes.data(), bytes.size(), wav, err)) {
            JsValue dom_err = make_dom_exception(r, "EncodingError", "Unable to decode audio data: " + err);
            if (args.size() >= 3 && args[2].is_function()) {
                js_call(args[2], undefined(), {dom_err});
            }
            return make_rejected_promise(r, dom_err);
        }
        auto buf_st = std::make_shared<AudioBufferState>();
        buf_st->data.number_of_channels = wav.num_channels;
        buf_st->data.length = wav.num_frames;
        buf_st->data.sample_rate = static_cast<double>(wav.sample_rate);
        buf_st->data.channels = std::move(wav.channels);
        JsValue audio_buf = r.make("AudioBuffer", buf_st);
        if (args.size() >= 2 && args[1].is_function()) {
            js_call(args[1], undefined(), {audio_buf});
        }
        return make_resolved_promise(r, audio_buf);
    });
}

JsValue init_audio_destination(Realm& r, const std::shared_ptr<BaseAudioContextState>& ctx, const JsValue& ctx_obj) {
    auto dst = std::make_shared<AudioNodeState>();
    dst->kind = AudioNodeKind::Destination;
    dst->ctx = ctx;
    dst->context_obj = ctx_obj;
    dst->number_of_inputs = 1;
    dst->number_of_outputs = 0;
    dst->channel_count = ctx->number_of_channels;
    ctx->destination_obj = r.make("AudioDestinationNode", dst);
    return ctx->destination_obj;
}

JsValue construct_audio_context(Realm& r, const InterfaceDef&, Args args, const JsValue* derived) {
    auto st = std::make_shared<BaseAudioContextState>();
    st->is_offline = false;
    st->sample_rate = 44100.0;
    if (!args.empty() && args[0].is_object_like()) {
        JsValue sr = args[0].get_property("sampleRate");
        if (!sr.is_undefined()) {
            const double d = to_double(sr);
            if (!std::isfinite(d) || d < 3000.0 || d > 768000.0) {
                throw_dom(r, "NotSupportedError", "Invalid AudioContext sampleRate");
            }
            st->sample_rate = d;
        }
    }
    JsValue obj = r.make("AudioContext", st, derived);
    init_audio_destination(r, st, obj);
    return obj;
}

void setup_audio_context(Iface& I) {
    I.getter("baseLatency", [](Realm&) { return num(0.01); });
    I.getter("outputLatency", [](Realm&) { return num(0.0); });
    I.method("suspend", 0, [](Realm& r, Args) {
        auto ctx = this_audio_ctx_ptr();
        if (ctx->state == "closed") return make_rejected_dom_promise(r, "InvalidStateError", "AudioContext is closed");
        ctx->state = "suspended";
        return make_resolved_promise(r, undefined());
    });
    I.method("resume", 0, [](Realm& r, Args) {
        auto ctx = this_audio_ctx_ptr();
        if (ctx->state == "closed") return make_rejected_dom_promise(r, "InvalidStateError", "AudioContext is closed");
        ctx->state = "running";
        return make_resolved_promise(r, undefined());
    });
    I.method("close", 0, [](Realm& r, Args) {
        auto ctx = this_audio_ctx_ptr();
        ctx->state = "closed";
        return make_resolved_promise(r, undefined());
    });
}

JsValue construct_offline_audio_context(Realm& r, const InterfaceDef&, Args args, const JsValue* derived) {
    require_args(args, 1, "OfflineAudioContext");
    int channels = 1;
    double length_d = 0.0;
    double sr = 44100.0;
    if (args.size() == 1 && args[0].is_object_like()) {
        JsValue ch_v = args[0].get_property("numberOfChannels");
        channels = ch_v.is_undefined() ? 1 : static_cast<int>(to_double(ch_v));
        length_d = to_double(args[0].get_property("length"));
        sr = to_double(args[0].get_property("sampleRate"));
    } else {
        require_args(args, 3, "OfflineAudioContext");
        channels = static_cast<int>(to_double(args[0]));
        length_d = to_double(args[1]);
        sr = to_double(args[2]);
    }
    if (channels < 1 || channels > 32) throw_dom(r, "NotSupportedError", "Invalid OfflineAudioContext numberOfChannels");
    if (!std::isfinite(length_d) || length_d < 1.0 || length_d > 64.0 * 1024.0 * 1024.0) {
        throw_dom(r, "NotSupportedError", "Invalid OfflineAudioContext length");
    }
    if (!std::isfinite(sr) || sr < 3000.0 || sr > 768000.0) {
        throw_dom(r, "NotSupportedError", "Invalid OfflineAudioContext sampleRate");
    }
    auto st = std::make_shared<BaseAudioContextState>();
    st->is_offline = true;
    st->number_of_channels = channels;
    st->offline_length = static_cast<std::size_t>(length_d);
    st->sample_rate = sr;
    st->state = "suspended";
    JsValue obj = r.make("OfflineAudioContext", st, derived);
    init_audio_destination(r, st, obj);
    return obj;
}

// Compute effective gain from `node` to destination (0.0 if disconnected from destination).
double path_gain_to_destination(const AudioNodeState* node, int depth = 0) {
    if (!node || depth > 32) return 0.0;
    if (node->kind == AudioNodeKind::Destination) return 1.0;
    const double local_gain = (node->kind == AudioNodeKind::Gain && node->gain_param) ? node->gain_param->value : 1.0;
    double total = 0.0;
    for (const auto& w : node->outputs) {
        if (auto next = w.lock()) {
            total += path_gain_to_destination(next.get(), depth + 1);
        }
    }
    return local_gain * total;
}

void setup_offline_audio_context(Iface& I) {
    I.getter("length", [](Realm&) {
        return num(static_cast<double>(this_audio_ctx_ptr()->offline_length));
    });
    I.method("startRendering", 0, [](Realm& r, Args) {
        auto ctx = this_audio_ctx_ptr();
        if (ctx->rendering_started) {
            return make_rejected_dom_promise(r, "InvalidStateError", "startRendering has already been called");
        }
        ctx->rendering_started = true;
        ctx->state = "running";

        std::vector<OfflineOscillatorSpec> oscs;
        std::vector<OfflineBufferSourceSpec> srcs;
        for (const auto& w : ctx->nodes) {
            auto node = w.lock();
            if (!node || !node->started) continue;
            const double g = path_gain_to_destination(node.get());
            if (g == 0.0) continue;
            if (node->kind == AudioNodeKind::Oscillator) {
                OfflineOscillatorSpec spec;
                if (node->osc_type == "square") spec.type = AudioOscillatorType::Square;
                else if (node->osc_type == "sawtooth") spec.type = AudioOscillatorType::Sawtooth;
                else if (node->osc_type == "triangle") spec.type = AudioOscillatorType::Triangle;
                else spec.type = AudioOscillatorType::Sine;
                spec.frequency = node->freq_param ? node->freq_param->value : 440.0;
                spec.detune = node->detune_param ? node->detune_param->value : 0.0;
                spec.start_time = node->start_time;
                spec.stop_time = node->stop_time;
                spec.gain = g;
                oscs.push_back(spec);
            } else if (node->kind == AudioNodeKind::BufferSource && node->buffer_state) {
                OfflineBufferSourceSpec spec;
                spec.buffer = &node->buffer_state->data;
                spec.playback_rate = node->playback_rate_param ? node->playback_rate_param->value : 1.0;
                spec.loop = node->loop;
                spec.start_time = node->start_time;
                spec.stop_time = node->stop_time;
                spec.gain = g;
                srcs.push_back(spec);
            }
        }

        auto out_st = std::make_shared<AudioBufferState>();
        out_st->data.number_of_channels = ctx->number_of_channels;
        out_st->data.length = ctx->offline_length;
        out_st->data.sample_rate = ctx->sample_rate;
        render_offline_audio_graph(out_st->data, oscs, srcs);
        ctx->current_time = static_cast<double>(ctx->offline_length) / ctx->sample_rate;
        ctx->state = "closed";
        return make_resolved_promise(r, r.make("AudioBuffer", out_st));
    });
}

// ============================================================================
// 3. W3C WebCodecs (§§3–9)
// ============================================================================

struct VideoFrameState : JsExotic {
    std::string format;
    int coded_width{0};
    int coded_height{0};
    int display_width{0};
    int display_height{0};
    int64_t timestamp{0};
    bool has_duration{false};
    int64_t duration{0};
    std::vector<VideoPlaneLayout> planes;
    std::vector<uint8_t> data;
    bool closed{false};
};

struct AudioDataState : JsExotic {
    std::string format;
    double sample_rate{0.0};
    uint32_t number_of_frames{0};
    uint32_t number_of_channels{0};
    int64_t timestamp{0};
    std::size_t bytes_per_sample{0};
    bool is_planar{false};
    std::vector<uint8_t> data;
    bool closed{false};
};

struct EncodedChunkState : JsExotic {
    bool is_video{true};
    std::string type; // "key" | "delta"
    int64_t timestamp{0};
    bool has_duration{false};
    int64_t duration{0};
    std::vector<uint8_t> data;
};

struct VideoCodecState : JsExotic {
    bool is_encoder{false};
    std::string state{"unconfigured"};
    std::string codec;
    int coded_width{0};
    int coded_height{0};
    uint32_t queue_size{0};
    JsValue output_cb{JsValue::make_undefined()};
    JsValue error_cb{JsValue::make_undefined()};

    void trace(Heap& heap) override {
        heap.mark_value(output_cb);
        heap.mark_value(error_cb);
    }
};

struct AudioCodecState : JsExotic {
    bool is_encoder{false};
    std::string state{"unconfigured"};
    std::string codec;
    double sample_rate{0.0};
    uint32_t number_of_channels{0};
    uint32_t queue_size{0};
    JsValue output_cb{JsValue::make_undefined()};
    JsValue error_cb{JsValue::make_undefined()};

    void trace(Heap& heap) override {
        heap.mark_value(output_cb);
        heap.mark_value(error_cb);
    }
};

JsValue make_rect_obj(int x, int y, int w, int h) {
    JsValue o = JsValue::make_object();
    o.set_property("x", num(x));
    o.set_property("y", num(y));
    o.set_property("width", num(w));
    o.set_property("height", num(h));
    o.set_property("top", num(y));
    o.set_property("left", num(x));
    o.set_property("right", num(x + w));
    o.set_property("bottom", num(y + h));
    return o;
}

JsValue construct_video_frame(Realm& r, const InterfaceDef&, Args args, const JsValue* derived) {
    require_args(args, 1, "VideoFrame");
    // Clone from existing VideoFrame: new VideoFrame(otherFrame, init?)
    if (auto* other = state_of<VideoFrameState>(args[0])) {
        if (other->closed) throw_dom(r, "InvalidStateError", "Cannot construct VideoFrame from a closed VideoFrame");
        auto st = std::make_shared<VideoFrameState>(*other);
        if (args.size() >= 2 && args[1].is_object_like()) {
            JsValue ts = args[1].get_property("timestamp");
            if (!ts.is_undefined()) st->timestamp = static_cast<int64_t>(to_double(ts));
            JsValue dur = args[1].get_property("duration");
            if (!dur.is_undefined()) { st->has_duration = true; st->duration = static_cast<int64_t>(to_double(dur)); }
        }
        return r.make("VideoFrame", st, derived);
    }
    // Construct from raw BufferSource + VideoFrameBufferInit
    if (is_buffer_source(args[0])) {
        require_args(args, 2, "VideoFrame");
        if (!args[1].is_object_like()) js_throw_type_error("VideoFrame buffer init dictionary is required");
        const JsValue& init = args[1];
        const std::string fmt = to_str(init.get_property("format"));
        const int cw = static_cast<int>(to_double(init.get_property("codedWidth")));
        const int ch = static_cast<int>(to_double(init.get_property("codedHeight")));
        const JsValue ts_v = init.get_property("timestamp");
        if (ts_v.is_undefined()) js_throw_type_error("VideoFrame init.timestamp is required");
        const int64_t ts = static_cast<int64_t>(to_double(ts_v));

        std::vector<VideoPlaneLayout> planes;
        std::size_t needed = 0;
        if (!video_pixel_format_layout(fmt, cw, ch, planes, needed)) {
            js_throw_type_error("Invalid VideoFrame format or dimensions");
        }
        std::vector<uint8_t> raw;
        if (!read_bytes_from_buffer_source(args[0], raw) || raw.size() < needed) {
            js_throw_type_error("VideoFrame source buffer is smaller than required size");
        }
        auto st = std::make_shared<VideoFrameState>();
        st->format = fmt;
        st->coded_width = cw;
        st->coded_height = ch;
        JsValue dw = init.get_property("displayWidth");
        JsValue dh = init.get_property("displayHeight");
        st->display_width = dw.is_undefined() ? cw : std::max(1, static_cast<int>(to_double(dw)));
        st->display_height = dh.is_undefined() ? ch : std::max(1, static_cast<int>(to_double(dh)));
        st->timestamp = ts;
        JsValue dur = init.get_property("duration");
        if (!dur.is_undefined()) {
            st->has_duration = true;
            st->duration = static_cast<int64_t>(to_double(dur));
        }
        st->planes = std::move(planes);
        st->data.assign(raw.begin(), raw.begin() + static_cast<std::ptrdiff_t>(needed));
        return r.make("VideoFrame", st, derived);
    }
    js_throw_type_error("Unsupported VideoFrame source");
    return undefined();
}

void setup_video_frame(Iface& I) {
    I.getter("format", [](Realm&) {
        auto& st = this_state<VideoFrameState>("VideoFrame");
        return st.closed ? null() : str(st.format);
    });
    I.getter("codedWidth", [](Realm&) {
        auto& st = this_state<VideoFrameState>("VideoFrame");
        return num(st.closed ? 0 : st.coded_width);
    });
    I.getter("codedHeight", [](Realm&) {
        auto& st = this_state<VideoFrameState>("VideoFrame");
        return num(st.closed ? 0 : st.coded_height);
    });
    I.getter("displayWidth", [](Realm&) {
        auto& st = this_state<VideoFrameState>("VideoFrame");
        return num(st.closed ? 0 : st.display_width);
    });
    I.getter("displayHeight", [](Realm&) {
        auto& st = this_state<VideoFrameState>("VideoFrame");
        return num(st.closed ? 0 : st.display_height);
    });
    I.getter("codedRect", [](Realm&) {
        auto& st = this_state<VideoFrameState>("VideoFrame");
        return st.closed ? null() : make_rect_obj(0, 0, st.coded_width, st.coded_height);
    });
    I.getter("visibleRect", [](Realm&) {
        auto& st = this_state<VideoFrameState>("VideoFrame");
        return st.closed ? null() : make_rect_obj(0, 0, st.coded_width, st.coded_height);
    });
    I.getter("timestamp", [](Realm&) {
        return num(static_cast<double>(this_state<VideoFrameState>("VideoFrame").timestamp));
    });
    I.getter("duration", [](Realm&) {
        auto& st = this_state<VideoFrameState>("VideoFrame");
        return st.has_duration ? num(static_cast<double>(st.duration)) : null();
    });
    I.method("allocationSize", 0, [](Realm& r, Args) {
        auto& st = this_state<VideoFrameState>("VideoFrame");
        if (st.closed) throw_dom(r, "InvalidStateError", "VideoFrame is closed");
        return num(static_cast<double>(st.data.size()));
    });
    I.method("copyTo", 1, [](Realm& r, Args args) {
        require_args(args, 1, "VideoFrame.copyTo");
        auto& st = this_state<VideoFrameState>("VideoFrame");
        if (st.closed) return make_rejected_dom_promise(r, "InvalidStateError", "VideoFrame is closed");
        if (!write_bytes_to_buffer_source(args[0], st.data.data(), st.data.size())) {
            return make_rejected_promise(r, make_type_error_obj(r, "VideoFrame.copyTo destination is invalid or too small"));
        }
        std::vector<JsValue> layout_elems;
        layout_elems.reserve(st.planes.size());
        for (const auto& p : st.planes) {
            JsValue po = JsValue::make_object();
            po.set_property("offset", num(static_cast<double>(p.offset)));
            po.set_property("stride", num(static_cast<double>(p.stride)));
            layout_elems.push_back(po);
        }
        return make_resolved_promise(r, JsValue::make_array(layout_elems));
    });
    I.method("clone", 0, [](Realm& r, Args) {
        auto& st = this_state<VideoFrameState>("VideoFrame");
        if (st.closed) throw_dom(r, "InvalidStateError", "Cannot clone a closed VideoFrame");
        return r.make("VideoFrame", std::make_shared<VideoFrameState>(st));
    });
    I.method("close", 0, [](Realm&, Args) {
        auto& st = this_state<VideoFrameState>("VideoFrame");
        st.closed = true;
        st.data.clear();
        st.planes.clear();
        return undefined();
    });
}

JsValue construct_audio_data(Realm& r, const InterfaceDef&, Args args, const JsValue* derived) {
    require_args(args, 1, "AudioData");
    if (!args[0].is_object_like()) js_throw_type_error("AudioData init dictionary is required");
    const JsValue& init = args[0];
    const std::string fmt = to_str(init.get_property("format"));
    std::size_t bps = 0;
    bool planar = false;
    if (!audio_sample_format_info(fmt, bps, planar)) {
        js_throw_type_error("Invalid AudioSampleFormat: " + fmt);
    }
    const double sr = to_double(init.get_property("sampleRate"));
    const double nf = to_double(init.get_property("numberOfFrames"));
    const double nc = to_double(init.get_property("numberOfChannels"));
    const JsValue ts_v = init.get_property("timestamp");
    if (!std::isfinite(sr) || sr <= 0.0 || !std::isfinite(nf) || nf < 1.0 ||
        !std::isfinite(nc) || nc < 1.0 || nc > 32.0 || ts_v.is_undefined()) {
        js_throw_type_error("Invalid AudioData init parameters");
    }
    const std::size_t frames = static_cast<std::size_t>(nf);
    const std::size_t chs = static_cast<std::size_t>(nc);
    if (frames > (64u * 1024u * 1024u) / (chs * bps)) {
        js_throw_type_error("AudioData exceeds memory budget");
    }
    const std::size_t needed = frames * chs * bps;
    std::vector<uint8_t> raw;
    if (!read_bytes_from_buffer_source(init.get_property("data"), raw) || raw.size() < needed) {
        js_throw_type_error("AudioData source buffer is smaller than required size");
    }
    auto st = std::make_shared<AudioDataState>();
    st->format = fmt;
    st->sample_rate = sr;
    st->number_of_frames = static_cast<uint32_t>(frames);
    st->number_of_channels = static_cast<uint32_t>(chs);
    st->timestamp = static_cast<int64_t>(to_double(ts_v));
    st->bytes_per_sample = bps;
    st->is_planar = planar;
    st->data.assign(raw.begin(), raw.begin() + static_cast<std::ptrdiff_t>(needed));
    return r.make("AudioData", st, derived);
}

void setup_audio_data(Iface& I) {
    I.getter("format", [](Realm&) {
        auto& st = this_state<AudioDataState>("AudioData");
        return st.closed ? null() : str(st.format);
    });
    I.getter("sampleRate", [](Realm&) {
        auto& st = this_state<AudioDataState>("AudioData");
        return num(st.closed ? 0.0 : st.sample_rate);
    });
    I.getter("numberOfFrames", [](Realm&) {
        auto& st = this_state<AudioDataState>("AudioData");
        return num(st.closed ? 0 : st.number_of_frames);
    });
    I.getter("numberOfChannels", [](Realm&) {
        auto& st = this_state<AudioDataState>("AudioData");
        return num(st.closed ? 0 : st.number_of_channels);
    });
    I.getter("duration", [](Realm&) {
        auto& st = this_state<AudioDataState>("AudioData");
        if (st.closed || st.sample_rate <= 0.0) return num(0);
        return num(std::floor(static_cast<double>(st.number_of_frames) * 1e6 / st.sample_rate));
    });
    I.getter("timestamp", [](Realm&) {
        return num(static_cast<double>(this_state<AudioDataState>("AudioData").timestamp));
    });
    I.method("allocationSize", 1, [](Realm& r, Args args) {
        require_args(args, 1, "AudioData.allocationSize");
        auto& st = this_state<AudioDataState>("AudioData");
        if (st.closed) throw_dom(r, "InvalidStateError", "AudioData is closed");
        const uint32_t plane_idx = args[0].is_object_like() ? to_ulong(args[0].get_property("planeIndex")) : 0u;
        const uint32_t plane_count = st.is_planar ? st.number_of_channels : 1u;
        if (plane_idx >= plane_count) throw_dom(r, "RangeError", "AudioData planeIndex out of range");
        const std::size_t plane_bytes = st.is_planar
            ? static_cast<std::size_t>(st.number_of_frames) * st.bytes_per_sample
            : st.data.size();
        return num(static_cast<double>(plane_bytes));
    });
    I.method("copyTo", 2, [](Realm& r, Args args) {
        require_args(args, 2, "AudioData.copyTo");
        auto& st = this_state<AudioDataState>("AudioData");
        if (st.closed) throw_dom(r, "InvalidStateError", "AudioData is closed");
        const uint32_t plane_idx = args[1].is_object_like() ? to_ulong(args[1].get_property("planeIndex")) : 0u;
        const uint32_t plane_count = st.is_planar ? st.number_of_channels : 1u;
        if (plane_idx >= plane_count) js_throw_range_error("AudioData planeIndex out of range");
        const std::size_t plane_bytes = st.is_planar
            ? static_cast<std::size_t>(st.number_of_frames) * st.bytes_per_sample
            : st.data.size();
        const std::size_t offset = st.is_planar ? plane_idx * plane_bytes : 0u;
        if (!write_bytes_to_buffer_source(args[0], st.data.data() + offset, plane_bytes)) {
            js_throw_type_error("AudioData.copyTo destination is invalid or too small");
        }
        return undefined();
    });
    I.method("clone", 0, [](Realm& r, Args) {
        auto& st = this_state<AudioDataState>("AudioData");
        if (st.closed) throw_dom(r, "InvalidStateError", "Cannot clone closed AudioData");
        return r.make("AudioData", std::make_shared<AudioDataState>(st));
    });
    I.method("close", 0, [](Realm&, Args) {
        auto& st = this_state<AudioDataState>("AudioData");
        st.closed = true;
        st.data.clear();
        return undefined();
    });
}

JsValue construct_encoded_chunk(Realm& r, bool is_video, Args args, const JsValue* derived) {
    const char* iface = is_video ? "EncodedVideoChunk" : "EncodedAudioChunk";
    require_args(args, 1, iface);
    if (!args[0].is_object_like()) js_throw_type_error(std::string(iface) + " init dictionary is required");
    const JsValue& init = args[0];
    const std::string type = to_str(init.get_property("type"));
    if (type != "key" && type != "delta") {
        js_throw_type_error(std::string("Invalid ") + iface + ".type: " + type);
    }
    const JsValue ts_v = init.get_property("timestamp");
    if (ts_v.is_undefined()) js_throw_type_error(std::string(iface) + " init.timestamp is required");
    std::vector<uint8_t> bytes;
    if (!read_bytes_from_buffer_source(init.get_property("data"), bytes)) {
        js_throw_type_error(std::string(iface) + " init.data must be a BufferSource");
    }
    auto st = std::make_shared<EncodedChunkState>();
    st->is_video = is_video;
    st->type = type;
    st->timestamp = static_cast<int64_t>(to_double(ts_v));
    JsValue dur = init.get_property("duration");
    if (!dur.is_undefined() && !dur.is_nullish()) {
        st->has_duration = true;
        st->duration = static_cast<int64_t>(to_double(dur));
    }
    st->data = std::move(bytes);
    return r.make(iface, st, derived);
}

JsValue construct_encoded_video_chunk(Realm& r, const InterfaceDef&, Args args, const JsValue* derived) {
    return construct_encoded_chunk(r, true, args, derived);
}

JsValue construct_encoded_audio_chunk(Realm& r, const InterfaceDef&, Args args, const JsValue* derived) {
    return construct_encoded_chunk(r, false, args, derived);
}

void setup_encoded_chunk(Iface& I) {
    I.getter("type", [](Realm&) { return str(this_state<EncodedChunkState>("EncodedChunk").type); });
    I.getter("timestamp", [](Realm&) { return num(static_cast<double>(this_state<EncodedChunkState>("EncodedChunk").timestamp)); });
    I.getter("duration", [](Realm&) {
        auto& st = this_state<EncodedChunkState>("EncodedChunk");
        return st.has_duration ? num(static_cast<double>(st.duration)) : null();
    });
    I.getter("byteLength", [](Realm&) { return num(static_cast<double>(this_state<EncodedChunkState>("EncodedChunk").data.size())); });
    I.method("copyTo", 1, [](Realm&, Args args) {
        require_args(args, 1, "EncodedChunk.copyTo");
        auto& st = this_state<EncodedChunkState>("EncodedChunk");
        if (!write_bytes_to_buffer_source(args[0], st.data.data(), st.data.size())) {
            js_throw_type_error("EncodedChunk.copyTo destination is invalid or too small");
        }
        return undefined();
    });
}

// --- VideoDecoder / VideoEncoder / AudioDecoder / AudioEncoder --------------

JsValue clone_config_summary(const JsValue& cfg, const std::vector<const char*>& keys) {
    JsValue out = JsValue::make_object();
    for (const char* k : keys) {
        JsValue v = cfg.get_property(k);
        if (!v.is_undefined()) out.set_property(k, v);
    }
    return out;
}

JsValue construct_video_codec(Realm& r, bool is_encoder, Args args, const JsValue* derived) {
    const char* iface = is_encoder ? "VideoEncoder" : "VideoDecoder";
    require_args(args, 1, iface);
    if (!args[0].is_object_like()) js_throw_type_error(std::string(iface) + " init dictionary is required");
    JsValue out_cb = args[0].get_property("output");
    JsValue err_cb = args[0].get_property("error");
    if (!out_cb.is_function() || !err_cb.is_function()) {
        js_throw_type_error(std::string(iface) + " init requires output and error callbacks");
    }
    auto st = std::make_shared<VideoCodecState>();
    st->is_encoder = is_encoder;
    st->output_cb = out_cb;
    st->error_cb = err_cb;
    return r.make(iface, st, derived);
}

JsValue construct_video_decoder(Realm& r, const InterfaceDef&, Args args, const JsValue* derived) {
    return construct_video_codec(r, false, args, derived);
}

JsValue construct_video_encoder(Realm& r, const InterfaceDef&, Args args, const JsValue* derived) {
    return construct_video_codec(r, true, args, derived);
}

JsValue video_is_config_supported(Realm& r, Args args, bool is_encoder) {
    if (args.empty() || !args[0].is_object_like()) {
        return make_rejected_promise(r, make_type_error_obj(r, "Config dictionary is required"));
    }
    const JsValue& cfg = args[0];
    const JsValue codec_v = cfg.get_property("codec");
    if (codec_v.is_undefined()) {
        return make_rejected_promise(r, make_type_error_obj(r, "Config requires a codec string"));
    }
    const std::string codec = to_str(codec_v);
    bool supported = webcodecs_video_codec_supported(codec);
    const char* w_key = is_encoder ? "width" : "codedWidth";
    const char* h_key = is_encoder ? "height" : "codedHeight";
    JsValue wv = cfg.get_property(w_key);
    JsValue hv = cfg.get_property(h_key);
    if (!wv.is_undefined()) {
        const double w = to_double(wv);
        if (!std::isfinite(w) || w <= 0.0 || w > 16384.0) supported = false;
    } else if (is_encoder) {
        return make_rejected_promise(r, make_type_error_obj(r, "VideoEncoderConfig requires width"));
    }
    if (!hv.is_undefined()) {
        const double h = to_double(hv);
        if (!std::isfinite(h) || h <= 0.0 || h > 16384.0) supported = false;
    } else if (is_encoder) {
        return make_rejected_promise(r, make_type_error_obj(r, "VideoEncoderConfig requires height"));
    }
    JsValue res = JsValue::make_object();
    res.set_property("supported", boolean(supported));
    res.set_property("config", clone_config_summary(cfg, {"codec", "codedWidth", "codedHeight", "width", "height", "displayAspectWidth", "displayAspectHeight", "bitrate", "framerate"}));
    return make_resolved_promise(r, res);
}

// Raw internal frame magic emitted by VideoEncoder so encode -> decode roundtrips cleanly.
constexpr uint8_t kRawFrameMagic[4] = {'L', 'B', 'V', 'F'};

void setup_video_decoder(Iface& I) {
    I.static_method("isConfigSupported", 1, [](Realm& r, Args args) {
        return video_is_config_supported(r, args, false);
    });
    I.getter("state", [](Realm&) { return str(this_state<VideoCodecState>("VideoDecoder").state); });
    I.getter("decodeQueueSize", [](Realm&) { return num(this_state<VideoCodecState>("VideoDecoder").queue_size); });
    I.method("configure", 1, [](Realm& r, Args args) {
        require_args(args, 1, "VideoDecoder.configure");
        auto& st = this_state<VideoCodecState>("VideoDecoder");
        if (st.state == "closed") throw_dom(r, "InvalidStateError", "VideoDecoder is closed");
        if (!args[0].is_object_like()) js_throw_type_error("VideoDecoderConfig must be an object");
        const std::string codec = to_str(args[0].get_property("codec"));
        if (!webcodecs_video_codec_supported(codec)) {
            throw_dom(r, "NotSupportedError", "Unsupported VideoDecoder codec: " + codec);
        }
        st.codec = codec;
        JsValue cw = args[0].get_property("codedWidth");
        JsValue ch = args[0].get_property("codedHeight");
        st.coded_width = cw.is_undefined() ? 0 : static_cast<int>(to_double(cw));
        st.coded_height = ch.is_undefined() ? 0 : static_cast<int>(to_double(ch));
        st.state = "configured";
        return undefined();
    });
    I.method("decode", 1, [](Realm& r, Args args) {
        require_args(args, 1, "VideoDecoder.decode");
        auto& st = this_state<VideoCodecState>("VideoDecoder");
        if (st.state != "configured") throw_dom(r, "InvalidStateError", "VideoDecoder is not configured");
        auto* chunk = state_of<EncodedChunkState>(args[0]);
        if (!chunk || !chunk->is_video) js_throw_type_error("VideoDecoder.decode requires an EncodedVideoChunk");

        int w = 0, h = 0;
        std::vector<uint8_t> rgba;
        bool ok = false;
        std::string err;

        // 1. Check our internal lossless VideoEncoder packet format ("LBVF" + u32 w + u32 h + RGBA).
        if (chunk->data.size() >= 12 && std::memcmp(chunk->data.data(), kRawFrameMagic, 4) == 0) {
            uint32_t uw = 0, uh = 0;
            std::memcpy(&uw, chunk->data.data() + 4, 4);
            std::memcpy(&uh, chunk->data.data() + 8, 4);
            if (uw > 0 && uh > 0 && RasterBudget::allows(uw, uh)) {
                const std::size_t expected = static_cast<std::size_t>(uw) * uh * 4u;
                if (chunk->data.size() >= 12u + expected) {
                    w = static_cast<int>(uw);
                    h = static_cast<int>(uh);
                    rgba.assign(chunk->data.begin() + 12, chunk->data.begin() + 12 + static_cast<std::ptrdiff_t>(expected));
                    ok = true;
                }
            }
        }
        // 2. Real VP8 keyframe decode via LiteBird's Vp8Decoder!
        if (!ok && chunk->type == "key" && !chunk->data.empty()) {
            ok = decode_vp8_to_rgba(chunk->data.data(), chunk->data.size(), w, h, rgba, err);
        }

        if (ok) {
            auto vf = std::make_shared<VideoFrameState>();
            vf->format = "RGBA";
            vf->coded_width = w;
            vf->coded_height = h;
            vf->display_width = w;
            vf->display_height = h;
            vf->timestamp = chunk->timestamp;
            vf->has_duration = chunk->has_duration;
            vf->duration = chunk->duration;
            std::size_t total = 0;
            video_pixel_format_layout("RGBA", w, h, vf->planes, total);
            vf->data = std::move(rgba);
            js_call(st.output_cb, undefined(), {r.make("VideoFrame", vf)});
        } else {
            JsValue dom_err = make_dom_exception(r, "EncodingError", err.empty() ? "Failed to decode video chunk" : err);
            js_call(st.error_cb, undefined(), {dom_err});
        }
        return undefined();
    });
    I.method("flush", 0, [](Realm& r, Args) {
        auto& st = this_state<VideoCodecState>("VideoDecoder");
        if (st.state != "configured") return make_rejected_dom_promise(r, "InvalidStateError", "VideoDecoder is not configured");
        return make_resolved_promise(r, undefined());
    });
    I.method("reset", 0, [](Realm& r, Args) {
        auto& st = this_state<VideoCodecState>("VideoDecoder");
        if (st.state == "closed") throw_dom(r, "InvalidStateError", "VideoDecoder is closed");
        st.state = "unconfigured";
        st.queue_size = 0;
        return undefined();
    });
    I.method("close", 0, [](Realm&, Args) {
        auto& st = this_state<VideoCodecState>("VideoDecoder");
        st.state = "closed";
        st.queue_size = 0;
        return undefined();
    });
}

void setup_video_encoder(Iface& I) {
    I.static_method("isConfigSupported", 1, [](Realm& r, Args args) {
        return video_is_config_supported(r, args, true);
    });
    I.getter("state", [](Realm&) { return str(this_state<VideoCodecState>("VideoEncoder").state); });
    I.getter("encodeQueueSize", [](Realm&) { return num(this_state<VideoCodecState>("VideoEncoder").queue_size); });
    I.method("configure", 1, [](Realm& r, Args args) {
        require_args(args, 1, "VideoEncoder.configure");
        auto& st = this_state<VideoCodecState>("VideoEncoder");
        if (st.state == "closed") throw_dom(r, "InvalidStateError", "VideoEncoder is closed");
        if (!args[0].is_object_like()) js_throw_type_error("VideoEncoderConfig must be an object");
        const std::string codec = to_str(args[0].get_property("codec"));
        if (!webcodecs_video_codec_supported(codec)) {
            throw_dom(r, "NotSupportedError", "Unsupported VideoEncoder codec: " + codec);
        }
        st.codec = codec;
        st.coded_width = static_cast<int>(to_double(args[0].get_property("width")));
        st.coded_height = static_cast<int>(to_double(args[0].get_property("height")));
        st.state = "configured";
        return undefined();
    });
    I.method("encode", 1, [](Realm& r, Args args) {
        require_args(args, 1, "VideoEncoder.encode");
        auto& st = this_state<VideoCodecState>("VideoEncoder");
        if (st.state != "configured") throw_dom(r, "InvalidStateError", "VideoEncoder is not configured");
        auto* vf = state_of<VideoFrameState>(args[0]);
        if (!vf || vf->closed) throw_dom(r, "InvalidStateError", "VideoFrame is closed or invalid");
        bool key_frame = true;
        if (args.size() >= 2 && args[1].is_object_like()) {
            JsValue kf = args[1].get_property("keyFrame");
            if (!kf.is_undefined()) key_frame = to_bool(kf);
        }
        auto chunk = std::make_shared<EncodedChunkState>();
        chunk->is_video = true;
        chunk->type = key_frame ? "key" : "delta";
        chunk->timestamp = vf->timestamp;
        chunk->has_duration = vf->has_duration;
        chunk->duration = vf->duration;
        const uint32_t uw = static_cast<uint32_t>(vf->coded_width);
        const uint32_t uh = static_cast<uint32_t>(vf->coded_height);
        chunk->data.resize(12u + vf->data.size());
        std::memcpy(chunk->data.data(), kRawFrameMagic, 4);
        std::memcpy(chunk->data.data() + 4, &uw, 4);
        std::memcpy(chunk->data.data() + 8, &uh, 4);
        if (!vf->data.empty()) std::memcpy(chunk->data.data() + 12, vf->data.data(), vf->data.size());
        JsValue meta = JsValue::make_object();
        JsValue dcfg = JsValue::make_object();
        dcfg.set_property("codec", str(st.codec));
        dcfg.set_property("codedWidth", num(vf->coded_width));
        dcfg.set_property("codedHeight", num(vf->coded_height));
        meta.set_property("decoderConfig", dcfg);
        js_call(st.output_cb, undefined(), {r.make("EncodedVideoChunk", chunk), meta});
        return undefined();
    });
    I.method("flush", 0, [](Realm& r, Args) {
        auto& st = this_state<VideoCodecState>("VideoEncoder");
        if (st.state != "configured") return make_rejected_dom_promise(r, "InvalidStateError", "VideoEncoder is not configured");
        return make_resolved_promise(r, undefined());
    });
    I.method("reset", 0, [](Realm& r, Args) {
        auto& st = this_state<VideoCodecState>("VideoEncoder");
        if (st.state == "closed") throw_dom(r, "InvalidStateError", "VideoEncoder is closed");
        st.state = "unconfigured";
        st.queue_size = 0;
        return undefined();
    });
    I.method("close", 0, [](Realm&, Args) {
        auto& st = this_state<VideoCodecState>("VideoEncoder");
        st.state = "closed";
        st.queue_size = 0;
        return undefined();
    });
}

JsValue construct_audio_codec(Realm& r, bool is_encoder, Args args, const JsValue* derived) {
    const char* iface = is_encoder ? "AudioEncoder" : "AudioDecoder";
    require_args(args, 1, iface);
    if (!args[0].is_object_like()) js_throw_type_error(std::string(iface) + " init dictionary is required");
    JsValue out_cb = args[0].get_property("output");
    JsValue err_cb = args[0].get_property("error");
    if (!out_cb.is_function() || !err_cb.is_function()) {
        js_throw_type_error(std::string(iface) + " init requires output and error callbacks");
    }
    auto st = std::make_shared<AudioCodecState>();
    st->is_encoder = is_encoder;
    st->output_cb = out_cb;
    st->error_cb = err_cb;
    return r.make(iface, st, derived);
}

JsValue construct_audio_decoder(Realm& r, const InterfaceDef&, Args args, const JsValue* derived) {
    return construct_audio_codec(r, false, args, derived);
}

JsValue construct_audio_encoder(Realm& r, const InterfaceDef&, Args args, const JsValue* derived) {
    return construct_audio_codec(r, true, args, derived);
}

JsValue audio_is_config_supported(Realm& r, Args args) {
    if (args.empty() || !args[0].is_object_like()) {
        return make_rejected_promise(r, make_type_error_obj(r, "Config dictionary is required"));
    }
    const JsValue& cfg = args[0];
    const JsValue codec_v = cfg.get_property("codec");
    const JsValue sr_v = cfg.get_property("sampleRate");
    const JsValue ch_v = cfg.get_property("numberOfChannels");
    if (codec_v.is_undefined() || sr_v.is_undefined() || ch_v.is_undefined()) {
        return make_rejected_promise(r, make_type_error_obj(r, "Audio config requires codec, sampleRate, and numberOfChannels"));
    }
    const std::string codec = to_str(codec_v);
    const double sr = to_double(sr_v);
    const double ch = to_double(ch_v);
    bool supported = webcodecs_audio_codec_supported(codec) &&
                     std::isfinite(sr) && sr >= 3000.0 && sr <= 768000.0 &&
                     std::isfinite(ch) && ch >= 1.0 && ch <= 32.0;
    JsValue res = JsValue::make_object();
    res.set_property("supported", boolean(supported));
    res.set_property("config", clone_config_summary(cfg, {"codec", "sampleRate", "numberOfChannels", "bitrate"}));
    return make_resolved_promise(r, res);
}

constexpr uint8_t kRawAudioMagic[4] = {'L', 'B', 'A', 'D'};

void setup_audio_decoder(Iface& I) {
    I.static_method("isConfigSupported", 1, [](Realm& r, Args args) {
        return audio_is_config_supported(r, args);
    });
    I.getter("state", [](Realm&) { return str(this_state<AudioCodecState>("AudioDecoder").state); });
    I.getter("decodeQueueSize", [](Realm&) { return num(this_state<AudioCodecState>("AudioDecoder").queue_size); });
    I.method("configure", 1, [](Realm& r, Args args) {
        require_args(args, 1, "AudioDecoder.configure");
        auto& st = this_state<AudioCodecState>("AudioDecoder");
        if (st.state == "closed") throw_dom(r, "InvalidStateError", "AudioDecoder is closed");
        if (!args[0].is_object_like()) js_throw_type_error("AudioDecoderConfig must be an object");
        const std::string codec = to_str(args[0].get_property("codec"));
        if (!webcodecs_audio_codec_supported(codec)) {
            throw_dom(r, "NotSupportedError", "Unsupported AudioDecoder codec: " + codec);
        }
        st.codec = codec;
        st.sample_rate = to_double(args[0].get_property("sampleRate"));
        st.number_of_channels = to_ulong(args[0].get_property("numberOfChannels"));
        st.state = "configured";
        return undefined();
    });
    I.method("decode", 1, [](Realm& r, Args args) {
        require_args(args, 1, "AudioDecoder.decode");
        auto& st = this_state<AudioCodecState>("AudioDecoder");
        if (st.state != "configured") throw_dom(r, "InvalidStateError", "AudioDecoder is not configured");
        auto* chunk = state_of<EncodedChunkState>(args[0]);
        if (!chunk || chunk->is_video) js_throw_type_error("AudioDecoder.decode requires an EncodedAudioChunk");

        // Support our AudioEncoder lossless packet ("LBAD" + u32 frames + u32 chs + u32 sr + f32 samples).
        if (chunk->data.size() >= 16 && std::memcmp(chunk->data.data(), kRawAudioMagic, 4) == 0) {
            uint32_t nf = 0, nc = 0, sr = 0;
            std::memcpy(&nf, chunk->data.data() + 4, 4);
            std::memcpy(&nc, chunk->data.data() + 8, 4);
            std::memcpy(&sr, chunk->data.data() + 12, 4);
            const std::size_t payload = chunk->data.size() - 16u;
            if (nf > 0 && nc > 0 && nc <= 32 && sr > 0 && payload >= static_cast<std::size_t>(nf) * nc * 4u) {
                auto ad = std::make_shared<AudioDataState>();
                ad->format = "f32-planar";
                ad->sample_rate = static_cast<double>(sr);
                ad->number_of_frames = nf;
                ad->number_of_channels = nc;
                ad->timestamp = chunk->timestamp;
                ad->bytes_per_sample = 4;
                ad->is_planar = true;
                ad->data.assign(chunk->data.begin() + 16,
                                chunk->data.begin() + 16 + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(nf) * nc * 4u));
                js_call(st.output_cb, undefined(), {r.make("AudioData", ad)});
                return undefined();
            }
        }
        // Also support decoding WAV chunks directly.
        WavAudio wav;
        std::string err;
        if (decode_wav(chunk->data.data(), chunk->data.size(), wav, err) && wav.num_frames > 0) {
            auto ad = std::make_shared<AudioDataState>();
            ad->format = "f32-planar";
            ad->sample_rate = static_cast<double>(wav.sample_rate);
            ad->number_of_frames = static_cast<uint32_t>(wav.num_frames);
            ad->number_of_channels = wav.num_channels;
            ad->timestamp = chunk->timestamp;
            ad->bytes_per_sample = 4;
            ad->is_planar = true;
            ad->data.resize(wav.num_frames * wav.num_channels * sizeof(float));
            for (uint16_t ch = 0; ch < wav.num_channels; ++ch) {
                std::memcpy(ad->data.data() + ch * wav.num_frames * sizeof(float),
                            wav.channels[ch].data(), wav.num_frames * sizeof(float));
            }
            js_call(st.output_cb, undefined(), {r.make("AudioData", ad)});
            return undefined();
        }
        JsValue dom_err = make_dom_exception(r, "EncodingError", "Failed to decode audio chunk");
        js_call(st.error_cb, undefined(), {dom_err});
        return undefined();
    });
    I.method("flush", 0, [](Realm& r, Args) {
        auto& st = this_state<AudioCodecState>("AudioDecoder");
        if (st.state != "configured") return make_rejected_dom_promise(r, "InvalidStateError", "AudioDecoder is not configured");
        return make_resolved_promise(r, undefined());
    });
    I.method("reset", 0, [](Realm& r, Args) {
        auto& st = this_state<AudioCodecState>("AudioDecoder");
        if (st.state == "closed") throw_dom(r, "InvalidStateError", "AudioDecoder is closed");
        st.state = "unconfigured";
        st.queue_size = 0;
        return undefined();
    });
    I.method("close", 0, [](Realm&, Args) {
        auto& st = this_state<AudioCodecState>("AudioDecoder");
        st.state = "closed";
        st.queue_size = 0;
        return undefined();
    });
}

void setup_audio_encoder(Iface& I) {
    I.static_method("isConfigSupported", 1, [](Realm& r, Args args) {
        return audio_is_config_supported(r, args);
    });
    I.getter("state", [](Realm&) { return str(this_state<AudioCodecState>("AudioEncoder").state); });
    I.getter("encodeQueueSize", [](Realm&) { return num(this_state<AudioCodecState>("AudioEncoder").queue_size); });
    I.method("configure", 1, [](Realm& r, Args args) {
        require_args(args, 1, "AudioEncoder.configure");
        auto& st = this_state<AudioCodecState>("AudioEncoder");
        if (st.state == "closed") throw_dom(r, "InvalidStateError", "AudioEncoder is closed");
        if (!args[0].is_object_like()) js_throw_type_error("AudioEncoderConfig must be an object");
        const std::string codec = to_str(args[0].get_property("codec"));
        if (!webcodecs_audio_codec_supported(codec)) {
            throw_dom(r, "NotSupportedError", "Unsupported AudioEncoder codec: " + codec);
        }
        st.codec = codec;
        st.sample_rate = to_double(args[0].get_property("sampleRate"));
        st.number_of_channels = to_ulong(args[0].get_property("numberOfChannels"));
        st.state = "configured";
        return undefined();
    });
    I.method("encode", 1, [](Realm& r, Args args) {
        require_args(args, 1, "AudioEncoder.encode");
        auto& st = this_state<AudioCodecState>("AudioEncoder");
        if (st.state != "configured") throw_dom(r, "InvalidStateError", "AudioEncoder is not configured");
        auto* ad = state_of<AudioDataState>(args[0]);
        if (!ad || ad->closed) throw_dom(r, "InvalidStateError", "AudioData is closed or invalid");
        auto chunk = std::make_shared<EncodedChunkState>();
        chunk->is_video = false;
        chunk->type = "key";
        chunk->timestamp = ad->timestamp;
        chunk->has_duration = true;
        chunk->duration = ad->sample_rate > 0.0
            ? static_cast<int64_t>(std::floor(static_cast<double>(ad->number_of_frames) * 1e6 / ad->sample_rate))
            : 0;
        const uint32_t nf = ad->number_of_frames;
        const uint32_t nc = ad->number_of_channels;
        const uint32_t sr = static_cast<uint32_t>(ad->sample_rate);
        chunk->data.resize(16u + ad->data.size());
        std::memcpy(chunk->data.data(), kRawAudioMagic, 4);
        std::memcpy(chunk->data.data() + 4, &nf, 4);
        std::memcpy(chunk->data.data() + 8, &nc, 4);
        std::memcpy(chunk->data.data() + 12, &sr, 4);
        if (!ad->data.empty()) std::memcpy(chunk->data.data() + 16, ad->data.data(), ad->data.size());
        JsValue meta = JsValue::make_object();
        JsValue dcfg = JsValue::make_object();
        dcfg.set_property("codec", str(st.codec));
        dcfg.set_property("sampleRate", num(ad->sample_rate));
        dcfg.set_property("numberOfChannels", num(ad->number_of_channels));
        meta.set_property("decoderConfig", dcfg);
        js_call(st.output_cb, undefined(), {r.make("EncodedAudioChunk", chunk), meta});
        return undefined();
    });
    I.method("flush", 0, [](Realm& r, Args) {
        auto& st = this_state<AudioCodecState>("AudioEncoder");
        if (st.state != "configured") return make_rejected_dom_promise(r, "InvalidStateError", "AudioEncoder is not configured");
        return make_resolved_promise(r, undefined());
    });
    I.method("reset", 0, [](Realm& r, Args) {
        auto& st = this_state<AudioCodecState>("AudioEncoder");
        if (st.state == "closed") throw_dom(r, "InvalidStateError", "AudioEncoder is closed");
        st.state = "unconfigured";
        st.queue_size = 0;
        return undefined();
    });
    I.method("close", 0, [](Realm&, Args) {
        auto& st = this_state<AudioCodecState>("AudioEncoder");
        st.state = "closed";
        st.queue_size = 0;
        return undefined();
    });
}

} // namespace

void register_media_interfaces() {
    // Media Source Extensions
    register_interface({"SourceBufferList", "EventTarget", setup_source_buffer_list, nullptr, 0, nullptr});
    register_interface({"SourceBuffer", "EventTarget", setup_source_buffer, nullptr, 0, nullptr});
    register_interface({"MediaSource", "EventTarget", setup_media_source, construct_media_source, 0, nullptr});

    // Web Audio API
    register_interface({"AudioParam", nullptr, setup_audio_param, nullptr, 0, nullptr});
    register_interface({"AudioBuffer", nullptr, setup_audio_buffer, construct_audio_buffer, 1, nullptr});
    register_interface({"AudioNode", "EventTarget", setup_audio_node, nullptr, 0, nullptr});
    register_interface({"AudioDestinationNode", "AudioNode", setup_audio_destination_node, nullptr, 0, nullptr});
    register_interface({"GainNode", "AudioNode", setup_gain_node, nullptr, 0, nullptr});
    register_interface({"OscillatorNode", "AudioNode", setup_oscillator_node, nullptr, 0, nullptr});
    register_interface({"AudioBufferSourceNode", "AudioNode", setup_buffer_source_node, nullptr, 0, nullptr});
    register_interface({"AnalyserNode", "AudioNode", setup_analyser_node, nullptr, 0, nullptr});
    register_interface({"BiquadFilterNode", "AudioNode", setup_biquad_filter_node, nullptr, 0, nullptr});
    register_interface({"BaseAudioContext", "EventTarget", setup_base_audio_context, nullptr, 0, nullptr});
    register_interface({"AudioContext", "BaseAudioContext", setup_audio_context, construct_audio_context, 0, nullptr});
    register_interface({"OfflineAudioContext", "BaseAudioContext", setup_offline_audio_context, construct_offline_audio_context, 1, nullptr});

    // W3C WebCodecs
    register_interface({"VideoFrame", nullptr, setup_video_frame, construct_video_frame, 1, nullptr});
    register_interface({"AudioData", nullptr, setup_audio_data, construct_audio_data, 1, nullptr});
    register_interface({"EncodedVideoChunk", nullptr, setup_encoded_chunk, construct_encoded_video_chunk, 1, nullptr});
    register_interface({"EncodedAudioChunk", nullptr, setup_encoded_chunk, construct_encoded_audio_chunk, 1, nullptr});
    register_interface({"VideoDecoder", nullptr, setup_video_decoder, construct_video_decoder, 1, nullptr});
    register_interface({"VideoEncoder", nullptr, setup_video_encoder, construct_video_encoder, 1, nullptr});
    register_interface({"AudioDecoder", nullptr, setup_audio_decoder, construct_audio_decoder, 1, nullptr});
    register_interface({"AudioEncoder", nullptr, setup_audio_encoder, construct_audio_encoder, 1, nullptr});
}

} // namespace dom
} // namespace litebird
