#include "litebird/shell/WebBrowserServer.hpp"
#include "litebird/net/Deflate.hpp"
#include "litebird/render/FontRenderer.hpp"
#include "litebird/render/ImageDecoder.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

namespace litebird {

namespace {

std::string lower_ascii(std::string_view sv) {
    std::string out(sv);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

std::string trim_ws(std::string_view sv) {
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) {
        sv.remove_prefix(1);
    }
    while (!sv.empty() && (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) {
        sv.remove_suffix(1);
    }
    return std::string(sv);
}

std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out;
}

// Minimal JSON field extractor for flat `{ "url": "...", "x": 123, ... }` payloads.
std::string extract_json_string(std::string_view body, std::string_view key) {
    const std::string needle = "\"" + std::string(key) + "\"";
    auto pos = body.find(needle);
    if (pos == std::string_view::npos) return {};
    pos = body.find(':', pos + needle.size());
    if (pos == std::string_view::npos) return {};
    ++pos;
    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) ++pos;
    if (pos >= body.size() || body[pos] != '"') return {};
    ++pos;
    std::string out;
    while (pos < body.size() && body[pos] != '"') {
        if (body[pos] == '\\' && pos + 1 < body.size()) {
            ++pos;
            switch (body[pos]) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                default: out.push_back(body[pos]); break;
            }
        } else {
            out.push_back(body[pos]);
        }
        ++pos;
    }
    return out;
}

double extract_json_number(std::string_view body, std::string_view key, double fallback = 0.0) {
    const std::string needle = "\"" + std::string(key) + "\"";
    auto pos = body.find(needle);
    if (pos == std::string_view::npos) return fallback;
    pos = body.find(':', pos + needle.size());
    if (pos == std::string_view::npos) return fallback;
    ++pos;
    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) ++pos;
    std::size_t end = pos;
    while (end < body.size() &&
           (std::isdigit(static_cast<unsigned char>(body[end])) ||
            body[end] == '-' || body[end] == '+' || body[end] == '.' || body[end] == 'e' || body[end] == 'E')) {
        ++end;
    }
    if (end == pos) return fallback;
    try {
        return std::stod(std::string(body.substr(pos, end - pos)));
    } catch (...) {
        return fallback;
    }
}

bool send_all_bytes(int fd, const void* data, std::size_t len) {
    const auto* p = static_cast<const uint8_t*>(data);
    std::size_t sent = 0;
    while (sent < len) {
#ifdef MSG_NOSIGNAL
        const ssize_t n = ::send(fd, p + sent, len - sent, MSG_NOSIGNAL);
#else
        const ssize_t n = ::send(fd, p + sent, len - sent, 0);
#endif
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool read_disk_file(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// Wrap uncompressed bytes in a valid RFC 1952 gzip member (using stored DEFLATE
// blocks + IEEE CRC-32 + ISIZE) so `/demo/*` exercises real HTTP gzip decoding!
std::string encode_gzip_stored(const uint8_t* data, std::size_t len) {
    std::vector<uint8_t> out;
    out.reserve(len + 32);
    // 10-byte gzip header: ID1=0x1f, ID2=0x8b, CM=8 (deflate), FLG=0, MTIME=0, XFL=0, OS=3
    const uint8_t hdr[10] = {0x1f, 0x8b, 0x08, 0x00, 0, 0, 0, 0, 0x00, 0x03};
    out.insert(out.end(), hdr, hdr + 10);

    // Stored DEFLATE blocks (zlib wrapper stripped: skip 2-byte header and 4-byte Adler-32)
    std::vector<uint8_t> z;
    Deflate::compress_zlib_stored(data, len, z);
    if (z.size() >= 6) {
        out.insert(out.end(), z.begin() + 2, z.end() - 4);
    }

    const uint32_t crc = Deflate::crc32(data, len);
    const uint32_t isize = static_cast<uint32_t>(len & 0xFFFFFFFFu);
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((crc >> (i * 8)) & 0xFFu));
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>((isize >> (i * 8)) & 0xFFu));
    return std::string(reinterpret_cast<const char*>(out.data()), out.size());
}

const char* kBrowserUiHtml = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Litebird Browser</title>
<style>
  :root {
    --bg: #0b1120;
    --chrome: #111827;
    --chrome-border: #1f2937;
    --surface: #1e293b;
    --text: #f1f5f9;
    --muted: #94a3b8;
    --accent: #3b82f6;
    --accent-hover: #2563eb;
    --ok: #10b981;
  }
  * { box-sizing: border-box; }
  body {
    margin: 0;
    background: var(--bg);
    color: var(--text);
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
    height: 100vh;
    display: flex;
    flex-direction: column;
    overflow: hidden;
  }
  .toolbar {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 8px 12px;
    background: var(--chrome);
    border-bottom: 1px solid var(--chrome-border);
  }
  .nav-btn {
    background: var(--surface);
    color: var(--text);
    border: 1px solid #334155;
    border-radius: 6px;
    height: 32px;
    min-width: 32px;
    padding: 0 10px;
    font-size: 13px;
    cursor: pointer;
    display: inline-flex;
    align-items: center;
    justify-content: center;
    transition: background 0.12s, border-color 0.12s;
  }
  .nav-btn:hover:not(:disabled) {
    background: #334155;
    border-color: #475569;
  }
  .nav-btn:disabled {
    opacity: 0.4;
    cursor: default;
  }
  .nav-btn.primary {
    background: var(--accent);
    border-color: var(--accent);
    font-weight: 600;
  }
  .nav-btn.primary:hover {
    background: var(--accent-hover);
  }
  .url-wrap {
    flex: 1;
    display: flex;
    align-items: center;
    background: #0f172a;
    border: 1px solid #334155;
    border-radius: 8px;
    padding: 0 10px;
    height: 34px;
  }
  .url-wrap:focus-within {
    border-color: var(--accent);
    box-shadow: 0 0 0 2px rgba(59,130,246,0.25);
  }
  .scheme-badge {
    font-size: 11px;
    font-weight: 600;
    padding: 2px 6px;
    border-radius: 4px;
    margin-right: 8px;
    background: rgba(16,185,129,0.16);
    color: #34d399;
    white-space: nowrap;
  }
  #url-input {
    flex: 1;
    background: transparent;
    border: none;
    outline: none;
    color: var(--text);
    font-size: 13px;
    font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
  }
  select.vp-select {
    background: var(--surface);
    color: var(--text);
    border: 1px solid #334155;
    border-radius: 6px;
    height: 32px;
    padding: 0 8px;
    font-size: 12px;
  }
  .bookmarks {
    display: flex;
    align-items: center;
    gap: 6px;
    padding: 5px 12px;
    background: #0f172a;
    border-bottom: 1px solid var(--chrome-border);
    font-size: 12px;
    overflow-x: auto;
  }
  .bm-chip {
    background: #1e293b;
    color: #cbd5e1;
    border: 1px solid #334155;
    border-radius: 999px;
    padding: 3px 10px;
    font-size: 11.5px;
    cursor: pointer;
    white-space: nowrap;
  }
  .bm-chip:hover {
    background: #334155;
    color: #fff;
  }
  .workspace {
    flex: 1;
    display: flex;
    min-height: 0;
  }
  .viewport-stage {
    flex: 1;
    display: flex;
    align-items: flex-start;
    justify-content: center;
    overflow: auto;
    padding: 14px;
    background: radial-gradient(circle at top, #172033 0%, #0b1120 75%);
  }
  .frame-shell {
    position: relative;
    border-radius: 8px;
    overflow: hidden;
    box-shadow: 0 20px 50px rgba(0,0,0,0.55), 0 0 0 1px #334155;
    background: #fff;
    outline: none;
  }
  .frame-shell:focus {
    box-shadow: 0 20px 50px rgba(0,0,0,0.55), 0 0 0 2px var(--accent);
  }
  #viewport-img {
    display: block;
    user-select: none;
    -webkit-user-drag: none;
    cursor: pointer;
  }
  .devtools {
    width: 310px;
    background: var(--chrome);
    border-left: 1px solid var(--chrome-border);
    padding: 12px;
    overflow-y: auto;
    font-size: 12px;
    display: flex;
    flex-direction: column;
    gap: 12px;
  }
  .devtools.hidden { display: none; }
  .panel-card {
    background: #0f172a;
    border: 1px solid #1e293b;
    border-radius: 8px;
    padding: 10px;
  }
  .panel-title {
    font-size: 11px;
    font-weight: 700;
    text-transform: uppercase;
    letter-spacing: 0.06em;
    color: var(--muted);
    margin-bottom: 8px;
  }
  .kv {
    display: flex;
    justify-content: space-between;
    padding: 3px 0;
    border-bottom: 1px solid rgba(51,65,85,0.35);
    gap: 8px;
  }
  .kv:last-child { border-bottom: none; }
  .kv span:last-child {
    color: #e2e8f0;
    font-family: ui-monospace, SFMono-Regular, monospace;
    text-align: right;
    word-break: break-all;
  }
  .statusbar {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 5px 12px;
    background: var(--chrome);
    border-top: 1px solid var(--chrome-border);
    font-size: 11.5px;
    color: var(--muted);
  }
</style>
</head>
<body>
  <div class="toolbar">
    <button class="nav-btn" id="btn-back" title="Back">◀</button>
    <button class="nav-btn" id="btn-fwd" title="Forward">▶</button>
    <button class="nav-btn" id="btn-reload" title="Reload">↻</button>
    <button class="nav-btn" id="btn-home" title="Multi-File Demo App">⌂</button>
    <form id="nav-form" class="url-wrap" style="margin:0;">
      <span class="scheme-badge" id="engine-badge">LITEBIRD C++20</span>
      <input id="url-input" type="text" spellcheck="false" autocomplete="off" placeholder="Enter URL or file path (e.g. https://github.com or demo)">
    </form>
    <button class="nav-btn primary" id="btn-go">Go</button>
    <select class="vp-select" id="vp-select" title="Viewport Size">
      <option value="1120x700">1120 × 700</option>
      <option value="1280x800">1280 × 800</option>
      <option value="800x600">800 × 600</option>
      <option value="420x740">420 × 740 (Mobile)</option>
    </select>
    <button class="nav-btn" id="btn-devtools" title="Toggle Inspector">Inspector</button>
  </div>

  <div class="bookmarks">
    <span style="color:#64748b;font-weight:600;">Quick Load:</span>
    <button class="bm-chip" data-url="__DEMO_HTTP__">Multi-File App (External CSS + JS + PNG + SVG + Fetch/XHR)</button>
    <button class="bm-chip" data-url="examples/usable_browser_demo/index.html">Local File Bundle</button>
    <button class="bm-chip" data-url="examples/dashboard.html">Analytics Dashboard</button>
    <button class="bm-chip" data-url="examples/svg_showcase.html">SVG Showcase</button>
    <button class="bm-chip" data-url="https://github.com">https://github.com (Live TLS)</button>
  </div>

  <div class="workspace">
    <div class="viewport-stage">
      <div class="frame-shell" id="frame-shell" tabindex="0">
        <img id="viewport-img" alt="Litebird Rendered Viewport">
      </div>
    </div>

    <aside class="devtools" id="devtools">
      <div class="panel-card">
        <div class="panel-title">Document &amp; Engine</div>
        <div class="kv"><span>Title</span><span id="dt-title">-</span></div>
        <div class="kv"><span>Viewport</span><span id="dt-vp">-</span></div>
        <div class="kv"><span>Content Height</span><span id="dt-height">-</span></div>
        <div class="kv"><span>Scroll Y</span><span id="dt-scroll">0px</span></div>
        <div class="kv"><span>DOM Elements</span><span id="dt-nodes">0</span></div>
        <div class="kv"><span>Interactive</span><span id="dt-inter">0</span></div>
        <div class="kv"><span>Font Rasterizer</span><span id="dt-font">-</span></div>
      </div>
      <div class="panel-card">
        <div class="panel-title">Live Hit-Test &amp; Geometry</div>
        <div style="color:#cbd5e1;font-family:ui-monospace,monospace;line-height:1.5;word-break:break-word;" id="dt-hover">
          Move cursor or click inside the viewport to inspect live layout boxes.
        </div>
      </div>
      <div class="panel-card">
        <div class="panel-title">Controls</div>
        <div style="color:#94a3b8;line-height:1.5;">
          • <b>Click</b> links, buttons, tabs, and inputs directly on the rendered PNG.<br>
          • <b>Scroll</b> with mouse wheel or PgUp/PgDn/Arrow keys.<br>
          • <b>Type</b> into focused <code>&lt;input&gt;</code> fields after clicking them.
        </div>
      </div>
    </aside>
  </div>

  <div class="statusbar">
    <span id="status-msg">Ready</span>
    <span id="frame-rev">Frame #1</span>
  </div>

<script>
(function() {
  const img = document.getElementById('viewport-img');
  const shell = document.getElementById('frame-shell');
  const urlInput = document.getElementById('url-input');
  const statusMsg = document.getElementById('status-msg');
  const frameRev = document.getElementById('frame-rev');
  const btnBack = document.getElementById('btn-back');
  const btnFwd = document.getElementById('btn-fwd');
  let demoHttpUrl = '';
  let lastRev = 0;

  async function api(path, body) {
    const opts = body !== undefined
      ? { method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(body) }
      : { method: 'GET' };
    const res = await fetch(path, opts);
    return await res.json();
  }

  function applyState(st) {
    if (!st) return;
    if (st.demoUrl) demoHttpUrl = st.demoUrl;
    if (document.activeElement !== urlInput) {
      urlInput.value = st.url || '';
    }
    btnBack.disabled = !st.canBack;
    btnFwd.disabled = !st.canForward;
    statusMsg.textContent = st.status || 'Ready';
    document.getElementById('dt-title').textContent = st.title || '(untitled)';
    document.getElementById('dt-vp').textContent = st.viewportW + ' × ' + st.viewportH;
    document.getElementById('dt-height').textContent = Math.round(st.contentHeight) + 'px';
    document.getElementById('dt-scroll').textContent = Math.round(st.scrollY) + 'px';
    document.getElementById('dt-nodes').textContent = st.elementCount;
    document.getElementById('dt-inter').textContent = st.interactiveCount;
    document.getElementById('dt-font').textContent = st.truetype ? 'TrueType AA' : 'Bitmap 8x8';
    if (st.hovered) {
      document.getElementById('dt-hover').textContent = st.hovered;
    }
    if (st.revision !== lastRev) {
      lastRev = st.revision;
      frameRev.textContent = 'Frame #' + st.revision;
      img.src = 'api/frame.png?r=' + st.revision;
    }
  }

  async function navigate(target) {
    if (!target) return;
    if (target === '__DEMO_HTTP__') target = demoHttpUrl || 'examples/usable_browser_demo/index.html';
    statusMsg.textContent = 'Loading ' + target + ' ...';
    const st = await api('api/navigate', { url: target });
    applyState(st);
  }

  document.getElementById('nav-form').addEventListener('submit', function(e) {
    e.preventDefault();
    navigate(urlInput.value.trim());
  });
  document.getElementById('btn-go').addEventListener('click', function() {
    navigate(urlInput.value.trim());
  });
  btnBack.addEventListener('click', async function() { applyState(await api('api/back', {})); });
  btnFwd.addEventListener('click', async function() { applyState(await api('api/forward', {})); });
  document.getElementById('btn-reload').addEventListener('click', async function() { applyState(await api('api/reload', {})); });
  document.getElementById('btn-home').addEventListener('click', function() { navigate('__DEMO_HTTP__'); });
  document.getElementById('btn-devtools').addEventListener('click', function() {
    document.getElementById('devtools').classList.toggle('hidden');
  });

  document.querySelectorAll('.bm-chip').forEach(function(btn) {
    btn.addEventListener('click', function() {
      navigate(btn.getAttribute('data-url'));
    });
  });

  document.getElementById('vp-select').addEventListener('change', async function(e) {
    const parts = e.target.value.split('x');
    applyState(await api('api/resize', { width: Number(parts[0]), height: Number(parts[1]) }));
  });

  function canvasCoords(ev) {
    const r = img.getBoundingClientRect();
    const scaleX = (img.naturalWidth || r.width) / Math.max(1, r.width);
    const scaleY = (img.naturalHeight || r.height) / Math.max(1, r.height);
    return {
      x: Math.max(0, (ev.clientX - r.left) * scaleX),
      y: Math.max(0, (ev.clientY - r.top) * scaleY)
    };
  }

  img.addEventListener('click', async function(ev) {
    shell.focus();
    const pt = canvasCoords(ev);
    applyState(await api('api/click', pt));
  });

  let hoverBusy = false;
  img.addEventListener('mousemove', async function(ev) {
    if (hoverBusy) return;
    hoverBusy = true;
    try {
      const pt = canvasCoords(ev);
      applyState(await api('api/hover', pt));
    } finally {
      setTimeout(function() { hoverBusy = false; }, 90);
    }
  });

  shell.addEventListener('wheel', async function(ev) {
    ev.preventDefault();
    applyState(await api('api/scroll', { dx: ev.deltaX, dy: ev.deltaY }));
  }, { passive: false });

  shell.addEventListener('keydown', async function(ev) {
    if (ev.metaKey || ev.ctrlKey || ev.altKey) return;
    ev.preventDefault();
    applyState(await api('api/key', { key: ev.key }));
  });

  api('api/state').then(applyState);
})();
</script>
</body>
</html>
)HTML";

} // namespace

WebBrowserServer::WebBrowserServer(float viewport_w, float viewport_h)
    : m_shell(viewport_w, viewport_h, 100, 40) {}

WebBrowserServer::~WebBrowserServer() {
    stop();
}

bool WebBrowserServer::navigate_to(const std::string& url) {
    std::lock_guard<std::mutex> lock(m_shell_mutex);
    const bool ok = m_shell.navigate_to(url);
    m_cached_png = m_shell.render_png_frame();
    ++m_frame_revision;
    return ok;
}

void WebBrowserServer::set_custom_handler(CustomRouteHandler handler) {
    std::lock_guard<std::mutex> lock(m_handler_mutex);
    m_custom_handler = std::move(handler);
}

bool WebBrowserServer::open_listener(uint16_t port, const std::string& bind_host) {
    if (m_running.load()) return false;
#ifdef SIGPIPE
    std::signal(SIGPIPE, SIG_IGN);
#endif

    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return false;

    int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (bind_host.empty() || bind_host == "0.0.0.0") {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (::inet_pton(AF_INET, bind_host.c_str(), &addr.sin_addr) != 1) {
        ::close(fd);
        return false;
    }

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return false;
    }
    if (::listen(fd, 64) != 0) {
        ::close(fd);
        return false;
    }

    sockaddr_in actual{};
    socklen_t alen = sizeof(actual);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&actual), &alen) == 0) {
        m_bound_port.store(ntohs(actual.sin_port));
    } else {
        m_bound_port.store(port);
    }

    m_listen_fd = fd;
    m_running.store(true);
    return true;
}

bool WebBrowserServer::start_background(uint16_t port, const std::string& bind_host) {
    if (!open_listener(port, bind_host)) return false;
    m_accept_thread = std::thread([this]() { accept_loop(); });
    return true;
}

bool WebBrowserServer::run_blocking(uint16_t port, const std::string& bind_host) {
    if (!open_listener(port, bind_host)) return false;
    accept_loop();
    return true;
}

void WebBrowserServer::stop() {
    m_running.store(false);
    if (m_listen_fd >= 0) {
        ::shutdown(m_listen_fd, SHUT_RDWR);
        ::close(m_listen_fd);
        m_listen_fd = -1;
    }
    if (m_accept_thread.joinable()) {
        m_accept_thread.join();
    }
    // Wait briefly for in-flight client worker threads to finish cleanly.
    for (int i = 0; i < 200 && m_active_clients.load() > 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void WebBrowserServer::accept_loop() {
    while (m_running.load()) {
        sockaddr_in cli{};
        socklen_t clen = sizeof(cli);
        const int cfd = ::accept(m_listen_fd, reinterpret_cast<sockaddr*>(&cli), &clen);
        if (cfd < 0) {
            if (!m_running.load()) break;
            continue;
        }
        if (m_active_clients.load() >= 32) {
            ::close(cfd);
            continue;
        }
        m_active_clients.fetch_add(1);
        std::thread([this, cfd]() {
            handle_client(cfd);
            ::close(cfd);
            m_active_clients.fetch_sub(1);
        }).detach();
    }
}

void WebBrowserServer::handle_client(int client_fd) {
    timeval tv{};
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    ::setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    std::string raw;
    raw.reserve(4096);
    char buf[4096];
    std::size_t header_end = std::string::npos;
    while (raw.size() < 65536) {
        const ssize_t n = ::recv(client_fd, buf, sizeof(buf), 0);
        if (n <= 0) return;
        raw.append(buf, static_cast<std::size_t>(n));
        header_end = raw.find("\r\n\r\n");
        if (header_end != std::string::npos) break;
    }
    if (header_end == std::string::npos) return;

    HttpServerRequest req;
    const std::string_view head_sv(raw.data(), header_end);
    const auto first_crlf = head_sv.find("\r\n");
    const std::string_view req_line = (first_crlf == std::string_view::npos) ? head_sv : head_sv.substr(0, first_crlf);
    const auto sp1 = req_line.find(' ');
    if (sp1 == std::string_view::npos) return;
    const auto sp2 = req_line.find(' ', sp1 + 1);
    if (sp2 == std::string_view::npos) return;

    req.method = std::string(req_line.substr(0, sp1));
    req.raw_target = std::string(req_line.substr(sp1 + 1, sp2 - (sp1 + 1)));
    const auto qpos = req.raw_target.find('?');
    if (qpos != std::string::npos) {
        req.path = req.raw_target.substr(0, qpos);
        req.query = req.raw_target.substr(qpos + 1);
    } else {
        req.path = req.raw_target;
    }

    if (first_crlf != std::string_view::npos) {
        std::size_t cur = first_crlf + 2;
        while (cur < head_sv.size()) {
            const auto nl = head_sv.find("\r\n", cur);
            const std::string_view line = (nl == std::string_view::npos) ? head_sv.substr(cur) : head_sv.substr(cur, nl - cur);
            const auto colon = line.find(':');
            if (colon != std::string_view::npos) {
                req.headers[lower_ascii(trim_ws(line.substr(0, colon)))] = trim_ws(line.substr(colon + 1));
            }
            if (nl == std::string_view::npos) break;
            cur = nl + 2;
        }
    }

    std::size_t content_len = 0;
    if (auto it = req.headers.find("content-length"); it != req.headers.end()) {
        try { content_len = std::min<std::size_t>(1024 * 1024, std::stoul(it->second)); } catch (...) {}
    }

    req.body = raw.substr(header_end + 4);
    while (req.body.size() < content_len) {
        const ssize_t n = ::recv(client_fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        req.body.append(buf, static_cast<std::size_t>(n));
    }
    if (req.body.size() > content_len && content_len > 0) {
        req.body.resize(content_len);
    }

    HttpServerResponse resp = dispatch_request(req);

    std::ostringstream hdr;
    hdr << "HTTP/1.1 " << resp.status << " " << resp.status_text << "\r\n";
    hdr << "Content-Type: " << resp.content_type << "\r\n";
    hdr << "Cache-Control: no-store\r\n";
    for (const auto& [hk, hv] : resp.headers) {
        hdr << hk << ": " << hv << "\r\n";
    }
    if (resp.chunked) {
        hdr << "Transfer-Encoding: chunked\r\n";
        hdr << "Connection: close\r\n\r\n";
        const std::string hdr_str = hdr.str();
        if (!send_all_bytes(client_fd, hdr_str.data(), hdr_str.size())) return;

        // Emit body in 64-byte chunks to exercise chunked transfer decoding.
        std::size_t pos = 0;
        while (pos < resp.body.size()) {
            const std::size_t chunk_len = std::min<std::size_t>(64, resp.body.size() - pos);
            std::ostringstream cs;
            cs << std::hex << chunk_len << "\r\n";
            const std::string cprefix = cs.str();
            if (!send_all_bytes(client_fd, cprefix.data(), cprefix.size())) return;
            if (!send_all_bytes(client_fd, resp.body.data() + pos, chunk_len)) return;
            if (!send_all_bytes(client_fd, "\r\n", 2)) return;
            pos += chunk_len;
        }
        send_all_bytes(client_fd, "0\r\n\r\n", 5);
    } else {
        hdr << "Content-Length: " << resp.body.size() << "\r\n";
        hdr << "Connection: close\r\n\r\n";
        const std::string hdr_str = hdr.str();
        if (!send_all_bytes(client_fd, hdr_str.data(), hdr_str.size())) return;
        if (!resp.body.empty() && req.method != "HEAD") {
            send_all_bytes(client_fd, resp.body.data(), resp.body.size());
        }
    }
}

bool WebBrowserServer::serve_demo_route(const HttpServerRequest& req, HttpServerResponse& resp) {
    if (req.path == "/demo") {
        resp.status = 302;
        resp.status_text = "Found";
        resp.headers.emplace_back("Location", "/demo/index.html");
        resp.body = "Redirecting to /demo/index.html";
        return true;
    }
    if (req.path == "/demo/api/status.json") {
        const std::string json =
            R"JSON({"engine":"Litebird Phase 36","http":"HTTP/1.1 + gzip + chunked","tls":"OpenSSL 3.0 SNI","fonts":"TrueType AA","items":["External CSS <link>","External JS <script defer>","PNG & SVG <img>","Live getBoundingClientRect()","Block-in-Inline Cards"]})JSON";
        resp.content_type = "application/json; charset=utf-8";
        resp.headers.emplace_back("Access-Control-Allow-Origin", "*");
        auto ae = req.headers.find("accept-encoding");
        if (ae != req.headers.end() && ae->second.find("gzip") != std::string::npos) {
            resp.headers.emplace_back("Content-Encoding", "gzip");
            resp.body = encode_gzip_stored(reinterpret_cast<const uint8_t*>(json.data()), json.size());
        } else {
            resp.body = json;
        }
        resp.chunked = true;
        return true;
    }
    if (req.path == "/demo/logo.png") {
        // Generate a crisp procedural 48x48 gradient + emblem PNG in-memory.
        ImageData icon;
        icon.width = 48;
        icon.height = 48;
        icon.pixels.resize(48 * 48, 0);
        for (int y = 0; y < 48; ++y) {
            for (int x = 0; x < 48; ++x) {
                const float dx = x - 23.5f;
                const float dy = y - 23.5f;
                const float dist = std::sqrt(dx * dx + dy * dy);
                if (dist > 22.5f) {
                    icon.set_pixel(x, y, Color(0, 0, 0, 0).to_argb());
                } else if (dist > 15.0f && dist < 18.5f) {
                    icon.set_pixel(x, y, Color(255, 255, 255, 255).to_argb());
                } else {
                    const uint8_t r = static_cast<uint8_t>(37 + (x * 90) / 48);
                    const uint8_t g = static_cast<uint8_t>(99 + (y * 110) / 48);
                    icon.set_pixel(x, y, Color(r, g, 245, 255).to_argb());
                }
            }
        }
        const std::vector<uint8_t> png = ImageDecoder::encode_png(icon);
        resp.content_type = "image/png";
        resp.body.assign(reinterpret_cast<const char*>(png.data()), png.size());
        return true;
    }
    if (req.path.rfind("/demo/", 0) == 0) {
        const std::string rel = req.path.substr(6);
        if (rel.empty() || rel.find("..") != std::string::npos || rel.find('/') != std::string::npos) {
            return false;
        }
        std::string disk;
        if (!read_disk_file("examples/usable_browser_demo/" + rel, disk)) {
            return false;
        }
        if (rel.ends_with(".html")) resp.content_type = "text/html; charset=utf-8";
        else if (rel.ends_with(".css")) resp.content_type = "text/css; charset=utf-8";
        else if (rel.ends_with(".js")) resp.content_type = "application/javascript; charset=utf-8";
        else if (rel.ends_with(".svg")) resp.content_type = "image/svg+xml";
        else if (rel.ends_with(".png")) resp.content_type = "image/png";

        auto ae = req.headers.find("accept-encoding");
        if (!rel.ends_with(".png") && ae != req.headers.end() && ae->second.find("gzip") != std::string::npos) {
            resp.headers.emplace_back("Content-Encoding", "gzip");
            resp.body = encode_gzip_stored(reinterpret_cast<const uint8_t*>(disk.data()), disk.size());
        } else {
            resp.body = std::move(disk);
        }
        return true;
    }
    return false;
}

HttpServerResponse WebBrowserServer::dispatch_request(const HttpServerRequest& req) {
    HttpServerResponse resp;

    // 1. Custom route handler (stateless; used by integration tests without locking shell).
    {
        CustomRouteHandler handler;
        {
            std::lock_guard<std::mutex> hlock(m_handler_mutex);
            handler = m_custom_handler;
        }
        if (handler && handler(req, resp)) {
            return resp;
        }
    }

    // 2. Built-in multi-file `/demo/*` web app (served without locking `m_shell_mutex`
    //    so `BrowserShell` can fetch `http://127.0.0.1:<port>/demo/...` over HTTP!).
    if (serve_demo_route(req, resp)) {
        return resp;
    }

    // 3. Browser UI HTML shell (`GET /`).
    if (req.path == "/" || req.path == "/index.html") {
        resp.content_type = "text/html; charset=utf-8";
        resp.body = kBrowserUiHtml;
        return resp;
    }

    // 4. Interactive Browser API endpoints (guarded by `m_shell_mutex`).
    std::lock_guard<std::mutex> lock(m_shell_mutex);

    auto build_state_json = [&]() -> std::string {
        m_shell.tick();
        if (m_shell.is_dirty() || m_cached_png.empty()) {
            m_cached_png = m_shell.render_png_frame();
            ++m_frame_revision;
        }
        auto doc = m_shell.document();
        const std::size_t el_count = doc ? doc->get_elements_by_tag_name("*").size() : 0;
        const std::string title = doc ? doc->title() : "";
        const std::string demo_url = "http://127.0.0.1:" + std::to_string(m_bound_port.load()) + "/demo";

        std::ostringstream js;
        js << "{"
           << "\"url\":\"" << json_escape(m_shell.current_url()) << "\","
           << "\"demoUrl\":\"" << json_escape(demo_url) << "\","
           << "\"title\":\"" << json_escape(title) << "\","
           << "\"status\":\"" << json_escape(m_shell.status_message()) << "\","
           << "\"hovered\":\"" << json_escape(m_hovered_summary) << "\","
           << "\"canBack\":" << (m_shell.can_go_back() ? "true" : "false") << ","
           << "\"canForward\":" << (m_shell.can_go_forward() ? "true" : "false") << ","
           << "\"viewportW\":" << static_cast<int>(m_shell.viewport_width()) << ","
           << "\"viewportH\":" << static_cast<int>(m_shell.viewport_height()) << ","
           << "\"contentHeight\":" << m_shell.content_height() << ","
           << "\"scrollY\":" << m_shell.scroll_y() << ","
           << "\"elementCount\":" << el_count << ","
           << "\"interactiveCount\":" << m_shell.interactive_elements().size() << ","
           << "\"truetype\":" << (FontRenderer::instance().has_truetype_font() ? "true" : "false") << ","
           << "\"revision\":" << m_frame_revision
           << "}";
        return js.str();
    };

    if (req.path == "/api/frame.png") {
        if (m_cached_png.empty()) {
            m_cached_png = m_shell.render_png_frame();
        }
        resp.content_type = "image/png";
        resp.body.assign(reinterpret_cast<const char*>(m_cached_png.data()), m_cached_png.size());
        return resp;
    }

    if (req.path == "/api/state") {
        resp.content_type = "application/json; charset=utf-8";
        resp.body = build_state_json();
        return resp;
    }

    if (req.path == "/api/navigate" && req.method == "POST") {
        std::string target = trim_ws(extract_json_string(req.body, "url"));
        if (target == "demo" || target == "/demo") {
            target = "http://127.0.0.1:" + std::to_string(m_bound_port.load()) + "/demo";
        } else if (!target.empty() &&
                   target.find("://") == std::string::npos &&
                   !target.starts_with("data:") &&
                   !target.starts_with("about:") &&
                   !target.starts_with("file:") &&
                   !target.starts_with("/") &&
                   !target.starts_with("examples/") &&
                   target.find('.') != std::string::npos &&
                   !std::ifstream(target).good()) {
            target = "https://" + target;
        }
        if (!target.empty()) {
            m_shell.navigate_to(target);
            m_cached_png = m_shell.render_png_frame();
            ++m_frame_revision;
        }
        resp.content_type = "application/json; charset=utf-8";
        resp.body = build_state_json();
        return resp;
    }

    if (req.path == "/api/back" && req.method == "POST") {
        if (m_shell.back()) {
            m_cached_png = m_shell.render_png_frame();
            ++m_frame_revision;
        }
        resp.content_type = "application/json; charset=utf-8";
        resp.body = build_state_json();
        return resp;
    }

    if (req.path == "/api/forward" && req.method == "POST") {
        if (m_shell.forward()) {
            m_cached_png = m_shell.render_png_frame();
            ++m_frame_revision;
        }
        resp.content_type = "application/json; charset=utf-8";
        resp.body = build_state_json();
        return resp;
    }

    if (req.path == "/api/reload" && req.method == "POST") {
        if (m_shell.reload()) {
            m_cached_png = m_shell.render_png_frame();
            ++m_frame_revision;
        }
        resp.content_type = "application/json; charset=utf-8";
        resp.body = build_state_json();
        return resp;
    }

    if (req.path == "/api/click" && req.method == "POST") {
        const float x = static_cast<float>(extract_json_number(req.body, "x", 0.0));
        const float y = static_cast<float>(extract_json_number(req.body, "y", 0.0));
        m_shell.click_at(x, y);
        m_cached_png = m_shell.render_png_frame();
        ++m_frame_revision;
        resp.content_type = "application/json; charset=utf-8";
        resp.body = build_state_json();
        return resp;
    }

    if (req.path == "/api/hover" && req.method == "POST") {
        const float x = static_cast<float>(extract_json_number(req.body, "x", 0.0));
        const float y = static_cast<float>(extract_json_number(req.body, "y", 0.0));
        const bool hover_changed = m_shell.hover_at(x, y);
        if (auto hit = m_shell.element_at(x, y)) {
            const Rect r = hit->bounding_box();
            std::ostringstream hs;
            hs << "<" << hit->tag_name();
            if (!hit->id().empty()) hs << "#" << hit->id();
            const std::string cls = hit->get_attribute("class");
            if (!cls.empty()) hs << "." << cls;
            hs << ">  rect=(" << static_cast<int>(std::lround(r.x)) << ", "
               << static_cast<int>(std::lround(r.y)) << ", "
               << static_cast<int>(std::lround(r.width)) << "×"
               << static_cast<int>(std::lround(r.height)) << ")";
            if (hit->tag_name() == "a" && hit->has_attribute("href")) {
                hs << "  href=\"" << hit->get_attribute("href") << "\"";
            }
            m_hovered_summary = hs.str();
        } else {
            m_hovered_summary = "(empty viewport area)";
        }
        if (hover_changed) {
            m_cached_png = m_shell.render_png_frame();
            ++m_frame_revision;
        }
        resp.content_type = "application/json; charset=utf-8";
        resp.body = build_state_json();
        return resp;
    }

    if (req.path == "/api/scroll" && req.method == "POST") {
        const float dx = static_cast<float>(extract_json_number(req.body, "dx", 0.0));
        const float dy = static_cast<float>(extract_json_number(req.body, "dy", 0.0));
        m_shell.scroll_by(dx, dy);
        m_cached_png = m_shell.render_png_frame();
        ++m_frame_revision;
        resp.content_type = "application/json; charset=utf-8";
        resp.body = build_state_json();
        return resp;
    }

    if (req.path == "/api/key" && req.method == "POST") {
        const std::string key = extract_json_string(req.body, "key");
        if (m_shell.handle_key(key)) {
            m_cached_png = m_shell.render_png_frame();
            ++m_frame_revision;
        }
        resp.content_type = "application/json; charset=utf-8";
        resp.body = build_state_json();
        return resp;
    }

    if (req.path == "/api/resize" && req.method == "POST") {
        const float w = static_cast<float>(extract_json_number(req.body, "width", 1120.0));
        const float h = static_cast<float>(extract_json_number(req.body, "height", 700.0));
        m_shell.set_viewport_size(w, h);
        m_cached_png = m_shell.render_png_frame();
        ++m_frame_revision;
        resp.content_type = "application/json; charset=utf-8";
        resp.body = build_state_json();
        return resp;
    }

    resp.status = 404;
    resp.status_text = "Not Found";
    resp.body = "Not Found";
    return resp;
}

} // namespace litebird
