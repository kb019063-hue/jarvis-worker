# Phase 275 — WebGL 1.0/2.0 software rendering pipeline, GLSL ES compiler, and canvas bindings

Finding: [`docs/findings/279-webgl-contexts-and-rasterizer-were-missing.md`](../findings/279-webgl-contexts-and-rasterizer-were-missing.md)

## Scope

Implements a complete, zero-dependency CPU software WebGL 1.0 / WebGL 2.0 rendering pipeline backed directly by LiteBird's existing `CanvasRenderingContext2D` RGBA backing store and exposes the Khronos WebGL 1.0/2.0 WebIDL surface on `HTMLCanvasElement` and `OffscreenCanvas`:

1. **Software WebGL 1.0 / 2.0 pipeline (`include/litebird/render/WebGL.hpp`, `src/render/WebGL.cpp`, `Makefile`):**
   - Full `WebGLContext` state machine with WebGL 1.0 and WebGL 2.0 `GLenum` constants, GL error queue (`NO_ERROR`, `INVALID_ENUM`, `INVALID_VALUE`, `INVALID_OPERATION`, `OUT_OF_MEMORY`, `INVALID_FRAMEBUFFER_OPERATION`), `RasterBudget::allows` bounds checks on textures, renderbuffers, and `readPixels`, and direct shared backing-store integration (`CanvasRenderingContext2D::image_data()`, converting between bottom-up WebGL coordinates and top-down `0xAARRGGBB` canvas pixels).
   - **GLSL ES 1.00 & GLSL ES 3.00 (`#version 300 es`) frontend and bounded AST evaluator:** `#define`/`#ifdef`/`#ifndef`/`#else`/`#endif` preprocessor, lexer, recursive-descent parser, `attribute`/`in`/`varying`/`out`/`uniform`/`precision` reflection (`getActiveAttrib`, `getActiveUniform`, `getAttribLocation`, `getUniformLocation`), `float`/`int`/`bool`/`vec2`–`vec4`/`ivec2`–`ivec4`/`bvec2`–`bvec4`/`mat2`–`mat4`/`sampler2D` types, vector swizzles, matrix/vector arithmetic, control flow (`if`/`else`/`for`/`while`/`break`/`continue`/`return`/`discard`), user functions, and GLSL ES built-ins (`abs`, `sign`, `floor`, `ceil`, `fract`, `mod`, `min`, `max`, `clamp`, `mix`, `step`, `smoothstep`, `length`, `distance`, `dot`, `cross`, `normalize`, `reflect`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `pow`, `exp`, `log`, `sqrt`, `inversesqrt`, `texture2D`, `texture`), with step limits so hostile shaders cannot hang the process.
   - **Software rasterizer:** `clearColor`/`clearDepth`/`clear`, `viewport`, `scissor` + `SCISSOR_TEST`, `colorMask`, `depthFunc`/`depthMask` + `DEPTH_TEST`, `blendFunc`/`blendFuncSeparate`/`blendEquation`/`blendEquationSeparate`/`blendColor` + `BLEND`, `cullFace`/`frontFace` + `CULL_FACE`, VBO (`ARRAY_BUFFER`), IBO (`ELEMENT_ARRAY_BUFFER` with `UNSIGNED_BYTE`/`UNSIGNED_SHORT`/`UNSIGNED_INT`), WebGL 2.0 VAOs (`createVertexArray`/`bindVertexArray`), `drawArrays` & `drawElements` for `POINTS`, `LINES`, `LINE_STRIP`, `LINE_LOOP`, `TRIANGLES`, `TRIANGLE_STRIP`, and `TRIANGLE_FAN` with edge-function triangle rasterization and perspective-correct varying interpolation.
   - **Textures & FBOs:** 2D textures (`texImage2D`, `texSubImage2D`, `texParameteri`, `activeTexture`, `REPEAT`/`CLAMP_TO_EDGE`/`MIRRORED_REPEAT`, `NEAREST`/`LINEAR`), offscreen `WebGLFramebuffer` & `WebGLRenderbuffer` (`framebufferTexture2D`, `framebufferRenderbuffer`, `checkFramebufferStatus`), and `readPixels`.
2. **WebIDL & Canvas bindings (`src/js/DomHtmlBindings.cpp`):**
   - Registered global interfaces `WebGLRenderingContext`, `WebGL2RenderingContext`, `WebGLShader`, `WebGLProgram`, `WebGLBuffer`, `WebGLTexture`, `WebGLFramebuffer`, `WebGLRenderbuffer`, `WebGLUniformLocation`, `WebGLActiveInfo`, `WebGLShaderPrecisionFormat`, and `WebGLVertexArrayObject` with WebGL 1.0/2.0 constants on constructors and prototypes.
   - Wired `HTMLCanvasElement.getContext` and `OffscreenCanvas.getContext` for `'webgl'`, `'experimental-webgl'`, and `'webgl2'` with singleton identity, GC-traced `gl.canvas` back-references, and WHATWG HTML §4.12.5.1 mutual exclusion with `'2d'`.
3. **Conformance & Tests:**
   - Promoted all **6** WebGL `known_failure` cases in `tests/conformance/media_webgl_codecs.json` (`webgl_interface_constructors_exist`, `canvas_get_context_webgl_returns_context`, `canvas_get_context_webgl2_returns_context`, `offscreen_canvas_get_context_webgl`, `webgl_clear_color_and_read_pixels`, `webgl_shader_program_and_triangle_draw`) — bringing `webgl` to **8/8 (100%)** and `media_webgl_codecs.json` to **18/25 PASS**.
   - Promoted `webgl_clear_paints_canvas_green` in `tests/render/reftests.json` — bringing render reftests to **65/66 PASS** (only AVIF picture decode remains).
   - Added `tests/suites/275-webgl-software-pipeline.cpp` exercising WebGL 1/2 context locking, scissor/clear/errors, GLSL ES 1.00 & 3.00 compilation/diagnostics/reflection, `drawElements` + varying interpolation + `mat4` uniforms, `DEPTH_TEST`, `BLEND`, `sampler2D` textures, offscreen FBO render-to-texture, WebGL 2.0 VAOs, and canvas backing-store compositing.

## Verification

- `make test`: all engine unit suites pass including `tests/suites/275-webgl-software-pipeline.cpp`.
- `make verify-media-webgl-codecs`: **18/25** pass, **7** known (`audio_video=10/12`, `webcodecs=0/5`, **`webgl=8/8`**), **0** fail, **0** fixed.
- `make render-conformance`:
  - `tests/render/reftests.json`: **65/66** pass, **1** known (`a_data_uri_avif_image_scales_to_its_box`).
  - `tests/render/body-origin.json`: **40/41** pass, **1** known.
- `check_numbers.py`, `check_docs.py`, `check_suites.py`, `check_ci_parts.py`, `check_rows.py`: all 0 failures.
