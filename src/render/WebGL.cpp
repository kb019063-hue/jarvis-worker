#include "litebird/render/WebGL.hpp"
#include "litebird/render/RasterBudget.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <sstream>

namespace litebird {

namespace {

constexpr std::size_t kMaxBufferBytes = 16u * 1024u * 1024u;
constexpr int kMaxGlslLoopIterations = 4096;
constexpr int kMaxGlslCallDepth = 16;
constexpr int kMaxDrawCount = 262144;

inline uint8_t float_to_u8(float v) {
    const float clamped = std::clamp(std::isfinite(v) ? v : 0.0f, 0.0f, 1.0f);
    return static_cast<uint8_t>(std::lround(clamped * 255.0f));
}

// ============================================================================
// GLSL ES 1.00 / 3.00 Mini-Compiler & AST Evaluator
// ============================================================================

enum class GlslType {
    Void,
    Bool,
    Int,
    Float,
    Vec2,
    Vec3,
    Vec4,
    IVec2,
    IVec3,
    IVec4,
    BVec2,
    BVec3,
    BVec4,
    Mat2,
    Mat3,
    Mat4,
    Sampler2D
};

int glsl_components(GlslType t) {
    switch (t) {
        case GlslType::Void: return 0;
        case GlslType::Bool:
        case GlslType::Int:
        case GlslType::Float:
        case GlslType::Sampler2D: return 1;
        case GlslType::Vec2:
        case GlslType::IVec2:
        case GlslType::BVec2: return 2;
        case GlslType::Vec3:
        case GlslType::IVec3:
        case GlslType::BVec3: return 3;
        case GlslType::Vec4:
        case GlslType::IVec4:
        case GlslType::BVec4:
        case GlslType::Mat2: return 4;
        case GlslType::Mat3: return 9;
        case GlslType::Mat4: return 16;
    }
    return 0;
}

bool glsl_is_matrix(GlslType t) {
    return t == GlslType::Mat2 || t == GlslType::Mat3 || t == GlslType::Mat4;
}

int glsl_matrix_dim(GlslType t) {
    if (t == GlslType::Mat2) return 2;
    if (t == GlslType::Mat3) return 3;
    if (t == GlslType::Mat4) return 4;
    return 0;
}

uint32_t glsl_to_gl_type(GlslType t) {
    switch (t) {
        case GlslType::Bool: return webgl::BOOL;
        case GlslType::Int: return webgl::INT;
        case GlslType::Float: return webgl::FLOAT;
        case GlslType::Vec2: return webgl::FLOAT_VEC2;
        case GlslType::Vec3: return webgl::FLOAT_VEC3;
        case GlslType::Vec4: return webgl::FLOAT_VEC4;
        case GlslType::IVec2: return webgl::INT_VEC2;
        case GlslType::IVec3: return webgl::INT_VEC3;
        case GlslType::IVec4: return webgl::INT_VEC4;
        case GlslType::BVec2: return webgl::BOOL_VEC2;
        case GlslType::BVec3: return webgl::BOOL_VEC3;
        case GlslType::BVec4: return webgl::BOOL_VEC4;
        case GlslType::Mat2: return webgl::FLOAT_MAT2;
        case GlslType::Mat3: return webgl::FLOAT_MAT3;
        case GlslType::Mat4: return webgl::FLOAT_MAT4;
        case GlslType::Sampler2D: return webgl::SAMPLER_2D;
        case GlslType::Void: return webgl::FLOAT;
    }
    return webgl::FLOAT;
}

GlslType glsl_vec_type_for_size(int n, GlslType base = GlslType::Float) {
    if (base == GlslType::Int || base == GlslType::IVec2 || base == GlslType::IVec3 || base == GlslType::IVec4) {
        if (n == 1) return GlslType::Int;
        if (n == 2) return GlslType::IVec2;
        if (n == 3) return GlslType::IVec3;
        return GlslType::IVec4;
    }
    if (base == GlslType::Bool || base == GlslType::BVec2 || base == GlslType::BVec3 || base == GlslType::BVec4) {
        if (n == 1) return GlslType::Bool;
        if (n == 2) return GlslType::BVec2;
        if (n == 3) return GlslType::BVec3;
        return GlslType::BVec4;
    }
    if (n == 1) return GlslType::Float;
    if (n == 2) return GlslType::Vec2;
    if (n == 3) return GlslType::Vec3;
    return GlslType::Vec4;
}

struct GlslVal {
    GlslType type{GlslType::Float};
    std::array<float, 16> f{};

    static GlslVal make_float(float v) {
        GlslVal r;
        r.type = GlslType::Float;
        r.f[0] = v;
        return r;
    }
    static GlslVal make_int(int v) {
        GlslVal r;
        r.type = GlslType::Int;
        r.f[0] = static_cast<float>(v);
        return r;
    }
    static GlslVal make_bool(bool v) {
        GlslVal r;
        r.type = GlslType::Bool;
        r.f[0] = v ? 1.0f : 0.0f;
        return r;
    }
    static GlslVal make_vec4(float x, float y, float z, float w) {
        GlslVal r;
        r.type = GlslType::Vec4;
        r.f[0] = x; r.f[1] = y; r.f[2] = z; r.f[3] = w;
        return r;
    }
    static GlslVal zero(GlslType t) {
        GlslVal r;
        r.type = t;
        return r;
    }
    bool truthy() const { return f[0] != 0.0f; }
};

enum class GlslTokKind {
    Eof,
    Ident,
    IntLit,
    FloatLit,
    LParen, RParen, LBrace, RBrace, LBracket, RBracket,
    Semi, Comma, Dot, Question, Colon,
    Plus, Minus, Star, Slash, Bang,
    PlusPlus, MinusMinus,
    Assign, PlusAssign, MinusAssign, StarAssign, SlashAssign,
    Eq, Neq, Lt, Le, Gt, Ge, AndAnd, OrOr
};

struct GlslTok {
    GlslTokKind kind{GlslTokKind::Eof};
    std::string text;
    double num{0.0};
    int line{1};
};

std::string preprocess_glsl(const std::string& src) {
    // Strip /* */ and // comments while preserving line count.
    std::string stripped;
    stripped.reserve(src.size());
    for (std::size_t i = 0; i < src.size();) {
        if (i + 1 < src.size() && src[i] == '/' && src[i + 1] == '/') {
            i += 2;
            while (i < src.size() && src[i] != '\n') ++i;
        } else if (i + 1 < src.size() && src[i] == '/' && src[i + 1] == '*') {
            i += 2;
            while (i + 1 < src.size() && !(src[i] == '*' && src[i + 1] == '/')) {
                if (src[i] == '\n') stripped.push_back('\n');
                ++i;
            }
            if (i + 1 < src.size()) i += 2;
        } else {
            stripped.push_back(src[i++]);
        }
    }

    // Simple directive handling: #version, #define, #ifdef/#ifndef/#else/#endif.
    std::unordered_map<std::string, std::string> defines;
    defines["GL_ES"] = "1";
    std::vector<bool> active_stack;
    auto is_active = [&]() {
        for (bool b : active_stack) if (!b) return false;
        return true;
    };

    std::ostringstream out;
    std::istringstream iss(stripped);
    std::string line;
    while (std::getline(iss, line)) {
        std::size_t pos = 0;
        while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) ++pos;
        if (pos < line.size() && line[pos] == '#') {
            std::istringstream ls(line.substr(pos + 1));
            std::string dir;
            ls >> dir;
            if (dir == "ifdef") {
                std::string name;
                ls >> name;
                active_stack.push_back(defines.count(name) > 0);
            } else if (dir == "ifndef") {
                std::string name;
                ls >> name;
                active_stack.push_back(defines.count(name) == 0);
            } else if (dir == "if") {
                std::string tok;
                ls >> tok;
                active_stack.push_back(tok != "0");
            } else if (dir == "else") {
                if (!active_stack.empty()) active_stack.back() = !active_stack.back();
            } else if (dir == "endif") {
                if (!active_stack.empty()) active_stack.pop_back();
            } else if (dir == "define" && is_active()) {
                std::string name;
                ls >> name;
                std::string rest;
                std::getline(ls, rest);
                std::size_t r = 0;
                while (r < rest.size() && std::isspace(static_cast<unsigned char>(rest[r]))) ++r;
                if (!name.empty() && name.find('(') == std::string::npos) {
                    defines[name] = rest.substr(r);
                }
            }
            out << '\n';
            continue;
        }
        if (!is_active()) {
            out << '\n';
            continue;
        }
        // Apply simple identifier macro substitution.
        if (!defines.empty()) {
            std::string expanded;
            expanded.reserve(line.size());
            for (std::size_t i = 0; i < line.size();) {
                if (std::isalpha(static_cast<unsigned char>(line[i])) || line[i] == '_') {
                    std::size_t start = i++;
                    while (i < line.size() &&
                           (std::isalnum(static_cast<unsigned char>(line[i])) || line[i] == '_')) ++i;
                    std::string id = line.substr(start, i - start);
                    auto it = defines.find(id);
                    if (it != defines.end() && id != "GL_ES") {
                        expanded += it->second;
                    } else {
                        expanded += id;
                    }
                } else {
                    expanded.push_back(line[i++]);
                }
            }
            out << expanded << '\n';
        } else {
            out << line << '\n';
        }
    }
    return out.str();
}

bool lex_glsl(const std::string& src, std::vector<GlslTok>& out, std::string& err) {
    out.clear();
    int line = 1;
    std::size_t i = 0;
    const std::size_t n = src.size();
    while (i < n) {
        char c = src[i];
        if (c == '\n') { ++line; ++i; continue; }
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }

        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::size_t start = i++;
            while (i < n && (std::isalnum(static_cast<unsigned char>(src[i])) || src[i] == '_')) ++i;
            out.push_back({GlslTokKind::Ident, src.substr(start, i - start), 0.0, line});
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '.' && i + 1 < n && std::isdigit(static_cast<unsigned char>(src[i + 1])))) {
            std::size_t start = i;
            bool is_float = false;
            if (c == '0' && i + 1 < n && (src[i + 1] == 'x' || src[i + 1] == 'X')) {
                i += 2;
                while (i < n && std::isxdigit(static_cast<unsigned char>(src[i]))) ++i;
                std::string s = src.substr(start, i - start);
                double val = static_cast<double>(std::strtoll(s.c_str(), nullptr, 16));
                out.push_back({GlslTokKind::IntLit, s, val, line});
                continue;
            }
            while (i < n && std::isdigit(static_cast<unsigned char>(src[i]))) ++i;
            if (i < n && src[i] == '.') {
                is_float = true;
                ++i;
                while (i < n && std::isdigit(static_cast<unsigned char>(src[i]))) ++i;
            }
            if (i < n && (src[i] == 'e' || src[i] == 'E')) {
                is_float = true;
                ++i;
                if (i < n && (src[i] == '+' || src[i] == '-')) ++i;
                while (i < n && std::isdigit(static_cast<unsigned char>(src[i]))) ++i;
            }
            if (i < n && (src[i] == 'f' || src[i] == 'F' || src[i] == 'u' || src[i] == 'U')) {
                if (src[i] == 'f' || src[i] == 'F') is_float = true;
                ++i;
            }
            std::string s = src.substr(start, i - start);
            double val = std::strtod(s.c_str(), nullptr);
            out.push_back({is_float ? GlslTokKind::FloatLit : GlslTokKind::IntLit, s, val, line});
            continue;
        }

        auto two = [&](char a, char b, GlslTokKind k) -> bool {
            if (c == a && i + 1 < n && src[i + 1] == b) {
                out.push_back({k, src.substr(i, 2), 0.0, line});
                i += 2;
                return true;
            }
            return false;
        };
        if (two('+', '+', GlslTokKind::PlusPlus) || two('-', '-', GlslTokKind::MinusMinus) ||
            two('+', '=', GlslTokKind::PlusAssign) || two('-', '=', GlslTokKind::MinusAssign) ||
            two('*', '=', GlslTokKind::StarAssign) || two('/', '=', GlslTokKind::SlashAssign) ||
            two('=', '=', GlslTokKind::Eq) || two('!', '=', GlslTokKind::Neq) ||
            two('<', '=', GlslTokKind::Le) || two('>', '=', GlslTokKind::Ge) ||
            two('&', '&', GlslTokKind::AndAnd) || two('|', '|', GlslTokKind::OrOr)) {
            continue;
        }

        GlslTokKind single = GlslTokKind::Eof;
        switch (c) {
            case '(': single = GlslTokKind::LParen; break;
            case ')': single = GlslTokKind::RParen; break;
            case '{': single = GlslTokKind::LBrace; break;
            case '}': single = GlslTokKind::RBrace; break;
            case '[': single = GlslTokKind::LBracket; break;
            case ']': single = GlslTokKind::RBracket; break;
            case ';': single = GlslTokKind::Semi; break;
            case ',': single = GlslTokKind::Comma; break;
            case '.': single = GlslTokKind::Dot; break;
            case '?': single = GlslTokKind::Question; break;
            case ':': single = GlslTokKind::Colon; break;
            case '+': single = GlslTokKind::Plus; break;
            case '-': single = GlslTokKind::Minus; break;
            case '*': single = GlslTokKind::Star; break;
            case '/': single = GlslTokKind::Slash; break;
            case '!': single = GlslTokKind::Bang; break;
            case '=': single = GlslTokKind::Assign; break;
            case '<': single = GlslTokKind::Lt; break;
            case '>': single = GlslTokKind::Gt; break;
            default:
                err = "Unexpected character '" + std::string(1, c) + "' at line " + std::to_string(line);
                return false;
        }
        out.push_back({single, std::string(1, c), 0.0, line});
        ++i;
    }
    out.push_back({GlslTokKind::Eof, "", 0.0, line});
    return true;
}

bool parse_glsl_type_name(const std::string& s, GlslType& out) {
    if (s == "void") { out = GlslType::Void; return true; }
    if (s == "bool") { out = GlslType::Bool; return true; }
    if (s == "int" || s == "uint") { out = GlslType::Int; return true; }
    if (s == "float") { out = GlslType::Float; return true; }
    if (s == "vec2") { out = GlslType::Vec2; return true; }
    if (s == "vec3") { out = GlslType::Vec3; return true; }
    if (s == "vec4") { out = GlslType::Vec4; return true; }
    if (s == "ivec2" || s == "uvec2") { out = GlslType::IVec2; return true; }
    if (s == "ivec3" || s == "uvec3") { out = GlslType::IVec3; return true; }
    if (s == "ivec4" || s == "uvec4") { out = GlslType::IVec4; return true; }
    if (s == "bvec2") { out = GlslType::BVec2; return true; }
    if (s == "bvec3") { out = GlslType::BVec3; return true; }
    if (s == "bvec4") { out = GlslType::BVec4; return true; }
    if (s == "mat2") { out = GlslType::Mat2; return true; }
    if (s == "mat3") { out = GlslType::Mat3; return true; }
    if (s == "mat4") { out = GlslType::Mat4; return true; }
    if (s == "sampler2D") { out = GlslType::Sampler2D; return true; }
    return false;
}

struct GlslExpr {
    enum class Kind {
        FloatLit, IntLit, BoolLit, Ident, Unary, Binary, Assign, Ternary, Member, Index, Call
    } kind{Kind::FloatLit};
    double num{0.0};
    bool bval{false};
    std::string name;
    GlslTokKind op{GlslTokKind::Eof};
    bool postfix{false};
    std::shared_ptr<GlslExpr> a;
    std::shared_ptr<GlslExpr> b;
    std::shared_ptr<GlslExpr> c;
    std::vector<std::shared_ptr<GlslExpr>> args;
};

struct GlslStmt {
    enum class Kind {
        Block, VarDecl, Expr, If, For, While, Return, Break, Continue, Discard
    } kind{Kind::Block};
    GlslType decl_type{GlslType::Float};
    struct DeclItem {
        std::string name;
        std::shared_ptr<GlslExpr> init;
    };
    std::vector<DeclItem> decls;
    std::shared_ptr<GlslExpr> expr;
    std::shared_ptr<GlslExpr> cond;
    std::shared_ptr<GlslExpr> step;
    std::shared_ptr<GlslStmt> init_stmt;
    std::shared_ptr<GlslStmt> then_branch;
    std::shared_ptr<GlslStmt> else_branch;
    std::vector<std::shared_ptr<GlslStmt>> stmts;
};

struct GlslFunc {
    GlslType return_type{GlslType::Void};
    std::string name;
    struct Param {
        GlslType type{GlslType::Float};
        std::string name;
    };
    std::vector<Param> params;
    std::shared_ptr<GlslStmt> body;
};

struct GlslDeclVar {
    std::string name;
    GlslType type{GlslType::Float};
    int explicit_location{-1};
    std::shared_ptr<GlslExpr> init;
};

} // namespace

struct GlslCompiledShader {
    uint32_t shader_type{webgl::VERTEX_SHADER};
    std::vector<GlslDeclVar> attributes;
    std::vector<GlslDeclVar> varyings;
    std::vector<GlslDeclVar> uniforms;
    std::vector<GlslDeclVar> globals;
    std::string frag_out_name; // GLSL ES 3.00 `out vec4 ...`
    std::vector<GlslFunc> functions;
    bool has_discard{false};
};

namespace {

class GlslParser {
public:
    GlslParser(const std::vector<GlslTok>& toks, uint32_t shader_type)
        : m_toks(toks), m_shader_type(shader_type) {}

    bool parse(GlslCompiledShader& out, std::string& err) {
        out.shader_type = m_shader_type;
        while (!at(GlslTokKind::Eof)) {
            if (!parse_top_level(out)) {
                err = m_err.empty() ? "GLSL parse error" : m_err;
                return false;
            }
        }
        bool found_main = false;
        for (const auto& fn : out.functions) {
            if (fn.name == "main") found_main = true;
        }
        if (!found_main) {
            err = "Shader does not define main()";
            return false;
        }
        out.has_discard = m_has_discard;
        return true;
    }

private:
    const GlslTok& peek(std::size_t off = 0) const {
        if (m_pos + off >= m_toks.size()) return m_toks.back();
        return m_toks[m_pos + off];
    }
    bool at(GlslTokKind k) const { return peek().kind == k; }
    bool match(GlslTokKind k) {
        if (at(k)) { ++m_pos; return true; }
        return false;
    }
    bool expect(GlslTokKind k, const char* msg) {
        if (match(k)) return true;
        fail(msg);
        return false;
    }
    void fail(const std::string& msg) {
        if (m_err.empty()) {
            m_err = msg + " at line " + std::to_string(peek().line);
        }
    }
    bool is_precision_kw(const std::string& s) const {
        return s == "lowp" || s == "mediump" || s == "highp";
    }
    void skip_precision_qualifiers() {
        while (at(GlslTokKind::Ident) && is_precision_kw(peek().text)) ++m_pos;
    }

    bool parse_top_level(GlslCompiledShader& out) {
        if (match(GlslTokKind::Semi)) return true;
        if (at(GlslTokKind::Ident) && peek().text == "precision") {
            ++m_pos;
            skip_precision_qualifiers();
            if (at(GlslTokKind::Ident)) ++m_pos;
            return expect(GlslTokKind::Semi, "Expected ';' after precision declaration");
        }

        int layout_loc = -1;
        std::string storage;
        while (at(GlslTokKind::Ident)) {
            const std::string& w = peek().text;
            if (w == "layout") {
                ++m_pos;
                if (!expect(GlslTokKind::LParen, "Expected '(' after layout")) return false;
                while (!at(GlslTokKind::RParen) && !at(GlslTokKind::Eof)) {
                    if (at(GlslTokKind::Ident) && peek().text == "location") {
                        ++m_pos;
                        if (match(GlslTokKind::Assign) && at(GlslTokKind::IntLit)) {
                            layout_loc = static_cast<int>(peek().num);
                            ++m_pos;
                        }
                    } else {
                        ++m_pos;
                    }
                    match(GlslTokKind::Comma);
                }
                if (!expect(GlslTokKind::RParen, "Expected ')' after layout")) return false;
            } else if (w == "attribute" || w == "varying" || w == "uniform" ||
                       w == "in" || w == "out" || w == "const") {
                storage = w;
                ++m_pos;
            } else if (is_precision_kw(w) || w == "flat" || w == "smooth" || w == "centroid") {
                ++m_pos;
            } else {
                break;
            }
        }

        if (!at(GlslTokKind::Ident)) {
            fail("Expected type name");
            return false;
        }
        GlslType type = GlslType::Float;
        if (!parse_glsl_type_name(peek().text, type)) {
            fail("Unknown type '" + peek().text + "'");
            return false;
        }
        ++m_pos;

        if (!at(GlslTokKind::Ident)) {
            fail("Expected identifier");
            return false;
        }
        std::string name = peek().text;
        ++m_pos;

        // Function definition or prototype
        if (storage.empty() && match(GlslTokKind::LParen)) {
            GlslFunc fn;
            fn.return_type = type;
            fn.name = name;
            if (!at(GlslTokKind::RParen)) {
                while (true) {
                    while (at(GlslTokKind::Ident) &&
                           (peek().text == "in" || peek().text == "out" || peek().text == "inout" ||
                            peek().text == "const" || is_precision_kw(peek().text))) {
                        ++m_pos;
                    }
                    if (!at(GlslTokKind::Ident)) { fail("Expected parameter type"); return false; }
                    GlslType pt = GlslType::Float;
                    if (!parse_glsl_type_name(peek().text, pt)) {
                        fail("Unknown parameter type '" + peek().text + "'");
                        return false;
                    }
                    ++m_pos;
                    if (pt == GlslType::Void && at(GlslTokKind::RParen)) break;
                    std::string pname;
                    if (at(GlslTokKind::Ident)) {
                        pname = peek().text;
                        ++m_pos;
                    }
                    fn.params.push_back({pt, pname});
                    if (!match(GlslTokKind::Comma)) break;
                }
            }
            if (!expect(GlslTokKind::RParen, "Expected ')' after parameters")) return false;
            if (match(GlslTokKind::Semi)) return true; // forward declaration
            fn.body = parse_block();
            if (!fn.body) return false;
            out.functions.push_back(std::move(fn));
            return true;
        }

        // Global / attribute / varying / uniform declaration(s)
        auto add_decl = [&](const std::string& var_name, std::shared_ptr<GlslExpr> init) {
            GlslDeclVar dv{var_name, type, layout_loc, std::move(init)};
            if (storage == "uniform") {
                out.uniforms.push_back(std::move(dv));
            } else if (storage == "attribute" ||
                       (storage == "in" && m_shader_type == webgl::VERTEX_SHADER)) {
                out.attributes.push_back(std::move(dv));
            } else if (storage == "varying" ||
                       (storage == "out" && m_shader_type == webgl::VERTEX_SHADER) ||
                       (storage == "in" && m_shader_type == webgl::FRAGMENT_SHADER)) {
                out.varyings.push_back(std::move(dv));
            } else if (storage == "out" && m_shader_type == webgl::FRAGMENT_SHADER) {
                if (out.frag_out_name.empty()) out.frag_out_name = var_name;
                out.globals.push_back(std::move(dv));
            } else {
                out.globals.push_back(std::move(dv));
            }
        };

        std::string cur_name = name;
        while (true) {
            // Skip optional array size `[N]`
            if (match(GlslTokKind::LBracket)) {
                while (!at(GlslTokKind::RBracket) && !at(GlslTokKind::Eof)) ++m_pos;
                if (!expect(GlslTokKind::RBracket, "Expected ']'")) return false;
            }
            std::shared_ptr<GlslExpr> init;
            if (match(GlslTokKind::Assign)) {
                init = parse_assignment();
                if (!init) return false;
            }
            add_decl(cur_name, std::move(init));
            if (!match(GlslTokKind::Comma)) break;
            if (!at(GlslTokKind::Ident)) { fail("Expected variable name"); return false; }
            cur_name = peek().text;
            ++m_pos;
        }
        return expect(GlslTokKind::Semi, "Expected ';' after declaration");
    }

    std::shared_ptr<GlslStmt> parse_block() {
        if (!expect(GlslTokKind::LBrace, "Expected '{'")) return nullptr;
        auto blk = std::make_shared<GlslStmt>();
        blk->kind = GlslStmt::Kind::Block;
        while (!at(GlslTokKind::RBrace) && !at(GlslTokKind::Eof)) {
            auto st = parse_stmt();
            if (!st) return nullptr;
            blk->stmts.push_back(std::move(st));
        }
        if (!expect(GlslTokKind::RBrace, "Expected '}'")) return nullptr;
        return blk;
    }

    bool looks_like_var_decl() const {
        std::size_t p = 0;
        while (peek(p).kind == GlslTokKind::Ident &&
               (peek(p).text == "const" || is_precision_kw(peek(p).text))) {
            ++p;
        }
        if (peek(p).kind != GlslTokKind::Ident) return false;
        GlslType dummy;
        if (!parse_glsl_type_name(peek(p).text, dummy)) return false;
        return peek(p + 1).kind == GlslTokKind::Ident;
    }

    std::shared_ptr<GlslStmt> parse_var_decl(bool expect_semi) {
        while (at(GlslTokKind::Ident) && (peek().text == "const" || is_precision_kw(peek().text))) ++m_pos;
        GlslType type = GlslType::Float;
        if (!at(GlslTokKind::Ident) || !parse_glsl_type_name(peek().text, type)) {
            fail("Expected type in declaration");
            return nullptr;
        }
        ++m_pos;
        auto st = std::make_shared<GlslStmt>();
        st->kind = GlslStmt::Kind::VarDecl;
        st->decl_type = type;
        while (true) {
            if (!at(GlslTokKind::Ident)) { fail("Expected variable name"); return nullptr; }
            std::string name = peek().text;
            ++m_pos;
            std::shared_ptr<GlslExpr> init;
            if (match(GlslTokKind::Assign)) {
                init = parse_assignment();
                if (!init) return nullptr;
            }
            st->decls.push_back({std::move(name), std::move(init)});
            if (!match(GlslTokKind::Comma)) break;
        }
        if (expect_semi && !expect(GlslTokKind::Semi, "Expected ';' after variable declaration")) return nullptr;
        return st;
    }

    std::shared_ptr<GlslStmt> parse_stmt() {
        if (at(GlslTokKind::LBrace)) return parse_block();
        if (match(GlslTokKind::Semi)) {
            auto st = std::make_shared<GlslStmt>();
            st->kind = GlslStmt::Kind::Block;
            return st;
        }
        if (at(GlslTokKind::Ident)) {
            const std::string& w = peek().text;
            if (w == "if") {
                ++m_pos;
                if (!expect(GlslTokKind::LParen, "Expected '(' after if")) return nullptr;
                auto cond = parse_expr();
                if (!cond || !expect(GlslTokKind::RParen, "Expected ')' after if condition")) return nullptr;
                auto then_b = parse_stmt();
                if (!then_b) return nullptr;
                std::shared_ptr<GlslStmt> else_b;
                if (at(GlslTokKind::Ident) && peek().text == "else") {
                    ++m_pos;
                    else_b = parse_stmt();
                    if (!else_b) return nullptr;
                }
                auto st = std::make_shared<GlslStmt>();
                st->kind = GlslStmt::Kind::If;
                st->cond = std::move(cond);
                st->then_branch = std::move(then_b);
                st->else_branch = std::move(else_b);
                return st;
            }
            if (w == "while") {
                ++m_pos;
                if (!expect(GlslTokKind::LParen, "Expected '(' after while")) return nullptr;
                auto cond = parse_expr();
                if (!cond || !expect(GlslTokKind::RParen, "Expected ')'")) return nullptr;
                auto body = parse_stmt();
                if (!body) return nullptr;
                auto st = std::make_shared<GlslStmt>();
                st->kind = GlslStmt::Kind::While;
                st->cond = std::move(cond);
                st->then_branch = std::move(body);
                return st;
            }
            if (w == "for") {
                ++m_pos;
                if (!expect(GlslTokKind::LParen, "Expected '(' after for")) return nullptr;
                auto st = std::make_shared<GlslStmt>();
                st->kind = GlslStmt::Kind::For;
                if (!match(GlslTokKind::Semi)) {
                    if (looks_like_var_decl()) {
                        st->init_stmt = parse_var_decl(true);
                    } else {
                        auto e = parse_expr();
                        if (!e || !expect(GlslTokKind::Semi, "Expected ';' in for")) return nullptr;
                        auto es = std::make_shared<GlslStmt>();
                        es->kind = GlslStmt::Kind::Expr;
                        es->expr = std::move(e);
                        st->init_stmt = std::move(es);
                    }
                    if (!st->init_stmt) return nullptr;
                }
                if (!at(GlslTokKind::Semi)) {
                    st->cond = parse_expr();
                    if (!st->cond) return nullptr;
                }
                if (!expect(GlslTokKind::Semi, "Expected ';' after for condition")) return nullptr;
                if (!at(GlslTokKind::RParen)) {
                    st->step = parse_expr();
                    if (!st->step) return nullptr;
                }
                if (!expect(GlslTokKind::RParen, "Expected ')' after for clauses")) return nullptr;
                st->then_branch = parse_stmt();
                if (!st->then_branch) return nullptr;
                return st;
            }
            if (w == "return") {
                ++m_pos;
                auto st = std::make_shared<GlslStmt>();
                st->kind = GlslStmt::Kind::Return;
                if (!at(GlslTokKind::Semi)) {
                    st->expr = parse_expr();
                    if (!st->expr) return nullptr;
                }
                if (!expect(GlslTokKind::Semi, "Expected ';' after return")) return nullptr;
                return st;
            }
            if (w == "break") {
                ++m_pos;
                if (!expect(GlslTokKind::Semi, "Expected ';' after break")) return nullptr;
                auto st = std::make_shared<GlslStmt>();
                st->kind = GlslStmt::Kind::Break;
                return st;
            }
            if (w == "continue") {
                ++m_pos;
                if (!expect(GlslTokKind::Semi, "Expected ';' after continue")) return nullptr;
                auto st = std::make_shared<GlslStmt>();
                st->kind = GlslStmt::Kind::Continue;
                return st;
            }
            if (w == "discard") {
                ++m_pos;
                m_has_discard = true;
                if (!expect(GlslTokKind::Semi, "Expected ';' after discard")) return nullptr;
                auto st = std::make_shared<GlslStmt>();
                st->kind = GlslStmt::Kind::Discard;
                return st;
            }
            if (looks_like_var_decl()) {
                return parse_var_decl(true);
            }
        }
        auto e = parse_expr();
        if (!e || !expect(GlslTokKind::Semi, "Expected ';' after expression")) return nullptr;
        auto st = std::make_shared<GlslStmt>();
        st->kind = GlslStmt::Kind::Expr;
        st->expr = std::move(e);
        return st;
    }

    std::shared_ptr<GlslExpr> parse_expr() { return parse_assignment(); }

    std::shared_ptr<GlslExpr> parse_assignment() {
        auto lhs = parse_ternary();
        if (!lhs) return nullptr;
        GlslTokKind k = peek().kind;
        if (k == GlslTokKind::Assign || k == GlslTokKind::PlusAssign ||
            k == GlslTokKind::MinusAssign || k == GlslTokKind::StarAssign ||
            k == GlslTokKind::SlashAssign) {
            ++m_pos;
            auto rhs = parse_assignment();
            if (!rhs) return nullptr;
            auto e = std::make_shared<GlslExpr>();
            e->kind = GlslExpr::Kind::Assign;
            e->op = k;
            e->a = std::move(lhs);
            e->b = std::move(rhs);
            return e;
        }
        return lhs;
    }

    std::shared_ptr<GlslExpr> parse_ternary() {
        auto c = parse_or();
        if (!c) return nullptr;
        if (match(GlslTokKind::Question)) {
            auto t = parse_expr();
            if (!t || !expect(GlslTokKind::Colon, "Expected ':' in ternary")) return nullptr;
            auto f = parse_ternary();
            if (!f) return nullptr;
            auto e = std::make_shared<GlslExpr>();
            e->kind = GlslExpr::Kind::Ternary;
            e->a = std::move(c);
            e->b = std::move(t);
            e->c = std::move(f);
            return e;
        }
        return c;
    }

    std::shared_ptr<GlslExpr> parse_or() {
        auto lhs = parse_and();
        while (lhs && match(GlslTokKind::OrOr)) {
            auto rhs = parse_and();
            if (!rhs) return nullptr;
            auto e = std::make_shared<GlslExpr>();
            e->kind = GlslExpr::Kind::Binary;
            e->op = GlslTokKind::OrOr;
            e->a = std::move(lhs);
            e->b = std::move(rhs);
            lhs = std::move(e);
        }
        return lhs;
    }

    std::shared_ptr<GlslExpr> parse_and() {
        auto lhs = parse_equality();
        while (lhs && match(GlslTokKind::AndAnd)) {
            auto rhs = parse_equality();
            if (!rhs) return nullptr;
            auto e = std::make_shared<GlslExpr>();
            e->kind = GlslExpr::Kind::Binary;
            e->op = GlslTokKind::AndAnd;
            e->a = std::move(lhs);
            e->b = std::move(rhs);
            lhs = std::move(e);
        }
        return lhs;
    }

    std::shared_ptr<GlslExpr> parse_equality() {
        auto lhs = parse_relational();
        while (lhs && (at(GlslTokKind::Eq) || at(GlslTokKind::Neq))) {
            GlslTokKind op = peek().kind;
            ++m_pos;
            auto rhs = parse_relational();
            if (!rhs) return nullptr;
            auto e = std::make_shared<GlslExpr>();
            e->kind = GlslExpr::Kind::Binary;
            e->op = op;
            e->a = std::move(lhs);
            e->b = std::move(rhs);
            lhs = std::move(e);
        }
        return lhs;
    }

    std::shared_ptr<GlslExpr> parse_relational() {
        auto lhs = parse_additive();
        while (lhs && (at(GlslTokKind::Lt) || at(GlslTokKind::Le) ||
                       at(GlslTokKind::Gt) || at(GlslTokKind::Ge))) {
            GlslTokKind op = peek().kind;
            ++m_pos;
            auto rhs = parse_additive();
            if (!rhs) return nullptr;
            auto e = std::make_shared<GlslExpr>();
            e->kind = GlslExpr::Kind::Binary;
            e->op = op;
            e->a = std::move(lhs);
            e->b = std::move(rhs);
            lhs = std::move(e);
        }
        return lhs;
    }

    std::shared_ptr<GlslExpr> parse_additive() {
        auto lhs = parse_multiplicative();
        while (lhs && (at(GlslTokKind::Plus) || at(GlslTokKind::Minus))) {
            GlslTokKind op = peek().kind;
            ++m_pos;
            auto rhs = parse_multiplicative();
            if (!rhs) return nullptr;
            auto e = std::make_shared<GlslExpr>();
            e->kind = GlslExpr::Kind::Binary;
            e->op = op;
            e->a = std::move(lhs);
            e->b = std::move(rhs);
            lhs = std::move(e);
        }
        return lhs;
    }

    std::shared_ptr<GlslExpr> parse_multiplicative() {
        auto lhs = parse_unary();
        while (lhs && (at(GlslTokKind::Star) || at(GlslTokKind::Slash))) {
            GlslTokKind op = peek().kind;
            ++m_pos;
            auto rhs = parse_unary();
            if (!rhs) return nullptr;
            auto e = std::make_shared<GlslExpr>();
            e->kind = GlslExpr::Kind::Binary;
            e->op = op;
            e->a = std::move(lhs);
            e->b = std::move(rhs);
            lhs = std::move(e);
        }
        return lhs;
    }

    std::shared_ptr<GlslExpr> parse_unary() {
        GlslTokKind k = peek().kind;
        if (k == GlslTokKind::Plus || k == GlslTokKind::Minus || k == GlslTokKind::Bang ||
            k == GlslTokKind::PlusPlus || k == GlslTokKind::MinusMinus) {
            ++m_pos;
            auto sub = parse_unary();
            if (!sub) return nullptr;
            auto e = std::make_shared<GlslExpr>();
            e->kind = GlslExpr::Kind::Unary;
            e->op = k;
            e->a = std::move(sub);
            return e;
        }
        return parse_postfix();
    }

    std::shared_ptr<GlslExpr> parse_postfix() {
        auto base = parse_primary();
        while (base) {
            if (match(GlslTokKind::Dot)) {
                if (!at(GlslTokKind::Ident)) { fail("Expected member name after '.'"); return nullptr; }
                auto e = std::make_shared<GlslExpr>();
                e->kind = GlslExpr::Kind::Member;
                e->a = std::move(base);
                e->name = peek().text;
                ++m_pos;
                base = std::move(e);
            } else if (match(GlslTokKind::LBracket)) {
                auto idx = parse_expr();
                if (!idx || !expect(GlslTokKind::RBracket, "Expected ']'")) return nullptr;
                auto e = std::make_shared<GlslExpr>();
                e->kind = GlslExpr::Kind::Index;
                e->a = std::move(base);
                e->b = std::move(idx);
                base = std::move(e);
            } else if (at(GlslTokKind::PlusPlus) || at(GlslTokKind::MinusMinus)) {
                GlslTokKind op = peek().kind;
                ++m_pos;
                auto e = std::make_shared<GlslExpr>();
                e->kind = GlslExpr::Kind::Unary;
                e->op = op;
                e->postfix = true;
                e->a = std::move(base);
                base = std::move(e);
            } else {
                break;
            }
        }
        return base;
    }

    std::shared_ptr<GlslExpr> parse_primary() {
        if (at(GlslTokKind::FloatLit)) {
            auto e = std::make_shared<GlslExpr>();
            e->kind = GlslExpr::Kind::FloatLit;
            e->num = peek().num;
            ++m_pos;
            return e;
        }
        if (at(GlslTokKind::IntLit)) {
            auto e = std::make_shared<GlslExpr>();
            e->kind = GlslExpr::Kind::IntLit;
            e->num = peek().num;
            ++m_pos;
            return e;
        }
        if (at(GlslTokKind::Ident)) {
            std::string name = peek().text;
            ++m_pos;
            if (name == "true" || name == "false") {
                auto e = std::make_shared<GlslExpr>();
                e->kind = GlslExpr::Kind::BoolLit;
                e->bval = (name == "true");
                return e;
            }
            if (match(GlslTokKind::LParen)) {
                auto e = std::make_shared<GlslExpr>();
                e->kind = GlslExpr::Kind::Call;
                e->name = std::move(name);
                if (!at(GlslTokKind::RParen)) {
                    while (true) {
                        auto arg = parse_assignment();
                        if (!arg) return nullptr;
                        e->args.push_back(std::move(arg));
                        if (!match(GlslTokKind::Comma)) break;
                    }
                }
                if (!expect(GlslTokKind::RParen, "Expected ')' after call arguments")) return nullptr;
                return e;
            }
            auto e = std::make_shared<GlslExpr>();
            e->kind = GlslExpr::Kind::Ident;
            e->name = std::move(name);
            return e;
        }
        if (match(GlslTokKind::LParen)) {
            auto inner = parse_expr();
            if (!inner || !expect(GlslTokKind::RParen, "Expected ')'")) return nullptr;
            return inner;
        }
        fail("Unexpected token '" + peek().text + "'");
        return nullptr;
    }

    const std::vector<GlslTok>& m_toks;
    uint32_t m_shader_type{webgl::VERTEX_SHADER};
    std::size_t m_pos{0};
    std::string m_err;
    bool m_has_discard{false};
};

int swizzle_index(char ch) {
    switch (ch) {
        case 'x': case 'r': case 's': return 0;
        case 'y': case 'g': case 't': return 1;
        case 'z': case 'b': case 'p': return 2;
        case 'w': case 'a': case 'q': return 3;
        default: return -1;
    }
}

GlslVal glsl_binary_op(GlslTokKind op, const GlslVal& a, const GlslVal& b) {
    // Matrix multiplication (OpenGL column-major: element (row, col) is f[col * dim + row]).
    if (op == GlslTokKind::Star && (glsl_is_matrix(a.type) || glsl_is_matrix(b.type))) {
        if (glsl_is_matrix(a.type) && glsl_is_matrix(b.type) && a.type == b.type) {
            const int d = glsl_matrix_dim(a.type);
            GlslVal r = GlslVal::zero(a.type);
            for (int col = 0; col < d; ++col) {
                for (int row = 0; row < d; ++row) {
                    float sum = 0.0f;
                    for (int k = 0; k < d; ++k) sum += a.f[k * d + row] * b.f[col * d + k];
                    r.f[col * d + row] = sum;
                }
            }
            return r;
        }
        if (glsl_is_matrix(a.type) && glsl_components(b.type) == glsl_matrix_dim(a.type)) {
            const int d = glsl_matrix_dim(a.type);
            GlslVal r = GlslVal::zero(glsl_vec_type_for_size(d));
            for (int row = 0; row < d; ++row) {
                float sum = 0.0f;
                for (int col = 0; col < d; ++col) sum += a.f[col * d + row] * b.f[col];
                r.f[row] = sum;
            }
            return r;
        }
        if (glsl_is_matrix(b.type) && glsl_components(a.type) == glsl_matrix_dim(b.type)) {
            const int d = glsl_matrix_dim(b.type);
            GlslVal r = GlslVal::zero(glsl_vec_type_for_size(d));
            for (int col = 0; col < d; ++col) {
                float sum = 0.0f;
                for (int row = 0; row < d; ++row) sum += a.f[row] * b.f[col * d + row];
                r.f[col] = sum;
            }
            return r;
        }
    }

    const int na = glsl_components(a.type);
    const int nb = glsl_components(b.type);
    const int n = std::max(na, nb);

    if (op == GlslTokKind::Eq || op == GlslTokKind::Neq) {
        bool eq = true;
        for (int i = 0; i < n; ++i) {
            const float va = a.f[na == 1 ? 0 : i];
            const float vb = b.f[nb == 1 ? 0 : i];
            if (va != vb) { eq = false; break; }
        }
        return GlslVal::make_bool(op == GlslTokKind::Eq ? eq : !eq);
    }
    if (op == GlslTokKind::Lt || op == GlslTokKind::Le ||
        op == GlslTokKind::Gt || op == GlslTokKind::Ge) {
        const float va = a.f[0], vb = b.f[0];
        bool res = false;
        if (op == GlslTokKind::Lt) res = va < vb;
        else if (op == GlslTokKind::Le) res = va <= vb;
        else if (op == GlslTokKind::Gt) res = va > vb;
        else if (op == GlslTokKind::Ge) res = va >= vb;
        return GlslVal::make_bool(res);
    }

    GlslVal r = GlslVal::zero(na >= nb ? a.type : b.type);
    for (int i = 0; i < n; ++i) {
        const float va = a.f[na == 1 ? 0 : i];
        const float vb = b.f[nb == 1 ? 0 : i];
        switch (op) {
            case GlslTokKind::Plus: r.f[i] = va + vb; break;
            case GlslTokKind::Minus: r.f[i] = va - vb; break;
            case GlslTokKind::Star: r.f[i] = va * vb; break;
            case GlslTokKind::Slash: r.f[i] = (vb != 0.0f) ? (va / vb) : 0.0f; break;
            default: break;
        }
    }
    return r;
}

class GlslExec {
public:
    GlslExec(const GlslCompiledShader& shader, const WebGLContext* ctx)
        : m_shader(shader), m_ctx(ctx) {}

    void set_var(const std::string& name, const GlslVal& v) {
        for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) {
                found->second = v;
                return;
            }
        }
        if (m_scopes.empty()) m_scopes.emplace_back();
        m_scopes.front()[name] = v;
    }

    void declare_local(const std::string& name, const GlslVal& v) {
        if (m_scopes.empty()) m_scopes.emplace_back();
        m_scopes.back()[name] = v;
    }

    GlslVal* lookup(const std::string& name) {
        for (auto it = m_scopes.rbegin(); it != m_scopes.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) return &found->second;
        }
        return nullptr;
    }

    void init_globals() {
        if (m_scopes.empty()) m_scopes.emplace_back();
        for (const auto& g : m_shader.globals) {
            GlslVal v = g.init ? eval_expr(*g.init) : GlslVal::zero(g.type);
            v.type = g.type;
            m_scopes.front()[g.name] = v;
        }
    }

    bool run_main() {
        for (const auto& fn : m_shader.functions) {
            if (fn.name == "main" && fn.body) {
                exec_stmt(*fn.body);
                return !m_discarded;
            }
        }
        return true;
    }

    bool discarded() const { return m_discarded; }

private:
    enum class Flow { None, Return, Break, Continue };

    GlslVal convert_to_type(GlslType target, const std::vector<GlslVal>& args) {
        GlslVal r = GlslVal::zero(target);
        if (glsl_is_matrix(target)) {
            const int d = glsl_matrix_dim(target);
            if (args.size() == 1 && glsl_components(args[0].type) == 1) {
                for (int i = 0; i < d; ++i) r.f[i * d + i] = args[0].f[0];
                return r;
            }
            if (args.size() == 1 && glsl_is_matrix(args[0].type)) {
                const int sd = glsl_matrix_dim(args[0].type);
                for (int i = 0; i < d; ++i) r.f[i * d + i] = 1.0f;
                const int md = std::min(d, sd);
                for (int c = 0; c < md; ++c) {
                    for (int row = 0; row < md; ++row) {
                        r.f[c * d + row] = args[0].f[c * sd + row];
                    }
                }
                return r;
            }
            int idx = 0;
            for (const auto& a : args) {
                const int nc = glsl_components(a.type);
                for (int k = 0; k < nc && idx < d * d; ++k) r.f[idx++] = a.f[k];
            }
            return r;
        }

        const int need = glsl_components(target);
        if (args.size() == 1 && glsl_components(args[0].type) == 1) {
            for (int i = 0; i < need; ++i) r.f[i] = args[0].f[0];
            return r;
        }
        int idx = 0;
        for (const auto& a : args) {
            const int nc = glsl_components(a.type);
            for (int k = 0; k < nc && idx < need; ++k) r.f[idx++] = a.f[k];
        }
        return r;
    }

    GlslVal call_builtin(const std::string& name, const std::vector<GlslVal>& args) {
        GlslType ctor_type;
        if (parse_glsl_type_name(name, ctor_type)) {
            return convert_to_type(ctor_type, args);
        }
        auto unary_fn = [&](auto fn) -> GlslVal {
            if (args.empty()) return GlslVal::make_float(0.0f);
            GlslVal r = args[0];
            const int n = glsl_components(r.type);
            for (int i = 0; i < n; ++i) r.f[i] = fn(r.f[i]);
            return r;
        };
        auto binary_fn = [&](auto fn) -> GlslVal {
            if (args.size() < 2) return GlslVal::make_float(0.0f);
            const int na = glsl_components(args[0].type);
            const int nb = glsl_components(args[1].type);
            const int n = std::max(na, nb);
            GlslVal r = GlslVal::zero(na >= nb ? args[0].type : args[1].type);
            for (int i = 0; i < n; ++i) {
                r.f[i] = fn(args[0].f[na == 1 ? 0 : i], args[1].f[nb == 1 ? 0 : i]);
            }
            return r;
        };

        if (name == "abs") return unary_fn([](float x) { return std::fabs(x); });
        if (name == "sign") return unary_fn([](float x) { return x > 0.0f ? 1.0f : (x < 0.0f ? -1.0f : 0.0f); });
        if (name == "floor") return unary_fn([](float x) { return std::floor(x); });
        if (name == "ceil") return unary_fn([](float x) { return std::ceil(x); });
        if (name == "fract") return unary_fn([](float x) { return x - std::floor(x); });
        if (name == "sin") return unary_fn([](float x) { return std::sin(x); });
        if (name == "cos") return unary_fn([](float x) { return std::cos(x); });
        if (name == "tan") return unary_fn([](float x) { return std::tan(x); });
        if (name == "asin") return unary_fn([](float x) { return std::asin(std::clamp(x, -1.0f, 1.0f)); });
        if (name == "acos") return unary_fn([](float x) { return std::acos(std::clamp(x, -1.0f, 1.0f)); });
        if (name == "exp") return unary_fn([](float x) { return std::exp(x); });
        if (name == "log") return unary_fn([](float x) { return x > 0.0f ? std::log(x) : 0.0f; });
        if (name == "exp2") return unary_fn([](float x) { return std::exp2(x); });
        if (name == "log2") return unary_fn([](float x) { return x > 0.0f ? std::log2(x) : 0.0f; });
        if (name == "sqrt") return unary_fn([](float x) { return x > 0.0f ? std::sqrt(x) : 0.0f; });
        if (name == "inversesqrt") return unary_fn([](float x) { return x > 0.0f ? 1.0f / std::sqrt(x) : 0.0f; });
        if (name == "radians") return unary_fn([](float x) { return x * 0.017453292519943295f; });
        if (name == "degrees") return unary_fn([](float x) { return x * 57.29577951308232f; });
        if (name == "atan") {
            if (args.size() >= 2) return binary_fn([](float y, float x) { return std::atan2(y, x); });
            return unary_fn([](float x) { return std::atan(x); });
        }
        if (name == "pow") return binary_fn([](float x, float y) { return std::pow(std::max(0.0f, x), y); });
        if (name == "mod") return binary_fn([](float x, float y) { return y != 0.0f ? x - y * std::floor(x / y) : 0.0f; });
        if (name == "min") return binary_fn([](float x, float y) { return std::min(x, y); });
        if (name == "max") return binary_fn([](float x, float y) { return std::max(x, y); });
        if (name == "step") return binary_fn([](float edge, float x) { return x < edge ? 0.0f : 1.0f; });
        if (name == "clamp" && args.size() >= 3) {
            const int na = glsl_components(args[0].type);
            const int nb = glsl_components(args[1].type);
            const int nc = glsl_components(args[2].type);
            GlslVal r = GlslVal::zero(args[0].type);
            for (int i = 0; i < na; ++i) {
                const float lo = args[1].f[nb == 1 ? 0 : i];
                const float hi = args[2].f[nc == 1 ? 0 : i];
                r.f[i] = std::clamp(args[0].f[i], lo, hi);
            }
            return r;
        }
        if (name == "mix" && args.size() >= 3) {
            const int na = glsl_components(args[0].type);
            const int nb = glsl_components(args[1].type);
            const int nc = glsl_components(args[2].type);
            const int n = std::max(na, nb);
            GlslVal r = GlslVal::zero(na >= nb ? args[0].type : args[1].type);
            for (int i = 0; i < n; ++i) {
                const float x = args[0].f[na == 1 ? 0 : i];
                const float y = args[1].f[nb == 1 ? 0 : i];
                const float a = args[2].f[nc == 1 ? 0 : i];
                r.f[i] = x * (1.0f - a) + y * a;
            }
            return r;
        }
        if (name == "smoothstep" && args.size() >= 3) {
            const int n0 = glsl_components(args[0].type);
            const int n1 = glsl_components(args[1].type);
            const int nx = glsl_components(args[2].type);
            GlslVal r = GlslVal::zero(args[2].type);
            for (int i = 0; i < nx; ++i) {
                const float e0 = args[0].f[n0 == 1 ? 0 : i];
                const float e1 = args[1].f[n1 == 1 ? 0 : i];
                const float x = args[2].f[i];
                const float t = (e1 != e0) ? std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f) : 0.0f;
                r.f[i] = t * t * (3.0f - 2.0f * t);
            }
            return r;
        }
        if (name == "dot" && args.size() >= 2) {
            const int n = std::min(glsl_components(args[0].type), glsl_components(args[1].type));
            float sum = 0.0f;
            for (int i = 0; i < n; ++i) sum += args[0].f[i] * args[1].f[i];
            return GlslVal::make_float(sum);
        }
        if (name == "length" && !args.empty()) {
            const int n = glsl_components(args[0].type);
            float sum = 0.0f;
            for (int i = 0; i < n; ++i) sum += args[0].f[i] * args[0].f[i];
            return GlslVal::make_float(std::sqrt(sum));
        }
        if (name == "distance" && args.size() >= 2) {
            const int n = std::min(glsl_components(args[0].type), glsl_components(args[1].type));
            float sum = 0.0f;
            for (int i = 0; i < n; ++i) {
                const float d = args[0].f[i] - args[1].f[i];
                sum += d * d;
            }
            return GlslVal::make_float(std::sqrt(sum));
        }
        if (name == "normalize" && !args.empty()) {
            const int n = glsl_components(args[0].type);
            float sum = 0.0f;
            for (int i = 0; i < n; ++i) sum += args[0].f[i] * args[0].f[i];
            const float inv = sum > 0.0f ? (1.0f / std::sqrt(sum)) : 0.0f;
            GlslVal r = args[0];
            for (int i = 0; i < n; ++i) r.f[i] *= inv;
            return r;
        }
        if (name == "cross" && args.size() >= 2) {
            const auto& a = args[0].f;
            const auto& b = args[1].f;
            GlslVal r = GlslVal::zero(GlslType::Vec3);
            r.f[0] = a[1] * b[2] - a[2] * b[1];
            r.f[1] = a[2] * b[0] - a[0] * b[2];
            r.f[2] = a[0] * b[1] - a[1] * b[0];
            return r;
        }
        if (name == "reflect" && args.size() >= 2) {
            const int n = std::min(glsl_components(args[0].type), glsl_components(args[1].type));
            float d = 0.0f;
            for (int i = 0; i < n; ++i) d += args[1].f[i] * args[0].f[i];
            GlslVal r = args[0];
            for (int i = 0; i < n; ++i) r.f[i] = args[0].f[i] - 2.0f * d * args[1].f[i];
            return r;
        }
        if ((name == "texture2D" || name == "texture") && args.size() >= 2 && m_ctx) {
            const int unit = static_cast<int>(args[0].f[0]);
            const auto c = m_ctx->sample_texture_2d(unit, args[1].f[0], args[1].f[1]);
            return GlslVal::make_vec4(c[0], c[1], c[2], c[3]);
        }

        // User-defined function
        if (m_call_depth < kMaxGlslCallDepth) {
            for (const auto& fn : m_shader.functions) {
                if (fn.name == name && fn.params.size() == args.size() && fn.body) {
                    ++m_call_depth;
                    m_scopes.emplace_back();
                    for (std::size_t i = 0; i < fn.params.size(); ++i) {
                        GlslVal pv = args[i];
                        pv.type = fn.params[i].type;
                        m_scopes.back()[fn.params[i].name] = pv;
                    }
                    Flow saved_flow = m_flow;
                    GlslVal saved_ret = m_return_val;
                    m_flow = Flow::None;
                    m_return_val = GlslVal::zero(fn.return_type);
                    exec_stmt(*fn.body);
                    GlslVal res = m_return_val;
                    m_flow = saved_flow;
                    m_return_val = saved_ret;
                    m_scopes.pop_back();
                    --m_call_depth;
                    return res;
                }
            }
        }
        return GlslVal::make_float(0.0f);
    }

    bool write_lvalue(const GlslExpr& lhs, const GlslVal& rhs) {
        if (lhs.kind == GlslExpr::Kind::Ident) {
            if (auto* v = lookup(lhs.name)) {
                const int n = glsl_components(v->type);
                const int rn = glsl_components(rhs.type);
                for (int i = 0; i < n; ++i) v->f[i] = rhs.f[rn == 1 ? 0 : i];
                return true;
            }
            set_var(lhs.name, rhs);
            return true;
        }
        if (lhs.kind == GlslExpr::Kind::Member && lhs.a && lhs.a->kind == GlslExpr::Kind::Ident) {
            if (auto* v = lookup(lhs.a->name)) {
                const int rn = glsl_components(rhs.type);
                for (std::size_t i = 0; i < lhs.name.size() && i < 4; ++i) {
                    int idx = swizzle_index(lhs.name[i]);
                    if (idx >= 0 && idx < 4) {
                        v->f[idx] = rhs.f[rn == 1 ? 0 : static_cast<int>(i)];
                    }
                }
                return true;
            }
        }
        if (lhs.kind == GlslExpr::Kind::Index && lhs.a && lhs.a->kind == GlslExpr::Kind::Ident && lhs.b) {
            if (auto* v = lookup(lhs.a->name)) {
                const int idx = static_cast<int>(eval_expr(*lhs.b).f[0]);
                if (glsl_is_matrix(v->type)) {
                    const int d = glsl_matrix_dim(v->type);
                    if (idx >= 0 && idx < d) {
                        for (int r = 0; r < d; ++r) v->f[idx * d + r] = rhs.f[r];
                        return true;
                    }
                } else if (idx >= 0 && idx < glsl_components(v->type)) {
                    v->f[idx] = rhs.f[0];
                    return true;
                }
            }
        }
        return false;
    }

    GlslVal eval_expr(const GlslExpr& e) {
        switch (e.kind) {
            case GlslExpr::Kind::FloatLit: return GlslVal::make_float(static_cast<float>(e.num));
            case GlslExpr::Kind::IntLit: return GlslVal::make_int(static_cast<int>(e.num));
            case GlslExpr::Kind::BoolLit: return GlslVal::make_bool(e.bval);
            case GlslExpr::Kind::Ident: {
                if (auto* v = lookup(e.name)) return *v;
                return GlslVal::make_float(0.0f);
            }
            case GlslExpr::Kind::Unary: {
                GlslVal v = eval_expr(*e.a);
                if (e.op == GlslTokKind::Plus) return v;
                if (e.op == GlslTokKind::Minus) {
                    const int n = glsl_components(v.type);
                    for (int i = 0; i < n; ++i) v.f[i] = -v.f[i];
                    return v;
                }
                if (e.op == GlslTokKind::Bang) {
                    return GlslVal::make_bool(!v.truthy());
                }
                if (e.op == GlslTokKind::PlusPlus || e.op == GlslTokKind::MinusMinus) {
                    GlslVal next = v;
                    const float delta = (e.op == GlslTokKind::PlusPlus) ? 1.0f : -1.0f;
                    const int n = glsl_components(next.type);
                    for (int i = 0; i < n; ++i) next.f[i] += delta;
                    write_lvalue(*e.a, next);
                    return e.postfix ? v : next;
                }
                return v;
            }
            case GlslExpr::Kind::Binary: {
                if (e.op == GlslTokKind::AndAnd) {
                    GlslVal a = eval_expr(*e.a);
                    return GlslVal::make_bool(a.truthy() && eval_expr(*e.b).truthy());
                }
                if (e.op == GlslTokKind::OrOr) {
                    GlslVal a = eval_expr(*e.a);
                    return GlslVal::make_bool(a.truthy() || eval_expr(*e.b).truthy());
                }
                return glsl_binary_op(e.op, eval_expr(*e.a), eval_expr(*e.b));
            }
            case GlslExpr::Kind::Assign: {
                GlslVal rhs = eval_expr(*e.b);
                if (e.op != GlslTokKind::Assign) {
                    GlslVal cur = eval_expr(*e.a);
                    GlslTokKind bop = GlslTokKind::Plus;
                    if (e.op == GlslTokKind::MinusAssign) bop = GlslTokKind::Minus;
                    else if (e.op == GlslTokKind::StarAssign) bop = GlslTokKind::Star;
                    else if (e.op == GlslTokKind::SlashAssign) bop = GlslTokKind::Slash;
                    rhs = glsl_binary_op(bop, cur, rhs);
                }
                write_lvalue(*e.a, rhs);
                return rhs;
            }
            case GlslExpr::Kind::Ternary: {
                return eval_expr(*e.a).truthy() ? eval_expr(*e.b) : eval_expr(*e.c);
            }
            case GlslExpr::Kind::Member: {
                GlslVal base = eval_expr(*e.a);
                const int len = static_cast<int>(std::min<std::size_t>(e.name.size(), 4));
                GlslVal r = GlslVal::zero(glsl_vec_type_for_size(len, base.type));
                for (int i = 0; i < len; ++i) {
                    int idx = swizzle_index(e.name[i]);
                    r.f[i] = (idx >= 0 && idx < 4) ? base.f[idx] : 0.0f;
                }
                return r;
            }
            case GlslExpr::Kind::Index: {
                GlslVal base = eval_expr(*e.a);
                const int idx = static_cast<int>(eval_expr(*e.b).f[0]);
                if (glsl_is_matrix(base.type)) {
                    const int d = glsl_matrix_dim(base.type);
                    GlslVal r = GlslVal::zero(glsl_vec_type_for_size(d));
                    if (idx >= 0 && idx < d) {
                        for (int row = 0; row < d; ++row) r.f[row] = base.f[idx * d + row];
                    }
                    return r;
                }
                if (idx >= 0 && idx < glsl_components(base.type)) {
                    return GlslVal::make_float(base.f[idx]);
                }
                return GlslVal::make_float(0.0f);
            }
            case GlslExpr::Kind::Call: {
                std::vector<GlslVal> arg_vals;
                arg_vals.reserve(e.args.size());
                for (const auto& a : e.args) arg_vals.push_back(eval_expr(*a));
                return call_builtin(e.name, arg_vals);
            }
        }
        return GlslVal::make_float(0.0f);
    }

    void exec_stmt(const GlslStmt& s) {
        if (m_flow != Flow::None || m_discarded) return;
        switch (s.kind) {
            case GlslStmt::Kind::Block: {
                m_scopes.emplace_back();
                for (const auto& sub : s.stmts) {
                    exec_stmt(*sub);
                    if (m_flow != Flow::None || m_discarded) break;
                }
                m_scopes.pop_back();
                break;
            }
            case GlslStmt::Kind::VarDecl: {
                for (const auto& d : s.decls) {
                    GlslVal v = d.init ? eval_expr(*d.init) : GlslVal::zero(s.decl_type);
                    if (v.type != s.decl_type) v = convert_to_type(s.decl_type, {v});
                    declare_local(d.name, v);
                }
                break;
            }
            case GlslStmt::Kind::Expr: {
                if (s.expr) eval_expr(*s.expr);
                break;
            }
            case GlslStmt::Kind::If: {
                if (s.cond && eval_expr(*s.cond).truthy()) {
                    if (s.then_branch) exec_stmt(*s.then_branch);
                } else if (s.else_branch) {
                    exec_stmt(*s.else_branch);
                }
                break;
            }
            case GlslStmt::Kind::While: {
                for (int iter = 0; iter < kMaxGlslLoopIterations; ++iter) {
                    if (!s.cond || !eval_expr(*s.cond).truthy()) break;
                    if (s.then_branch) exec_stmt(*s.then_branch);
                    if (m_discarded || m_flow == Flow::Return) break;
                    if (m_flow == Flow::Break) { m_flow = Flow::None; break; }
                    if (m_flow == Flow::Continue) m_flow = Flow::None;
                }
                break;
            }
            case GlslStmt::Kind::For: {
                m_scopes.emplace_back();
                if (s.init_stmt) exec_stmt(*s.init_stmt);
                for (int iter = 0; iter < kMaxGlslLoopIterations; ++iter) {
                    if (s.cond && !eval_expr(*s.cond).truthy()) break;
                    if (s.then_branch) exec_stmt(*s.then_branch);
                    if (m_discarded || m_flow == Flow::Return) break;
                    if (m_flow == Flow::Break) { m_flow = Flow::None; break; }
                    if (m_flow == Flow::Continue) m_flow = Flow::None;
                    if (s.step) eval_expr(*s.step);
                }
                m_scopes.pop_back();
                break;
            }
            case GlslStmt::Kind::Return: {
                if (s.expr) m_return_val = eval_expr(*s.expr);
                m_flow = Flow::Return;
                break;
            }
            case GlslStmt::Kind::Break: m_flow = Flow::Break; break;
            case GlslStmt::Kind::Continue: m_flow = Flow::Continue; break;
            case GlslStmt::Kind::Discard: m_discarded = true; break;
        }
    }

    const GlslCompiledShader& m_shader;
    const WebGLContext* m_ctx{nullptr};
    std::vector<std::unordered_map<std::string, GlslVal>> m_scopes;
    Flow m_flow{Flow::None};
    GlslVal m_return_val{};
    bool m_discarded{false};
    int m_call_depth{0};
};

} // namespace

// ============================================================================
// WebGLContext Implementation
// ============================================================================

WebGLContext::WebGLContext(std::shared_ptr<CanvasRenderingContext2D> backing, bool is_webgl2)
    : m_backing(std::move(backing)), m_is_webgl2(is_webgl2) {
    sync_backing_size();
    m_vp_x = 0;
    m_vp_y = 0;
    m_vp_w = drawing_buffer_width();
    m_vp_h = drawing_buffer_height();
    m_sc_x = 0;
    m_sc_y = 0;
    m_sc_w = m_vp_w;
    m_sc_h = m_vp_h;
}

int WebGLContext::drawing_buffer_width() const {
    if (!m_backing || !m_backing->has_backing_store() || !m_backing->image_data()) return 0;
    return m_backing->image_data()->width;
}

int WebGLContext::drawing_buffer_height() const {
    if (!m_backing || !m_backing->has_backing_store() || !m_backing->image_data()) return 0;
    return m_backing->image_data()->height;
}

void WebGLContext::sync_backing_size() {
    const int w = drawing_buffer_width();
    const int h = drawing_buffer_height();
    if (w != m_depth_w || h != m_depth_h) {
        m_depth_w = w;
        m_depth_h = h;
        const std::size_t n = (w > 0 && h > 0) ? static_cast<std::size_t>(w) * static_cast<std::size_t>(h) : 0;
        m_default_depth.assign(n, 1.0f);
        m_default_stencil.assign(n, 0);
    }
}

uint32_t WebGLContext::get_error() {
    const uint32_t e = m_error;
    m_error = webgl::NO_ERROR;
    return e;
}

void WebGLContext::set_error(uint32_t err) {
    if (m_error == webgl::NO_ERROR && err != webgl::NO_ERROR) {
        m_error = err;
    }
}

WebGLVertexArrayObj& WebGLContext::current_vao() {
    if (m_bound_vao != 0) {
        auto it = m_vaos.find(m_bound_vao);
        if (it != m_vaos.end() && !it->second.deleted) return it->second;
    }
    return m_default_vao;
}

const WebGLVertexArrayObj& WebGLContext::current_vao() const {
    if (m_bound_vao != 0) {
        auto it = m_vaos.find(m_bound_vao);
        if (it != m_vaos.end() && !it->second.deleted) return it->second;
    }
    return m_default_vao;
}

uint32_t WebGLContext::bound_element_buffer_id() const {
    return current_vao().element_array_buffer;
}

bool WebGLContext::resolve_render_target(RenderTarget& out) {
    sync_backing_size();
    out = RenderTarget{};
    if (m_bound_framebuffer == 0) {
        if (!m_backing || !m_backing->has_backing_store() || !m_backing->image_data()) return false;
        auto img = m_backing->image_data();
        if (img->width <= 0 || img->height <= 0 || img->pixels.empty()) return false;
        out.argb_color = img->pixels.data();
        out.depth = m_default_depth.empty() ? nullptr : m_default_depth.data();
        out.width = img->width;
        out.height = img->height;
        out.flip_y = true;
        return true;
    }
    auto fb_it = m_framebuffers.find(m_bound_framebuffer);
    if (fb_it == m_framebuffers.end() || fb_it->second.deleted) return false;
    const auto& fb = fb_it->second;
    if (fb.color_tex_id != 0) {
        auto tex_it = m_textures.find(fb.color_tex_id);
        if (tex_it == m_textures.end() || tex_it->second.width <= 0 || tex_it->second.height <= 0) return false;
        out.rgba_color = tex_it->second.pixels.data();
        out.width = tex_it->second.width;
        out.height = tex_it->second.height;
        out.flip_y = false;
    } else if (fb.color_rb_id != 0) {
        auto rb_it = m_renderbuffers.find(fb.color_rb_id);
        if (rb_it == m_renderbuffers.end() || rb_it->second.width <= 0 || rb_it->second.height <= 0) return false;
        out.rgba_color = rb_it->second.color.data();
        out.width = rb_it->second.width;
        out.height = rb_it->second.height;
        out.flip_y = false;
    } else {
        return false;
    }
    out.depth = nullptr;
    if (fb.depth_rb_id != 0) {
        auto drb_it = m_renderbuffers.find(fb.depth_rb_id);
        if (drb_it != m_renderbuffers.end() &&
            drb_it->second.width == out.width && drb_it->second.height == out.height &&
            !drb_it->second.depth.empty()) {
            out.depth = drb_it->second.depth.data();
        }
    }
    return true;
}

void WebGLContext::clear_color(float r, float g, float b, float a) {
    m_clear_color = {
        std::clamp(std::isfinite(r) ? r : 0.0f, 0.0f, 1.0f),
        std::clamp(std::isfinite(g) ? g : 0.0f, 0.0f, 1.0f),
        std::clamp(std::isfinite(b) ? b : 0.0f, 0.0f, 1.0f),
        std::clamp(std::isfinite(a) ? a : 0.0f, 0.0f, 1.0f)
    };
}

void WebGLContext::clear_depth(float d) {
    m_clear_depth = std::clamp(std::isfinite(d) ? d : 1.0f, 0.0f, 1.0f);
}

void WebGLContext::clear_stencil(int s) {
    m_clear_stencil = s;
}

void WebGLContext::clear(uint32_t mask) {
    if ((mask & ~(webgl::COLOR_BUFFER_BIT | webgl::DEPTH_BUFFER_BIT | webgl::STENCIL_BUFFER_BIT)) != 0) {
        set_error(webgl::INVALID_VALUE);
        return;
    }
    RenderTarget rt;
    if (!resolve_render_target(rt)) {
        if (m_bound_framebuffer != 0) set_error(webgl::INVALID_FRAMEBUFFER_OPERATION);
        return;
    }
    int x0 = 0, y0 = 0, x1 = rt.width, y1 = rt.height;
    if (m_scissor_test) {
        x0 = std::max(0, m_sc_x);
        y0 = std::max(0, m_sc_y);
        x1 = std::min(rt.width, m_sc_x + std::max(0, m_sc_w));
        y1 = std::min(rt.height, m_sc_y + std::max(0, m_sc_h));
    }
    if (x0 >= x1 || y0 >= y1) return;

    if (mask & webgl::COLOR_BUFFER_BIT) {
        const uint8_t cr = float_to_u8(m_clear_color[0]);
        const uint8_t cg = float_to_u8(m_clear_color[1]);
        const uint8_t cb = float_to_u8(m_clear_color[2]);
        const uint8_t ca = float_to_u8(m_clear_color[3]);
        if (rt.argb_color) {
            for (int y = y0; y < y1; ++y) {
                const int row = rt.flip_y ? (rt.height - 1 - y) : y;
                uint32_t* p = rt.argb_color + static_cast<std::size_t>(row) * rt.width + x0;
                for (int x = x0; x < x1; ++x, ++p) {
                    const uint32_t cur = *p;
                    const uint8_t r = m_color_mask[0] ? cr : static_cast<uint8_t>((cur >> 16) & 0xFF);
                    const uint8_t g = m_color_mask[1] ? cg : static_cast<uint8_t>((cur >> 8) & 0xFF);
                    const uint8_t b = m_color_mask[2] ? cb : static_cast<uint8_t>(cur & 0xFF);
                    const uint8_t a = m_color_mask[3] ? ca : static_cast<uint8_t>((cur >> 24) & 0xFF);
                    *p = (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(r) << 16) |
                         (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
                }
            }
        } else if (rt.rgba_color) {
            for (int y = y0; y < y1; ++y) {
                const int row = rt.flip_y ? (rt.height - 1 - y) : y;
                uint8_t* p = rt.rgba_color + (static_cast<std::size_t>(row) * rt.width + x0) * 4;
                for (int x = x0; x < x1; ++x, p += 4) {
                    if (m_color_mask[0]) p[0] = cr;
                    if (m_color_mask[1]) p[1] = cg;
                    if (m_color_mask[2]) p[2] = cb;
                    if (m_color_mask[3]) p[3] = ca;
                }
            }
        }
    }
    if ((mask & webgl::DEPTH_BUFFER_BIT) && rt.depth && m_depth_mask) {
        for (int y = y0; y < y1; ++y) {
            float* dp = rt.depth + static_cast<std::size_t>(y) * rt.width + x0;
            std::fill(dp, dp + (x1 - x0), m_clear_depth);
        }
    }
    if ((mask & webgl::STENCIL_BUFFER_BIT) && m_bound_framebuffer == 0 && !m_default_stencil.empty()) {
        const uint8_t sv = static_cast<uint8_t>(m_clear_stencil & 0xFF);
        for (int y = y0; y < y1; ++y) {
            uint8_t* sp = m_default_stencil.data() + static_cast<std::size_t>(y) * rt.width + x0;
            std::fill(sp, sp + (x1 - x0), sv);
        }
    }
}

void WebGLContext::viewport(int x, int y, int w, int h) {
    if (w < 0 || h < 0) { set_error(webgl::INVALID_VALUE); return; }
    m_vp_x = x; m_vp_y = y; m_vp_w = w; m_vp_h = h;
}

void WebGLContext::scissor(int x, int y, int w, int h) {
    if (w < 0 || h < 0) { set_error(webgl::INVALID_VALUE); return; }
    m_sc_x = x; m_sc_y = y; m_sc_w = w; m_sc_h = h;
}

void WebGLContext::enable(uint32_t cap) {
    switch (cap) {
        case webgl::DEPTH_TEST: m_depth_test = true; break;
        case webgl::SCISSOR_TEST: m_scissor_test = true; break;
        case webgl::BLEND: m_blend = true; break;
        case webgl::CULL_FACE: m_cull_face_enabled = true; break;
        case webgl::DITHER: m_dither = true; break;
        case webgl::STENCIL_TEST: m_stencil_test = true; break;
        case webgl::POLYGON_OFFSET_FILL: m_polygon_offset_fill = true; break;
        default: set_error(webgl::INVALID_ENUM); break;
    }
}

void WebGLContext::disable(uint32_t cap) {
    switch (cap) {
        case webgl::DEPTH_TEST: m_depth_test = false; break;
        case webgl::SCISSOR_TEST: m_scissor_test = false; break;
        case webgl::BLEND: m_blend = false; break;
        case webgl::CULL_FACE: m_cull_face_enabled = false; break;
        case webgl::DITHER: m_dither = false; break;
        case webgl::STENCIL_TEST: m_stencil_test = false; break;
        case webgl::POLYGON_OFFSET_FILL: m_polygon_offset_fill = false; break;
        default: set_error(webgl::INVALID_ENUM); break;
    }
}

bool WebGLContext::is_enabled(uint32_t cap) const {
    switch (cap) {
        case webgl::DEPTH_TEST: return m_depth_test;
        case webgl::SCISSOR_TEST: return m_scissor_test;
        case webgl::BLEND: return m_blend;
        case webgl::CULL_FACE: return m_cull_face_enabled;
        case webgl::DITHER: return m_dither;
        case webgl::STENCIL_TEST: return m_stencil_test;
        case webgl::POLYGON_OFFSET_FILL: return m_polygon_offset_fill;
        default: return false;
    }
}

void WebGLContext::color_mask(bool r, bool g, bool b, bool a) { m_color_mask = {r, g, b, a}; }
void WebGLContext::depth_mask(bool flag) { m_depth_mask = flag; }

void WebGLContext::depth_func(uint32_t func) {
    if (func < webgl::NEVER || func > webgl::ALWAYS) { set_error(webgl::INVALID_ENUM); return; }
    m_depth_func = func;
}

void WebGLContext::depth_range(float z_near, float z_far) {
    if (z_near > z_far) { set_error(webgl::INVALID_OPERATION); return; }
    m_depth_near = std::clamp(z_near, 0.0f, 1.0f);
    m_depth_far = std::clamp(z_far, 0.0f, 1.0f);
}

void WebGLContext::cull_face(uint32_t mode) {
    if (mode != webgl::FRONT && mode != webgl::BACK && mode != webgl::FRONT_AND_BACK) {
        set_error(webgl::INVALID_ENUM);
        return;
    }
    m_cull_face = mode;
}

void WebGLContext::front_face(uint32_t mode) {
    if (mode != webgl::CW && mode != webgl::CCW) { set_error(webgl::INVALID_ENUM); return; }
    m_front_face = mode;
}

void WebGLContext::line_width(float w) {
    if (!(w > 0.0f) || !std::isfinite(w)) { set_error(webgl::INVALID_VALUE); return; }
    m_line_width = w;
}

void WebGLContext::blend_func(uint32_t sfactor, uint32_t dfactor) {
    blend_func_separate(sfactor, dfactor, sfactor, dfactor);
}

void WebGLContext::blend_func_separate(uint32_t src_rgb, uint32_t dst_rgb,
                                       uint32_t src_a, uint32_t dst_a) {
    m_blend_src_rgb = src_rgb;
    m_blend_dst_rgb = dst_rgb;
    m_blend_src_alpha = src_a;
    m_blend_dst_alpha = dst_a;
}

void WebGLContext::blend_equation(uint32_t mode) {
    blend_equation_separate(mode, mode);
}

void WebGLContext::blend_equation_separate(uint32_t mode_rgb, uint32_t mode_a) {
    m_blend_eq_rgb = mode_rgb;
    m_blend_eq_alpha = mode_a;
}

void WebGLContext::blend_color(float r, float g, float b, float a) {
    m_blend_color = {
        std::clamp(r, 0.0f, 1.0f),
        std::clamp(g, 0.0f, 1.0f),
        std::clamp(b, 0.0f, 1.0f),
        std::clamp(a, 0.0f, 1.0f)
    };
}

void WebGLContext::pixel_storei(uint32_t pname, int param) {
    switch (pname) {
        case webgl::UNPACK_ALIGNMENT:
            if (param != 1 && param != 2 && param != 4 && param != 8) {
                set_error(webgl::INVALID_VALUE);
                return;
            }
            m_unpack_alignment = param;
            break;
        case webgl::PACK_ALIGNMENT:
            if (param != 1 && param != 2 && param != 4 && param != 8) {
                set_error(webgl::INVALID_VALUE);
                return;
            }
            break;
        case webgl::UNPACK_FLIP_Y_WEBGL: m_unpack_flip_y = (param != 0); break;
        case webgl::UNPACK_PREMULTIPLY_ALPHA_WEBGL: m_unpack_premultiply_alpha = (param != 0); break;
        default: break;
    }
}

// --- Shaders & Programs -----------------------------------------------------

uint32_t WebGLContext::create_shader(uint32_t type) {
    if (type != webgl::VERTEX_SHADER && type != webgl::FRAGMENT_SHADER) {
        set_error(webgl::INVALID_ENUM);
        return 0;
    }
    const uint32_t id = m_next_id++;
    m_shaders[id] = WebGLShaderObj{id, type, "", false, false, "", nullptr};
    return id;
}

void WebGLContext::delete_shader(uint32_t id) {
    auto it = m_shaders.find(id);
    if (it != m_shaders.end()) it->second.deleted = true;
}

bool WebGLContext::is_shader(uint32_t id) const {
    auto it = m_shaders.find(id);
    return it != m_shaders.end() && !it->second.deleted;
}

void WebGLContext::shader_source(uint32_t id, const std::string& src) {
    auto it = m_shaders.find(id);
    if (it == m_shaders.end()) { set_error(webgl::INVALID_VALUE); return; }
    it->second.source = src;
}

std::string WebGLContext::get_shader_source(uint32_t id) const {
    auto it = m_shaders.find(id);
    return it != m_shaders.end() ? it->second.source : std::string();
}

void WebGLContext::compile_shader(uint32_t id) {
    auto it = m_shaders.find(id);
    if (it == m_shaders.end()) { set_error(webgl::INVALID_VALUE); return; }
    auto& sh = it->second;
    sh.compiled = false;
    sh.info_log.clear();
    sh.ast.reset();

    const std::string pre = preprocess_glsl(sh.source);
    std::vector<GlslTok> toks;
    std::string err;
    if (!lex_glsl(pre, toks, err)) {
        sh.info_log = err;
        return;
    }
    auto ast = std::make_shared<GlslCompiledShader>();
    GlslParser parser(toks, sh.type);
    if (!parser.parse(*ast, err)) {
        sh.info_log = err;
        return;
    }
    sh.ast = std::move(ast);
    sh.compiled = true;
}

bool WebGLContext::get_shader_parameter(uint32_t id, uint32_t pname, int& out_int, bool& is_bool) {
    auto it = m_shaders.find(id);
    if (it == m_shaders.end()) { set_error(webgl::INVALID_VALUE); return false; }
    const auto& sh = it->second;
    is_bool = false;
    switch (pname) {
        case webgl::SHADER_TYPE: out_int = static_cast<int>(sh.type); return true;
        case webgl::DELETE_STATUS: is_bool = true; out_int = sh.deleted ? 1 : 0; return true;
        case webgl::COMPILE_STATUS: is_bool = true; out_int = sh.compiled ? 1 : 0; return true;
        default: set_error(webgl::INVALID_ENUM); return false;
    }
}

std::string WebGLContext::get_shader_info_log(uint32_t id) const {
    auto it = m_shaders.find(id);
    return it != m_shaders.end() ? it->second.info_log : std::string();
}

uint32_t WebGLContext::create_program() {
    const uint32_t id = m_next_id++;
    WebGLProgramObj prog;
    prog.id = id;
    m_programs[id] = std::move(prog);
    return id;
}

void WebGLContext::delete_program(uint32_t id) {
    auto it = m_programs.find(id);
    if (it != m_programs.end()) it->second.deleted = true;
}

bool WebGLContext::is_program(uint32_t id) const {
    auto it = m_programs.find(id);
    return it != m_programs.end() && !it->second.deleted;
}

void WebGLContext::attach_shader(uint32_t prog_id, uint32_t shader_id) {
    auto pit = m_programs.find(prog_id);
    auto sit = m_shaders.find(shader_id);
    if (pit == m_programs.end() || sit == m_shaders.end()) {
        set_error(webgl::INVALID_VALUE);
        return;
    }
    if (sit->second.type == webgl::VERTEX_SHADER) {
        if (pit->second.vertex_shader_id != 0) { set_error(webgl::INVALID_OPERATION); return; }
        pit->second.vertex_shader_id = shader_id;
    } else {
        if (pit->second.fragment_shader_id != 0) { set_error(webgl::INVALID_OPERATION); return; }
        pit->second.fragment_shader_id = shader_id;
    }
}

void WebGLContext::detach_shader(uint32_t prog_id, uint32_t shader_id) {
    auto pit = m_programs.find(prog_id);
    if (pit == m_programs.end()) { set_error(webgl::INVALID_VALUE); return; }
    if (pit->second.vertex_shader_id == shader_id) pit->second.vertex_shader_id = 0;
    else if (pit->second.fragment_shader_id == shader_id) pit->second.fragment_shader_id = 0;
    else set_error(webgl::INVALID_OPERATION);
}

void WebGLContext::bind_attrib_location(uint32_t prog_id, uint32_t index, const std::string& name) {
    auto pit = m_programs.find(prog_id);
    if (pit == m_programs.end() || index >= static_cast<uint32_t>(kMaxAttribs)) {
        set_error(webgl::INVALID_VALUE);
        return;
    }
    if (name.rfind("gl_", 0) == 0) { set_error(webgl::INVALID_OPERATION); return; }
    pit->second.bound_attrib_locations[name] = static_cast<int>(index);
}

void WebGLContext::link_program(uint32_t prog_id) {
    auto pit = m_programs.find(prog_id);
    if (pit == m_programs.end()) { set_error(webgl::INVALID_VALUE); return; }
    auto& prog = pit->second;
    prog.linked = false;
    prog.info_log.clear();
    prog.active_attribs.clear();
    prog.active_uniforms.clear();
    prog.uniform_locations.clear();
    prog.uniform_values.clear();
    prog.vs.reset();
    prog.fs.reset();

    auto vs_it = m_shaders.find(prog.vertex_shader_id);
    auto fs_it = m_shaders.find(prog.fragment_shader_id);
    if (vs_it == m_shaders.end() || fs_it == m_shaders.end()) {
        prog.info_log = "Program must have both vertex and fragment shaders attached";
        return;
    }
    if (!vs_it->second.compiled || !vs_it->second.ast) {
        prog.info_log = "Vertex shader is not compiled";
        return;
    }
    if (!fs_it->second.compiled || !fs_it->second.ast) {
        prog.info_log = "Fragment shader is not compiled";
        return;
    }
    prog.vs = vs_it->second.ast;
    prog.fs = fs_it->second.ast;

    // Assign attribute locations respecting bindAttribLocation and explicit layout(location = N).
    std::array<bool, kMaxAttribs> used{};
    for (const auto& attr : prog.vs->attributes) {
        int loc = attr.explicit_location;
        auto bit = prog.bound_attrib_locations.find(attr.name);
        if (bit != prog.bound_attrib_locations.end()) loc = bit->second;
        if (loc >= 0 && loc < kMaxAttribs) used[loc] = true;
    }
    for (const auto& attr : prog.vs->attributes) {
        int loc = attr.explicit_location;
        auto bit = prog.bound_attrib_locations.find(attr.name);
        if (bit != prog.bound_attrib_locations.end()) loc = bit->second;
        if (loc < 0 || loc >= kMaxAttribs) {
            for (int i = 0; i < kMaxAttribs; ++i) {
                if (!used[i]) { loc = i; used[i] = true; break; }
            }
        }
        prog.active_attribs.push_back({attr.name, glsl_to_gl_type(attr.type), 1, loc});
    }

    // Collect active uniforms from both stages.
    int next_u = 1;
    auto register_uniforms = [&](const std::vector<GlslDeclVar>& list) {
        for (const auto& u : list) {
            if (prog.uniform_locations.count(u.name)) continue;
            const int loc = next_u++;
            const uint32_t gt = glsl_to_gl_type(u.type);
            prog.uniform_locations[u.name] = loc;
            prog.active_uniforms.push_back({u.name, gt, 1, loc});
            WebGLUniformValue uv;
            uv.gl_type = gt;
            if (u.type == GlslType::Mat2) { uv.data[0] = 1; uv.data[3] = 1; }
            else if (u.type == GlslType::Mat3) { uv.data[0] = 1; uv.data[4] = 1; uv.data[8] = 1; }
            else if (u.type == GlslType::Mat4) { uv.data[0] = 1; uv.data[5] = 1; uv.data[10] = 1; uv.data[15] = 1; }
            prog.uniform_values[loc] = uv;
        }
    };
    register_uniforms(prog.vs->uniforms);
    register_uniforms(prog.fs->uniforms);
    prog.linked = true;
}

void WebGLContext::validate_program(uint32_t prog_id) {
    auto pit = m_programs.find(prog_id);
    if (pit == m_programs.end()) { set_error(webgl::INVALID_VALUE); return; }
    pit->second.validated = pit->second.linked;
}

void WebGLContext::use_program(uint32_t prog_id) {
    if (prog_id == 0) { m_current_program = 0; return; }
    auto pit = m_programs.find(prog_id);
    if (pit == m_programs.end() || !pit->second.linked) {
        set_error(webgl::INVALID_OPERATION);
        return;
    }
    m_current_program = prog_id;
}

bool WebGLContext::get_program_parameter(uint32_t prog_id, uint32_t pname, int& out_int, bool& is_bool) {
    auto pit = m_programs.find(prog_id);
    if (pit == m_programs.end()) { set_error(webgl::INVALID_VALUE); return false; }
    const auto& p = pit->second;
    is_bool = false;
    switch (pname) {
        case webgl::DELETE_STATUS: is_bool = true; out_int = p.deleted ? 1 : 0; return true;
        case webgl::LINK_STATUS: is_bool = true; out_int = p.linked ? 1 : 0; return true;
        case webgl::VALIDATE_STATUS: is_bool = true; out_int = p.validated ? 1 : 0; return true;
        case webgl::ATTACHED_SHADERS:
            out_int = (p.vertex_shader_id ? 1 : 0) + (p.fragment_shader_id ? 1 : 0);
            return true;
        case webgl::ACTIVE_ATTRIBUTES: out_int = static_cast<int>(p.active_attribs.size()); return true;
        case webgl::ACTIVE_UNIFORMS: out_int = static_cast<int>(p.active_uniforms.size()); return true;
        default: set_error(webgl::INVALID_ENUM); return false;
    }
}

std::string WebGLContext::get_program_info_log(uint32_t prog_id) const {
    auto pit = m_programs.find(prog_id);
    return pit != m_programs.end() ? pit->second.info_log : std::string();
}

int WebGLContext::get_attrib_location(uint32_t prog_id, const std::string& name) {
    auto pit = m_programs.find(prog_id);
    if (pit == m_programs.end() || !pit->second.linked) {
        set_error(webgl::INVALID_OPERATION);
        return -1;
    }
    for (const auto& a : pit->second.active_attribs) {
        if (a.name == name) return a.location;
    }
    return -1;
}

int WebGLContext::get_uniform_location(uint32_t prog_id, const std::string& name) {
    auto pit = m_programs.find(prog_id);
    if (pit == m_programs.end() || !pit->second.linked) {
        set_error(webgl::INVALID_OPERATION);
        return -1;
    }
    auto it = pit->second.uniform_locations.find(name);
    return it != pit->second.uniform_locations.end() ? it->second : -1;
}

bool WebGLContext::get_active_attrib(uint32_t prog_id, uint32_t index, WebGLActiveVar& out) {
    auto pit = m_programs.find(prog_id);
    if (pit == m_programs.end() || index >= pit->second.active_attribs.size()) {
        set_error(webgl::INVALID_VALUE);
        return false;
    }
    out = pit->second.active_attribs[index];
    return true;
}

bool WebGLContext::get_active_uniform(uint32_t prog_id, uint32_t index, WebGLActiveVar& out) {
    auto pit = m_programs.find(prog_id);
    if (pit == m_programs.end() || index >= pit->second.active_uniforms.size()) {
        set_error(webgl::INVALID_VALUE);
        return false;
    }
    out = pit->second.active_uniforms[index];
    return true;
}

void WebGLContext::uniform_floats(int loc, uint32_t expected_type, const float* vals, int count, int components) {
    if (loc <= 0) return;
    auto pit = m_programs.find(m_current_program);
    if (pit == m_programs.end()) { set_error(webgl::INVALID_OPERATION); return; }
    auto uit = pit->second.uniform_values.find(loc);
    if (uit == pit->second.uniform_values.end()) { set_error(webgl::INVALID_OPERATION); return; }
    (void)expected_type;
    const int n = std::min(components * std::max(1, count), 16);
    for (int i = 0; i < n; ++i) uit->second.data[i] = vals[i];
}

void WebGLContext::uniform_ints(int loc, uint32_t expected_type, const int* vals, int count, int components) {
    if (loc <= 0) return;
    auto pit = m_programs.find(m_current_program);
    if (pit == m_programs.end()) { set_error(webgl::INVALID_OPERATION); return; }
    auto uit = pit->second.uniform_values.find(loc);
    if (uit == pit->second.uniform_values.end()) { set_error(webgl::INVALID_OPERATION); return; }
    (void)expected_type;
    const int n = std::min(components * std::max(1, count), 16);
    for (int i = 0; i < n; ++i) uit->second.data[i] = static_cast<float>(vals[i]);
}

void WebGLContext::uniform_matrix(int loc, int dim, bool transpose, const float* vals, int count) {
    if (loc <= 0 || count < 1) return;
    auto pit = m_programs.find(m_current_program);
    if (pit == m_programs.end()) { set_error(webgl::INVALID_OPERATION); return; }
    auto uit = pit->second.uniform_values.find(loc);
    if (uit == pit->second.uniform_values.end()) { set_error(webgl::INVALID_OPERATION); return; }
    if (transpose && !m_is_webgl2) { set_error(webgl::INVALID_VALUE); return; }
    for (int col = 0; col < dim; ++col) {
        for (int row = 0; row < dim; ++row) {
            const float v = transpose ? vals[row * dim + col] : vals[col * dim + row];
            uit->second.data[col * dim + row] = v;
        }
    }
}

// --- Buffers & VAOs ---------------------------------------------------------

uint32_t WebGLContext::create_buffer() {
    const uint32_t id = m_next_id++;
    WebGLBufferObj buf;
    buf.id = id;
    m_buffers[id] = std::move(buf);
    return id;
}

void WebGLContext::delete_buffer(uint32_t id) {
    auto it = m_buffers.find(id);
    if (it != m_buffers.end()) {
        it->second.deleted = true;
        it->second.data.clear();
    }
    if (m_array_buffer == id) m_array_buffer = 0;
    if (current_vao().element_array_buffer == id) current_vao().element_array_buffer = 0;
}

bool WebGLContext::is_buffer(uint32_t id) const {
    auto it = m_buffers.find(id);
    return it != m_buffers.end() && !it->second.deleted;
}

void WebGLContext::bind_buffer(uint32_t target, uint32_t id) {
    if (id != 0 && !is_buffer(id)) { set_error(webgl::INVALID_OPERATION); return; }
    if (target == webgl::ARRAY_BUFFER) m_array_buffer = id;
    else if (target == webgl::ELEMENT_ARRAY_BUFFER) current_vao().element_array_buffer = id;
    else set_error(webgl::INVALID_ENUM);
}

void WebGLContext::buffer_data_size(uint32_t target, std::size_t size, uint32_t usage) {
    if (size > kMaxBufferBytes) { set_error(webgl::OUT_OF_MEMORY); return; }
    const uint32_t bid = (target == webgl::ARRAY_BUFFER) ? m_array_buffer :
                         (target == webgl::ELEMENT_ARRAY_BUFFER) ? current_vao().element_array_buffer : 0;
    if (target != webgl::ARRAY_BUFFER && target != webgl::ELEMENT_ARRAY_BUFFER) {
        set_error(webgl::INVALID_ENUM);
        return;
    }
    auto it = m_buffers.find(bid);
    if (it == m_buffers.end()) { set_error(webgl::INVALID_OPERATION); return; }
    it->second.usage = usage;
    it->second.data.assign(size, 0);
}

void WebGLContext::buffer_data_bytes(uint32_t target, const uint8_t* data, std::size_t size, uint32_t usage) {
    if (size > kMaxBufferBytes) { set_error(webgl::OUT_OF_MEMORY); return; }
    if (target != webgl::ARRAY_BUFFER && target != webgl::ELEMENT_ARRAY_BUFFER) {
        set_error(webgl::INVALID_ENUM);
        return;
    }
    const uint32_t bid = (target == webgl::ARRAY_BUFFER) ? m_array_buffer : current_vao().element_array_buffer;
    auto it = m_buffers.find(bid);
    if (it == m_buffers.end()) { set_error(webgl::INVALID_OPERATION); return; }
    it->second.usage = usage;
    it->second.data.assign(data, data + size);
}

void WebGLContext::buffer_sub_data(uint32_t target, std::size_t offset, const uint8_t* data, std::size_t size) {
    if (target != webgl::ARRAY_BUFFER && target != webgl::ELEMENT_ARRAY_BUFFER) {
        set_error(webgl::INVALID_ENUM);
        return;
    }
    const uint32_t bid = (target == webgl::ARRAY_BUFFER) ? m_array_buffer : current_vao().element_array_buffer;
    auto it = m_buffers.find(bid);
    if (it == m_buffers.end()) { set_error(webgl::INVALID_OPERATION); return; }
    if (offset > it->second.data.size() || size > it->second.data.size() - offset) {
        set_error(webgl::INVALID_VALUE);
        return;
    }
    if (size > 0 && data) std::memcpy(it->second.data.data() + offset, data, size);
}

int WebGLContext::get_buffer_parameter(uint32_t target, uint32_t pname) {
    const uint32_t bid = (target == webgl::ARRAY_BUFFER) ? m_array_buffer :
                         (target == webgl::ELEMENT_ARRAY_BUFFER) ? current_vao().element_array_buffer : 0;
    auto it = m_buffers.find(bid);
    if (it == m_buffers.end()) { set_error(webgl::INVALID_OPERATION); return 0; }
    if (pname == webgl::BUFFER_SIZE) return static_cast<int>(it->second.data.size());
    if (pname == webgl::BUFFER_USAGE) return static_cast<int>(it->second.usage);
    set_error(webgl::INVALID_ENUM);
    return 0;
}

void WebGLContext::enable_vertex_attrib_array(uint32_t index) {
    if (index >= static_cast<uint32_t>(kMaxAttribs)) { set_error(webgl::INVALID_VALUE); return; }
    current_vao().attribs[index].enabled = true;
}

void WebGLContext::disable_vertex_attrib_array(uint32_t index) {
    if (index >= static_cast<uint32_t>(kMaxAttribs)) { set_error(webgl::INVALID_VALUE); return; }
    current_vao().attribs[index].enabled = false;
}

void WebGLContext::vertex_attrib_pointer(uint32_t index, int size, uint32_t type, bool normalized,
                                         int stride, std::size_t offset) {
    if (index >= static_cast<uint32_t>(kMaxAttribs) || size < 1 || size > 4 || stride < 0 || stride > 255) {
        set_error(webgl::INVALID_VALUE);
        return;
    }
    if (m_array_buffer == 0) { set_error(webgl::INVALID_OPERATION); return; }
    auto& a = current_vao().attribs[index];
    a.size = size;
    a.type = type;
    a.normalized = normalized;
    a.stride = stride;
    a.offset = offset;
    a.buffer_id = m_array_buffer;
}

void WebGLContext::vertex_attrib_4f(uint32_t index, float x, float y, float z, float w) {
    if (index >= static_cast<uint32_t>(kMaxAttribs)) { set_error(webgl::INVALID_VALUE); return; }
    current_vao().attribs[index].generic_value = {x, y, z, w};
}

uint32_t WebGLContext::create_vertex_array() {
    const uint32_t id = m_next_id++;
    m_vaos[id] = WebGLVertexArrayObj{id};
    return id;
}

void WebGLContext::delete_vertex_array(uint32_t id) {
    auto it = m_vaos.find(id);
    if (it != m_vaos.end()) it->second.deleted = true;
    if (m_bound_vao == id) m_bound_vao = 0;
}

bool WebGLContext::is_vertex_array(uint32_t id) const {
    auto it = m_vaos.find(id);
    return it != m_vaos.end() && !it->second.deleted;
}

void WebGLContext::bind_vertex_array(uint32_t id) {
    if (id != 0 && !is_vertex_array(id)) { set_error(webgl::INVALID_OPERATION); return; }
    m_bound_vao = id;
}

// --- Textures ---------------------------------------------------------------

uint32_t WebGLContext::create_texture() {
    const uint32_t id = m_next_id++;
    WebGLTextureObj tex;
    tex.id = id;
    m_textures[id] = std::move(tex);
    return id;
}

void WebGLContext::delete_texture(uint32_t id) {
    auto it = m_textures.find(id);
    if (it != m_textures.end()) {
        it->second.deleted = true;
        it->second.pixels.clear();
    }
    for (auto& u : m_texture_units) if (u == id) u = 0;
}

bool WebGLContext::is_texture(uint32_t id) const {
    auto it = m_textures.find(id);
    return it != m_textures.end() && !it->second.deleted;
}

void WebGLContext::active_texture(uint32_t unit_enum) {
    if (unit_enum < webgl::TEXTURE0 || unit_enum >= webgl::TEXTURE0 + kMaxTextureUnits) {
        set_error(webgl::INVALID_ENUM);
        return;
    }
    m_active_texture_unit = unit_enum - webgl::TEXTURE0;
}

void WebGLContext::bind_texture(uint32_t target, uint32_t id) {
    if (target != webgl::TEXTURE_2D && target != webgl::TEXTURE_CUBE_MAP) {
        set_error(webgl::INVALID_ENUM);
        return;
    }
    if (id != 0 && !is_texture(id)) { set_error(webgl::INVALID_OPERATION); return; }
    if (target == webgl::TEXTURE_2D) m_texture_units[m_active_texture_unit] = id;
}

void WebGLContext::tex_parameteri(uint32_t target, uint32_t pname, int param) {
    if (target != webgl::TEXTURE_2D) { set_error(webgl::INVALID_ENUM); return; }
    auto it = m_textures.find(m_texture_units[m_active_texture_unit]);
    if (it == m_textures.end()) { set_error(webgl::INVALID_OPERATION); return; }
    switch (pname) {
        case webgl::TEXTURE_MIN_FILTER: it->second.min_filter = static_cast<uint32_t>(param); break;
        case webgl::TEXTURE_MAG_FILTER: it->second.mag_filter = static_cast<uint32_t>(param); break;
        case webgl::TEXTURE_WRAP_S: it->second.wrap_s = static_cast<uint32_t>(param); break;
        case webgl::TEXTURE_WRAP_T: it->second.wrap_t = static_cast<uint32_t>(param); break;
        default: set_error(webgl::INVALID_ENUM); break;
    }
}

static int format_channels(uint32_t fmt) {
    switch (fmt) {
        case webgl::ALPHA:
        case webgl::LUMINANCE: return 1;
        case webgl::LUMINANCE_ALPHA: return 2;
        case webgl::RGB:
        case webgl::RGB8: return 3;
        case webgl::RGBA:
        case webgl::RGBA8: return 4;
        default: return 0;
    }
}

void WebGLContext::tex_image_2d(uint32_t target, int level, uint32_t internal_format,
                                int width, int height, int border, uint32_t format, uint32_t type,
                                const uint8_t* pixels, std::size_t pixel_byte_len) {
    if (target != webgl::TEXTURE_2D) { set_error(webgl::INVALID_ENUM); return; }
    if (level < 0 || width < 0 || height < 0 || border != 0 ||
        width > kMaxTextureSize || height > kMaxTextureSize) {
        set_error(webgl::INVALID_VALUE);
        return;
    }
    if (level > 0) return; // Base level stored; mip levels accepted as no-op
    auto it = m_textures.find(m_texture_units[m_active_texture_unit]);
    if (it == m_textures.end()) { set_error(webgl::INVALID_OPERATION); return; }
    const int ch = format_channels(format);
    if (ch == 0 || type != webgl::UNSIGNED_BYTE) {
        set_error(webgl::INVALID_ENUM);
        return;
    }
    if (width > 0 && height > 0 && !RasterBudget::allows(width, height)) { set_error(webgl::OUT_OF_MEMORY); return; }

    auto& tex = it->second;
    tex.width = width;
    tex.height = height;
    tex.internal_format = internal_format;
    tex.pixels.assign(static_cast<std::size_t>(width) * height * 4, 0);
    if (!pixels || width == 0 || height == 0) return;

    const std::size_t row_bytes_unpadded = static_cast<std::size_t>(width) * ch;
    const std::size_t align = static_cast<std::size_t>(std::max(1, m_unpack_alignment));
    const std::size_t row_stride = ((row_bytes_unpadded + align - 1) / align) * align;

    for (int y = 0; y < height; ++y) {
        const int dst_y = m_unpack_flip_y ? (height - 1 - y) : y;
        const std::size_t src_row_off = static_cast<std::size_t>(y) * row_stride;
        for (int x = 0; x < width; ++x) {
            const std::size_t s = src_row_off + static_cast<std::size_t>(x) * ch;
            if (s + ch > pixel_byte_len) break;
            uint8_t r = 0, g = 0, b = 0, a = 255;
            if (format == webgl::RGBA || format == webgl::RGBA8) {
                r = pixels[s]; g = pixels[s + 1]; b = pixels[s + 2]; a = pixels[s + 3];
            } else if (format == webgl::RGB || format == webgl::RGB8) {
                r = pixels[s]; g = pixels[s + 1]; b = pixels[s + 2];
            } else if (format == webgl::ALPHA) {
                a = pixels[s];
            } else if (format == webgl::LUMINANCE) {
                r = g = b = pixels[s];
            } else if (format == webgl::LUMINANCE_ALPHA) {
                r = g = b = pixels[s]; a = pixels[s + 1];
            }
            if (m_unpack_premultiply_alpha && a < 255) {
                r = static_cast<uint8_t>((static_cast<uint32_t>(r) * a + 127) / 255);
                g = static_cast<uint8_t>((static_cast<uint32_t>(g) * a + 127) / 255);
                b = static_cast<uint8_t>((static_cast<uint32_t>(b) * a + 127) / 255);
            }
            const std::size_t d = (static_cast<std::size_t>(dst_y) * width + x) * 4;
            tex.pixels[d] = r; tex.pixels[d + 1] = g; tex.pixels[d + 2] = b; tex.pixels[d + 3] = a;
        }
    }
}

void WebGLContext::tex_sub_image_2d(uint32_t target, int level, int xoffset, int yoffset,
                                    int width, int height, uint32_t format, uint32_t type,
                                    const uint8_t* pixels, std::size_t pixel_byte_len) {
    if (target != webgl::TEXTURE_2D || type != webgl::UNSIGNED_BYTE) { set_error(webgl::INVALID_ENUM); return; }
    if (level != 0) return;
    auto it = m_textures.find(m_texture_units[m_active_texture_unit]);
    if (it == m_textures.end()) { set_error(webgl::INVALID_OPERATION); return; }
    auto& tex = it->second;
    if (xoffset < 0 || yoffset < 0 || width < 0 || height < 0 ||
        xoffset + width > tex.width || yoffset + height > tex.height) {
        set_error(webgl::INVALID_VALUE);
        return;
    }
    const int ch = format_channels(format);
    if (ch == 0 || !pixels) return;
    const std::size_t row_bytes_unpadded = static_cast<std::size_t>(width) * ch;
    const std::size_t align = static_cast<std::size_t>(std::max(1, m_unpack_alignment));
    const std::size_t row_stride = ((row_bytes_unpadded + align - 1) / align) * align;
    for (int y = 0; y < height; ++y) {
        const int sy = m_unpack_flip_y ? (height - 1 - y) : y;
        for (int x = 0; x < width; ++x) {
            const std::size_t s = static_cast<std::size_t>(sy) * row_stride + static_cast<std::size_t>(x) * ch;
            if (s + ch > pixel_byte_len) break;
            uint8_t r = 0, g = 0, b = 0, a = 255;
            if (ch == 4) { r = pixels[s]; g = pixels[s + 1]; b = pixels[s + 2]; a = pixels[s + 3]; }
            else if (ch == 3) { r = pixels[s]; g = pixels[s + 1]; b = pixels[s + 2]; }
            else if (format == webgl::ALPHA) { a = pixels[s]; }
            else if (format == webgl::LUMINANCE) { r = g = b = pixels[s]; }
            else { r = g = b = pixels[s]; a = pixels[s + 1]; }
            const std::size_t d = (static_cast<std::size_t>(yoffset + y) * tex.width + (xoffset + x)) * 4;
            tex.pixels[d] = r; tex.pixels[d + 1] = g; tex.pixels[d + 2] = b; tex.pixels[d + 3] = a;
        }
    }
}

void WebGLContext::generate_mipmap(uint32_t target) {
    if (target != webgl::TEXTURE_2D) set_error(webgl::INVALID_ENUM);
}

static float apply_wrap(float coord, uint32_t wrap) {
    if (wrap == webgl::CLAMP_TO_EDGE) return std::clamp(coord, 0.0f, 1.0f);
    if (wrap == webgl::MIRRORED_REPEAT) {
        float fl = std::floor(coord);
        float f = coord - fl;
        return (static_cast<int>(fl) & 1) ? (1.0f - f) : f;
    }
    return coord - std::floor(coord);
}

std::array<float, 4> WebGLContext::sample_texture_2d(int unit, float u, float v) const {
    if (unit < 0 || unit >= kMaxTextureUnits) return {0.0f, 0.0f, 0.0f, 1.0f};
    auto it = m_textures.find(m_texture_units[unit]);
    if (it == m_textures.end() || it->second.width <= 0 || it->second.height <= 0 || it->second.pixels.empty()) {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }
    const auto& tex = it->second;
    const float wu = apply_wrap(u, tex.wrap_s);
    const float wv = apply_wrap(v, tex.wrap_t);
    auto fetch = [&](int px, int py) -> std::array<float, 4> {
        px = std::clamp(px, 0, tex.width - 1);
        py = std::clamp(py, 0, tex.height - 1);
        const std::size_t idx = (static_cast<std::size_t>(py) * tex.width + px) * 4;
        return {
            tex.pixels[idx] / 255.0f,
            tex.pixels[idx + 1] / 255.0f,
            tex.pixels[idx + 2] / 255.0f,
            tex.pixels[idx + 3] / 255.0f
        };
    };
    const bool linear = (tex.mag_filter == webgl::LINEAR || tex.min_filter == webgl::LINEAR);
    if (!linear) {
        int px = std::clamp(static_cast<int>(std::floor(wu * tex.width)), 0, tex.width - 1);
        int py = std::clamp(static_cast<int>(std::floor(wv * tex.height)), 0, tex.height - 1);
        return fetch(px, py);
    }
    const float fx = wu * tex.width - 0.5f;
    const float fy = wv * tex.height - 0.5f;
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const float tx = fx - x0;
    const float ty = fy - y0;
    const auto c00 = fetch(x0, y0);
    const auto c10 = fetch(x0 + 1, y0);
    const auto c01 = fetch(x0, y0 + 1);
    const auto c11 = fetch(x0 + 1, y0 + 1);
    std::array<float, 4> out{};
    for (int i = 0; i < 4; ++i) {
        const float top = c00[i] * (1.0f - tx) + c10[i] * tx;
        const float bot = c01[i] * (1.0f - tx) + c11[i] * tx;
        out[i] = top * (1.0f - ty) + bot * ty;
    }
    return out;
}

// --- Framebuffers & Renderbuffers -------------------------------------------

uint32_t WebGLContext::create_framebuffer() {
    const uint32_t id = m_next_id++;
    m_framebuffers[id] = WebGLFramebufferObj{id};
    return id;
}

void WebGLContext::delete_framebuffer(uint32_t id) {
    auto it = m_framebuffers.find(id);
    if (it != m_framebuffers.end()) it->second.deleted = true;
    if (m_bound_framebuffer == id) m_bound_framebuffer = 0;
}

bool WebGLContext::is_framebuffer(uint32_t id) const {
    auto it = m_framebuffers.find(id);
    return it != m_framebuffers.end() && !it->second.deleted;
}

void WebGLContext::bind_framebuffer(uint32_t target, uint32_t id) {
    if (target != webgl::FRAMEBUFFER) { set_error(webgl::INVALID_ENUM); return; }
    if (id != 0 && !is_framebuffer(id)) { set_error(webgl::INVALID_OPERATION); return; }
    m_bound_framebuffer = id;
}

uint32_t WebGLContext::create_renderbuffer() {
    const uint32_t id = m_next_id++;
    WebGLRenderbufferObj rb;
    rb.id = id;
    m_renderbuffers[id] = std::move(rb);
    return id;
}

void WebGLContext::delete_renderbuffer(uint32_t id) {
    auto it = m_renderbuffers.find(id);
    if (it != m_renderbuffers.end()) it->second.deleted = true;
    if (m_bound_renderbuffer == id) m_bound_renderbuffer = 0;
}

bool WebGLContext::is_renderbuffer(uint32_t id) const {
    auto it = m_renderbuffers.find(id);
    return it != m_renderbuffers.end() && !it->second.deleted;
}

void WebGLContext::bind_renderbuffer(uint32_t target, uint32_t id) {
    if (target != webgl::RENDERBUFFER) { set_error(webgl::INVALID_ENUM); return; }
    if (id != 0 && !is_renderbuffer(id)) { set_error(webgl::INVALID_OPERATION); return; }
    m_bound_renderbuffer = id;
}

void WebGLContext::renderbuffer_storage(uint32_t target, uint32_t internal_format, int width, int height) {
    if (target != webgl::RENDERBUFFER) { set_error(webgl::INVALID_ENUM); return; }
    if (width < 0 || height < 0 || width > kMaxTextureSize || height > kMaxTextureSize) {
        set_error(webgl::INVALID_VALUE);
        return;
    }
    auto it = m_renderbuffers.find(m_bound_renderbuffer);
    if (it == m_renderbuffers.end()) { set_error(webgl::INVALID_OPERATION); return; }
    if (width > 0 && height > 0 && !RasterBudget::allows(width, height)) { set_error(webgl::OUT_OF_MEMORY); return; }
    auto& rb = it->second;
    rb.width = width;
    rb.height = height;
    rb.internal_format = internal_format;
    const std::size_t n = static_cast<std::size_t>(width) * height;
    if (internal_format == webgl::DEPTH_COMPONENT16 || internal_format == webgl::DEPTH_COMPONENT24 ||
        internal_format == webgl::DEPTH_STENCIL) {
        rb.depth.assign(n, 1.0f);
        rb.color.clear();
    } else {
        rb.color.assign(n * 4, 0);
        rb.depth.clear();
    }
}

void WebGLContext::framebuffer_texture_2d(uint32_t target, uint32_t attachment, uint32_t textarget,
                                          uint32_t texture_id, int level) {
    if (target != webgl::FRAMEBUFFER || textarget != webgl::TEXTURE_2D || level != 0) {
        set_error(webgl::INVALID_ENUM);
        return;
    }
    auto it = m_framebuffers.find(m_bound_framebuffer);
    if (it == m_framebuffers.end()) { set_error(webgl::INVALID_OPERATION); return; }
    if (attachment == webgl::COLOR_ATTACHMENT0) {
        it->second.color_tex_id = texture_id;
        it->second.color_rb_id = 0;
    } else {
        set_error(webgl::INVALID_ENUM);
    }
}

void WebGLContext::framebuffer_renderbuffer(uint32_t target, uint32_t attachment, uint32_t rbtarget,
                                            uint32_t rb_id) {
    if (target != webgl::FRAMEBUFFER || rbtarget != webgl::RENDERBUFFER) {
        set_error(webgl::INVALID_ENUM);
        return;
    }
    auto it = m_framebuffers.find(m_bound_framebuffer);
    if (it == m_framebuffers.end()) { set_error(webgl::INVALID_OPERATION); return; }
    if (attachment == webgl::COLOR_ATTACHMENT0) {
        it->second.color_rb_id = rb_id;
        it->second.color_tex_id = 0;
    } else if (attachment == webgl::DEPTH_ATTACHMENT) {
        it->second.depth_rb_id = rb_id;
    } else {
        set_error(webgl::INVALID_ENUM);
    }
}

uint32_t WebGLContext::check_framebuffer_status(uint32_t target) {
    if (target != webgl::FRAMEBUFFER) { set_error(webgl::INVALID_ENUM); return 0; }
    if (m_bound_framebuffer == 0) return webgl::FRAMEBUFFER_COMPLETE;
    auto it = m_framebuffers.find(m_bound_framebuffer);
    if (it == m_framebuffers.end()) return webgl::FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
    const auto& fb = it->second;
    int cw = 0, ch = 0;
    if (fb.color_tex_id != 0) {
        auto tit = m_textures.find(fb.color_tex_id);
        if (tit == m_textures.end() || tit->second.width <= 0 || tit->second.height <= 0)
            return webgl::FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        cw = tit->second.width; ch = tit->second.height;
    } else if (fb.color_rb_id != 0) {
        auto rit = m_renderbuffers.find(fb.color_rb_id);
        if (rit == m_renderbuffers.end() || rit->second.width <= 0 || rit->second.height <= 0)
            return webgl::FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
        cw = rit->second.width; ch = rit->second.height;
    } else {
        return webgl::FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT;
    }
    if (fb.depth_rb_id != 0) {
        auto dit = m_renderbuffers.find(fb.depth_rb_id);
        if (dit == m_renderbuffers.end() || dit->second.width != cw || dit->second.height != ch)
            return webgl::FRAMEBUFFER_INCOMPLETE_DIMENSIONS;
    }
    return webgl::FRAMEBUFFER_COMPLETE;
}

// --- Vertex Fetch, Blending, Depth, Rasterization ---------------------------

static std::size_t gl_type_bytes(uint32_t type) {
    switch (type) {
        case webgl::BYTE:
        case webgl::UNSIGNED_BYTE: return 1;
        case webgl::SHORT:
        case webgl::UNSIGNED_SHORT: return 2;
        case webgl::INT:
        case webgl::UNSIGNED_INT:
        case webgl::FLOAT: return 4;
        default: return 0;
    }
}

std::array<float, 4> WebGLContext::fetch_vertex_attrib(const WebGLVertexAttribState& attr,
                                                       int vertex_index) const {
    if (!attr.enabled || attr.buffer_id == 0) return attr.generic_value;
    auto bit = m_buffers.find(attr.buffer_id);
    if (bit == m_buffers.end() || bit->second.deleted) return attr.generic_value;
    const std::size_t elem_sz = gl_type_bytes(attr.type);
    if (elem_sz == 0) return attr.generic_value;
    const std::size_t stride = attr.stride > 0 ? static_cast<std::size_t>(attr.stride)
                                               : elem_sz * static_cast<std::size_t>(attr.size);
    const std::size_t base = attr.offset + static_cast<std::size_t>(vertex_index) * stride;
    const auto& buf = bit->second.data;
    std::array<float, 4> out{0.0f, 0.0f, 0.0f, 1.0f};
    for (int i = 0; i < attr.size; ++i) {
        const std::size_t off = base + static_cast<std::size_t>(i) * elem_sz;
        if (off + elem_sz > buf.size()) break;
        switch (attr.type) {
            case webgl::FLOAT: {
                float v = 0.0f;
                std::memcpy(&v, buf.data() + off, sizeof(float));
                out[i] = v;
                break;
            }
            case webgl::UNSIGNED_BYTE: {
                uint8_t v = buf[off];
                out[i] = attr.normalized ? (v / 255.0f) : static_cast<float>(v);
                break;
            }
            case webgl::BYTE: {
                int8_t v = static_cast<int8_t>(buf[off]);
                out[i] = attr.normalized ? std::max(-1.0f, v / 127.0f) : static_cast<float>(v);
                break;
            }
            case webgl::UNSIGNED_SHORT: {
                uint16_t v = 0;
                std::memcpy(&v, buf.data() + off, sizeof(uint16_t));
                out[i] = attr.normalized ? (v / 65535.0f) : static_cast<float>(v);
                break;
            }
            case webgl::SHORT: {
                int16_t v = 0;
                std::memcpy(&v, buf.data() + off, sizeof(int16_t));
                out[i] = attr.normalized ? std::max(-1.0f, v / 32767.0f) : static_cast<float>(v);
                break;
            }
            case webgl::UNSIGNED_INT: {
                uint32_t v = 0;
                std::memcpy(&v, buf.data() + off, sizeof(uint32_t));
                out[i] = attr.normalized ? (static_cast<float>(v) / 4294967295.0f) : static_cast<float>(v);
                break;
            }
            case webgl::INT: {
                int32_t v = 0;
                std::memcpy(&v, buf.data() + off, sizeof(int32_t));
                out[i] = attr.normalized ? std::max(-1.0f, static_cast<float>(v) / 2147483647.0f) : static_cast<float>(v);
                break;
            }
            default: break;
        }
    }
    return out;
}

bool WebGLContext::depth_passes(float incoming, float stored) const {
    switch (m_depth_func) {
        case webgl::NEVER: return false;
        case webgl::LESS: return incoming < stored;
        case webgl::EQUAL: return incoming == stored;
        case webgl::LEQUAL: return incoming <= stored;
        case webgl::GREATER: return incoming > stored;
        case webgl::NOTEQUAL: return incoming != stored;
        case webgl::GEQUAL: return incoming >= stored;
        case webgl::ALWAYS: return true;
        default: return incoming < stored;
    }
}

static std::array<float, 4> eval_blend_factor(uint32_t factor, const std::array<float, 4>& s,
                                              const std::array<float, 4>& d,
                                              const std::array<float, 4>& c) {
    switch (factor) {
        case webgl::ZERO: return {0, 0, 0, 0};
        case webgl::ONE: return {1, 1, 1, 1};
        case webgl::SRC_COLOR: return s;
        case webgl::ONE_MINUS_SRC_COLOR: return {1 - s[0], 1 - s[1], 1 - s[2], 1 - s[3]};
        case webgl::DST_COLOR: return d;
        case webgl::ONE_MINUS_DST_COLOR: return {1 - d[0], 1 - d[1], 1 - d[2], 1 - d[3]};
        case webgl::SRC_ALPHA: return {s[3], s[3], s[3], s[3]};
        case webgl::ONE_MINUS_SRC_ALPHA: return {1 - s[3], 1 - s[3], 1 - s[3], 1 - s[3]};
        case webgl::DST_ALPHA: return {d[3], d[3], d[3], d[3]};
        case webgl::ONE_MINUS_DST_ALPHA: return {1 - d[3], 1 - d[3], 1 - d[3], 1 - d[3]};
        case webgl::CONSTANT_COLOR: return c;
        case webgl::ONE_MINUS_CONSTANT_COLOR: return {1 - c[0], 1 - c[1], 1 - c[2], 1 - c[3]};
        case webgl::CONSTANT_ALPHA: return {c[3], c[3], c[3], c[3]};
        case webgl::ONE_MINUS_CONSTANT_ALPHA: return {1 - c[3], 1 - c[3], 1 - c[3], 1 - c[3]};
        case webgl::SRC_ALPHA_SATURATE: {
            float f = std::min(s[3], 1.0f - d[3]);
            return {f, f, f, 1.0f};
        }
        default: return {1, 1, 1, 1};
    }
}

static float apply_blend_eq(uint32_t eq, float s, float sf, float d, float df) {
    switch (eq) {
        case webgl::FUNC_ADD: return s * sf + d * df;
        case webgl::FUNC_SUBTRACT: return s * sf - d * df;
        case webgl::FUNC_REVERSE_SUBTRACT: return d * df - s * sf;
        case webgl::MIN: return std::min(s, d);
        case webgl::MAX: return std::max(s, d);
        default: return s * sf + d * df;
    }
}

void WebGLContext::write_pixel(RenderTarget& rt, int x, int y, float depth,
                               const std::array<float, 4>& rgba) {
    if (x < 0 || y < 0 || x >= rt.width || y >= rt.height) return;
    if (m_scissor_test) {
        if (x < m_sc_x || y < m_sc_y || x >= m_sc_x + m_sc_w || y >= m_sc_y + m_sc_h) return;
    }
    if (m_depth_test && rt.depth) {
        const std::size_t di = static_cast<std::size_t>(y) * rt.width + x;
        if (!depth_passes(depth, rt.depth[di])) return;
        if (m_depth_mask) rt.depth[di] = depth;
    }

    const int row = rt.flip_y ? (rt.height - 1 - y) : y;
    const std::size_t pix_idx = static_cast<std::size_t>(row) * rt.width + x;
    uint8_t dst_r = 0, dst_g = 0, dst_b = 0, dst_a = 0;
    if (rt.argb_color) {
        const uint32_t cur = rt.argb_color[pix_idx];
        dst_a = static_cast<uint8_t>((cur >> 24) & 0xFF);
        dst_r = static_cast<uint8_t>((cur >> 16) & 0xFF);
        dst_g = static_cast<uint8_t>((cur >> 8) & 0xFF);
        dst_b = static_cast<uint8_t>(cur & 0xFF);
    } else if (rt.rgba_color) {
        const uint8_t* dp = rt.rgba_color + pix_idx * 4;
        dst_r = dp[0]; dst_g = dp[1]; dst_b = dp[2]; dst_a = dp[3];
    } else {
        return;
    }
    std::array<float, 4> out = rgba;
    if (m_blend) {
        std::array<float, 4> d{dst_r / 255.0f, dst_g / 255.0f, dst_b / 255.0f, dst_a / 255.0f};
        const auto srgb = eval_blend_factor(m_blend_src_rgb, rgba, d, m_blend_color);
        const auto drgb = eval_blend_factor(m_blend_dst_rgb, rgba, d, m_blend_color);
        const auto sa = eval_blend_factor(m_blend_src_alpha, rgba, d, m_blend_color);
        const auto da = eval_blend_factor(m_blend_dst_alpha, rgba, d, m_blend_color);
        for (int i = 0; i < 3; ++i) out[i] = apply_blend_eq(m_blend_eq_rgb, rgba[i], srgb[i], d[i], drgb[i]);
        out[3] = apply_blend_eq(m_blend_eq_alpha, rgba[3], sa[3], d[3], da[3]);
    }
    const uint8_t nr = m_color_mask[0] ? float_to_u8(out[0]) : dst_r;
    const uint8_t ng = m_color_mask[1] ? float_to_u8(out[1]) : dst_g;
    const uint8_t nb = m_color_mask[2] ? float_to_u8(out[2]) : dst_b;
    const uint8_t na = m_color_mask[3] ? float_to_u8(out[3]) : dst_a;
    if (rt.argb_color) {
        rt.argb_color[pix_idx] = (static_cast<uint32_t>(na) << 24) | (static_cast<uint32_t>(nr) << 16) |
                                 (static_cast<uint32_t>(ng) << 8) | static_cast<uint32_t>(nb);
    } else {
        uint8_t* dp = rt.rgba_color + pix_idx * 4;
        dp[0] = nr; dp[1] = ng; dp[2] = nb; dp[3] = na;
    }
}

void WebGLContext::draw_primitives(uint32_t mode, const std::vector<int>& indices) {
    if (m_current_program == 0) { set_error(webgl::INVALID_OPERATION); return; }
    auto pit = m_programs.find(m_current_program);
    if (pit == m_programs.end() || !pit->second.linked || !pit->second.vs || !pit->second.fs) {
        set_error(webgl::INVALID_OPERATION);
        return;
    }
    RenderTarget rt;
    if (!resolve_render_target(rt)) {
        if (m_bound_framebuffer != 0) set_error(webgl::INVALID_FRAMEBUFFER_OPERATION);
        return;
    }
    if (indices.empty() || m_vp_w <= 0 || m_vp_h <= 0) return;

    const auto& prog = pit->second;
    const auto& vs = *prog.vs;
    const auto& fs = *prog.fs;
    const auto& vao = current_vao();

    // Pre-populate uniform GlslVals once for this draw call.
    std::vector<std::pair<std::string, GlslVal>> uniform_bindings;
    uniform_bindings.reserve(prog.active_uniforms.size());
    for (const auto& u : prog.active_uniforms) {
        GlslVal v;
        switch (u.type) {
            case webgl::FLOAT: v.type = GlslType::Float; break;
            case webgl::INT: v.type = GlslType::Int; break;
            case webgl::BOOL: v.type = GlslType::Bool; break;
            case webgl::FLOAT_VEC2: v.type = GlslType::Vec2; break;
            case webgl::FLOAT_VEC3: v.type = GlslType::Vec3; break;
            case webgl::FLOAT_VEC4: v.type = GlslType::Vec4; break;
            case webgl::INT_VEC2: v.type = GlslType::IVec2; break;
            case webgl::INT_VEC3: v.type = GlslType::IVec3; break;
            case webgl::INT_VEC4: v.type = GlslType::IVec4; break;
            case webgl::FLOAT_MAT2: v.type = GlslType::Mat2; break;
            case webgl::FLOAT_MAT3: v.type = GlslType::Mat3; break;
            case webgl::FLOAT_MAT4: v.type = GlslType::Mat4; break;
            case webgl::SAMPLER_2D: v.type = GlslType::Sampler2D; break;
            default: v.type = GlslType::Float; break;
        }
        auto uit = prog.uniform_values.find(u.location);
        if (uit != prog.uniform_values.end()) v.f = uit->second.data;
        uniform_bindings.push_back({u.name, v});
    }

    struct ShadedVertex {
        float sx{0.0f}, sy{0.0f}, sz{0.0f}, inv_w{1.0f};
        float clip_w{1.0f};
        float point_size{1.0f};
        std::vector<GlslVal> varyings;
    };

    auto run_vertex = [&](int vidx) -> ShadedVertex {
        GlslExec exec(vs, this);
        exec.init_globals();
        for (const auto& ub : uniform_bindings) exec.set_var(ub.first, ub.second);
        for (const auto& a : prog.active_attribs) {
            const auto raw = (a.location >= 0 && a.location < kMaxAttribs)
                                 ? fetch_vertex_attrib(vao.attribs[a.location], vidx)
                                 : std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f};
            GlslVal av;
            switch (a.type) {
                case webgl::FLOAT: av.type = GlslType::Float; break;
                case webgl::FLOAT_VEC2: av.type = GlslType::Vec2; break;
                case webgl::FLOAT_VEC3: av.type = GlslType::Vec3; break;
                case webgl::FLOAT_VEC4: av.type = GlslType::Vec4; break;
                default: av.type = GlslType::Vec4; break;
            }
            for (int k = 0; k < 4; ++k) av.f[k] = raw[k];
            exec.set_var(a.name, av);
        }
        for (const auto& var : vs.varyings) {
            exec.set_var(var.name, GlslVal::zero(var.type));
        }
        exec.set_var("gl_Position", GlslVal::make_vec4(0.0f, 0.0f, 0.0f, 1.0f));
        exec.set_var("gl_PointSize", GlslVal::make_float(1.0f));
        exec.run_main();

        ShadedVertex sv;
        const GlslVal* pos = exec.lookup("gl_Position");
        const float cx = pos ? pos->f[0] : 0.0f;
        const float cy = pos ? pos->f[1] : 0.0f;
        const float cz = pos ? pos->f[2] : 0.0f;
        const float cw = pos ? pos->f[3] : 1.0f;
        sv.clip_w = cw;
        const float safe_w = (std::fabs(cw) > 1e-7f) ? cw : 1e-7f;
        sv.inv_w = 1.0f / safe_w;
        const float nx = cx * sv.inv_w;
        const float ny = cy * sv.inv_w;
        const float nz = cz * sv.inv_w;
        sv.sx = static_cast<float>(m_vp_x) + (nx * 0.5f + 0.5f) * static_cast<float>(m_vp_w);
        sv.sy = static_cast<float>(m_vp_y) + (ny * 0.5f + 0.5f) * static_cast<float>(m_vp_h);
        sv.sz = m_depth_near + (nz * 0.5f + 0.5f) * (m_depth_far - m_depth_near);
        if (const GlslVal* ps = exec.lookup("gl_PointSize")) {
            sv.point_size = std::clamp(ps->f[0], 1.0f, 64.0f);
        }
        sv.varyings.reserve(fs.varyings.size());
        for (const auto& fv : fs.varyings) {
            if (const GlslVal* vv = exec.lookup(fv.name)) {
                sv.varyings.push_back(*vv);
            } else {
                sv.varyings.push_back(GlslVal::zero(fv.type));
            }
        }
        return sv;
    };

    auto shade_and_write = [&](int px, int py, float depth, float inv_w, bool front_facing,
                               const std::vector<GlslVal>& interp_varyings,
                               float point_u = 0.5f, float point_v = 0.5f) {
        if (px < 0 || py < 0 || px >= rt.width || py >= rt.height) return;
        if (m_scissor_test) {
            if (px < m_sc_x || py < m_sc_y || px >= m_sc_x + m_sc_w || py >= m_sc_y + m_sc_h) return;
        }
        if (m_depth_test && rt.depth && !fs.has_discard) {
            const std::size_t di = static_cast<std::size_t>(py) * rt.width + px;
            if (!depth_passes(depth, rt.depth[di])) return;
        }

        GlslExec fexec(fs, this);
        fexec.init_globals();
        for (const auto& ub : uniform_bindings) fexec.set_var(ub.first, ub.second);
        for (std::size_t i = 0; i < fs.varyings.size() && i < interp_varyings.size(); ++i) {
            fexec.set_var(fs.varyings[i].name, interp_varyings[i]);
        }
        fexec.set_var("gl_FragCoord", GlslVal::make_vec4(px + 0.5f, py + 0.5f, depth, inv_w));
        fexec.set_var("gl_FrontFacing", GlslVal::make_bool(front_facing));
        GlslVal ptc = GlslVal::zero(GlslType::Vec2);
        ptc.f[0] = point_u; ptc.f[1] = point_v;
        fexec.set_var("gl_PointCoord", ptc);
        fexec.set_var("gl_FragColor", GlslVal::make_vec4(0.0f, 0.0f, 0.0f, 1.0f));
        if (!fexec.run_main()) return; // discarded

        const GlslVal* out_col = !fs.frag_out_name.empty() ? fexec.lookup(fs.frag_out_name)
                                                           : fexec.lookup("gl_FragColor");
        if (!out_col) out_col = fexec.lookup("gl_FragColor");
        std::array<float, 4> rgba{0.0f, 0.0f, 0.0f, 1.0f};
        if (out_col) {
            rgba = {out_col->f[0], out_col->f[1], out_col->f[2], out_col->f[3]};
        }
        write_pixel(rt, px, py, depth, rgba);
    };

    std::vector<ShadedVertex> verts;
    verts.reserve(indices.size());
    for (int idx : indices) verts.push_back(run_vertex(idx));

    const int clip_x0 = std::max(0, m_scissor_test ? std::max(m_vp_x, m_sc_x) : m_vp_x);
    const int clip_y0 = std::max(0, m_scissor_test ? std::max(m_vp_y, m_sc_y) : m_vp_y);
    const int clip_x1 = std::min(rt.width - 1,
        (m_scissor_test ? std::min(m_vp_x + m_vp_w, m_sc_x + m_sc_w) : (m_vp_x + m_vp_w)) - 1);
    const int clip_y1 = std::min(rt.height - 1,
        (m_scissor_test ? std::min(m_vp_y + m_vp_h, m_sc_y + m_sc_h) : (m_vp_y + m_vp_h)) - 1);
    if (clip_x0 > clip_x1 || clip_y0 > clip_y1) return;

    auto raster_triangle = [&](const ShadedVertex& v0, const ShadedVertex& v1, const ShadedVertex& v2) {
        if (v0.clip_w <= 0.0f || v1.clip_w <= 0.0f || v2.clip_w <= 0.0f) return;
        const float area = (v1.sx - v0.sx) * (v2.sy - v0.sy) - (v1.sy - v0.sy) * (v2.sx - v0.sx);
        if (std::fabs(area) < 1e-6f) return;
        const bool is_ccw = area > 0.0f;
        const bool front_facing = (m_front_face == webgl::CCW) ? is_ccw : !is_ccw;
        if (m_cull_face_enabled) {
            if (m_cull_face == webgl::FRONT_AND_BACK) return;
            if (m_cull_face == webgl::FRONT && front_facing) return;
            if (m_cull_face == webgl::BACK && !front_facing) return;
        }

        const int min_x = std::max(clip_x0, static_cast<int>(std::floor(std::min({v0.sx, v1.sx, v2.sx}))));
        const int max_x = std::min(clip_x1, static_cast<int>(std::ceil(std::max({v0.sx, v1.sx, v2.sx}))));
        const int min_y = std::max(clip_y0, static_cast<int>(std::floor(std::min({v0.sy, v1.sy, v2.sy}))));
        const int max_y = std::min(clip_y1, static_cast<int>(std::ceil(std::max({v0.sy, v1.sy, v2.sy}))));
        const float inv_area = 1.0f / area;

        std::vector<GlslVal> interp(fs.varyings.size());
        for (int py = min_y; py <= max_y; ++py) {
            const float cy = py + 0.5f;
            for (int px = min_x; px <= max_x; ++px) {
                const float cx = px + 0.5f;
                const float w0 = ((v1.sx - cx) * (v2.sy - cy) - (v1.sy - cy) * (v2.sx - cx)) * inv_area;
                const float w1 = ((v2.sx - cx) * (v0.sy - cy) - (v2.sy - cy) * (v0.sx - cx)) * inv_area;
                const float w2 = 1.0f - w0 - w1;
                if (w0 < -1e-5f || w1 < -1e-5f || w2 < -1e-5f) continue;

                const float depth = w0 * v0.sz + w1 * v1.sz + w2 * v2.sz;
                if (depth < m_depth_near - 1e-5f || depth > m_depth_far + 1e-5f) continue;
                const float denom = w0 * v0.inv_w + w1 * v1.inv_w + w2 * v2.inv_w;
                const float inv_denom = (std::fabs(denom) > 1e-7f) ? (1.0f / denom) : 1.0f;
                const float p0 = w0 * v0.inv_w * inv_denom;
                const float p1 = w1 * v1.inv_w * inv_denom;
                const float p2 = w2 * v2.inv_w * inv_denom;

                for (std::size_t vi = 0; vi < interp.size(); ++vi) {
                    GlslVal val = GlslVal::zero(fs.varyings[vi].type);
                    const int nc = glsl_components(val.type);
                    for (int c = 0; c < nc; ++c) {
                        val.f[c] = p0 * v0.varyings[vi].f[c] +
                                   p1 * v1.varyings[vi].f[c] +
                                   p2 * v2.varyings[vi].f[c];
                    }
                    interp[vi] = val;
                }
                shade_and_write(px, py, depth, denom, front_facing, interp);
            }
        }
    };

    auto raster_line = [&](const ShadedVertex& v0, const ShadedVertex& v1) {
        if (v0.clip_w <= 0.0f || v1.clip_w <= 0.0f) return;
        const float dx = v1.sx - v0.sx;
        const float dy = v1.sy - v0.sy;
        const int steps = std::max(1, static_cast<int>(std::ceil(std::max(std::fabs(dx), std::fabs(dy)))));
        std::vector<GlslVal> interp(fs.varyings.size());
        for (int s = 0; s <= steps; ++s) {
            const float t = static_cast<float>(s) / static_cast<float>(steps);
            const int px = static_cast<int>(std::floor(v0.sx + dx * t));
            const int py = static_cast<int>(std::floor(v0.sy + dy * t));
            if (px < clip_x0 || px > clip_x1 || py < clip_y0 || py > clip_y1) continue;
            const float depth = v0.sz * (1.0f - t) + v1.sz * t;
            const float iw = v0.inv_w * (1.0f - t) + v1.inv_w * t;
            for (std::size_t vi = 0; vi < interp.size(); ++vi) {
                GlslVal val = GlslVal::zero(fs.varyings[vi].type);
                const int nc = glsl_components(val.type);
                for (int c = 0; c < nc; ++c) {
                    val.f[c] = v0.varyings[vi].f[c] * (1.0f - t) + v1.varyings[vi].f[c] * t;
                }
                interp[vi] = val;
            }
            shade_and_write(px, py, depth, iw, true, interp);
        }
    };

    switch (mode) {
        case webgl::POINTS: {
            for (const auto& v : verts) {
                if (v.clip_w <= 0.0f) continue;
                const float half = v.point_size * 0.5f;
                const int x0 = std::max(clip_x0, static_cast<int>(std::floor(v.sx - half)));
                const int x1 = std::min(clip_x1, static_cast<int>(std::floor(v.sx + half)));
                const int y0 = std::max(clip_y0, static_cast<int>(std::floor(v.sy - half)));
                const int y1 = std::min(clip_y1, static_cast<int>(std::floor(v.sy + half)));
                for (int py = y0; py <= y1; ++py) {
                    for (int px = x0; px <= x1; ++px) {
                        const float pu = std::clamp((px + 0.5f - (v.sx - half)) / v.point_size, 0.0f, 1.0f);
                        const float pv = std::clamp(1.0f - (py + 0.5f - (v.sy - half)) / v.point_size, 0.0f, 1.0f);
                        shade_and_write(px, py, v.sz, v.inv_w, true, v.varyings, pu, pv);
                    }
                }
            }
            break;
        }
        case webgl::LINES:
            for (std::size_t i = 0; i + 1 < verts.size(); i += 2) raster_line(verts[i], verts[i + 1]);
            break;
        case webgl::LINE_STRIP:
            for (std::size_t i = 0; i + 1 < verts.size(); ++i) raster_line(verts[i], verts[i + 1]);
            break;
        case webgl::LINE_LOOP:
            for (std::size_t i = 0; i + 1 < verts.size(); ++i) raster_line(verts[i], verts[i + 1]);
            if (verts.size() >= 2) raster_line(verts.back(), verts.front());
            break;
        case webgl::TRIANGLES:
            for (std::size_t i = 0; i + 2 < verts.size(); i += 3) {
                raster_triangle(verts[i], verts[i + 1], verts[i + 2]);
            }
            break;
        case webgl::TRIANGLE_STRIP:
            for (std::size_t i = 0; i + 2 < verts.size(); ++i) {
                if (i & 1) raster_triangle(verts[i + 1], verts[i], verts[i + 2]);
                else raster_triangle(verts[i], verts[i + 1], verts[i + 2]);
            }
            break;
        case webgl::TRIANGLE_FAN:
            for (std::size_t i = 1; i + 1 < verts.size(); ++i) {
                raster_triangle(verts[0], verts[i], verts[i + 1]);
            }
            break;
        default:
            set_error(webgl::INVALID_ENUM);
            break;
    }
}

void WebGLContext::draw_arrays(uint32_t mode, int first, int count) {
    if (first < 0 || count < 0 || count > kMaxDrawCount) {
        set_error(webgl::INVALID_VALUE);
        return;
    }
    if (mode > webgl::TRIANGLE_FAN) { set_error(webgl::INVALID_ENUM); return; }
    if (count == 0) return;
    std::vector<int> indices(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) indices[static_cast<std::size_t>(i)] = first + i;
    draw_primitives(mode, indices);
}

void WebGLContext::draw_elements(uint32_t mode, int count, uint32_t type, std::size_t offset) {
    if (count < 0 || count > kMaxDrawCount) { set_error(webgl::INVALID_VALUE); return; }
    if (mode > webgl::TRIANGLE_FAN) { set_error(webgl::INVALID_ENUM); return; }
    if (type != webgl::UNSIGNED_BYTE && type != webgl::UNSIGNED_SHORT &&
        !(m_is_webgl2 && type == webgl::UNSIGNED_INT)) {
        set_error(webgl::INVALID_ENUM);
        return;
    }
    const uint32_t eb_id = current_vao().element_array_buffer;
    auto bit = m_buffers.find(eb_id);
    if (bit == m_buffers.end() || bit->second.deleted) {
        set_error(webgl::INVALID_OPERATION);
        return;
    }
    if (count == 0) return;
    const std::size_t elem_sz = gl_type_bytes(type);
    const auto& buf = bit->second.data;
    if (offset > buf.size() || static_cast<std::size_t>(count) * elem_sz > buf.size() - offset) {
        set_error(webgl::INVALID_OPERATION);
        return;
    }
    std::vector<int> indices;
    indices.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const std::size_t pos = offset + static_cast<std::size_t>(i) * elem_sz;
        uint32_t idx = 0;
        if (type == webgl::UNSIGNED_BYTE) idx = buf[pos];
        else if (type == webgl::UNSIGNED_SHORT) {
            uint16_t v = 0; std::memcpy(&v, buf.data() + pos, 2); idx = v;
        } else {
            std::memcpy(&idx, buf.data() + pos, 4);
        }
        indices.push_back(static_cast<int>(idx));
    }
    draw_primitives(mode, indices);
}

bool WebGLContext::read_pixels(int x, int y, int width, int height, uint32_t format, uint32_t type,
                               std::vector<uint8_t>& out_rgba) {
    if (width < 0 || height < 0) { set_error(webgl::INVALID_VALUE); return false; }
    if (format != webgl::RGBA || type != webgl::UNSIGNED_BYTE) {
        set_error(webgl::INVALID_ENUM);
        return false;
    }
    RenderTarget rt;
    if (!resolve_render_target(rt)) {
        if (m_bound_framebuffer != 0) set_error(webgl::INVALID_FRAMEBUFFER_OPERATION);
        return false;
    }
    if (width > 0 && height > 0 && !RasterBudget::allows(width, height)) { set_error(webgl::OUT_OF_MEMORY); return false; }
    out_rgba.assign(static_cast<std::size_t>(width) * height * 4, 0);
    for (int ry = 0; ry < height; ++ry) {
        const int gy = y + ry;
        if (gy < 0 || gy >= rt.height) continue;
        const int src_row = rt.flip_y ? (rt.height - 1 - gy) : gy;
        for (int rx = 0; rx < width; ++rx) {
            const int gx = x + rx;
            if (gx < 0 || gx >= rt.width) continue;
            const std::size_t d = (static_cast<std::size_t>(ry) * width + rx) * 4;
            if (rt.argb_color) {
                const uint32_t cur = rt.argb_color[static_cast<std::size_t>(src_row) * rt.width + gx];
                out_rgba[d] = static_cast<uint8_t>((cur >> 16) & 0xFF);
                out_rgba[d + 1] = static_cast<uint8_t>((cur >> 8) & 0xFF);
                out_rgba[d + 2] = static_cast<uint8_t>(cur & 0xFF);
                out_rgba[d + 3] = static_cast<uint8_t>((cur >> 24) & 0xFF);
            } else if (rt.rgba_color) {
                const std::size_t s = (static_cast<std::size_t>(src_row) * rt.width + gx) * 4;
                out_rgba[d] = rt.rgba_color[s];
                out_rgba[d + 1] = rt.rgba_color[s + 1];
                out_rgba[d + 2] = rt.rgba_color[s + 2];
                out_rgba[d + 3] = rt.rgba_color[s + 3];
            }
        }
    }
    return true;
}

} // namespace litebird
