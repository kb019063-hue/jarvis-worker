#include "litebird/render/MediaAudioCodecs.hpp"

#include "litebird/net/DataUrl.hpp"
#include "litebird/render/RasterBudget.hpp"
#include "litebird/render/Vp8Decoder.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace litebird {

namespace {

constexpr uint16_t kMaxWavChannels = 32;
constexpr std::size_t kMaxWavTotalSamples = 64u * 1024u * 1024u;
constexpr double kTwoPi = 6.28318530717958647692;

uint16_t read_u16_le(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t read_u32_le(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

std::string ascii_lower_str(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

std::string trim_ws(std::string_view s) {
    std::size_t b = 0;
    while (b < s.size() && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    std::size_t e = s.size();
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    return std::string(s.substr(b, e - b));
}

bool starts_with(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

bool is_hex_str(std::string_view s) {
    if (s.empty()) return false;
    for (unsigned char c : s) {
        if (!std::isxdigit(c)) return false;
    }
    return true;
}

} // namespace

// --- PCM WAV decoder --------------------------------------------------------

bool decode_wav(const uint8_t* data, std::size_t size, WavAudio& out, std::string& error) {
    out = WavAudio{};
    if (!data || size < 12) {
        error = "WAV buffer too small for RIFF header";
        return false;
    }
    if (std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0) {
        error = "Not a RIFF/WAVE container";
        return false;
    }

    bool have_fmt = false;
    bool have_data = false;
    uint16_t audio_format = 0;
    uint16_t num_channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;
    uint16_t block_align = 0;
    const uint8_t* pcm_ptr = nullptr;
    std::size_t pcm_bytes = 0;

    std::size_t pos = 12;
    while (pos + 8 <= size) {
        const char* cid = reinterpret_cast<const char*>(data + pos);
        const uint32_t chunk_size = read_u32_le(data + pos + 4);
        pos += 8;
        const std::size_t avail = size - pos;
        const std::size_t clamped = std::min<std::size_t>(chunk_size, avail);

        if (std::memcmp(cid, "fmt ", 4) == 0) {
            if (clamped < 16) {
                error = "WAV fmt chunk truncated";
                return false;
            }
            audio_format = read_u16_le(data + pos);
            num_channels = read_u16_le(data + pos + 2);
            sample_rate = read_u32_le(data + pos + 4);
            block_align = read_u16_le(data + pos + 12);
            bits_per_sample = read_u16_le(data + pos + 14);
            // WAVE_FORMAT_EXTENSIBLE (0xFFFE): read SubFormat GUID's first 2 bytes.
            if (audio_format == 0xFFFE && clamped >= 40) {
                audio_format = read_u16_le(data + pos + 24);
            }
            have_fmt = true;
        } else if (std::memcmp(cid, "data", 4) == 0) {
            pcm_ptr = data + pos;
            pcm_bytes = clamped;
            have_data = true;
            if (have_fmt) break;
        }

        const std::size_t step = static_cast<std::size_t>(chunk_size) + (chunk_size & 1u);
        if (step > size - pos) break;
        pos += step;
    }

    if (!have_fmt || !have_data) {
        error = "WAV missing fmt or data chunk";
        return false;
    }
    if (audio_format != 1 && audio_format != 3) {
        error = "Unsupported WAV audio format (only PCM and IEEE float are supported)";
        return false;
    }
    if (num_channels == 0 || num_channels > kMaxWavChannels) {
        error = "Invalid WAV channel count";
        return false;
    }
    if (sample_rate == 0 || sample_rate > 768000u) {
        error = "Invalid WAV sample rate";
        return false;
    }
    const bool valid_pcm_bps = (audio_format == 1) &&
        (bits_per_sample == 8 || bits_per_sample == 16 || bits_per_sample == 24 || bits_per_sample == 32);
    const bool valid_flt_bps = (audio_format == 3) && (bits_per_sample == 32);
    if (!valid_pcm_bps && !valid_flt_bps) {
        error = "Unsupported WAV bits per sample";
        return false;
    }

    const std::size_t bytes_per_sample = bits_per_sample / 8u;
    const std::size_t computed_align = static_cast<std::size_t>(num_channels) * bytes_per_sample;
    const std::size_t frame_bytes = (block_align >= computed_align) ? block_align : computed_align;
    if (frame_bytes == 0) {
        error = "Zero WAV frame size";
        return false;
    }

    const std::size_t num_frames = pcm_bytes / frame_bytes;
    if (num_frames > kMaxWavTotalSamples / num_channels) {
        error = "WAV audio exceeds sample budget";
        return false;
    }

    out.sample_rate = sample_rate;
    out.num_channels = num_channels;
    out.bits_per_sample = bits_per_sample;
    out.audio_format = audio_format;
    out.num_frames = num_frames;
    out.duration_seconds = static_cast<double>(num_frames) / static_cast<double>(sample_rate);
    out.channels.assign(num_channels, std::vector<float>(num_frames, 0.0f));

    for (std::size_t f = 0; f < num_frames; ++f) {
        const uint8_t* fp = pcm_ptr + f * frame_bytes;
        for (uint16_t ch = 0; ch < num_channels; ++ch) {
            const uint8_t* sp = fp + static_cast<std::size_t>(ch) * bytes_per_sample;
            float sample = 0.0f;
            if (audio_format == 1) {
                if (bits_per_sample == 8) {
                    sample = (static_cast<float>(sp[0]) - 128.0f) / 128.0f;
                } else if (bits_per_sample == 16) {
                    const int16_t v = static_cast<int16_t>(read_u16_le(sp));
                    sample = static_cast<float>(v) / 32768.0f;
                } else if (bits_per_sample == 24) {
                    int32_t v = static_cast<int32_t>(sp[0]) |
                                (static_cast<int32_t>(sp[1]) << 8) |
                                (static_cast<int32_t>(sp[2]) << 16);
                    if (v & 0x800000) v |= ~0xFFFFFF;
                    sample = static_cast<float>(v) / 8388608.0f;
                } else if (bits_per_sample == 32) {
                    const int32_t v = static_cast<int32_t>(read_u32_le(sp));
                    sample = static_cast<float>(static_cast<double>(v) / 2147483648.0);
                }
            } else {
                const uint32_t bits = read_u32_le(sp);
                float v = 0.0f;
                std::memcpy(&v, &bits, sizeof(float));
                sample = std::isfinite(v) ? std::clamp(v, -1.0f, 1.0f) : 0.0f;
            }
            out.channels[ch][f] = sample;
        }
    }
    return true;
}

bool decode_wav_data_uri(std::string_view url, WavAudio& out, std::string& error) {
    // Strip fragment per Fetch data: URL processor contract.
    if (const std::size_t hash = url.find('#'); hash != std::string_view::npos) {
        url = url.substr(0, hash);
    }
    MimeType mime;
    std::string body;
    if (!process_data_url(url, mime, body)) {
        error = "Malformed data: URL";
        return false;
    }
    return decode_wav(reinterpret_cast<const uint8_t*>(body.data()), body.size(), out, error);
}

// --- MIME / Codec registries ------------------------------------------------

bool media_source_is_type_supported(std::string_view mime_type) {
    if (mime_type.empty()) return false;
    MimeType parsed;
    if (!MimeType::parse(mime_type, parsed)) return false;
    const std::string essence = parsed.essence();

    const bool is_mp4 = (essence == "video/mp4" || essence == "audio/mp4");
    const bool is_webm = (essence == "video/webm" || essence == "audio/webm");
    const bool is_mpeg = (essence == "audio/mpeg");
    const bool is_wav = (essence == "audio/wav" || essence == "audio/wave" || essence == "audio/x-wav");
    if (!is_mp4 && !is_webm && !is_mpeg && !is_wav) return false;

    const std::string* codecs_param = parsed.parameter("codecs");
    if (!codecs_param) {
        // Bare container without codecs: audio/mpeg and audio/wav are unambiguous;
        // video/mp4 and video/webm also commonly queried without codecs in feature checks.
        return true;
    }

    std::string_view rest = *codecs_param;
    bool any = false;
    while (!rest.empty()) {
        const std::size_t comma = rest.find(',');
        std::string token = ascii_lower_str(trim_ws(comma == std::string_view::npos ? rest : rest.substr(0, comma)));
        rest = (comma == std::string_view::npos) ? std::string_view{} : rest.substr(comma + 1);
        if (token.empty()) return false;
        any = true;

        bool ok = false;
        if (is_mp4) {
            if (essence == "video/mp4") {
                ok = starts_with(token, "avc1.") || starts_with(token, "avc3.") ||
                     starts_with(token, "av01.") || starts_with(token, "vp09.") ||
                     token == "vp9" || starts_with(token, "mp4a.") || token == "opus" || token == "flac";
            } else {
                ok = starts_with(token, "mp4a.") || token == "opus" || token == "flac" || token == "mp3";
            }
        } else if (is_webm) {
            if (essence == "video/webm") {
                ok = token == "vp8" || token == "vp9" || starts_with(token, "vp09.") ||
                     starts_with(token, "av01.") || token == "vorbis" || token == "opus";
            } else {
                ok = token == "vorbis" || token == "opus";
            }
        } else if (is_mpeg) {
            ok = token == "mp3" || token == "mp4a.40.34";
        } else if (is_wav) {
            ok = token == "1" || token == "3" || token == "pcm";
        }
        if (!ok) return false;
    }
    return any;
}

bool webcodecs_video_codec_supported(std::string_view codec_in) {
    const std::string c = ascii_lower_str(trim_ws(codec_in));
    if (c.empty()) return false;
    if (c == "vp8") return true;
    if (starts_with(c, "vp09.")) return c.size() >= 10;
    if (starts_with(c, "avc1.") || starts_with(c, "avc3.")) {
        return c.size() == 11 && is_hex_str(std::string_view(c).substr(5));
    }
    if (starts_with(c, "av01.")) return c.size() >= 10;
    return false;
}

bool webcodecs_audio_codec_supported(std::string_view codec_in) {
    const std::string c = ascii_lower_str(trim_ws(codec_in));
    if (c.empty()) return false;
    if (c == "opus" || c == "flac" || c == "mp3" || c == "vorbis" ||
        c == "ulaw" || c == "alaw" ||
        c == "pcm-u8" || c == "pcm-s16" || c == "pcm-s24" || c == "pcm-s32" || c == "pcm-f32") {
        return true;
    }
    if (starts_with(c, "mp4a.")) return c.size() >= 7;
    return false;
}

// --- WebCodecs pixel & audio layouts + VP8 bridge ---------------------------

bool video_pixel_format_layout(std::string_view format, int width, int height,
                               std::vector<VideoPlaneLayout>& planes,
                               std::size_t& total_bytes) {
    planes.clear();
    total_bytes = 0;
    if (width <= 0 || height <= 0 || !RasterBudget::allows(width, height)) return false;

    const std::size_t w = static_cast<std::size_t>(width);
    const std::size_t h = static_cast<std::size_t>(height);

    if (format == "RGBA" || format == "RGBX" || format == "BGRA" || format == "BGRX") {
        const std::size_t stride = w * 4u;
        planes.push_back({0, stride, h});
        total_bytes = stride * h;
        return true;
    }
    if (format == "I420" || format == "I420A") {
        const std::size_t cw = (w + 1u) / 2u;
        const std::size_t ch = (h + 1u) / 2u;
        const std::size_t y_size = w * h;
        const std::size_t c_size = cw * ch;
        planes.push_back({0, w, h});
        planes.push_back({y_size, cw, ch});
        planes.push_back({y_size + c_size, cw, ch});
        total_bytes = y_size + 2u * c_size;
        if (format == "I420A") {
            planes.push_back({total_bytes, w, h});
            total_bytes += y_size;
        }
        return true;
    }
    if (format == "NV12") {
        const std::size_t cw = ((w + 1u) / 2u) * 2u;
        const std::size_t ch = (h + 1u) / 2u;
        const std::size_t y_size = w * h;
        planes.push_back({0, w, h});
        planes.push_back({y_size, cw, ch});
        total_bytes = y_size + cw * ch;
        return true;
    }
    if (format == "I422") {
        const std::size_t cw = (w + 1u) / 2u;
        const std::size_t y_size = w * h;
        const std::size_t c_size = cw * h;
        planes.push_back({0, w, h});
        planes.push_back({y_size, cw, h});
        planes.push_back({y_size + c_size, cw, h});
        total_bytes = y_size + 2u * c_size;
        return true;
    }
    if (format == "I444") {
        const std::size_t p_size = w * h;
        planes.push_back({0, w, h});
        planes.push_back({p_size, w, h});
        planes.push_back({2u * p_size, w, h});
        total_bytes = 3u * p_size;
        return true;
    }
    return false;
}

bool audio_sample_format_info(std::string_view format, std::size_t& bytes_per_sample, bool& is_planar) {
    if (format == "u8") { bytes_per_sample = 1; is_planar = false; return true; }
    if (format == "s16") { bytes_per_sample = 2; is_planar = false; return true; }
    if (format == "s32") { bytes_per_sample = 4; is_planar = false; return true; }
    if (format == "f32") { bytes_per_sample = 4; is_planar = false; return true; }
    if (format == "u8-planar") { bytes_per_sample = 1; is_planar = true; return true; }
    if (format == "s16-planar") { bytes_per_sample = 2; is_planar = true; return true; }
    if (format == "s32-planar") { bytes_per_sample = 4; is_planar = true; return true; }
    if (format == "f32-planar") { bytes_per_sample = 4; is_planar = true; return true; }
    return false;
}

bool decode_vp8_to_rgba(const uint8_t* data, std::size_t size,
                        int& out_width, int& out_height,
                        std::vector<uint8_t>& out_rgba,
                        std::string& error) {
    Vp8Decoder::Frame frame;
    const auto status = Vp8Decoder::decode(data, size, frame, error);
    if (status != Vp8Decoder::Status::Ok) return false;
    std::vector<uint32_t> argb;
    Vp8Decoder::to_argb(frame, nullptr, 0, argb);
    out_width = frame.width;
    out_height = frame.height;
    out_rgba.resize(static_cast<std::size_t>(out_width) * out_height * 4u);
    for (std::size_t i = 0; i < argb.size(); ++i) {
        const uint32_t p = argb[i];
        out_rgba[i * 4u] = static_cast<uint8_t>((p >> 16) & 0xFF);
        out_rgba[i * 4u + 1u] = static_cast<uint8_t>((p >> 8) & 0xFF);
        out_rgba[i * 4u + 2u] = static_cast<uint8_t>(p & 0xFF);
        out_rgba[i * 4u + 3u] = static_cast<uint8_t>((p >> 24) & 0xFF);
    }
    return true;
}

// --- Web Audio offline DSP renderer -----------------------------------------

void render_offline_audio_graph(OfflineAudioBufferData& output,
                                const std::vector<OfflineOscillatorSpec>& oscillators,
                                const std::vector<OfflineBufferSourceSpec>& buffer_sources) {
    if (output.number_of_channels <= 0 || output.length == 0 || output.sample_rate <= 0.0) return;
    output.channels.assign(static_cast<std::size_t>(output.number_of_channels),
                           std::vector<float>(output.length, 0.0f));

    for (const auto& osc : oscillators) {
        const double freq = osc.frequency * std::pow(2.0, osc.detune / 1200.0);
        if (!std::isfinite(freq) || freq <= 0.0 || !std::isfinite(osc.gain) || osc.gain == 0.0) continue;
        const std::size_t start_f = osc.start_time > 0.0
            ? static_cast<std::size_t>(std::min<double>(osc.start_time * output.sample_rate, static_cast<double>(output.length)))
            : 0u;
        const std::size_t end_f = (osc.stop_time >= 0.0)
            ? static_cast<std::size_t>(std::min<double>(osc.stop_time * output.sample_rate, static_cast<double>(output.length)))
            : output.length;
        if (end_f <= start_f) continue;

        double phase = 0.0;
        const double phase_inc = freq / output.sample_rate;
        for (std::size_t f = start_f; f < end_f; ++f) {
            double s = 0.0;
            switch (osc.type) {
                case AudioOscillatorType::Sine:
                    s = std::sin(phase * kTwoPi);
                    break;
                case AudioOscillatorType::Square:
                    s = (phase < 0.5) ? 1.0 : -1.0;
                    break;
                case AudioOscillatorType::Sawtooth:
                    s = 2.0 * phase - 1.0;
                    break;
                case AudioOscillatorType::Triangle:
                    s = (phase < 0.25) ? (4.0 * phase)
                        : (phase < 0.75) ? (2.0 - 4.0 * phase)
                        : (4.0 * phase - 4.0);
                    break;
            }
            const float val = static_cast<float>(s * osc.gain);
            for (int ch = 0; ch < output.number_of_channels; ++ch) {
                output.channels[static_cast<std::size_t>(ch)][f] += val;
            }
            phase += phase_inc;
            phase -= std::floor(phase);
        }
    }

    for (const auto& src : buffer_sources) {
        if (!src.buffer || src.buffer->length == 0 || src.buffer->channels.empty()) continue;
        if (!std::isfinite(src.playback_rate) || src.playback_rate <= 0.0 || src.gain == 0.0) continue;
        const std::size_t start_f = src.start_time > 0.0
            ? static_cast<std::size_t>(std::min<double>(src.start_time * output.sample_rate, static_cast<double>(output.length)))
            : 0u;
        const std::size_t end_f = (src.stop_time >= 0.0)
            ? static_cast<std::size_t>(std::min<double>(src.stop_time * output.sample_rate, static_cast<double>(output.length)))
            : output.length;
        if (end_f <= start_f) continue;

        const double rate = src.playback_rate * (src.buffer->sample_rate / output.sample_rate);
        const std::size_t src_chs = src.buffer->channels.size();
        for (std::size_t f = start_f; f < end_f; ++f) {
            double pos = static_cast<double>(f - start_f) * rate;
            if (src.loop) {
                pos = std::fmod(pos, static_cast<double>(src.buffer->length));
            } else if (pos >= static_cast<double>(src.buffer->length)) {
                break;
            }
            const std::size_t i0 = static_cast<std::size_t>(pos);
            const std::size_t i1 = std::min(i0 + 1u, src.buffer->length - 1u);
            const double frac = pos - static_cast<double>(i0);
            for (int ch = 0; ch < output.number_of_channels; ++ch) {
                const std::size_t sch = (static_cast<std::size_t>(ch) < src_chs) ? static_cast<std::size_t>(ch) : 0u;
                const auto& ch_data = src.buffer->channels[sch];
                const double v = ch_data[i0] * (1.0 - frac) + ch_data[i1] * frac;
                output.channels[static_cast<std::size_t>(ch)][f] += static_cast<float>(v * src.gain);
            }
        }
    }
}

} // namespace litebird
