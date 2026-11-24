# Finding 279 — WebGL 1.0/2.0 rendering contexts, GLSL ES shaders, and rasterizer were missing

Phase: [`docs/phases/275-webgl-software-pipeline.md`](../phases/275-webgl-software-pipeline.md)

## What was missing

Before Phase 275, `HTMLCanvasElement.getContext()` and `OffscreenCanvas.getContext()` in `src/js/DomHtmlBindings.cpp` accepted only `"2d"`, returning `null` for `"webgl"`, `"experimental-webgl"`, and `"webgl2"`. None of the Khronos WebGL 1.0/2.0 global interface constructors (`WebGLRenderingContext`, `WebGL2RenderingContext`, `WebGLShader`, `WebGLProgram`, `WebGLBuffer`, `WebGLTexture`, `WebGLFramebuffer`, `WebGLRenderbuffer`, `WebGLUniformLocation`, `WebGLActiveInfo`, `WebGLShaderPrecisionFormat`, `WebGLVertexArrayObject`) were registered on `Window`.

As a result:

- 6 of 8 `webgl` conformance checks in `tests/conformance/media_webgl_codecs.json` failed as `known_failure` (`webgl_interface_constructors_exist`, `canvas_get_context_webgl_returns_context`, `canvas_get_context_webgl2_returns_context`, `offscreen_canvas_get_context_webgl`, `webgl_clear_color_and_read_pixels`, `webgl_shader_program_and_triangle_draw`).
- The end-to-end pixel reftest `webgl_clear_paints_canvas_green` in `tests/render/reftests.json` painted white (`#ffffff`) instead of `#00ff00` because `getContext('webgl')` returned `null`.

## Resolution

Implemented a self-contained C++20 software WebGL 1.0/2.0 pipeline (`include/litebird/render/WebGL.hpp`, `src/render/WebGL.cpp`) and full WebIDL bindings in `src/js/DomHtmlBindings.cpp`:

- **Shared canvas backing store:** `WebGLContext` writes directly into the `<canvas>` / `OffscreenCanvas` `CanvasRenderingContext2D::image_data()` pixel buffer (`0xAARRGGBB`, converting bottom-up WebGL Y coordinates to top-down canvas rows), so WebGL rendering automatically composites through LiteBird's existing `BitmapRenderer`, `TerminalRenderer`, `toDataURL()`, and `toBlob()` paths without extra copies.
- **GLSL ES 1.00 & 3.00 compiler/evaluator:** Preprocesses `#define`/`#ifdef`, lexes and parses GLSL ES vertex and fragment shaders into an AST, reflects active attributes/uniforms, reports compile/link diagnostics via `COMPILE_STATUS`/`LINK_STATUS`/`getShaderInfoLog`/`getProgramInfoLog`, and evaluates shaders under a bounded execution step limit.
- **Software rasterizer:** Supports `clear`, `viewport`, `scissor`, `colorMask`, `DEPTH_TEST`, `BLEND`, `CULL_FACE`, `ARRAY_BUFFER`/`ELEMENT_ARRAY_BUFFER`, WebGL 2.0 VAOs, `drawArrays`/`drawElements` across point/line/triangle topologies with perspective-correct varying interpolation, 2D textures (`texImage2D`/`texSubImage2D`/`sampler2D`), offscreen FBOs/renderbuffers, and `readPixels`.
- **Context exclusivity & GC safety:** `HTMLCanvasElement.getContext` and `OffscreenCanvas.getContext` enforce WHATWG HTML §4.12.5.1 singleton/mutual-exclusion semantics across `'2d'`, `'webgl'`/`'experimental-webgl'`, and `'webgl2'`, with GC-traced `gl.canvas` and `OffscreenCanvasState::context_obj` links.

## Remaining limitations

- Full WebGL 2.0 extensions such as 3D/array/cubemap textures, instanced drawing (`drawArraysInstanced`/`drawElementsInstanced`), transform feedback, uniform buffer objects, multisampled renderbuffers (`renderbufferStorageMultisample`/`blitFramebuffer`), and `gl_FragCoord` derivatives (`dFdx`/`dFdy`) are not yet implemented.
- Shader execution runs on a CPU AST interpreter rather than a JIT/GPU backend, which is intended for deterministic headless conformance and modest canvas sizes.
