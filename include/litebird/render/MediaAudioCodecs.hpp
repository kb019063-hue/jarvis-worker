#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace litebird {

// --- PCM WAV decoder (RIFF / WAVE) ------------------------------------------

struct WavAudio {
    uint32_t sample_rate{0};
    uint16_t num_channels{0};
    uint16_t bits_per_sample{0};
    uint16_t audio_format{0}; // 1 = PCM, 3 = IEEE float
    std::size_t num_frames{0};
    double duration_seconds{0.0};
    /// Planar float samples in [-1.0, 1.0]: channels[ch][frame].
    std::vector<std::vector<float>> channels;
};

/// Decodes a RIFF/WAVE PCM or IEEE-float audio buffer safely with strict bounds
/// checks (caps channels <= 32, total samples <= 64M).
bool decode_wav(const uint8_t* data, std::size_t size, WavAudio& out, std::string& error);

/// Decodes a `data:` URI carrying WAV bytes via Fetch's `process_data_url`.
bool decode_wav_data_uri(std::string_view url, WavAudio& out, std::string& error);

// --- MIME / Codec support tables (MSE + WebCodecs) --------------------------

/// W3C Media Source Extensions §3.1 MediaSource.isTypeSupported(type).
bool media_source_is_type_supported(std::string_view mime_type);

/// W3C WebCodecs §3.2 VideoDecoder / VideoEncoder codec string validation.
bool webcodecs_video_codec_supported(std::string_view codec);

/// W3C WebCodecs §3.1 AudioDecoder / AudioEncoder codec string validation.
bool webcodecs_audio_codec_supported(std::string_view codec);

// --- WebCodecs pixel / audio format geometry & VP8 decode -------------------

struct VideoPlaneLayout {
    std::size_t offset{0};
    std::size_t stride{0};
    std::size_t rows{0};
};

/// Computes W3C WebCodecs VideoPixelFormat plane layouts and total byte size.
/// Returns false if `format` is unknown, dimensions are <= 0, or dimensions
/// exceed `RasterBudget::allows`.
bool video_pixel_format_layout(std::string_view format, int width, int height,
                               std::vector<VideoPlaneLayout>& planes,
                               std::size_t& total_bytes);

/// Validates a W3C WebCodecs AudioSampleFormat string and reports its bytes per
/// sample and whether it is planar.
bool audio_sample_format_info(std::string_view format, std::size_t& bytes_per_sample,
                              bool& is_planar);

/// Decodes a raw VP8 keyframe into tightly-packed RGBA8 bytes using LiteBird's
/// `Vp8Decoder`.
bool decode_vp8_to_rgba(const uint8_t* data, std::size_t size,
                        int& out_width, int& out_height,
                        std::vector<uint8_t>& out_rgba,
                        std::string& error);

// --- Web Audio API offline DSP renderer -------------------------------------

enum class AudioOscillatorType {
    Sine,
    Square,
    Sawtooth,
    Triangle
};

struct OfflineAudioBufferData {
    int number_of_channels{1};
    std::size_t length{0};
    double sample_rate{44100.0};
    std::vector<std::vector<float>> channels;
};

struct OfflineOscillatorSpec {
    AudioOscillatorType type{AudioOscillatorType::Sine};
    double frequency{440.0};
    double detune{0.0};
    double start_time{0.0};
    double stop_time{-1.0}; // < 0 means until end of render
    double gain{1.0};
};

struct OfflineBufferSourceSpec {
    const OfflineAudioBufferData* buffer{nullptr};
    double playback_rate{1.0};
    bool loop{false};
    double start_time{0.0};
    double stop_time{-1.0};
    double gain{1.0};
};

/// Renders oscillators and buffer sources (each with its accumulated graph gain)
/// into `output`, clamping/summing channels according to Web Audio up-mixing.
void render_offline_audio_graph(OfflineAudioBufferData& output,
                                const std::vector<OfflineOscillatorSpec>& oscillators,
                                const std::vector<OfflineBufferSourceSpec>& buffer_sources);

} // namespace litebird
