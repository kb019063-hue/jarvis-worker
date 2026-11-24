#pragma once

#include "litebird/render/Canvas2D.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace litebird {

namespace webgl {

// Standard WebGL 1.0 & WebGL 2.0 constants (Khronos WebGL 1.0 §5.14 / 2.0 §3.7).
constexpr uint32_t DEPTH_BUFFER_BIT = 0x00000100;
constexpr uint32_t STENCIL_BUFFER_BIT = 0x00000400;
constexpr uint32_t COLOR_BUFFER_BIT = 0x00004000;

constexpr uint32_t POINTS = 0x0000;
constexpr uint32_t LINES = 0x0001;
constexpr uint32_t LINE_LOOP = 0x0002;
constexpr uint32_t LINE_STRIP = 0x0003;
constexpr uint32_t TRIANGLES = 0x0004;
constexpr uint32_t TRIANGLE_STRIP = 0x0005;
constexpr uint32_t TRIANGLE_FAN = 0x0006;

constexpr uint32_t ZERO = 0;
constexpr uint32_t ONE = 1;
constexpr uint32_t SRC_COLOR = 0x0300;
constexpr uint32_t ONE_MINUS_SRC_COLOR = 0x0301;
constexpr uint32_t SRC_ALPHA = 0x0302;
constexpr uint32_t ONE_MINUS_SRC_ALPHA = 0x0303;
constexpr uint32_t DST_ALPHA = 0x0304;
constexpr uint32_t ONE_MINUS_DST_ALPHA = 0x0305;
constexpr uint32_t DST_COLOR = 0x0306;
constexpr uint32_t ONE_MINUS_DST_COLOR = 0x0307;
constexpr uint32_t SRC_ALPHA_SATURATE = 0x0308;
constexpr uint32_t CONSTANT_COLOR = 0x8001;
constexpr uint32_t ONE_MINUS_CONSTANT_COLOR = 0x8002;
constexpr uint32_t CONSTANT_ALPHA = 0x8003;
constexpr uint32_t ONE_MINUS_CONSTANT_ALPHA = 0x8004;

constexpr uint32_t FUNC_ADD = 0x8006;
constexpr uint32_t FUNC_SUBTRACT = 0x800A;
constexpr uint32_t FUNC_REVERSE_SUBTRACT = 0x800B;
constexpr uint32_t MIN = 0x8007;
constexpr uint32_t MAX = 0x8008;

constexpr uint32_t ARRAY_BUFFER = 0x8892;
constexpr uint32_t ELEMENT_ARRAY_BUFFER = 0x8893;
constexpr uint32_t ARRAY_BUFFER_BINDING = 0x8894;
constexpr uint32_t ELEMENT_ARRAY_BUFFER_BINDING = 0x8895;
constexpr uint32_t STREAM_DRAW = 0x88E0;
constexpr uint32_t STATIC_DRAW = 0x88E4;
constexpr uint32_t DYNAMIC_DRAW = 0x88E8;
constexpr uint32_t BUFFER_SIZE = 0x8764;
constexpr uint32_t BUFFER_USAGE = 0x8765;

constexpr uint32_t CULL_FACE = 0x0B44;
constexpr uint32_t BLEND = 0x0BE2;
constexpr uint32_t DITHER = 0x0BD0;
constexpr uint32_t STENCIL_TEST = 0x0B90;
constexpr uint32_t DEPTH_TEST = 0x0B71;
constexpr uint32_t SCISSOR_TEST = 0x0C11;
constexpr uint32_t POLYGON_OFFSET_FILL = 0x8037;

constexpr uint32_t FRONT = 0x0404;
constexpr uint32_t BACK = 0x0405;
constexpr uint32_t FRONT_AND_BACK = 0x0408;
constexpr uint32_t CW = 0x0900;
constexpr uint32_t CCW = 0x0901;

constexpr uint32_t NO_ERROR = 0;
constexpr uint32_t INVALID_ENUM = 0x0500;
constexpr uint32_t INVALID_VALUE = 0x0501;
constexpr uint32_t INVALID_OPERATION = 0x0502;
constexpr uint32_t OUT_OF_MEMORY = 0x0505;
constexpr uint32_t INVALID_FRAMEBUFFER_OPERATION = 0x0506;

constexpr uint32_t NEVER = 0x0200;
constexpr uint32_t LESS = 0x0201;
constexpr uint32_t EQUAL = 0x0202;
constexpr uint32_t LEQUAL = 0x0203;
constexpr uint32_t GREATER = 0x0204;
constexpr uint32_t NOTEQUAL = 0x0205;
constexpr uint32_t GEQUAL = 0x0206;
constexpr uint32_t ALWAYS = 0x0207;

constexpr uint32_t BYTE = 0x1400;
constexpr uint32_t UNSIGNED_BYTE = 0x1401;
constexpr uint32_t SHORT = 0x1402;
constexpr uint32_t UNSIGNED_SHORT = 0x1403;
constexpr uint32_t INT = 0x1404;
constexpr uint32_t UNSIGNED_INT = 0x1405;
constexpr uint32_t FLOAT = 0x1406;

constexpr uint32_t DEPTH_COMPONENT = 0x1902;
constexpr uint32_t ALPHA = 0x1906;
constexpr uint32_t RGB = 0x1907;
constexpr uint32_t RGBA = 0x1908;
constexpr uint32_t LUMINANCE = 0x1909;
constexpr uint32_t LUMINANCE_ALPHA = 0x190A;
constexpr uint32_t RGB8 = 0x8051;
constexpr uint32_t RGBA8 = 0x8058;
constexpr uint32_t RGBA4 = 0x8056;
constexpr uint32_t RGB5_A1 = 0x8057;
constexpr uint32_t RGB565 = 0x8D62;
constexpr uint32_t DEPTH_COMPONENT16 = 0x81A5;
constexpr uint32_t DEPTH_COMPONENT24 = 0x81A6;
constexpr uint32_t STENCIL_INDEX8 = 0x8D48;
constexpr uint32_t DEPTH_STENCIL = 0x84F9;

constexpr uint32_t FRAGMENT_SHADER = 0x8B30;
constexpr uint32_t VERTEX_SHADER = 0x8B31;
constexpr uint32_t SHADER_TYPE = 0x8B4F;
constexpr uint32_t DELETE_STATUS = 0x8B80;
constexpr uint32_t COMPILE_STATUS = 0x8B81;
constexpr uint32_t LINK_STATUS = 0x8B82;
constexpr uint32_t VALIDATE_STATUS = 0x8B83;
constexpr uint32_t ATTACHED_SHADERS = 0x8B85;
constexpr uint32_t ACTIVE_UNIFORMS = 0x8B86;
constexpr uint32_t ACTIVE_ATTRIBUTES = 0x8B89;
constexpr uint32_t CURRENT_PROGRAM = 0x8B8D;

constexpr uint32_t FLOAT_VEC2 = 0x8B50;
constexpr uint32_t FLOAT_VEC3 = 0x8B51;
constexpr uint32_t FLOAT_VEC4 = 0x8B52;
constexpr uint32_t INT_VEC2 = 0x8B53;
constexpr uint32_t INT_VEC3 = 0x8B54;
constexpr uint32_t INT_VEC4 = 0x8B55;
constexpr uint32_t BOOL = 0x8B56;
constexpr uint32_t BOOL_VEC2 = 0x8B57;
constexpr uint32_t BOOL_VEC3 = 0x8B58;
constexpr uint32_t BOOL_VEC4 = 0x8B59;
constexpr uint32_t FLOAT_MAT2 = 0x8B5A;
constexpr uint32_t FLOAT_MAT3 = 0x8B5B;
constexpr uint32_t FLOAT_MAT4 = 0x8B5C;
constexpr uint32_t SAMPLER_2D = 0x8B5E;

constexpr uint32_t TEXTURE_2D = 0x0DE1;
constexpr uint32_t TEXTURE_CUBE_MAP = 0x8513;
constexpr uint32_t TEXTURE_BINDING_2D = 0x8069;
constexpr uint32_t ACTIVE_TEXTURE = 0x84E0;
constexpr uint32_t TEXTURE0 = 0x84C0;
constexpr uint32_t NEAREST = 0x2600;
constexpr uint32_t LINEAR = 0x2601;
constexpr uint32_t NEAREST_MIPMAP_NEAREST = 0x2700;
constexpr uint32_t LINEAR_MIPMAP_NEAREST = 0x2701;
constexpr uint32_t NEAREST_MIPMAP_LINEAR = 0x2702;
constexpr uint32_t LINEAR_MIPMAP_LINEAR = 0x2703;
constexpr uint32_t TEXTURE_MAG_FILTER = 0x2800;
constexpr uint32_t TEXTURE_MIN_FILTER = 0x2801;
constexpr uint32_t TEXTURE_WRAP_S = 0x2802;
constexpr uint32_t TEXTURE_WRAP_T = 0x2803;
constexpr uint32_t REPEAT = 0x2901;
constexpr uint32_t CLAMP_TO_EDGE = 0x812F;
constexpr uint32_t MIRRORED_REPEAT = 0x8370;

constexpr uint32_t UNPACK_ALIGNMENT = 0x0CF5;
constexpr uint32_t PACK_ALIGNMENT = 0x0D05;
constexpr uint32_t UNPACK_FLIP_Y_WEBGL = 0x9240;
constexpr uint32_t UNPACK_PREMULTIPLY_ALPHA_WEBGL = 0x9241;

constexpr uint32_t FRAMEBUFFER = 0x8D40;
constexpr uint32_t RENDERBUFFER = 0x8D41;
constexpr uint32_t FRAMEBUFFER_BINDING = 0x8CA6;
constexpr uint32_t RENDERBUFFER_BINDING = 0x8CA7;
constexpr uint32_t COLOR_ATTACHMENT0 = 0x8CE0;
constexpr uint32_t DEPTH_ATTACHMENT = 0x8D00;
constexpr uint32_t STENCIL_ATTACHMENT = 0x8D20;
constexpr uint32_t FRAMEBUFFER_COMPLETE = 0x8CD5;
constexpr uint32_t FRAMEBUFFER_INCOMPLETE_ATTACHMENT = 0x8CD6;
constexpr uint32_t FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT = 0x8CD7;
constexpr uint32_t FRAMEBUFFER_INCOMPLETE_DIMENSIONS = 0x8CD9;

constexpr uint32_t LINE_WIDTH = 0x0B21;
constexpr uint32_t CULL_FACE_MODE = 0x0B45;
constexpr uint32_t FRONT_FACE = 0x0B46;
constexpr uint32_t DEPTH_RANGE = 0x0B70;
constexpr uint32_t DEPTH_WRITEMASK = 0x0B72;
constexpr uint32_t DEPTH_CLEAR_VALUE = 0x0B73;
constexpr uint32_t DEPTH_FUNC = 0x0B74;
constexpr uint32_t VIEWPORT = 0x0BA2;
constexpr uint32_t SCISSOR_BOX = 0x0C10;
constexpr uint32_t COLOR_CLEAR_VALUE = 0x0C22;
constexpr uint32_t COLOR_WRITEMASK = 0x0C23;
constexpr uint32_t BLEND_EQUATION_RGB = 0x8009;
constexpr uint32_t BLEND_EQUATION_ALPHA = 0x883D;
constexpr uint32_t BLEND_DST_RGB = 0x80C8;
constexpr uint32_t BLEND_SRC_RGB = 0x80C9;
constexpr uint32_t BLEND_DST_ALPHA = 0x80CA;
constexpr uint32_t BLEND_SRC_ALPHA = 0x80CB;
constexpr uint32_t BLEND_COLOR = 0x8005;

constexpr uint32_t MAX_TEXTURE_SIZE = 0x0D33;
constexpr uint32_t MAX_VIEWPORT_DIMS = 0x0D3A;
constexpr uint32_t MAX_VERTEX_ATTRIBS = 0x8869;
constexpr uint32_t MAX_VERTEX_UNIFORM_VECTORS = 0x8DFB;
constexpr uint32_t MAX_VARYING_VECTORS = 0x8DFC;
constexpr uint32_t MAX_COMBINED_TEXTURE_IMAGE_UNITS = 0x8B4D;
constexpr uint32_t MAX_VERTEX_TEXTURE_IMAGE_UNITS = 0x8B4C;
constexpr uint32_t MAX_TEXTURE_IMAGE_UNITS = 0x8872;
constexpr uint32_t MAX_FRAGMENT_UNIFORM_VECTORS = 0x8DFD;
constexpr uint32_t MAX_RENDERBUFFER_SIZE = 0x84E8;
constexpr uint32_t VENDOR = 0x1F00;
constexpr uint32_t RENDERER = 0x1F01;
constexpr uint32_t VERSION = 0x1F02;
constexpr uint32_t SHADING_LANGUAGE_VERSION = 0x8B8C;

constexpr uint32_t LOW_FLOAT = 0x8DF0;
constexpr uint32_t MEDIUM_FLOAT = 0x8DF1;
constexpr uint32_t HIGH_FLOAT = 0x8DF2;
constexpr uint32_t LOW_INT = 0x8DF3;
constexpr uint32_t MEDIUM_INT = 0x8DF4;
constexpr uint32_t HIGH_INT = 0x8DF5;

constexpr uint32_t VERTEX_ARRAY_BINDING = 0x85B5;

} // namespace webgl

struct GlslCompiledShader;

struct WebGLActiveVar {
    std::string name;
    uint32_t type{webgl::FLOAT};
    int size{1};
    int location{-1};
};

struct WebGLShaderObj {
    uint32_t id{0};
    uint32_t type{0};
    std::string source;
    bool compiled{false};
    bool deleted{false};
    std::string info_log;
    std::shared_ptr<GlslCompiledShader> ast;
};

struct WebGLUniformValue {
    uint32_t gl_type{webgl::FLOAT};
    std::array<float, 16> data{};
    int count{1};
};

struct WebGLProgramObj {
    uint32_t id{0};
    uint32_t vertex_shader_id{0};
    uint32_t fragment_shader_id{0};
    bool linked{false};
    bool validated{false};
    bool deleted{false};
    std::string info_log;
    std::shared_ptr<GlslCompiledShader> vs;
    std::shared_ptr<GlslCompiledShader> fs;
    std::unordered_map<std::string, int> bound_attrib_locations;
    std::vector<WebGLActiveVar> active_attribs;
    std::vector<WebGLActiveVar> active_uniforms;
    std::unordered_map<std::string, int> uniform_locations;
    std::unordered_map<int, WebGLUniformValue> uniform_values;
};

struct WebGLBufferObj {
    uint32_t id{0};
    uint32_t usage{webgl::STATIC_DRAW};
    bool deleted{false};
    std::vector<uint8_t> data;
};

struct WebGLTextureObj {
    uint32_t id{0};
    int width{0};
    int height{0};
    uint32_t internal_format{webgl::RGBA};
    uint32_t min_filter{webgl::NEAREST};
    uint32_t mag_filter{webgl::LINEAR};
    uint32_t wrap_s{webgl::REPEAT};
    uint32_t wrap_t{webgl::REPEAT};
    bool deleted{false};
    // Stored in OpenGL bottom-up row order (y=0 is bottom row), RGBA8.
    std::vector<uint8_t> pixels;
};

struct WebGLRenderbufferObj {
    uint32_t id{0};
    int width{0};
    int height{0};
    uint32_t internal_format{webgl::RGBA4};
    bool deleted{false};
    std::vector<uint8_t> color;
    std::vector<float> depth;
};

struct WebGLFramebufferObj {
    uint32_t id{0};
    uint32_t color_tex_id{0};
    uint32_t color_rb_id{0};
    uint32_t depth_rb_id{0};
    bool deleted{false};
};

struct WebGLVertexAttribState {
    bool enabled{false};
    int size{4};
    uint32_t type{webgl::FLOAT};
    bool normalized{false};
    int stride{0};
    std::size_t offset{0};
    uint32_t buffer_id{0};
    std::array<float, 4> generic_value{0.0f, 0.0f, 0.0f, 1.0f};
};

struct WebGLVertexArrayObj {
    uint32_t id{0};
    bool deleted{false};
    uint32_t element_array_buffer{0};
    std::array<WebGLVertexAttribState, 16> attribs{};
};

/// Software WebGL 1.0 / 2.0 rendering context backed by a CanvasRenderingContext2D
/// RGBA8 image store so HTMLCanvasElement compositing, toDataURL, toBlob,
/// drawImage, and OffscreenCanvas all share the exact same pixels.
class WebGLContext {
public:
    static constexpr int kMaxAttribs = 16;
    static constexpr int kMaxTextureUnits = 16;
    static constexpr int kMaxTextureSize = 2048;

    WebGLContext(std::shared_ptr<CanvasRenderingContext2D> backing, bool is_webgl2);

    bool is_webgl2() const { return m_is_webgl2; }
    std::shared_ptr<CanvasRenderingContext2D> backing() const { return m_backing; }

    int drawing_buffer_width() const;
    int drawing_buffer_height() const;
    void sync_backing_size();

    // Error state
    uint32_t get_error();
    void set_error(uint32_t err);

    // Clear & viewport / scissor / state
    void clear_color(float r, float g, float b, float a);
    void clear_depth(float d);
    void clear_stencil(int s);
    void clear(uint32_t mask);
    void viewport(int x, int y, int w, int h);
    void scissor(int x, int y, int w, int h);
    void enable(uint32_t cap);
    void disable(uint32_t cap);
    bool is_enabled(uint32_t cap) const;
    void color_mask(bool r, bool g, bool b, bool a);
    void depth_mask(bool flag);
    void depth_func(uint32_t func);
    void depth_range(float z_near, float z_far);
    void cull_face(uint32_t mode);
    void front_face(uint32_t mode);
    void line_width(float w);
    void blend_func(uint32_t sfactor, uint32_t dfactor);
    void blend_func_separate(uint32_t src_rgb, uint32_t dst_rgb, uint32_t src_a, uint32_t dst_a);
    void blend_equation(uint32_t mode);
    void blend_equation_separate(uint32_t mode_rgb, uint32_t mode_a);
    void blend_color(float r, float g, float b, float a);
    void pixel_storei(uint32_t pname, int param);

    // Shaders & programs
    uint32_t create_shader(uint32_t type);
    void delete_shader(uint32_t id);
    bool is_shader(uint32_t id) const;
    void shader_source(uint32_t id, const std::string& src);
    std::string get_shader_source(uint32_t id) const;
    void compile_shader(uint32_t id);
    bool get_shader_parameter(uint32_t id, uint32_t pname, int& out_int, bool& is_bool);
    std::string get_shader_info_log(uint32_t id) const;

    uint32_t create_program();
    void delete_program(uint32_t id);
    bool is_program(uint32_t id) const;
    void attach_shader(uint32_t prog_id, uint32_t shader_id);
    void detach_shader(uint32_t prog_id, uint32_t shader_id);
    void bind_attrib_location(uint32_t prog_id, uint32_t index, const std::string& name);
    void link_program(uint32_t prog_id);
    void validate_program(uint32_t prog_id);
    void use_program(uint32_t prog_id);
    bool get_program_parameter(uint32_t prog_id, uint32_t pname, int& out_int, bool& is_bool);
    std::string get_program_info_log(uint32_t prog_id) const;
    int get_attrib_location(uint32_t prog_id, const std::string& name);
    int get_uniform_location(uint32_t prog_id, const std::string& name);
    bool get_active_attrib(uint32_t prog_id, uint32_t index, WebGLActiveVar& out);
    bool get_active_uniform(uint32_t prog_id, uint32_t index, WebGLActiveVar& out);

    // Uniform setters
    void uniform_floats(int loc, uint32_t expected_type, const float* vals, int count, int components);
    void uniform_ints(int loc, uint32_t expected_type, const int* vals, int count, int components);
    void uniform_matrix(int loc, int dim, bool transpose, const float* vals, int count);

    // Buffers
    uint32_t create_buffer();
    void delete_buffer(uint32_t id);
    bool is_buffer(uint32_t id) const;
    void bind_buffer(uint32_t target, uint32_t id);
    void buffer_data_size(uint32_t target, std::size_t size, uint32_t usage);
    void buffer_data_bytes(uint32_t target, const uint8_t* data, std::size_t size, uint32_t usage);
    void buffer_sub_data(uint32_t target, std::size_t offset, const uint8_t* data, std::size_t size);
    int get_buffer_parameter(uint32_t target, uint32_t pname);

    // Vertex attributes & VAOs
    void enable_vertex_attrib_array(uint32_t index);
    void disable_vertex_attrib_array(uint32_t index);
    void vertex_attrib_pointer(uint32_t index, int size, uint32_t type, bool normalized,
                               int stride, std::size_t offset);
    void vertex_attrib_4f(uint32_t index, float x, float y, float z, float w);
    uint32_t create_vertex_array();
    void delete_vertex_array(uint32_t id);
    bool is_vertex_array(uint32_t id) const;
    void bind_vertex_array(uint32_t id);

    // Textures
    uint32_t create_texture();
    void delete_texture(uint32_t id);
    bool is_texture(uint32_t id) const;
    void active_texture(uint32_t unit_enum);
    void bind_texture(uint32_t target, uint32_t id);
    void tex_parameteri(uint32_t target, uint32_t pname, int param);
    void tex_image_2d(uint32_t target, int level, uint32_t internal_format, int width, int height,
                      int border, uint32_t format, uint32_t type, const uint8_t* pixels,
                      std::size_t pixel_byte_len);
    void tex_sub_image_2d(uint32_t target, int level, int xoffset, int yoffset, int width, int height,
                          uint32_t format, uint32_t type, const uint8_t* pixels,
                          std::size_t pixel_byte_len);
    void generate_mipmap(uint32_t target);

    // Framebuffers & Renderbuffers
    uint32_t create_framebuffer();
    void delete_framebuffer(uint32_t id);
    bool is_framebuffer(uint32_t id) const;
    void bind_framebuffer(uint32_t target, uint32_t id);
    uint32_t create_renderbuffer();
    void delete_renderbuffer(uint32_t id);
    bool is_renderbuffer(uint32_t id) const;
    void bind_renderbuffer(uint32_t target, uint32_t id);
    void renderbuffer_storage(uint32_t target, uint32_t internal_format, int width, int height);
    void framebuffer_texture_2d(uint32_t target, uint32_t attachment, uint32_t textarget,
                                uint32_t texture_id, int level);
    void framebuffer_renderbuffer(uint32_t target, uint32_t attachment, uint32_t rbtarget,
                                  uint32_t rb_id);
    uint32_t check_framebuffer_status(uint32_t target);

    // Drawing & readback
    void draw_arrays(uint32_t mode, int first, int count);
    void draw_elements(uint32_t mode, int count, uint32_t type, std::size_t offset);
    bool read_pixels(int x, int y, int width, int height, uint32_t format, uint32_t type,
                     std::vector<uint8_t>& out_rgba);

    // State queries for getParameter
    std::array<float, 4> clear_color_value() const { return m_clear_color; }
    float clear_depth_value() const { return m_clear_depth; }
    std::array<int, 4> viewport_box() const { return {m_vp_x, m_vp_y, m_vp_w, m_vp_h}; }
    std::array<int, 4> scissor_box() const { return {m_sc_x, m_sc_y, m_sc_w, m_sc_h}; }
    std::array<bool, 4> color_mask_value() const { return m_color_mask; }
    bool depth_mask_value() const { return m_depth_mask; }
    uint32_t depth_func_value() const { return m_depth_func; }
    uint32_t cull_face_value() const { return m_cull_face; }
    uint32_t front_face_value() const { return m_front_face; }
    float line_width_value() const { return m_line_width; }
    uint32_t current_program_id() const { return m_current_program; }
    uint32_t bound_array_buffer_id() const { return m_array_buffer; }
    uint32_t bound_element_buffer_id() const;
    uint32_t bound_framebuffer_id() const { return m_bound_framebuffer; }
    uint32_t bound_renderbuffer_id() const { return m_bound_renderbuffer; }
    uint32_t bound_texture_2d_id() const { return m_texture_units[m_active_texture_unit]; }
    uint32_t active_texture_enum() const { return webgl::TEXTURE0 + m_active_texture_unit; }
    uint32_t bound_vao_id() const { return m_bound_vao; }
    uint32_t blend_src_rgb() const { return m_blend_src_rgb; }
    uint32_t blend_dst_rgb() const { return m_blend_dst_rgb; }
    uint32_t blend_src_alpha() const { return m_blend_src_alpha; }
    uint32_t blend_dst_alpha() const { return m_blend_dst_alpha; }
    uint32_t blend_eq_rgb() const { return m_blend_eq_rgb; }
    uint32_t blend_eq_alpha() const { return m_blend_eq_alpha; }
    std::array<float, 4> blend_color_value() const { return m_blend_color; }
    std::array<float, 2> depth_range_value() const { return {m_depth_near, m_depth_far}; }

    // Texture sampling helper (used by GLSL evaluator)
    std::array<float, 4> sample_texture_2d(int unit, float u, float v) const;

private:
    struct RenderTarget {
        uint8_t* rgba_color{nullptr};  // FBO texture / renderbuffer (RGBA8, bottom-up)
        uint32_t* argb_color{nullptr}; // Default Canvas2D backing (0xAARRGGBB, top-down)
        float* depth{nullptr};
        int width{0};
        int height{0};
        bool flip_y{false};
    };

    bool resolve_render_target(RenderTarget& out);
    WebGLVertexArrayObj& current_vao();
    const WebGLVertexArrayObj& current_vao() const;
    std::array<float, 4> fetch_vertex_attrib(const WebGLVertexAttribState& attr, int vertex_index) const;
    void write_pixel(RenderTarget& rt, int x, int y, float depth, const std::array<float, 4>& rgba);
    bool depth_passes(float incoming, float stored) const;
    void draw_primitives(uint32_t mode, const std::vector<int>& indices);

    std::shared_ptr<CanvasRenderingContext2D> m_backing;
    bool m_is_webgl2{false};
    int m_depth_w{0};
    int m_depth_h{0};
    std::vector<float> m_default_depth;
    std::vector<uint8_t> m_default_stencil;

    uint32_t m_error{webgl::NO_ERROR};
    uint32_t m_next_id{1};

    std::array<float, 4> m_clear_color{0.0f, 0.0f, 0.0f, 0.0f};
    float m_clear_depth{1.0f};
    int m_clear_stencil{0};

    int m_vp_x{0}, m_vp_y{0}, m_vp_w{0}, m_vp_h{0};
    int m_sc_x{0}, m_sc_y{0}, m_sc_w{0}, m_sc_h{0};

    bool m_depth_test{false};
    bool m_scissor_test{false};
    bool m_blend{false};
    bool m_cull_face_enabled{false};
    bool m_dither{true};
    bool m_stencil_test{false};
    bool m_polygon_offset_fill{false};

    std::array<bool, 4> m_color_mask{true, true, true, true};
    bool m_depth_mask{true};
    uint32_t m_depth_func{webgl::LESS};
    float m_depth_near{0.0f};
    float m_depth_far{1.0f};
    uint32_t m_cull_face{webgl::BACK};
    uint32_t m_front_face{webgl::CCW};
    float m_line_width{1.0f};

    uint32_t m_blend_src_rgb{webgl::ONE};
    uint32_t m_blend_dst_rgb{webgl::ZERO};
    uint32_t m_blend_src_alpha{webgl::ONE};
    uint32_t m_blend_dst_alpha{webgl::ZERO};
    uint32_t m_blend_eq_rgb{webgl::FUNC_ADD};
    uint32_t m_blend_eq_alpha{webgl::FUNC_ADD};
    std::array<float, 4> m_blend_color{0.0f, 0.0f, 0.0f, 0.0f};

    int m_unpack_alignment{4};
    bool m_unpack_flip_y{false};
    bool m_unpack_premultiply_alpha{false};

    uint32_t m_current_program{0};
    uint32_t m_array_buffer{0};
    uint32_t m_active_texture_unit{0};
    std::array<uint32_t, kMaxTextureUnits> m_texture_units{};
    uint32_t m_bound_framebuffer{0};
    uint32_t m_bound_renderbuffer{0};
    uint32_t m_bound_vao{0};
    WebGLVertexArrayObj m_default_vao{};

    std::unordered_map<uint32_t, WebGLShaderObj> m_shaders;
    std::unordered_map<uint32_t, WebGLProgramObj> m_programs;
    std::unordered_map<uint32_t, WebGLBufferObj> m_buffers;
    std::unordered_map<uint32_t, WebGLTextureObj> m_textures;
    std::unordered_map<uint32_t, WebGLRenderbufferObj> m_renderbuffers;
    std::unordered_map<uint32_t, WebGLFramebufferObj> m_framebuffers;
    std::unordered_map<uint32_t, WebGLVertexArrayObj> m_vaos;
};

} // namespace litebird
