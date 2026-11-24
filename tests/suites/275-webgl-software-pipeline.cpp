// Phase 275, finding 279: WebGL 1.0/2.0 software rendering pipeline.
//
// Khronos WebGL 1.0 §§2, 5 and WebGL 2.0 §§2, 3 define WebGLRenderingContext
// and WebGL2RenderingContext on HTMLCanvasElement and OffscreenCanvas.
// This suite exercises:
//  * Context creation ('webgl', 'experimental-webgl', 'webgl2') on both
//    HTMLCanvasElement and OffscreenCanvas, singleton identity, and mutual
//    exclusion with '2d';
//  * Interface constructors and constants on WebGLRenderingContext /
//    WebGL2RenderingContext and resource interfaces;
//  * clearColor / clear / scissor / colorMask / viewport / readPixels;
//  * GLSL ES 1.00 and GLSL ES 3.00 (#version 300 es) vertex & fragment shaders,
//    compile/link diagnostics, active attrib/uniform reflection, varying
//    interpolation, uniforms (vec4 + mat4), and drawArrays / drawElements;
//  * Depth testing (DEPTH_TEST), alpha blending (BLEND), textures (texImage2D /
//    texSubImage2D + sampler2D), offscreen FBO render-to-texture, WebGL2 VAOs,
//    error reporting, and page compositing via the shared canvas backing store.

TEST(webgl_software_pipeline_renders_shaders_textures_depth_and_fbos) {
    TestPage page;
    JsInterpreter& js = page.js;

    // 1. Global WebGL interface constructors exist and context creation follows
    //    WHATWG HTML §4.12.5.1 singleton & mutual exclusion rules.
    assert(js.eval(
        "typeof WebGLRenderingContext === 'function' && "
        "typeof WebGL2RenderingContext === 'function' && "
        "typeof WebGLShader === 'function' && "
        "typeof WebGLProgram === 'function' && "
        "typeof WebGLBuffer === 'function' && "
        "typeof WebGLTexture === 'function' && "
        "typeof WebGLFramebuffer === 'function' && "
        "typeof WebGLRenderbuffer === 'function' && "
        "typeof WebGLUniformLocation === 'function' && "
        "typeof WebGLActiveInfo === 'function' && "
        "typeof WebGLShaderPrecisionFormat === 'function' && "
        "typeof WebGLVertexArrayObject === 'function'").is_truthy());

    assert(js.eval(
        "(function() {"
        "  var c1 = document.createElement('canvas'); c1.width = 32; c1.height = 24;"
        "  var gl = c1.getContext('webgl');"
        "  if (!(gl instanceof WebGLRenderingContext)) return false;"
        "  if (gl.canvas !== c1 || gl.drawingBufferWidth !== 32 || gl.drawingBufferHeight !== 24) return false;"
        "  if (c1.getContext('webgl') !== gl || c1.getContext('experimental-webgl') !== gl) return false;"
        "  if (c1.getContext('2d') !== null || c1.getContext('webgl2') !== null) return false;"
        "  var c2 = document.createElement('canvas'); c2.width = 20; c2.height = 10;"
        "  var gl2 = c2.getContext('webgl2');"
        "  if (!(gl2 instanceof WebGL2RenderingContext)) return false;"
        "  if (c2.getContext('webgl2') !== gl2 || c2.getContext('webgl') !== null || c2.getContext('2d') !== null) return false;"
        "  var oc = new OffscreenCanvas(16, 16);"
        "  var ogl = oc.getContext('webgl');"
        "  if (!(ogl instanceof WebGLRenderingContext) || ogl.canvas !== oc) return false;"
        "  if (oc.getContext('2d') !== null || oc.getContext('webgl') !== ogl) return false;"
        "  var oc2 = new OffscreenCanvas(16, 16);"
        "  var ogl2 = oc2.getContext('webgl2');"
        "  return (ogl2 instanceof WebGL2RenderingContext) && oc2.getContext('webgl2') === ogl2;"
        "})()").is_truthy());

    // 2. Clear + scissor + colorMask + readPixels + getParameter + getError.
    assert(js.eval(
        "(function() {"
        "  var c = document.createElement('canvas'); c.width = 16; c.height = 16;"
        "  var gl = c.getContext('webgl');"
        "  gl.clearColor(0, 0, 1, 1);"
        "  gl.clear(gl.COLOR_BUFFER_BIT);"
        "  gl.enable(gl.SCISSOR_TEST);"
        "  gl.scissor(4, 4, 8, 8);"
        "  gl.clearColor(0, 1, 0, 1);"
        "  gl.clear(gl.COLOR_BUFFER_BIT);"
        "  gl.disable(gl.SCISSOR_TEST);"
        "  var pxOut = new Uint8Array(4), pxIn = new Uint8Array(4);"
        "  gl.readPixels(1, 1, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pxOut);"
        "  gl.readPixels(8, 8, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pxIn);"
        "  if (pxOut[0] !== 0 || pxOut[1] !== 0 || pxOut[2] !== 255 || pxOut[3] !== 255) return false;"
        "  if (pxIn[0] !== 0 || pxIn[1] !== 255 || pxIn[2] !== 0 || pxIn[3] !== 255) return false;"
        "  var ver = gl.getParameter(gl.VERSION);"
        "  var vp = gl.getParameter(gl.VIEWPORT);"
        "  var prec = gl.getShaderPrecisionFormat(gl.FRAGMENT_SHADER, gl.HIGH_FLOAT);"
        "  if (typeof ver !== 'string' || ver.indexOf('WebGL 1.0') !== 0) return false;"
        "  if (!vp || vp[2] !== 16 || vp[3] !== 16) return false;"
        "  if (!(prec instanceof WebGLShaderPrecisionFormat) || prec.precision <= 0) return false;"
        "  gl.enable(0xDEAD);"
        "  return gl.getError() === gl.INVALID_ENUM && gl.getError() === gl.NO_ERROR;"
        "})()").is_truthy());

    // 3. GLSL ES 1.00 shaders, compile error reporting, reflection, varying
    //    interpolation, mat4 uniform, and drawElements (UNSIGNED_SHORT).
    assert(js.eval(
        "(function() {"
        "  var c = document.createElement('canvas'); c.width = 32; c.height = 32;"
        "  var gl = c.getContext('webgl');"
        "  var bad = gl.createShader(gl.VERTEX_SHADER);"
        "  gl.shaderSource(bad, 'this is not valid glsl {{{');"
        "  gl.compileShader(bad);"
        "  if (gl.getShaderParameter(bad, gl.COMPILE_STATUS) !== false) return false;"
        "  if (!gl.getShaderInfoLog(bad)) return false;"
        "  var vs = gl.createShader(gl.VERTEX_SHADER);"
        "  gl.shaderSource(vs,"
        "    'attribute vec2 aPos; attribute vec3 aCol; uniform mat4 uMat; varying vec3 vCol;\\n'"
        "    + 'void main() { vCol = aCol; gl_Position = uMat * vec4(aPos, 0.0, 1.0); }');"
        "  gl.compileShader(vs);"
        "  var fs = gl.createShader(gl.FRAGMENT_SHADER);"
        "  gl.shaderSource(fs,"
        "    'precision mediump float; varying vec3 vCol; uniform vec4 uScale;\\n'"
        "    + 'void main() { gl_FragColor = vec4(vCol, 1.0) * uScale; }');"
        "  gl.compileShader(fs);"
        "  var prog = gl.createProgram();"
        "  gl.attachShader(prog, vs); gl.attachShader(prog, fs); gl.linkProgram(prog);"
        "  if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) return false;"
        "  if (gl.getProgramParameter(prog, gl.ACTIVE_ATTRIBUTES) !== 2) return false;"
        "  if (gl.getProgramParameter(prog, gl.ACTIVE_UNIFORMS) !== 2) return false;"
        "  var ai = gl.getActiveAttrib(prog, 0);"
        "  var ui = gl.getActiveUniform(prog, 0);"
        "  if (!(ai instanceof WebGLActiveInfo) || !(ui instanceof WebGLActiveInfo)) return false;"
        "  gl.useProgram(prog);"
        "  var posBuf = gl.createBuffer();"
        "  gl.bindBuffer(gl.ARRAY_BUFFER, posBuf);"
        "  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1,-1, 1,-1, 1,1, -1,1]), gl.STATIC_DRAW);"
        "  var aPos = gl.getAttribLocation(prog, 'aPos');"
        "  gl.enableVertexAttribArray(aPos);"
        "  gl.vertexAttribPointer(aPos, 2, gl.FLOAT, false, 0, 0);"
        "  var colBuf = gl.createBuffer();"
        "  gl.bindBuffer(gl.ARRAY_BUFFER, colBuf);"
        "  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([1,0,0, 0,1,0, 0,0,1, 1,1,0]), gl.STATIC_DRAW);"
        "  var aCol = gl.getAttribLocation(prog, 'aCol');"
        "  gl.enableVertexAttribArray(aCol);"
        "  gl.vertexAttribPointer(aCol, 3, gl.FLOAT, false, 0, 0);"
        "  var idxBuf = gl.createBuffer();"
        "  gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, idxBuf);"
        "  gl.bufferData(gl.ELEMENT_ARRAY_BUFFER, new Uint16Array([0,1,2, 0,2,3]), gl.STATIC_DRAW);"
        "  gl.uniformMatrix4fv(gl.getUniformLocation(prog, 'uMat'), false,"
        "    new Float32Array([1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1]));"
        "  gl.uniform4f(gl.getUniformLocation(prog, 'uScale'), 1.0, 1.0, 1.0, 1.0);"
        "  gl.viewport(0, 0, 32, 32);"
        "  gl.drawElements(gl.TRIANGLES, 6, gl.UNSIGNED_SHORT, 0);"
        "  var bl = new Uint8Array(4), br = new Uint8Array(4), tr = new Uint8Array(4);"
        "  gl.readPixels(1, 1, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, bl);"
        "  gl.readPixels(30, 1, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, br);"
        "  gl.readPixels(30, 30, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, tr);"
        "  return bl[0] > 200 && bl[1] < 60 && br[1] > 200 && br[0] < 60 && tr[2] > 200 && gl.getError() === gl.NO_ERROR;"
        "})()").is_truthy());

    // 4. Depth test and alpha blending.
    assert(js.eval(
        "(function() {"
        "  var c = document.createElement('canvas'); c.width = 16; c.height = 16;"
        "  var gl = c.getContext('webgl');"
        "  var vs = gl.createShader(gl.VERTEX_SHADER);"
        "  gl.shaderSource(vs, 'attribute vec3 p; void main(){ gl_Position = vec4(p, 1.0); }');"
        "  gl.compileShader(vs);"
        "  var fs = gl.createShader(gl.FRAGMENT_SHADER);"
        "  gl.shaderSource(fs, 'precision mediump float; uniform vec4 uC; void main(){ gl_FragColor = uC; }');"
        "  gl.compileShader(fs);"
        "  var prog = gl.createProgram();"
        "  gl.attachShader(prog, vs); gl.attachShader(prog, fs); gl.linkProgram(prog); gl.useProgram(prog);"
        "  var buf = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, buf);"
        "  var loc = gl.getAttribLocation(prog, 'p'); gl.enableVertexAttribArray(loc);"
        "  var uC = gl.getUniformLocation(prog, 'uC');"
        "  gl.enable(gl.DEPTH_TEST);"
        "  gl.clearColor(0, 0, 0, 1); gl.clearDepth(1.0);"
        "  gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);"
        "  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1,-1,-0.5, 1,-1,-0.5, -1,1,-0.5, -1,1,-0.5, 1,-1,-0.5, 1,1,-0.5]), gl.STATIC_DRAW);"
        "  gl.vertexAttribPointer(loc, 3, gl.FLOAT, false, 0, 0);"
        "  gl.uniform4f(uC, 1, 0, 0, 1); gl.drawArrays(gl.TRIANGLES, 0, 6);"
        "  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1,-1,0.5, 1,-1,0.5, -1,1,0.5, -1,1,0.5, 1,-1,0.5, 1,1,0.5]), gl.STATIC_DRAW);"
        "  gl.vertexAttribPointer(loc, 3, gl.FLOAT, false, 0, 0);"
        "  gl.uniform4f(uC, 0, 0, 1, 1); gl.drawArrays(gl.TRIANGLES, 0, 6);"
        "  var px = new Uint8Array(4); gl.readPixels(8, 8, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);"
        "  if (px[0] !== 255 || px[2] !== 0) return false;"
        "  gl.disable(gl.DEPTH_TEST);"
        "  gl.enable(gl.BLEND); gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);"
        "  gl.uniform4f(uC, 0, 1, 0, 0.5); gl.drawArrays(gl.TRIANGLES, 0, 6);"
        "  gl.readPixels(8, 8, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);"
        "  return Math.abs(px[0] - 128) <= 2 && Math.abs(px[1] - 128) <= 2 && px[2] === 0;"
        "})()").is_truthy());

    // 5. WebGL 2.0 + GLSL ES 3.00 (#version 300 es), VAO, texture sampling, and
    //    FBO render-to-texture roundtrip.
    assert(js.eval(
        "(function() {"
        "  var c = document.createElement('canvas'); c.width = 16; c.height = 16;"
        "  var gl = c.getContext('webgl2');"
        "  if (gl.getParameter(gl.VERSION).indexOf('WebGL 2.0') !== 0) return false;"
        "  var tex = gl.createTexture(); gl.bindTexture(gl.TEXTURE_2D, tex);"
        "  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);"
        "  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);"
        "  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, 8, 8, 0, gl.RGBA, gl.UNSIGNED_BYTE, null);"
        "  var fbo = gl.createFramebuffer(); gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);"
        "  gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, tex, 0);"
        "  if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE) return false;"
        "  gl.viewport(0, 0, 8, 8);"
        "  gl.clearColor(1, 1, 0, 1); gl.clear(gl.COLOR_BUFFER_BIT);"
        "  gl.bindFramebuffer(gl.FRAMEBUFFER, null);"
        "  var vs = gl.createShader(gl.VERTEX_SHADER);"
        "  gl.shaderSource(vs,"
        "    '#version 300 es\\n'"
        "    + 'in vec2 aPos; in vec2 aUv; out vec2 vUv;\\n'"
        "    + 'void main() { vUv = aUv; gl_Position = vec4(aPos, 0.0, 1.0); }');"
        "  gl.compileShader(vs);"
        "  var fs = gl.createShader(gl.FRAGMENT_SHADER);"
        "  gl.shaderSource(fs,"
        "    '#version 300 es\\n'"
        "    + 'precision mediump float; in vec2 vUv; uniform sampler2D uTex; out vec4 fragColor;\\n'"
        "    + 'void main() { fragColor = texture(uTex, vUv); }');"
        "  gl.compileShader(fs);"
        "  var prog = gl.createProgram();"
        "  gl.attachShader(prog, vs); gl.attachShader(prog, fs); gl.linkProgram(prog);"
        "  if (!gl.getProgramParameter(prog, gl.LINK_STATUS)) return false;"
        "  gl.useProgram(prog);"
        "  var vao = gl.createVertexArray(); gl.bindVertexArray(vao);"
        "  var buf = gl.createBuffer(); gl.bindBuffer(gl.ARRAY_BUFFER, buf);"
        "  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array(["
        "    -1,-1, 0,0,  1,-1, 1,0,  -1,1, 0,1,"
        "    -1,1,  0,1,  1,-1, 1,0,   1,1, 1,1"
        "  ]), gl.STATIC_DRAW);"
        "  var aPos = gl.getAttribLocation(prog, 'aPos');"
        "  var aUv = gl.getAttribLocation(prog, 'aUv');"
        "  gl.enableVertexAttribArray(aPos); gl.vertexAttribPointer(aPos, 2, gl.FLOAT, false, 16, 0);"
        "  gl.enableVertexAttribArray(aUv);  gl.vertexAttribPointer(aUv,  2, gl.FLOAT, false, 16, 8);"
        "  gl.activeTexture(gl.TEXTURE0); gl.bindTexture(gl.TEXTURE_2D, tex);"
        "  gl.uniform1i(gl.getUniformLocation(prog, 'uTex'), 0);"
        "  gl.viewport(0, 0, 16, 16);"
        "  gl.drawArrays(gl.TRIANGLES, 0, 6);"
        "  var px = new Uint8Array(4); gl.readPixels(8, 8, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);"
        "  return px[0] === 255 && px[1] === 255 && px[2] === 0 && px[3] === 255 && gl.getError() === gl.NO_ERROR;"
        "})()").is_truthy());

    // 6. Compositing: WebGL drawing shares the <canvas> backing store so page
    //    painting sees the WebGL pixels immediately.
    js.eval(
        "var c = document.createElement('canvas'); c.id = 'glc'; c.width = 12; c.height = 12;"
        "document.body.appendChild(c);"
        "var gl = c.getContext('webgl');"
        "gl.clearColor(0, 1, 0, 1); gl.clear(gl.COLOR_BUFFER_BIT);");
    auto el = page.document().get_element_by_id("glc");
    assert(el != nullptr && el->canvas_context() != nullptr && el->canvas_context()->image_data() != nullptr);
    auto img = el->canvas_context()->image_data();
    assert(img->width == 12 && img->height == 12);
    assert(img->get_pixel(6, 6) == 0xFF00FF00u);
}
