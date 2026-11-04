/**
 * The worn print on the label, ported from the cassette-wear tool
 * (`filtro-cassette-desgastado-v7.html`, WebGL2).
 *
 * The shader is the tool's, character for character, and so are its defaults - the same
 * wear on the edges, scratches, fade, coffee and water rings, grain and seed, so a label
 * rendered today is the one rendered next month. The one addition is the read-back: the
 * tool draws on the screen, this draws into a 402x188 framebuffer, reads the pixels and
 * hands them back flipped (WebGL's origin is the bottom row, an ImageData's is the top).
 *
 * The filter is a convenience, never a reason to lose a label: a browser without WebGL2,
 * or a shader that will not compile, answers null and the plain print is used instead.
 */

const VERTEX_SHADER = `#version 300 es
in vec2 pos;
void main(){ gl_Position = vec4(pos, 0.0, 1.0); }`;

/** The tool's fragment shader, verbatim, plus the entry point the page calls. */
const FRAGMENT_SHADER = `#version 300 es
precision highp float;

uniform sampler2D iChannel0;
uniform vec3  iResolution;
uniform float uWear;     // desgaste de bordes y roces del azul
uniform float uScratch;  // arañazos (surcos)
uniform float uFade;     // descoloración / lavado
uniform float uCoffee;   // manchas de café (probabilidad de cada mancha)
uniform float uWater;    // manchas de agua (probabilidad de cada mancha)
uniform float uGrain;    // grano
uniform float uSeed;     // semilla de la textura aleatoria

out vec4 fragColor;

float hash(vec2 p){
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float vnoise(vec2 p){
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm(vec2 p){
    float v = 0.0;
    float a = 0.5;
    for(int i = 0; i < 5; i++){
        v += a * vnoise(p);
        p = p * 2.03 + 17.1;
        a *= 0.5;
    }
    return v;
}

// Surcos: x = núcleo oscuro, y = labio claro al lado del surco
vec2 scratches(vec2 uv){
    float core = 0.0;
    float lip = 0.0;
    for(int i = 0; i < 14; i++){
        float fi = float(i);
        float on   = step(hash(vec2(fi, 3.7 + uSeed)), uScratch);
        float x    = hash(vec2(fi, 1.3 + uSeed));
        float tilt = (hash(vec2(fi, 9.1 + uSeed)) - 0.5) * 0.05;
        float w    = 0.0012 + 0.0018 * hash(vec2(fi, 5.5 + uSeed));
        float dy   = uv.y - (x + tilt * uv.x);
        float len  = smoothstep(0.25, 0.5, vnoise(vec2(fi * 7.0 + uSeed, uv.x * 3.0)));
        float c    = 1.0 - smoothstep(0.0, w, abs(dy));
        float q    = (dy - w * 1.6) / (w * 0.9);
        float l    = exp(-q * q);
        core += on * len * c;
        lip  += on * len * l;
    }
    return vec2(clamp(core, 0.0, 1.0), clamp(lip, 0.0, 1.0));
}

// Mancha circular con borde irregular.
// x = relleno (interior más claro/más marcado), y = anillo del borde
vec2 blob(vec2 uv, float fi, float salt){
    float asp = iResolution.x / iResolution.y;
    vec2 c = vec2(hash(vec2(fi, salt + uSeed)), hash(vec2(fi + 0.5, salt * 1.7 + uSeed)));
    float r = 0.012 + 0.018 * hash(vec2(fi, salt * 2.3 + uSeed));
    vec2 p = (uv - c) * vec2(asp, 1.0);
    float rd = length(p) / r;
    rd += (fbm(p / r * 1.5 + salt) - 0.5) * 0.45;
    float fill = 1.0 - smoothstep(0.6, 1.0, rd);
    float ring = smoothstep(0.82, 0.96, rd) * (1.0 - smoothstep(0.96, 1.04, rd));
    return vec2(fill, ring);
}

void mainImage(out vec4 fragColor, in vec2 fragCoord){
    vec2 uv = fragCoord / iResolution.xy;
    vec3 col = texture(iChannel0, uv).rgb;

    // 1. Descoloración: menos saturación, bajo contraste, negros levantados
    float lum = dot(col, vec3(0.299, 0.587, 0.114));
    col = mix(vec3(lum), col, 1.0 - 0.5 * uFade);
    col = mix(vec3(0.5), col, 1.0 - 0.35 * uFade);
    col = col * (1.0 - 0.10 * uFade) + vec3(0.06, 0.07, 0.09) * uFade;

    float mott = fbm(uv * vec2(3.0, 5.0) + uSeed * 3.0);
    col *= 1.0 + (mott - 0.5) * 0.3 * uFade;

    // 2. Desgaste: el azul se pierde en los bordes y en roces, y aparece el cartón claro
    vec2 e = min(uv, 1.0 - uv);
    float edge = min(e.x, e.y);
    float n = fbm(uv * 18.0 + uSeed);
    float edgeWear = 1.0 - smoothstep(0.0, 0.09, edge + (n - 0.5) * 0.08);
    float scuff = smoothstep(0.6, 0.78, fbm(uv * vec2(6.0, 10.0) + uSeed * 2.0));
    float wear = clamp(edgeWear + scuff * 0.6, 0.0, 1.0) * uWear;
    vec3 paper = vec3(0.88, 0.86, 0.82) * (0.8 + 0.2 * n);
    col = mix(col, paper, wear * 0.9);

    // 3. Manchas de café: relleno marrón y anillo oscuro en el borde
    float cofF = 0.0;
    float cofR = 0.0;
    for(int i = 0; i < 4; i++){
        float fi = float(i);
        float on = step(hash(vec2(fi, 101.0 + uSeed)), uCoffee);
        vec2 b = blob(uv, fi, 11.0);
        cofF += on * b.x;
        cofR += on * b.y;
    }
    cofF = clamp(cofF, 0.0, 1.0);
    cofR = clamp(cofR, 0.0, 1.0);
    col = mix(col, col * vec3(0.72, 0.52, 0.32), cofF * 0.55);
    col = mix(col, col * vec3(0.42, 0.27, 0.14), cofR * 0.75);

    // 4. Manchas de agua: el color se aclara en el centro y queda un cerco tenue
    float watF = 0.0;
    float watR = 0.0;
    for(int i = 0; i < 4; i++){
        float fi = float(i);
        float on = step(hash(vec2(fi, 201.0 + uSeed)), uWater);
        vec2 b = blob(uv, fi, 37.0);
        watF += on * b.x;
        watR += on * b.y;
    }
    watF = clamp(watF, 0.0, 1.0);
    watR = clamp(watR, 0.0, 1.0);
    float lum2 = dot(col, vec3(0.299, 0.587, 0.114));
    col = mix(col, vec3(lum2) + 0.12, watF * 0.4);
    col *= 1.0 - watR * 0.25;

    // 5. Arañazos: surco oscuro y labio claro al lado
    vec2 sc = scratches(uv);
    col *= 1.0 - sc.x * 0.6;
    col = mix(col, vec3(0.95), sc.y * 0.45);

    // 6. Viñeta suave en los bordes, ligada al desgaste
    float vig = pow(16.0 * uv.x * (1.0 - uv.x) * uv.y * (1.0 - uv.y), 0.15);
    col *= mix(1.0, vig, 0.5 * uWear);

    // 7. Grano
    float g = hash(fragCoord + uSeed * 13.0) - 0.5;
    col += g * uGrain * 0.12;

    fragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}

void main(){ mainImage(fragColor, gl_FragCoord.xy); }
`;

/** The tool's defaults. One number changed is a different look, so none is a setting. */
const WEAR = {
  uWear: 0.6,
  uScratch: 0.6,
  uFade: 0.5,
  uCoffee: 0.5,
  uWater: 0.5,
  uGrain: 0.5,
  uSeed: 7,
} as const;

type Uniforms = Record<string, WebGLUniformLocation | null>;

interface Filter {
  gl: WebGL2RenderingContext;
  program: WebGLProgram;
  texture: WebGLTexture;
  uniforms: Uniforms;
}

function surface(width: number, height: number): HTMLCanvasElement | OffscreenCanvas {
  if (typeof OffscreenCanvas === 'function') return new OffscreenCanvas(width, height);
  const canvas = document.createElement('canvas');
  canvas.width = width;
  canvas.height = height;
  return canvas;
}

function compile(gl: WebGL2RenderingContext, type: number, source: string): WebGLShader {
  const shader = gl.createShader(type);
  if (shader === null) throw new Error('No shader for the worn label.');
  gl.shaderSource(shader, source);
  gl.compileShader(shader);
  if (gl.getShaderParameter(shader, gl.COMPILE_STATUS) !== true) {
    throw new Error(gl.getShaderInfoLog(shader) ?? "The worn label's shader would not compile.");
  }
  return shader;
}

function build(width: number, height: number): Filter | null {
  try {
    const canvas = surface(width, height);
    const gl = canvas.getContext('webgl2', {
      preserveDrawingBuffer: true,
      premultipliedAlpha: false,
    }) as WebGL2RenderingContext | null;
    if (gl === null) return null;

    const program = gl.createProgram();
    if (program === null) return null;
    gl.attachShader(program, compile(gl, gl.VERTEX_SHADER, VERTEX_SHADER));
    gl.attachShader(program, compile(gl, gl.FRAGMENT_SHADER, FRAGMENT_SHADER));
    gl.bindAttribLocation(program, 0, 'pos');
    gl.linkProgram(program);
    if (gl.getProgramParameter(program, gl.LINK_STATUS) !== true) return null;

    const buffer = gl.createBuffer();
    gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
    gl.bufferData(
      gl.ARRAY_BUFFER,
      new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]),
      gl.STATIC_DRAW,
    );
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);

    const texture = gl.createTexture();
    if (texture === null) return null;
    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);

    const uniforms: Uniforms = {};
    for (const name of ['iChannel0', 'iResolution', ...Object.keys(WEAR)]) {
      uniforms[name] = gl.getUniformLocation(program, name);
    }

    return { gl, program, texture, uniforms };
  } catch {
    return null;
  }
}

/* One context for the whole run: the writes are serialised, and a shader compile per Track
 * would cost more than the render. A failure is cached too, so a browser without WebGL2 is
 * asked once. */
let cached: { width: number; height: number; filter: Filter | null } | null = null;

function filterFor(width: number, height: number): Filter | null {
  if (cached === null || cached.width !== width || cached.height !== height) {
    cached = { width, height, filter: build(width, height) };
  }
  return cached.filter;
}

/**
 * Paint the wear over a canvas and hand back its pixels top-down, RGBA, ready for
 * `putImageData`; null when this browser cannot run the shader.
 */
export function wearLabel(
  source: HTMLCanvasElement | OffscreenCanvas,
  width: number,
  height: number,
): Uint8ClampedArray<ArrayBuffer> | null {
  const filter = filterFor(width, height);
  if (filter === null) return null;

  const { gl, program, texture, uniforms } = filter;
  try {
    gl.viewport(0, 0, width, height);
    gl.useProgram(program);
    gl.activeTexture(gl.TEXTURE0);
    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, true);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, source);
    gl.uniform1i(uniforms.iChannel0, 0);
    gl.uniform3f(uniforms.iResolution, width, height, 1);
    for (const [name, value] of Object.entries(WEAR)) {
      gl.uniform1f(uniforms[name], value);
    }
    gl.drawArrays(gl.TRIANGLE_STRIP, 0, 4);

    const pixels = new Uint8Array(width * height * 4);
    gl.readPixels(0, 0, width, height, gl.RGBA, gl.UNSIGNED_BYTE, pixels);

    const row = width * 4;
    const flipped = new Uint8ClampedArray(pixels.length);
    for (let y = 0; y < height; y += 1) {
      flipped.set(pixels.subarray((height - 1 - y) * row, (height - y) * row), y * row);
    }
    return flipped;
  } catch {
    return null;
  }
}
