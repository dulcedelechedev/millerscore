/**
 * The boot scene: gold light ribbons swirling in depth, a spiral that draws itself (the mark),
 * a burst, a soft flash. Hand-written WebGL2, no libraries.
 *
 * Runs in a dedicated Worker on an OffscreenCanvas (boot/boot.js starts it), so the app's own
 * startup work on the main thread cannot make it stutter. Everything is a function of the
 * timeline time t (seconds since the jingle's start) and the cue points from assets/boot/boot.json:
 *
 *   sparkles[]   intro glints            swellStart  ribbons start converging in depth
 *   boom         ribbons collide          spiralStart the spiral starts drawing itself
 *   arpeggio[]   one spiral segment and a spark per onset
 *   logoHit      burst, the wordmark lands (the DOM does the wordmark)
 *   flash        soft flash               fadeOut / end  handled by the page (overlay fade)
 *
 * Messages in:  init {canvas, cues, mark, layout, mode}, start {t0}, env {rms, low, rate},
 *               resize {layout}, stop.
 * Messages out: ready, error {message}, done {frames, maxGap, scale}.
 */
(function() {
"use strict";

var TAU = Math.PI * 2;
function clamp(x, a, b) { return x < a ? a : x > b ? b : x; }
function sat(x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
function smooth(a, b, x) { var t = sat((x - a) / (b - a)); return t * t * (3 - 2 * t); }
function easeOut3(x) { x = sat(x); return 1 - (1 - x) * (1 - x) * (1 - x); }
function mix(a, b, t) { return a + (b - a) * t; }
/** deterministic random, so every boot looks the same */
function rng(seed) {
	var s = seed >>> 0;
	return function() {
		s = (s + 0x6D2B79F5) >>> 0;
		var t = s;
		t = Math.imul(t ^ (t >>> 15), t | 1);
		t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
		return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
	};
}

// ---- shaders ---------------------------------------------------------------------------------

var VS_STRIP = "#version 300 es\n" +
	"in vec2 a_pos; in vec4 a_col; in vec4 a_w;\n" +
	"uniform vec2 u_res;\n" +
	"out vec4 v_col; out vec4 v_w;\n" +
	"void main() { v_col = a_col; v_w = a_w;\n" +
	"  gl_Position = vec4(a_pos / u_res * vec2(2.0, -2.0) + vec2(-1.0, 1.0), 0.0, 1.0); }";
// a_w: x = across (-1..1), y = half width px, z = core radius px, w = halo decay px
var FS_STRIP = "#version 300 es\nprecision highp float;\n" +
	"in vec4 v_col; in vec4 v_w; out vec4 o;\n" +
	"void main() {\n" +
	"  float d = abs(v_w.x) * v_w.y;\n" +
	"  float core = exp(-d * d / (v_w.z * v_w.z));\n" +
	"  float halo = exp(-d / v_w.w) * (1.0 - smoothstep(0.55, 1.0, abs(v_w.x)));\n" +
	"  vec3 c = v_col.rgb * (core + 0.32 * halo) + vec3(core * core) * v_col.a;\n" +
	"  o = vec4(c, 1.0); }";

var VS_SPRITE = "#version 300 es\n" +
	"in vec2 a_pos; in vec2 a_uv; in vec4 a_col; in float a_star;\n" +
	"uniform vec2 u_res;\n" +
	"out vec2 v_uv; out vec4 v_col; out float v_star;\n" +
	"void main() { v_uv = a_uv; v_col = a_col; v_star = a_star;\n" +
	"  gl_Position = vec4(a_pos / u_res * vec2(2.0, -2.0) + vec2(-1.0, 1.0), 0.0, 1.0); }";
// a round orb with a hot centre; a_star > 0 adds a 4-point flare (the glints)
var FS_SPRITE = "#version 300 es\nprecision highp float;\n" +
	"in vec2 v_uv; in vec4 v_col; in float v_star; out vec4 o;\n" +
	"void main() {\n" +
	"  vec2 p = v_uv; float r = length(p);\n" +
	"  float edge = 1.0 - smoothstep(0.7, 1.0, r);\n" +
	"  float orb = exp(-r * r * 9.0 * (1.0 + 4.0 * step(0.001, v_star))) + 0.18 * exp(-r * 5.0) * edge;\n" +
	"  float ax = abs(p.x), ay = abs(p.y);\n" +
	"  float arms = exp(-ay * 55.0) * pow(1.0 - min(ax, 1.0), 3.0) + exp(-ax * 55.0) * pow(1.0 - min(ay, 1.0), 3.0);\n" +
	"  float d1 = abs(p.x - p.y) * 0.7071, d2 = abs(p.x + p.y) * 0.7071;\n" +
	"  arms += 0.35 * (exp(-d1 * 70.0) + exp(-d2 * 70.0)) * pow(max(1.0 - r, 0.0), 4.0);\n" +
	"  vec3 c = v_col.rgb * (orb * edge + v_star * arms) + vec3(exp(-r * r * 60.0)) * v_col.a;\n" +
	"  o = vec4(c, 1.0); }";

var VS_QUAD = "#version 300 es\n" +
	"in vec2 a_pos; out vec2 v_uv;\n" +
	"void main() { v_uv = a_pos * 0.5 + 0.5; gl_Position = vec4(a_pos, 0.0, 1.0); }";
// dual-filter bloom: 5-tap down, 8-tap up
var FS_DOWN = "#version 300 es\nprecision highp float;\n" +
	"in vec2 v_uv; uniform sampler2D u_tex; uniform vec2 u_texel; out vec4 o;\n" +
	"void main() { vec2 h = u_texel;\n" +
	"  vec3 c = texture(u_tex, v_uv).rgb * 4.0;\n" +
	"  c += texture(u_tex, v_uv - h).rgb + texture(u_tex, v_uv + h).rgb;\n" +
	"  c += texture(u_tex, v_uv + vec2(h.x, -h.y)).rgb + texture(u_tex, v_uv - vec2(h.x, -h.y)).rgb;\n" +
	"  o = vec4(c / 8.0, 1.0); }";
var FS_UP = "#version 300 es\nprecision highp float;\n" +
	"in vec2 v_uv; uniform sampler2D u_tex; uniform vec2 u_texel; uniform float u_gain; out vec4 o;\n" +
	"void main() { vec2 h = u_texel;\n" +
	"  vec3 c = texture(u_tex, v_uv + vec2(-h.x * 2.0, 0.0)).rgb + texture(u_tex, v_uv + vec2(h.x * 2.0, 0.0)).rgb;\n" +
	"  c += texture(u_tex, v_uv + vec2(0.0, -h.y * 2.0)).rgb + texture(u_tex, v_uv + vec2(0.0, h.y * 2.0)).rgb;\n" +
	"  c += (texture(u_tex, v_uv + vec2(-h.x, h.y)).rgb + texture(u_tex, v_uv + vec2(h.x, h.y)).rgb) * 2.0;\n" +
	"  c += (texture(u_tex, v_uv + vec2(-h.x, -h.y)).rgb + texture(u_tex, v_uv + vec2(h.x, -h.y)).rgb) * 2.0;\n" +
	"  o = vec4(c / 12.0 * u_gain, 1.0); }";
// background, burst rays, shockwave, lens streak, bloom mix, tone map, flash, vignette, dither
var FS_COMPOSITE = "#version 300 es\nprecision highp float;\n" +
	"in vec2 v_uv; out vec4 o;\n" +
	"uniform sampler2D u_scene; uniform sampler2D u_bloom;\n" +
	"uniform vec2 u_res; uniform vec2 u_center; uniform float u_markR;\n" +
	"uniform float u_haze; uniform float u_bloomK; uniform float u_exposure; uniform float u_flash;\n" +
	"uniform vec3 u_rays; uniform vec3 u_ring; uniform vec2 u_streak; uniform float u_fade; uniform float u_time;\n" +
	"float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }\n" +
	"void main() {\n" +
	"  vec2 px = vec2(v_uv.x, 1.0 - v_uv.y) * u_res;\n" +
	"  vec2 d = px - u_center; float r = length(d);\n" +
	"  float diag = length(u_res);\n" +
	"  vec3 gold = vec3(1.0, 0.64, 0.22);\n" +
	// warm near-black field, a little lighter in the middle, and a haze that grows with the swell
	"  float fall = r / diag;\n" +
	"  vec3 c = mix(vec3(0.030, 0.022, 0.013), vec3(0.006, 0.004, 0.003), smoothstep(0.0, 0.75, fall));\n" +
	"  c += gold * u_haze * (0.55 * exp(-r / (u_markR * 2.4)) + 0.25 * exp(-r / (u_markR * 7.0)));\n" +
	// burst rays: x = strength, y = rotation, z = reach (px)
	"  if (u_rays.x > 0.0) {\n" +
	"    float a = atan(d.y, d.x);\n" +
	"    float f = 0.5 + 0.25 * sin(a * 11.0 + u_rays.y) + 0.25 * sin(a * 27.0 - u_rays.y * 1.7);\n" +
	"    f = pow(clamp(f, 0.0, 1.0), 5.0) + 0.35 * pow(0.5 + 0.5 * sin(a * 53.0 + u_rays.y * 0.6), 12.0);\n" +
	"    c += gold * u_rays.x * f * exp(-r / u_rays.z) * smoothstep(u_markR * 0.4, u_markR * 1.3, r);\n" +
	"  }\n" +
	// shockwave ring: x = strength, y = radius, z = width
	"  if (u_ring.x > 0.0) { float q = (r - u_ring.y) / u_ring.z; c += vec3(1.0, 0.78, 0.45) * u_ring.x * exp(-q * q); }\n" +
	// anamorphic streak through the mark's centre: x = strength, y = half height
	"  if (u_streak.x > 0.0) { float dy = abs(d.y);\n" +
	"    c += vec3(1.0, 0.8, 0.5) * u_streak.x * (exp(-dy / u_streak.y) * exp(-abs(d.x) / (u_res.x * 0.35)) + 0.25 * exp(-dy / (u_streak.y * 8.0)) * exp(-abs(d.x) / (u_res.x * 0.2)));\n" +
	"  }\n" +
	"  c += texture(u_scene, v_uv).rgb + texture(u_bloom, v_uv).rgb * u_bloomK;\n" +
	"  c += vec3(1.0, 0.78, 0.5) * u_flash * (0.18 + 0.82 * exp(-r / (u_markR * 5.0)));\n" +
	"  c = 1.0 - exp(-c * u_exposure);\n" +
	"  c *= mix(1.0, 0.72, smoothstep(0.35, 0.95, length(v_uv - 0.5) * 1.35));\n" +
	"  c *= u_fade;\n" +
	"  c += (hash(px + fract(u_time) * 37.0) - 0.5) / 255.0;\n" +
	"  o = vec4(c, 1.0); }";

// ---- GL helpers ------------------------------------------------------------------------------

function Renderer(canvas) {
	var gl = canvas.getContext("webgl2", { alpha: false, antialias: false, depth: false, stencil: false,
		premultipliedAlpha: false, preserveDrawingBuffer: false, powerPreference: "high-performance" });
	if (!gl) throw new Error("WebGL2 is not available");
	this.gl = gl;
	this.canvas = canvas;
	this.hdr = !!gl.getExtension("EXT_color_buffer_float");
	function shader(type, src) {
		var s = gl.createShader(type);
		gl.shaderSource(s, src);
		gl.compileShader(s);
		if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s));
		return s;
	}
	function program(vs, fs, attrs) {
		var p = gl.createProgram();
		gl.attachShader(p, shader(gl.VERTEX_SHADER, vs));
		gl.attachShader(p, shader(gl.FRAGMENT_SHADER, fs));
		for (var i = 0; i < attrs.length; i++) gl.bindAttribLocation(p, i, attrs[i]);
		gl.linkProgram(p);
		if (!gl.getProgramParameter(p, gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(p));
		var u = {}, n = gl.getProgramParameter(p, gl.ACTIVE_UNIFORMS);
		for (var k = 0; k < n; k++) {
			var info = gl.getActiveUniform(p, k);
			u[info.name] = gl.getUniformLocation(p, info.name);
		}
		return { p: p, u: u };
	}
	this.strip = program(VS_STRIP, FS_STRIP, ["a_pos", "a_col", "a_w"]);
	this.sprite = program(VS_SPRITE, FS_SPRITE, ["a_pos", "a_uv", "a_col", "a_star"]);
	this.down = program(VS_QUAD, FS_DOWN, ["a_pos"]);
	this.up = program(VS_QUAD, FS_UP, ["a_pos"]);
	this.comp = program(VS_QUAD, FS_COMPOSITE, ["a_pos"]);

	this.quadVao = gl.createVertexArray();
	gl.bindVertexArray(this.quadVao);
	var qb = gl.createBuffer();
	gl.bindBuffer(gl.ARRAY_BUFFER, qb);
	gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-1, -1, 3, -1, -1, 3]), gl.STATIC_DRAW);
	gl.enableVertexAttribArray(0);
	gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);

	// strips: pos2 col4 w4 = 10 floats; sprites: pos2 uv2 col4 star1 = 9 floats
	this.stripVao = gl.createVertexArray();
	gl.bindVertexArray(this.stripVao);
	this.stripBuf = gl.createBuffer();
	gl.bindBuffer(gl.ARRAY_BUFFER, this.stripBuf);
	var sb = 40;
	gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 2, gl.FLOAT, false, sb, 0);
	gl.enableVertexAttribArray(1); gl.vertexAttribPointer(1, 4, gl.FLOAT, false, sb, 8);
	gl.enableVertexAttribArray(2); gl.vertexAttribPointer(2, 4, gl.FLOAT, false, sb, 24);
	this.spriteVao = gl.createVertexArray();
	gl.bindVertexArray(this.spriteVao);
	this.spriteBuf = gl.createBuffer();
	gl.bindBuffer(gl.ARRAY_BUFFER, this.spriteBuf);
	var pb = 36;
	gl.enableVertexAttribArray(0); gl.vertexAttribPointer(0, 2, gl.FLOAT, false, pb, 0);
	gl.enableVertexAttribArray(1); gl.vertexAttribPointer(1, 2, gl.FLOAT, false, pb, 8);
	gl.enableVertexAttribArray(2); gl.vertexAttribPointer(2, 4, gl.FLOAT, false, pb, 16);
	gl.enableVertexAttribArray(3); gl.vertexAttribPointer(3, 1, gl.FLOAT, false, pb, 32);
	gl.bindVertexArray(null);
	this.targets = [];
	this.w = 0; this.h = 0;
}
Renderer.prototype.target = function(w, h) {
	var gl = this.gl;
	var tex = gl.createTexture();
	gl.bindTexture(gl.TEXTURE_2D, tex);
	gl.texImage2D(gl.TEXTURE_2D, 0, this.hdr ? gl.RGBA16F : gl.RGBA8, w, h, 0, gl.RGBA,
		this.hdr ? gl.HALF_FLOAT : gl.UNSIGNED_BYTE, null);
	gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
	gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
	gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
	gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
	var fb = gl.createFramebuffer();
	gl.bindFramebuffer(gl.FRAMEBUFFER, fb);
	gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, tex, 0);
	return { tex: tex, fb: fb, w: w, h: h };
};
Renderer.prototype.resize = function(w, h) {
	var gl = this.gl;
	if (w == this.w && h == this.h) return;
	this.w = w; this.h = h;
	for (var i = 0; i < this.targets.length; i++) {
		gl.deleteTexture(this.targets[i].tex);
		gl.deleteFramebuffer(this.targets[i].fb);
	}
	this.targets = [];
	this.scene = this.target(w, h);
	this.targets.push(this.scene);
	this.mips = [];
	var mw = w, mh = h;
	for (var k = 0; k < 6; k++) {
		mw = Math.max(1, mw >> 1); mh = Math.max(1, mh >> 1);
		var t = this.target(mw, mh);
		this.mips.push(t);
		this.targets.push(t);
		if (mw < 8 || mh < 8) break;
	}
};
Renderer.prototype.dispose = function() {
	var gl = this.gl;
	var ext = gl.getExtension("WEBGL_lose_context");
	if (ext) ext.loseContext();
};

// ---- scene -----------------------------------------------------------------------------------

var GOLD = [1.0, 0.64, 0.22];

function Scene(canvas, opt) {
	this.r = new Renderer(canvas);
	this.canvas = canvas;
	this.mode = opt.mode || "boot";
	this.cues = normaliseCues(opt.cues || {});
	this.markPts = opt.mark;
	this.markLen = [];
	var acc = 0;
	for (var i = 0; i < this.markPts.length; i += 2) {
		if (i) acc += Math.hypot(this.markPts[i] - this.markPts[i - 2], this.markPts[i + 1] - this.markPts[i - 1]);
		this.markLen.push(acc);
	}
	this.markStroke = opt.markStroke || 0.24;
	this.env = null;
	this.stripData = new Float32Array(10 * 6 * 4096);
	this.spriteData = new Float32Array(9 * 6 * 1024);
	this.scale = 1; // internal resolution scale, lowered if frames are slow
	this.setLayout(opt.layout);
	this.build();
}

function normaliseCues(c) {
	var o = {};
	for (var k in c) o[k] = c[k];
	function num(k, v) { if (typeof o[k] != "number" || !isFinite(o[k])) o[k] = v; }
	num("logoHit", 2.85);
	num("flash", o.logoHit + 0.12);
	num("boom", o.logoHit - 1.3);
	num("swellStart", o.boom - 0.9);
	num("spiralStart", o.boom + 0.1);
	num("fadeOut", o.flash + 0.5);
	num("end", o.fadeOut + 0.6);
	if (!Array.isArray(o.sparkles)) o.sparkles = [];
	if (!Array.isArray(o.arpeggio) || o.arpeggio.length == 0) {
		o.arpeggio = [];
		for (var i = 1; i <= 10; i++) o.arpeggio.push(mix(o.spiralStart, o.logoHit, i / 10));
	}
	o.sparkles = o.sparkles.filter(function(t) { return typeof t == "number"; }).sort(function(a, b) { return a - b; });
	o.arpeggio = o.arpeggio.filter(function(t) { return typeof t == "number"; }).sort(function(a, b) { return a - b; });
	// the spiral is drawn by the onsets between its start and the hit
	o.draw = o.arpeggio.filter(function(t) { return t > o.spiralStart && t <= o.logoHit + 1e-3; });
	if (o.draw.length == 0 || o.draw[o.draw.length - 1] < o.logoHit - 1e-3) o.draw.push(o.logoHit);
	return o;
}

Scene.prototype.setLayout = function(l) {
	this.layout = l;
	var dpr = l.dpr || 1;
	var s = this.scale;
	this.pw = Math.max(2, Math.round(l.w * dpr * s));
	this.ph = Math.max(2, Math.round(l.h * dpr * s));
	this.canvas.width = this.pw;
	this.canvas.height = this.ph;
	this.px = dpr * s;                 // device px per CSS px
	this.cx = l.cx * this.px;
	this.cy = l.cy * this.px;
	this.markR = l.markR * this.px;    // mark radius in device px
	this.r.resize(this.pw, this.ph);
};

Scene.prototype.build = function() {
	var rand = rng(this.mode == "about" ? 7 : 1987);
	var about = this.mode == "about";
	var c = this.cues;
	// half the window in mark radii, to place things on screen
	var hx = this.layout.w / 2 / this.layout.markR, hy = this.layout.h / 2 / this.layout.markR;
	var D0 = 7.6;
	var ribbons = [];
	var n = about ? 4 : 9;
	for (var i = 0; i < n; i++) {
		var z0 = -14 + rand() * 12 + (i % 3 == 0 ? 7 : 0);
		var rb = {
			theta0: i / n * TAU + rand() * 0.6,
			dir: i % 5 == 4 ? -1 : 1,
			turns: 1.0 + rand() * 0.8,
			// start around the edge of the frame, wherever their depth puts them
			r0: (0.75 + rand() * 0.5) * Math.max(hx, hy) * (D0 - z0) / 6,
			z0: z0,
			tx: (rand() - 0.5) * 1.2,
			ty: (rand() - 0.5) * 1.2,
			delay: rand() * 0.4,
			farR: about ? 2.6 + rand() * 0.9 : 4.0 + rand() * 2.2,
			hitR: 1.35 + rand() * 0.3,
			orbitTilt: 0.45 + rand() * 0.6,
			orbitRot: i / n * Math.PI + rand() * 0.3,
			orbitPhase: rand() * TAU,
			speed: 0.85 + rand() * 0.3,
			bright: (0.75 + rand() * 0.5) * (about ? 0.5 : 1),
			hue: rand(),
		};
		ribbons.push(rb);
	}
	this.ribbons = ribbons;
	// after the boom: the angle of each orbit, integrated once (it speeds up as it tightens)
	this.angStep = 1 / 240;
	for (var r = 0; r < ribbons.length; r++) {
		var rb2 = ribbons[r], ang = [0], acc = 0, count = Math.ceil((c.end + 1.5 - c.boom) / this.angStep);
		for (var s = 1; s <= count; s++) {
			var tau = c.boom + s * this.angStep;
			var w = rb2.speed * (0.28 + 1.9 * Math.pow(this.tighten(tau), 1.3));
			if (tau > c.logoHit) w *= Math.exp(-(tau - c.logoHit) / 0.6);
			acc += w * this.angStep;
			ang.push(acc);
		}
		rb2.ang = ang;
	}
	// dust: spread over the whole frame, in depth
	var dust = [];
	var nd = about ? 50 : 190;
	for (var j = 0; j < nd; j++) {
		var dz = -14 + rand() * 17;
		var k = (D0 - dz) / 6;
		var dx = (rand() * 2.3 - 1.15) * hx * k, dy = (rand() * 2.3 - 1.15) * hy * k;
		dust.push({
			r: Math.hypot(dx, dy), a: Math.atan2(dy, dx), z: dz,
			spin: (0.03 + rand() * 0.08) * (rand() < 0.85 ? 1 : -1),
			size: 0.6 + rand() * 1.6,
			tw: 1.5 + rand() * 4,
			ph: rand() * TAU,
			b: 0.2 + rand() * 0.8,
		});
	}
	this.dust = dust;
	// intro glints: one per sparkle cue, spread around the frame, a little in depth
	var spots = [[-0.46, -0.3], [0.42, -0.4], [-0.3, 0.42], [0.5, 0.26], [0.04, -0.6], [-0.62, 0.08]];
	this.glints = c.sparkles.map(function(t, k) {
		var sp = spots[k % spots.length], gz = -1 - (k % 3) * 1.1, gk = (D0 - gz) / 6;
		return { t: t, x: sp[0] * hx * gk, y: sp[1] * hy * gk, z: gz, size: 1.5 + (k % 2) * 0.5 };
	});
	// burst streaks
	var streaks = [];
	for (var q = 0; q < (about ? 26 : 56); q++) {
		streaks.push({ a: rand() * TAU, v: 5 + rand() * 12, len: 0.4 + rand() * 1.4, b: 0.5 + rand(), d: rand() * 0.08 });
	}
	this.streaks = streaks;
	// sparks thrown off the pen tip on every onset
	var sparks = [];
	for (var p = 0; p < c.arpeggio.length; p++) {
		for (var m = 0; m < 5; m++) sparks.push({ k: p, a: rand() * TAU, v: 1.2 + rand() * 2.2, life: 0.35 + rand() * 0.3 });
	}
	this.sparks = sparks;
};
/** 0..1: how far the orbiting ribbons have closed in on the mark (slow, then fast into the hit) */
Scene.prototype.tighten = function(tau) {
	var c = this.cues;
	return Math.pow(sat((tau - c.spiralStart) / Math.max(0.2, c.logoHit - c.spiralStart)), 2.2);
};

/** audio level (0..1) at t: from the jingle's envelope when there is one, else a sketch of it */
Scene.prototype.level = function(t, low) {
	var e = this.env;
	if (e) {
		var arr = low ? e.low : e.rms;
		var i = t * e.rate;
		if (i < 0 || i >= arr.length - 1) return 0;
		var i0 = i | 0;
		return mix(arr[i0], arr[i0 + 1], i - i0);
	}
	var c = this.cues;
	if (low) {
		return smooth(c.swellStart, c.boom, t) * 0.7 * (1 - smooth(c.boom, c.boom + 0.4, t) * 0.5)
			+ Math.exp(-Math.abs(t - c.boom) * 8) * 0.5;
	}
	return 0.35 + 0.25 * smooth(c.swellStart, c.boom, t) + 0.4 * smooth(c.spiralStart, c.logoHit, t)
		- 0.6 * smooth(c.flash, c.end, t);
};
/** sum of decaying pulses from the onsets in `list` that already happened */
function pulses(list, t, decay) {
	var s = 0;
	for (var i = 0; i < list.length; i++) {
		var d = t - list[i];
		if (d >= 0 && d < decay * 6) s += Math.exp(-d / decay);
	}
	return s;
}

/** spiral drawing progress 0..1: one segment per onset, eased */
Scene.prototype.drawProgress = function(t) {
	var c = this.cues, list = c.draw, n = list.length;
	if (t <= c.spiralStart) return 0;
	var prog = 0, prevT = c.spiralStart, prevP = 0;
	for (var i = 0; i < n; i++) {
		var target = (i + 1) / n;
		// the pen creeps forward between onsets, and jumps on each onset
		var creep = mix(prevP, target, 0.35 * sat((t - prevT) / Math.max(0.05, list[i] - prevT)));
		if (t < list[i]) { prog = creep; return prog; }
		var jump = easeOut3((t - list[i]) / 0.09);
		prevP = mix(mix(prevP, target, 0.35), target, jump);
		prevT = list[i];
		prog = prevP;
		if (jump < 1) return prog;
		prevP = target;
	}
	return 1;
};
/** point along the mark at arc fraction f, in mark units */
Scene.prototype.markAt = function(f) {
	var len = this.markLen, total = len[len.length - 1], d = sat(f) * total;
	var lo = 0, hi = len.length - 1;
	while (hi - lo > 1) { var m = (lo + hi) >> 1; if (len[m] < d) lo = m; else hi = m; }
	var t = (d - len[lo]) / ((len[hi] - len[lo]) || 1), p = this.markPts;
	return [mix(p[lo * 2], p[hi * 2], t), mix(p[lo * 2 + 1], p[hi * 2 + 1], t)];
};

// camera: world units are mark radii at z = 0; it pushes in during the swell
Scene.prototype.cameraD = function(t) {
	var c = this.cues;
	return mix(7.6, 6.0, easeOut3(smooth(c.swellStart - 0.3, c.boom, t) * 1.0));
};
Scene.prototype.project = function(x, y, z, D, out) {
	var dz = D - z;
	if (dz < 0.35) return false;
	var f = this.markR * 6.0 / dz;
	out[0] = this.cx + x * f;
	out[1] = this.cy + y * f;
	out[2] = f / this.markR; // perspective scale, 1 at z = 0 after the push-in
	return true;
};

/** a ribbon head's position at time tau, or null before it exists */
Scene.prototype.ribbonAt = function(rb, tau, out) {
	var c = this.cues;
	var s0 = mix(c.swellStart, c.boom, rb.delay);
	if (this.mode == "about") s0 = c.boom;
	if (tau < s0) return false;
	var x, y, z;
	if (tau < c.boom) {
		var p = (tau - s0) / (c.boom - s0), e = Math.pow(p, 1.7);
		var r = rb.r0 * Math.pow(1 - e, 1.15);
		var th = rb.theta0 + rb.dir * rb.turns * TAU * e;
		x = Math.cos(th) * r; y = Math.sin(th) * r * 0.78; z = rb.z0 * Math.pow(1 - e, 1.25);
		// the helices lean away from the view axis, and straighten as they converge
		var k = 1 - e, ax = rb.tx * k, ay = rb.ty * k;
		var y1 = y * Math.cos(ax) - z * Math.sin(ax), z1 = y * Math.sin(ax) + z * Math.cos(ax);
		var x2 = x * Math.cos(ay) + z1 * Math.sin(ay), z2 = -x * Math.sin(ay) + z1 * Math.cos(ay);
		out[0] = x2; out[1] = y1; out[2] = z2;
		return true;
	}
	// after the boom: they fly out of the centre onto wide tilted orbits, close in on the mark
	// through the arpeggio (slowly, then fast) and burst outwards on the hit
	var dt = tau - c.boom;
	var tight = this.tighten(tau);
	var rr = mix(rb.farR * easeOut3(dt / 0.55), rb.hitR, tight);
	var fi = dt / this.angStep, a0i = Math.min(rb.ang.length - 2, fi | 0);
	var ang = mix(rb.ang[a0i], rb.ang[a0i + 1], Math.min(1, fi - a0i));
	var th2 = rb.orbitPhase + rb.dir * TAU * ang;
	if (tau > c.logoHit) rr += 7.5 * Math.pow(tau - c.logoHit, 0.75);
	var ox = Math.cos(th2) * rr, oy = Math.sin(th2) * rr;
	// tilt the orbit plane about x, then turn it about the view axis
	var ty = oy * Math.cos(rb.orbitTilt), tz = oy * Math.sin(rb.orbitTilt);
	var cr = Math.cos(rb.orbitRot), sr = Math.sin(rb.orbitRot);
	out[0] = ox * cr - ty * sr; out[1] = ox * sr + ty * cr; out[2] = tz;
	return true;
};

/** a point of the mark (mark units) on screen at time t: it settles from a slight 3D turn */
Scene.prototype.markToScreen = function(mx, my, t, D, out) {
	var c = this.cues;
	var kk = sat((t - c.spiralStart) / Math.max(0.2, c.logoHit - c.spiralStart));
	var rest = Math.pow(1 - kk, 2), spin = -0.55 * rest, tilt = 0.5 * rest, grow = 1 + 0.7 * rest;
	var cs = Math.cos(spin) * grow, sn = Math.sin(spin) * grow;
	var x1 = mx * cs - my * sn, y1 = mx * sn + my * cs;
	return this.project(x1, y1 * Math.cos(tilt), y1 * Math.sin(tilt), D, out);
};

// ---- geometry writers ------------------------------------------------------------------------

Scene.prototype.beginFrame = function() { this.ns = 0; this.np = 0; };
/** a polyline of device px points -> quads with a glowing cross-section */
Scene.prototype.strip = function(pts, count, style) {
	// pts: [x, y, halfW, core, halo, r, g, b, hot] * count
	var S = 9, d = this.stripData;
	if (count < 2) return;
	var maxV = d.length / 10;
	var nx0 = 0, ny0 = 0;
	for (var i = 0; i < count - 1; i++) {
		if (this.ns + 6 > maxV) return;
		var a = i * S, b = a + S;
		var dx = pts[b] - pts[a], dy = pts[b + 1] - pts[a + 1];
		var l = Math.hypot(dx, dy) || 1;
		var nx = -dy / l, ny = dx / l;
		// average normals at the joints so the glow does not crack on curves
		var pnx = i == 0 ? nx : (nx + nx0) * 0.5, pny = i == 0 ? ny : (ny + ny0) * 0.5;
		var pl = Math.hypot(pnx, pny) || 1; pnx /= pl; pny /= pl;
		var qnx = nx, qny = ny;
		if (i + 2 < count) {
			var c2 = b + S, ex = pts[c2] - pts[b], ey = pts[c2 + 1] - pts[b + 1], el = Math.hypot(ex, ey) || 1;
			qnx = nx - ey / el; qny = ny + ex / el;
			var ql = Math.hypot(qnx, qny) || 1; qnx /= ql; qny /= ql;
		}
		nx0 = nx; ny0 = ny;
		this.vtx(pts, a, pnx, pny, -1); this.vtx(pts, a, pnx, pny, 1); this.vtx(pts, b, qnx, qny, 1);
		this.vtx(pts, a, pnx, pny, -1); this.vtx(pts, b, qnx, qny, 1); this.vtx(pts, b, qnx, qny, -1);
	}
};
Scene.prototype.vtx = function(p, o, nx, ny, side) {
	var d = this.stripData, k = this.ns * 10, hw = p[o + 2];
	d[k] = p[o] + nx * hw * side; d[k + 1] = p[o + 1] + ny * hw * side;
	d[k + 2] = p[o + 5]; d[k + 3] = p[o + 6]; d[k + 4] = p[o + 7]; d[k + 5] = p[o + 8];
	d[k + 6] = side; d[k + 7] = hw; d[k + 8] = p[o + 3]; d[k + 9] = p[o + 4];
	this.ns++;
};
Scene.prototype.sprite = function(x, y, size, r, g, b, hot, star) {
	if (this.np + 6 > this.spriteData.length / 9) return;
	var d = this.spriteData;
	var c = [[-1, -1], [1, -1], [1, 1], [-1, -1], [1, 1], [-1, 1]];
	for (var i = 0; i < 6; i++) {
		var k = this.np * 9;
		d[k] = x + c[i][0] * size; d[k + 1] = y + c[i][1] * size;
		d[k + 2] = c[i][0]; d[k + 3] = c[i][1];
		d[k + 4] = r; d[k + 5] = g; d[k + 6] = b; d[k + 7] = hot; d[k + 8] = star;
		this.np++;
	}
};

// ---- the frame -------------------------------------------------------------------------------

Scene.prototype.frame = function(t) {
	var c = this.cues, px = this.px, D = this.cameraD(t);
	var lvl = this.level(t, false), low = this.level(t, true);
	var about = this.mode == "about";
	this.beginFrame();
	var P = [0, 0, 0], Q = [0, 0, 0];
	var hitAge = t - c.logoHit;
	var hitPulse = hitAge >= 0 ? Math.exp(-hitAge / 0.35) : 0;
	var boomAge = t - c.boom;
	var boomPulse = boomAge >= 0 ? Math.exp(-boomAge / 0.3) : 0;
	var onset = pulses(c.arpeggio, t, 0.12);

	// dust: slow drifting motes in depth, drawn in with the swell, thrown out by the burst
	var pull = smooth(c.swellStart, c.boom, t);
	for (var i = 0; i < this.dust.length; i++) {
		var m = this.dust[i];
		var a = m.a + m.spin * t * (1 + 2.5 * pull);
		var rr = m.r * (1 - 0.35 * pull * pull);
		if (hitAge > 0) rr += 2.2 * Math.pow(hitAge, 0.6);
		var z = m.z + t * 0.45;
		if (!this.project(Math.cos(a) * rr, Math.sin(a) * rr, z, D, P)) continue;
		var near = sat((D - z - 0.35) / 1.5);
		var tw = 0.55 + 0.45 * Math.sin(t * m.tw + m.ph);
		var b = m.b * tw * near * (0.45 + 0.9 * lvl + 0.6 * onset * 0.3) * smooth(0, 0.5, t + 0.2);
		b *= 1 - smooth(c.fadeOut, c.end, t) * 0.5;
		var sz = Math.max(1.2 * px, m.size * px * P[2] * 1.4);
		this.sprite(P[0], P[1], sz * 3, 1.0 * b, 0.78 * b, 0.46 * b, 0.0, 0);
	}

	// intro glints: a star flare on each sparkle cue, which then drifts to the centre and is
	// swallowed at the boom
	for (var g = 0; g < this.glints.length; g++) {
		var gl = this.glints[g], age = t - gl.t;
		if (age < 0) continue;
		var drift = smooth(gl.t, c.boom, t);
		var gx = mix(gl.x, 0, drift * drift), gy = mix(gl.y, 0, drift * drift), gz = mix(gl.z, 0, drift);
		if (!this.project(gx, gy, gz, D, P)) continue;
		var flare = Math.min(1, age / 0.05) * Math.exp(-age / 0.45);
		var orb = (0.5 + 0.3 * lvl) * (1 - smooth(c.boom - 0.15, c.boom, t));
		var s = this.markR * P[2] * gl.size;
		if (flare > 0.01) this.sprite(P[0], P[1], s * (0.8 + 0.6 * flare), 2.2 * flare, 1.6 * flare, 0.8 * flare, 0.9 * flare, 1.0);
		if (orb > 0.01) this.sprite(P[0], P[1], 5 * px * P[2] + 3 * px, 1.6 * orb, 1.1 * orb, 0.5 * orb, 0.4 * orb, 0);
	}

	// ribbons
	var pts = this.tmpPts || (this.tmpPts = new Float32Array(9 * 160));
	for (var ri = 0; ri < this.ribbons.length; ri++) {
		var rb = this.ribbons[ri];
		var trail = t < c.boom ? 0.55 : mix(0.55, 0.42, smooth(c.boom, c.boom + 0.5, t)) - 0.14 * this.tighten(t);
		var N = 90, cnt = 0;
		var bright = rb.bright;
		if (t < c.boom) bright *= (0.55 + 0.9 * low) * smooth(c.swellStart, c.swellStart + 0.4, t);
		else bright *= 0.3 + 0.8 * this.tighten(t) + 0.08 * onset + 1.0 * hitPulse;
		bright *= 1 + 2.0 * boomPulse;
		if (hitAge > 0) bright *= Math.exp(-hitAge / 0.35);
		if (bright < 0.004) continue;
		for (var k = 0; k < N; k++) {
			var sfrac = k / (N - 1); // 0 = tail, 1 = head
			var tau = t - trail * (1 - sfrac);
			if (!this.ribbonAt(rb, tau, Q)) continue;
			if (!this.project(Q[0], Q[1], Q[2], D, P)) { cnt = 0; continue; }
			var fade = Math.pow(sfrac, 1.6) * sat((D - Q[2] - 0.35) / 1.2);
			var e = bright * fade;
			var ps = clamp(P[2], 0.25, 2.5) * (about ? 0.6 : 1);
			var o = cnt * 9;
			pts[o] = P[0]; pts[o + 1] = P[1];
			pts[o + 2] = (9 + 9 * sfrac) * px * ps;          // half width (glow extent)
			pts[o + 3] = (0.9 + 0.9 * sfrac) * px * ps;      // core radius
			pts[o + 4] = (3.5 + 2.5 * sfrac) * px * ps;      // halo decay
			var warm = 0.9 + 0.2 * rb.hue;
			pts[o + 5] = GOLD[0] * e * 1.6; pts[o + 6] = GOLD[1] * e * 1.6 * warm; pts[o + 7] = GOLD[2] * e * 1.4;
			pts[o + 8] = e * 0.9 * sfrac;
			cnt++;
		}
		this.strip(pts, cnt);
		// a hot orb at the head
		if (this.ribbonAt(rb, t, Q) && this.project(Q[0], Q[1], Q[2], D, P)) {
			var hb = bright * sat((D - Q[2] - 0.35) / 1.2);
			this.sprite(P[0], P[1], 16 * px * clamp(P[2], 0.3, 2), 1.6 * hb, 1.1 * hb, 0.55 * hb, 0.6 * hb, 0);
		}
	}

	// the boom: the ribbons collide in a bright core
	if (boomAge > -0.25 && boomAge < 1.2) {
		var charge = boomAge < 0 ? Math.pow(1 + boomAge / 0.25, 2) * 0.6 : boomPulse * 2.2;
		this.sprite(this.cx, this.cy, this.markR * (0.9 + 1.4 * (1 - boomPulse)), 1.8 * charge, 1.25 * charge, 0.6 * charge, 1.0 * charge, 0.5);
	}

	// the spiral: the mark drawing itself from the centre, settling from a slight 3D turn
	var prog = this.drawProgress(t);
	if (prog > 0) {
		var span2 = Math.max(0.2, c.logoHit - c.spiralStart);
		var kk = sat((t - c.spiralStart) / span2);
		var mp = this.markPts, total = this.markLen[this.markLen.length - 1];
		var glow = (0.85 + 0.45 * kk + 0.25 * onset + 0.35 * hitPulse) * (1 - 0.75 * smooth(c.flash - 0.1, c.flash + 0.6, t));
		var sp = this.tmpSpiral || (this.tmpSpiral = new Float32Array(9 * (mp.length / 2 + 2)));
		var n2 = 0, stop = prog * total;
		var tip = null;
		for (var j = 0; j < mp.length / 2; j++) {
			var last = this.markLen[j] >= stop;
			var mx = mp[j * 2], my = mp[j * 2 + 1];
			if (last) { var tp = this.markAt(prog); mx = tp[0]; my = tp[1]; }
			if (!this.markToScreen(mx, my, t, D, P)) continue;
			// brighter towards the pen
			var along = this.markLen[j] / Math.max(1e-3, stop);
			var eb = glow * (0.55 + 0.45 * Math.pow(Math.min(1, along), 3));
			var ps2 = P[2], o2 = n2 * 9;
			var strokePx = this.markStroke * this.markR * ps2;
			sp[o2] = P[0]; sp[o2 + 1] = P[1];
			// narrow strip (a wide one would fold on the tight corner); the bloom adds the glow
			sp[o2 + 2] = strokePx * 1.1 + 6 * px;
			sp[o2 + 3] = strokePx * 0.22 + 0.6 * px;
			sp[o2 + 4] = strokePx * 0.3 + 2 * px;
			sp[o2 + 5] = GOLD[0] * eb * 1.7; sp[o2 + 6] = GOLD[1] * eb * 1.7; sp[o2 + 7] = GOLD[2] * eb * 1.4;
			sp[o2 + 8] = eb * 0.5;
			n2++;
			if (last) { tip = [P[0], P[1], ps2]; break; }
		}
		this.strip(sp, n2);
		if (tip && prog < 1) {
			var pen = 0.8 + 0.7 * Math.min(1.5, onset) + 0.4 * lvl;
			this.sprite(tip[0], tip[1], this.markR * 0.4 * tip[2], 1.6 * pen, 1.15 * pen, 0.6 * pen, 0.8 * pen, 0.2 + 0.4 * Math.min(1, onset));
		}
	}

	// a spark flare and a few sparks at the pen on each onset
	for (var oi = 0; oi < c.arpeggio.length; oi++) {
		var at = c.arpeggio[oi], age2 = t - at;
		if (age2 < 0 || age2 > 0.8 || at < c.spiralStart || at > c.logoHit + 0.2) continue;
		var pp = this.markAt(this.drawProgress(at + 0.09));
		if (!this.markToScreen(pp[0], pp[1], at + 0.09, D, P)) continue;
		var fl = Math.exp(-age2 / 0.14);
		this.sprite(P[0], P[1], this.markR * 0.55 * P[2], 1.2 * fl, 0.85 * fl, 0.45 * fl, 0.4 * fl, 1.0);
	}
	for (var si = 0; si < this.sparks.length; si++) {
		var spk = this.sparks[si], st0 = c.arpeggio[spk.k], age3 = t - st0;
		if (age3 < 0 || age3 > spk.life || st0 < c.spiralStart || st0 > c.logoHit + 0.2) continue;
		var base = this.markAt(this.drawProgress(st0 + 0.09));
		var dist = spk.v * (1 - Math.exp(-age3 / 0.18)) * 0.35;
		if (!this.markToScreen(base[0] + Math.cos(spk.a) * dist, base[1] + Math.sin(spk.a) * dist, st0 + 0.09, D, P)) continue;
		var sb2 = 1.4 * (1 - age3 / spk.life);
		this.sprite(P[0], P[1], 4 * px, 1.8 * sb2, 1.3 * sb2, 0.7 * sb2, 0.8 * sb2, 0);
	}

	// the burst on the hit: streaks flying out of the mark
	if (hitAge > -0.02 && hitAge < 1.4) {
		for (var q = 0; q < this.streaks.length; q++) {
			var sk = this.streaks[q], ag = hitAge - sk.d;
			if (ag < 0) continue;
			var head = 0.9 + sk.v * (1 - Math.exp(-ag / 0.35)) * 0.9;
			var tail = Math.max(0.9, head - sk.len * Math.exp(-ag / 0.6));
			var sb3 = sk.b * Math.exp(-ag / 0.32) * 1.4;
			if (sb3 < 0.01) continue;
			var ca = Math.cos(sk.a), sa = Math.sin(sk.a);
			var o3 = 0;
			for (var z3 = 0; z3 < 4; z3++) {
				var fr = z3 / 3, rad = mix(tail, head, fr) * this.markR;
				pts[o3] = this.cx + ca * rad; pts[o3 + 1] = this.cy + sa * rad;
				pts[o3 + 2] = 8 * px; pts[o3 + 3] = 0.9 * px; pts[o3 + 4] = 3 * px;
				var eb3 = sb3 * fr;
				pts[o3 + 5] = GOLD[0] * eb3 * 1.5; pts[o3 + 6] = GOLD[1] * eb3 * 1.5; pts[o3 + 7] = GOLD[2] * eb3 * 1.3; pts[o3 + 8] = eb3 * 0.6;
				o3 += 9;
			}
			this.strip(pts, 4);
		}
		// the core behind the mark
		this.sprite(this.cx, this.cy, this.markR * (1.3 + 2.2 * (1 - hitPulse)), 0.6 * hitPulse, 0.42 * hitPulse, 0.2 * hitPulse, 0.15 * hitPulse, 0);
	}

	this.draw(t, {
		haze: (about ? 0.05 : 0.02) + 0.05 * smooth(-0.1, 0.6, t) * (0.5 + 0.5 * lvl) + 0.05 * sat(pulses(c.sparkles, t, 0.35)) + 0.08 * pull + 0.1 * low + 0.12 * sat(onset * 0.25) * smooth(c.spiralStart, c.logoHit, t)
			+ 0.15 * hitPulse + (hitAge > 0 ? 0.08 * Math.exp(-hitAge / 1.5) : 0),
		bloomK: 0.9 + 0.8 * boomPulse + 0.5 * hitPulse + 0.2 * sat(onset * 0.3),
		exposure: 1.25,
		flash: this.flashAt(t),
		rays: hitAge > 0 ? [0.8 * Math.exp(-hitAge / 0.45), t * 0.9, this.markR * (2.0 + 5.0 * sat(hitAge / 0.5))] : [0, 0, 1],
		ring: hitAge > 0 && hitAge < 1.2 ? [0.9 * Math.exp(-hitAge / 0.3), this.markR * (1.15 + 11 * Math.pow(hitAge, 0.7)), (5 + 40 * hitAge) * px]
			: boomAge > 0 && boomAge < 0.9 ? [0.9 * Math.exp(-boomAge / 0.22), this.markR * (0.2 + 6 * Math.pow(boomAge, 0.7)), (4 + 25 * boomAge) * px]
			: [0, 0, 1],
		streak: hitAge > 0 ? [1.1 * Math.exp(-hitAge / 0.5), (1.6 + 2 * hitAge) * px] : [0, 1],
		fade: smooth(-0.15, 0.25, t),
	});
};

Scene.prototype.flashAt = function(t) {
	var c = this.cues, a = t - c.flash;
	if (a < -0.06) return 0;
	var peak = this.mode == "about" ? 0.6 : 1.0;
	if (a < 0) return peak * easeOut3((a + 0.06) / 0.06);
	return peak * Math.exp(-a / 0.35);
};

Scene.prototype.draw = function(t, u) {
	var r = this.r, gl = r.gl;
	gl.disable(gl.DEPTH_TEST);
	// scene: additive light
	gl.bindFramebuffer(gl.FRAMEBUFFER, r.scene.fb);
	gl.viewport(0, 0, r.scene.w, r.scene.h);
	gl.clearColor(0, 0, 0, 1);
	gl.clear(gl.COLOR_BUFFER_BIT);
	gl.enable(gl.BLEND);
	gl.blendFunc(gl.ONE, gl.ONE);
	if (this.ns > 0) {
		gl.useProgram(r.strip.p);
		gl.uniform2f(r.strip.u.u_res, r.scene.w, r.scene.h);
		gl.bindVertexArray(r.stripVao);
		gl.bindBuffer(gl.ARRAY_BUFFER, r.stripBuf);
		gl.bufferData(gl.ARRAY_BUFFER, this.stripData.subarray(0, this.ns * 10), gl.STREAM_DRAW);
		gl.drawArrays(gl.TRIANGLES, 0, this.ns);
	}
	if (this.np > 0) {
		gl.useProgram(r.sprite.p);
		gl.uniform2f(r.sprite.u.u_res, r.scene.w, r.scene.h);
		gl.bindVertexArray(r.spriteVao);
		gl.bindBuffer(gl.ARRAY_BUFFER, r.spriteBuf);
		gl.bufferData(gl.ARRAY_BUFFER, this.spriteData.subarray(0, this.np * 9), gl.STREAM_DRAW);
		gl.drawArrays(gl.TRIANGLES, 0, this.np);
	}
	gl.disable(gl.BLEND);
	// bloom: down the chain, then back up adding each level
	gl.bindVertexArray(r.quadVao);
	gl.useProgram(r.down.p);
	gl.activeTexture(gl.TEXTURE0);
	gl.uniform1i(r.down.u.u_tex, 0);
	var src = r.scene;
	for (var i = 0; i < r.mips.length; i++) {
		var dst = r.mips[i];
		gl.bindFramebuffer(gl.FRAMEBUFFER, dst.fb);
		gl.viewport(0, 0, dst.w, dst.h);
		gl.bindTexture(gl.TEXTURE_2D, src.tex);
		gl.uniform2f(r.down.u.u_texel, 0.5 / src.w, 0.5 / src.h);
		gl.drawArrays(gl.TRIANGLES, 0, 3);
		src = dst;
	}
	gl.useProgram(r.up.p);
	gl.uniform1i(r.up.u.u_tex, 0);
	gl.enable(gl.BLEND);
	gl.blendFunc(gl.ONE, gl.ONE);
	for (var j = r.mips.length - 1; j > 0; j--) {
		var from = r.mips[j], to = r.mips[j - 1];
		gl.bindFramebuffer(gl.FRAMEBUFFER, to.fb);
		gl.viewport(0, 0, to.w, to.h);
		gl.bindTexture(gl.TEXTURE_2D, from.tex);
		gl.uniform2f(r.up.u.u_texel, 0.5 / from.w, 0.5 / from.h);
		gl.uniform1f(r.up.u.u_gain, 0.9);
		gl.drawArrays(gl.TRIANGLES, 0, 3);
	}
	gl.disable(gl.BLEND);
	// composite to the canvas
	gl.bindFramebuffer(gl.FRAMEBUFFER, null);
	gl.viewport(0, 0, this.pw, this.ph);
	var cu = r.comp.u;
	gl.useProgram(r.comp.p);
	gl.activeTexture(gl.TEXTURE0);
	gl.bindTexture(gl.TEXTURE_2D, r.scene.tex);
	gl.uniform1i(cu.u_scene, 0);
	gl.activeTexture(gl.TEXTURE1);
	gl.bindTexture(gl.TEXTURE_2D, r.mips[0].tex);
	gl.uniform1i(cu.u_bloom, 1);
	gl.uniform2f(cu.u_res, this.pw, this.ph);
	gl.uniform2f(cu.u_center, this.cx, this.cy);
	gl.uniform1f(cu.u_markR, this.markR);
	gl.uniform1f(cu.u_haze, u.haze);
	gl.uniform1f(cu.u_bloomK, r.hdr ? u.bloomK : u.bloomK * 1.6);
	gl.uniform1f(cu.u_exposure, u.exposure);
	gl.uniform1f(cu.u_flash, u.flash);
	gl.uniform3f(cu.u_rays, u.rays[0], u.rays[1], u.rays[2]);
	gl.uniform3f(cu.u_ring, u.ring[0], u.ring[1], u.ring[2]);
	gl.uniform2f(cu.u_streak, u.streak[0], u.streak[1]);
	gl.uniform1f(cu.u_fade, u.fade);
	gl.uniform1f(cu.u_time, t);
	gl.drawArrays(gl.TRIANGLES, 0, 3);
	gl.activeTexture(gl.TEXTURE0);
};

Scene.prototype.dispose = function() {
	this.r.dispose();
	this.r = null;
};

// ---- worker glue -----------------------------------------------------------------------------

function absNow() { return performance.timeOrigin + performance.now(); }

var scene = null, t0 = null, running = false, stopAt = Infinity, frozen = null;
var stats = { frames: 0, maxGap: 0, last: 0, slow: 0, cpu: 0, cpuMax: 0 };
var raf = self.requestAnimationFrame ? self.requestAnimationFrame.bind(self) : function(f) { return setTimeout(function() { f(performance.now()); }, 16); };
var caf = self.cancelAnimationFrame ? self.cancelAnimationFrame.bind(self) : clearTimeout;
var rafId = 0;

function tick() {
	rafId = 0;
	if (!running || !scene) return;
	var now = absNow();
	var t = frozen != null ? frozen : t0 == null ? -0.2 : (now - t0) / 1000;
	if (stats.last) {
		var gap = now - stats.last;
		if (t > 0.3 && gap > stats.maxGap) stats.maxGap = gap;
		// if frames are slow, render fewer pixels rather than stutter
		stats.slow = gap > 26 ? stats.slow + 1 : Math.max(0, stats.slow - 1);
		if (stats.slow > 12 && scene.scale > 0.6 && frozen == null) {
			scene.scale = Math.max(0.6, scene.scale - 0.15);
			scene.setLayout(scene.layout);
			stats.slow = 0;
		}
	}
	stats.last = now;
	try {
		var c0 = performance.now();
		scene.frame(t);
		var cpu = performance.now() - c0;
		stats.cpu += cpu;
		if (cpu > stats.cpuMax) stats.cpuMax = cpu;
	} catch (e) {
		post({ type: "error", message: String(e && e.stack || e) });
		running = false;
		return;
	}
	stats.frames++;
	if (t >= stopAt && frozen == null) {
		running = false;
		post({ type: "done", frames: stats.frames, maxGap: Math.round(stats.maxGap), scale: scene.scale, hdr: scene.r.hdr,
			cpuAvg: +(stats.cpu / Math.max(1, stats.frames)).toFixed(2), cpuMax: +stats.cpuMax.toFixed(2) });
		return;
	}
	rafId = raf(tick);
}

function post(m) { self.postMessage(m); }

self.onmessage = function(e) {
	var m = e.data;
	switch (m.type) {
		case "init":
			try {
				scene = new Scene(m.canvas, m);
				stopAt = m.stopAt != null ? m.stopAt : scene.cues.end;
				running = true;
				tick();
				var gl = scene.r.gl, info = gl.getExtension("WEBGL_debug_renderer_info");
				var renderer = String(gl.getParameter(info ? info.UNMASKED_RENDERER_WEBGL : gl.RENDERER));
				post({ type: "ready", hdr: scene.r.hdr, renderer: renderer,
					software: /swiftshader|llvmpipe|softpipe|basic render|software/i.test(renderer) });
			} catch (x) {
				post({ type: "error", message: String(x && x.stack || x) });
			}
			break;
		case "start": t0 = m.t0; break;
		case "env": if (scene) scene.env = m.env; break;
		case "cues": // re-timed to the jingle's length
			if (scene) { scene.cues = normaliseCues(m.cues); scene.build(); stopAt = scene.cues.end; }
			break;
		case "resize": if (scene) scene.setLayout(m.layout); break;
		case "freeze": // checking frames: hold the scene at time m.t (null to resume)
			frozen = m.t;
			if (scene && scene.scale != 1) { scene.scale = 1; scene.setLayout(scene.layout); }
			if (frozen != null && !running && scene) { running = true; tick(); }
			break;
		case "stop":
			running = false;
			if (rafId) caf(rafId);
			if (scene) scene.dispose();
			scene = null;
			post({ type: "stopped", frames: stats.frames, maxGap: Math.round(stats.maxGap) });
			break;
	}
};
})();
