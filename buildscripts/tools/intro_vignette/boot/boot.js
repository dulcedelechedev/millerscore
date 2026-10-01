/**
 * Boot animation controller (main thread). index.html runs this at the top of <body>, before the
 * app's scripts, so the overlay covers the first paint. It never holds the app up: the scene
 * renders in a Worker (boot/scene.js) on an OffscreenCanvas, the wordmark and the fades are Web
 * Animations the compositor runs on its own, and the app keeps starting behind the overlay.
 *
 * - Skipped by any key or click; turned off by Preferences > Application ("boot=0" in the URL,
 *   set by main.js, which also leaves it out of every window but the first).
 * - prefers-reduced-motion (or data-gg-motion="reduce" on <html>): a static logo fade instead.
 * - Jingle: assets/boot/jingle.ogg, .wav or .mp3 (first found). Cue points come from
 *   assets/boot/boot.json ("jingle" when there is a jingle, "silent" when there is none).
 * - When it ends, everything is torn down: worker, WebGL context, DOM, listeners. The jingle
 *   rings out under the app and its audio context closes when it finishes.
 *
 * window.GGBoot: state ("off" | "playing" | "done"), mode ("webgl" | "static"), skip(),
 * whenDone (Promise), about(host) for the About box.
 */
(function() {
"use strict";
var doc = document, root = doc.documentElement;
var SILENT = { // used when boot.json is missing or unreadable
	sparkles: [0.12, 0.26, 0.42, 0.6], swellStart: 0.45, boom: 1.45, spiralStart: 1.52,
	arpeggio: [1.64, 1.76, 1.87, 1.98, 2.08, 2.18, 2.28, 2.38, 2.47, 2.56, 2.65, 2.72],
	logoHit: 2.78, flash: 2.88, fadeOut: 3.45, end: 4.0,
};
var ABOUT = {
	sparkles: [], swellStart: -1, boom: 0.0, spiralStart: 0.08,
	arpeggio: [0.17, 0.26, 0.35, 0.43, 0.51, 0.59, 0.67, 0.75], logoHit: 0.82, flash: 0.9, fadeOut: 99, end: 2.2,
};

var done, api = window.GGBoot = {
	state: "off", mode: null, jingle: null, cues: null, stats: null,
	whenDone: new Promise(function(r) { done = r; }),
	skip: function() {},
	about: about,
};

function absNow() { return performance.timeOrigin + performance.now(); }
function nodeRequire(name) {
	try { return typeof require == "function" ? require(name) : null; } catch (e) { return null; }
}
function reducedMotion() {
	return root.getAttribute("data-gg-motion") == "reduce"
		|| !!(window.matchMedia && matchMedia("(prefers-reduced-motion: reduce)").matches);
}
function canWebGL() {
	return typeof Worker == "function" && typeof OffscreenCanvas == "function"
		&& !!HTMLCanvasElement.prototype.transferControlToOffscreen;
}

/** mark SVG with the gold gradient */
function markSvg(cls, id) {
	var e = GGMark.extent;
	return '<svg class="' + cls + '" viewBox="' + (-e) + " " + (-e) + " " + (2 * e) + " " + (2 * e) + '" aria-hidden="true">'
		+ '<defs><linearGradient id="' + id + '" x1="0" y1="-1" x2="0" y2="1" gradientUnits="userSpaceOnUse">'
		+ '<stop offset="0" stop-color="#FFEBB0"/><stop offset="0.5" stop-color="#F3C654"/><stop offset="1" stop-color="#C78C22"/>'
		+ '</linearGradient></defs><path d="' + GGMark.svgPath(1) + '" fill="none" stroke="url(#' + id + ')" stroke-width="'
		+ GGMark.stroke + '" stroke-linecap="round" stroke-linejoin="round"/>'
		+ '<path d="' + GGMark.sparklePath(1) + '" fill="#FFF2A8"/></svg>';
}
function logoHtml(id) {
	return '<div class="gg-boot__logo">' + markSvg("gg-boot__mark", id)
		+ '<div class="gg-boot__wordmark"><span class="gg-boot__game">Miller</span>'
		+ '<span class="gg-boot__gold">Score</span></div></div>';
}
/** where the mark is, for the scene: CSS px relative to `box` */
function layoutOf(box, mark) {
	var b = box.getBoundingClientRect(), m = mark.getBoundingClientRect();
	return { w: b.width, h: b.height, dpr: window.devicePixelRatio || 1,
		cx: m.left - b.left + m.width / 2, cy: m.top - b.top + m.height / 2,
		markR: m.width / 2 / GGMark.extent };
}

/** starts boot/scene.js in a worker on `canvas`; returns null if that is not possible */
function runScene(canvas, box, mark, cues, mode, onMessage) {
	if (!canWebGL()) return null;
	var worker;
	try {
		worker = new Worker("boot/scene.js");
		var off = canvas.transferControlToOffscreen();
		worker.onmessage = function(e) { onMessage(e.data); };
		worker.onerror = function(e) { onMessage({ type: "error", message: e.message }); };
		worker.postMessage({ type: "init", canvas: off, cues: cues, mode: mode,
			mark: GGMark.points(0.02), markStroke: GGMark.stroke, layout: layoutOf(box, mark) }, [off]);
	} catch (x) {
		if (worker) worker.terminate();
		return null;
	}
	var onResize = function() { worker.postMessage({ type: "resize", layout: layoutOf(box, mark) }); };
	window.addEventListener("resize", onResize);
	return {
		worker: worker,
		start: function(t0) { worker.postMessage({ type: "start", t0: t0 }); },
		post: function(m) { worker.postMessage(m); },
		dispose: function() {
			window.removeEventListener("resize", onResize);
			worker.postMessage({ type: "stop" });
			// give it a moment to release its context itself, then make sure it is gone
			setTimeout(function() { worker.terminate(); }, 60);
		},
	};
}

// ---- boot ------------------------------------------------------------------------------------

function boot() {
	var params = new URLSearchParams(location.search);
	// boot=0: off (main.js); for checking it: boot=silent (ignore the jingle), boot=static,
	// boot=webgl (use WebGL even when it is software), boot=always; they can be combined
	var flags = params.getAll("boot");
	function flag(name) { return flags.indexOf(name) >= 0; }
	if (flag("0")) { done(); return; }
	// once per window: reloading the page (plugin work, Reload) does not play it again
	var again = false;
	try {
		again = sessionStorage.getItem("gg-boot-played") == "1";
		sessionStorage.setItem("gg-boot-played", "1");
	} catch (e) {}
	if (again && flags.length == 0) { done(); return; }
	var fs = nodeRequire("fs"), path = nodeRequire("path");

	// assets/boot sits at the repo root, three levels above bin/resources/app
	var config = {}, jingle = null;
	if (fs && path) {
		var page = decodeURIComponent(location.pathname);
		if (/^\/[A-Za-z]:/.test(page)) page = page.slice(1);
		var dir = path.resolve(path.dirname(page), "../../../assets/boot");
		try { config = JSON.parse(fs.readFileSync(path.join(dir, "boot.json"), "utf8")) || {}; } catch (e) {}
		var exts = ["ogg", "wav", "mp3"];
		for (var i = 0; i < exts.length && !jingle; i++) {
			var p = path.join(dir, "jingle." + exts[i]);
			if (fs.existsSync(p)) jingle = p;
		}
	}
	if (flag("silent")) jingle = null;
	var cues = (jingle ? config.jingle : config.silent) || (jingle ? null : SILENT);
	if (!cues) cues = scaleCues(config.silent || SILENT, null); // re-timed once the jingle's length is known
	var volume = typeof config.volume == "number" ? config.volume : 1;

	var staticMode = reducedMotion() || flag("static") || !canWebGL();
	api.mode = staticMode ? "static" : "webgl";
	api.jingle = jingle;
	api.cues = cues;
	api.state = "playing";

	var el = doc.createElement("div");
	el.id = "gg-boot";
	el.className = "gg-boot" + (staticMode ? " gg-boot--static" : "");
	el.setAttribute("aria-hidden", "true");
	el.innerHTML = (staticMode ? "" : '<canvas class="gg-boot__canvas"></canvas>') + logoHtml("gg-boot-gold");
	doc.body.insertBefore(el, doc.body.firstChild);
	var electron = params.has("title-bar-overlay") ? nodeRequire("electron") : null;
	function captionButtons(onDark) {
		try { if (electron) electron.ipcRenderer.send("boot-overlay", onDark); } catch (e) {}
	}
	captionButtons(true);
	var mark = el.querySelector(".gg-boot__mark"), word = el.querySelector(".gg-boot__wordmark");

	var anims = [], timers = [], scene = null, t0 = null, finished = false;
	var ctx = null, gain = null, source = null;

	function later(ms, fn) { timers.push(setTimeout(fn, Math.max(0, ms))); }
	function animate(target, frames, cueSec, durMs, easing) {
		var a = target.animate(frames, { delay: t0 + cueSec * 1000 - absNow(), duration: durMs,
			easing: easing || "linear", fill: "both" });
		a._base = absNow() - t0;
		anims.push(a);
		return a;
	}

	function start(at) {
		if (t0 != null || finished) return;
		t0 = at;
		api.t0 = t0;
		if (scene) scene.start(t0);
		schedule();
	}
	function schedule() {
		var c = api.cues;
		if (staticMode) {
			// reduced motion: the logo fades in, holds, fades out; no movement
			var len = Math.min(c.end, typeof config.staticLength == "number" ? config.staticLength : 2.2);
			animate(el.querySelector(".gg-boot__logo"), [{ opacity: 0 }, { opacity: 1 }], 0.1, 400, "ease-out");
			animate(el, [{ opacity: 1 }, { opacity: 0 }], len - 0.4, 400, "ease-in");
			later(t0 + len * 1000 - absNow() + 30, finish);
			return;
		}
		// the mark appears where the spiral finished, the wordmark lands on the hit
		animate(mark, [{ opacity: 0, transform: "scale(1.035)" }, { opacity: 1, transform: "scale(1)" }],
			c.logoHit - 0.02, 220, "cubic-bezier(0.2, 0.8, 0.2, 1)");
		animate(word, [
			{ opacity: 0, transform: "translateY(0.12em) scale(1.08)", filter: "blur(10px)" },
			{ opacity: 1, transform: "translateY(0) scale(1)", filter: "blur(0px)" },
		], c.logoHit - 0.04, 420, "cubic-bezier(0.16, 1, 0.3, 1)");
		animate(el, [{ opacity: 1 }, { opacity: 0 }], c.fadeOut, (c.end - c.fadeOut) * 1000, "cubic-bezier(0.4, 0, 0.6, 1)");
		later(t0 + c.end * 1000 - absNow() + 30, finish);
	}

	function onInput(e) {
		if (finished) return;
		e.preventDefault();
		e.stopImmediatePropagation();
		if (e.type == "keydown" || e.type == "pointerdown") skip();
	}
	var inputs = ["keydown", "pointerdown", "mousedown", "click", "contextmenu"];
	for (var k = 0; k < inputs.length; k++) window.addEventListener(inputs[k], onInput, true);

	function stopAudio(fadeMs) {
		if (!ctx) return;
		var c = ctx;
		ctx = null;
		try {
			if (gain && fadeMs > 0) {
				gain.gain.setValueAtTime(gain.gain.value, c.currentTime);
				gain.gain.linearRampToValueAtTime(0, c.currentTime + fadeMs / 1000);
			}
			if (source) source.stop(c.currentTime + fadeMs / 1000 + 0.02);
		} catch (e) {}
		setTimeout(function() { c.close().catch(function() {}); }, fadeMs + 80);
	}

	function skip() {
		if (finished || api.state != "playing") return;
		api.state = "skipping";
		for (var i = 0; i < anims.length; i++) anims[i].pause();
		for (var j = 0; j < timers.length; j++) clearTimeout(timers[j]);
		timers = [];
		stopAudio(150);
		var from = parseFloat(getComputedStyle(el).opacity) || 1;
		el.animate([{ opacity: from }, { opacity: 0 }], { duration: 200, easing: "ease-out", fill: "forwards" });
		setTimeout(finish, 210);
	}
	api.skip = skip;

	function finish() {
		if (finished) return;
		finished = true;
		for (var i = 0; i < inputs.length; i++) window.removeEventListener(inputs[i], onInput, true);
		for (var j = 0; j < timers.length; j++) clearTimeout(timers[j]);
		for (var k = 0; k < anims.length; k++) anims[k].cancel();
		anims = []; timers = [];
		if (scene) scene.dispose();
		scene = null;
		if (el.parentNode) el.parentNode.removeChild(el);
		el = mark = word = null;
		captionButtons(false);
		// the jingle rings out under the app; its context closes when the sound ends
		if (ctx && source) {
			var c = ctx;
			source.onended = function() { c.close().catch(function() {}); };
			ctx = null;
		} else stopAudio(0); // still decoding: never play it
		api.state = "done";
		done();
	}

	/** checking frames: hold everything at timeline time t (seconds) */
	api.freeze = function(t) {
		for (var i = 0; i < timers.length; i++) clearTimeout(timers[i]);
		timers = [];
		stopAudio(0);
		for (var j = 0; j < anims.length; j++) {
			anims[j].pause();
			anims[j].currentTime = Math.max(0, t * 1000 - anims[j]._base);
		}
		if (scene) scene.post({ type: "freeze", t: t });
	};

	/** no WebGL after all, or only a software one: the static logo fade, which costs nothing */
	function goStatic() {
		if (staticMode || finished) return;
		staticMode = true;
		api.mode = "static";
		if (scene) scene.dispose();
		scene = null;
		var canvas = el.querySelector(".gg-boot__canvas");
		if (canvas) canvas.parentNode.removeChild(canvas);
		el.classList.add("gg-boot--static");
		for (var i = 0; i < anims.length; i++) anims[i].cancel();
		for (var j = 0; j < timers.length; j++) clearTimeout(timers[j]);
		anims = []; timers = [];
		sceneReady = true;
		if (t0 != null) schedule(); else tryGo();
	}

	// sound and pictures start together, once both are ready: the scene has drawn its first frame and
	// the jingle is decoded. If either takes too long, the pictures go static or play without sound.
	var sceneReady = staticMode, audioDone = !jingle, decoded = null;
	function tryGo() {
		if (t0 != null || finished || !sceneReady || !audioDone) return;
		clearTimeout(cap);
		if (decoded && ctx) playFrom(decoded); else start(absNow() + 30);
	}
	var cap = setTimeout(function() {
		if (!sceneReady) goStatic();
		audioDone = true;
		tryGo();
	}, 3000);

	if (!staticMode) {
		scene = runScene(el.querySelector(".gg-boot__canvas"), el, mark, cues, "boot", function(m) {
			if (m.type == "error") {
				console.warn("boot animation:", m.message);
				goStatic();
			} else if (m.type == "done" || m.type == "stopped") {
				if (!api.stats) api.stats = m;
			} else if (m.type == "ready") {
				api.hdr = m.hdr;
				api.renderer = m.renderer;
				// software WebGL would take CPU from the app's startup (and stutter anyway)
				if (m.software && !flag("webgl")) goStatic();
				sceneReady = true;
				tryGo();
			}
		});
		if (!scene) {
			staticMode = true;
			api.mode = "static";
			el.classList.add("gg-boot--static");
			var cv = el.querySelector(".gg-boot__canvas");
			if (cv) cv.parentNode.removeChild(cv);
			sceneReady = true;
		}
	}

	function playFrom(audio) {
		var lead = 0.05;
		var when = ctx.currentTime + lead;
		var ts = ctx.getOutputTimestamp ? ctx.getOutputTimestamp() : null;
		var heard = ts && ts.performanceTime > 0
			? performance.timeOrigin + ts.performanceTime + (when - ts.contextTime) * 1000
			: absNow() + (lead + (ctx.outputLatency || ctx.baseLatency || 0)) * 1000;
		var offset = 0;
		if (t0 != null) { // pictures already running (the cap fired): join them at the matching point
			offset = (heard - t0) / 1000;
			if (offset > cues.flash) return;
		}
		source = ctx.createBufferSource();
		source.buffer = audio;
		gain = ctx.createGain();
		gain.gain.value = volume;
		source.connect(gain).connect(ctx.destination);
		if (ctx.state == "suspended") ctx.resume();
		source.start(when, Math.max(0, offset));
		start(heard);
	}

	if (!jingle) { tryGo(); return; }
	try {
		ctx = new (window.AudioContext || window.webkitAudioContext)({ latencyHint: "interactive" });
		var bytes = fs.readFileSync(jingle);
		var buf = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
		ctx.decodeAudioData(buf).then(function(audio) {
			if (finished || !ctx) return;
			if (!config.jingle) { // no cue points for this jingle: stretch the default ones over it
				api.cues = cues = scaleCues(config.silent || SILENT, audio.duration);
				if (scene) scene.post({ type: "cues", cues: cues });
			}
			if (scene) scene.post({ type: "env", env: envelope(audio) });
			decoded = audio;
			if (t0 != null) { playFrom(audio); return; } // late: join the running pictures
			audioDone = true;
			tryGo();
		}, function(e) {
			console.warn("boot animation: could not decode " + jingle, e);
			stopAudio(0);
			audioDone = true;
			tryGo();
		});
	} catch (e) {
		console.warn("boot animation: no sound", e);
		stopAudio(0);
		audioDone = true;
		tryGo();
	}
}

/** stretches cue points written for one length over `duration` (null: keep them) */
function scaleCues(c, duration) {
	var k = duration ? duration / (c.end + 0.8) : 1, o = {};
	for (var key in c) {
		var v = c[key];
		o[key] = Array.isArray(v) ? v.map(function(x) { return x * k; }) : typeof v == "number" ? v * k : v;
	}
	return o;
}

/** loudness and bass envelopes of the jingle, 60 per second, 0..1 */
function envelope(audio) {
	var rate = 60, sr = audio.sampleRate, hop = Math.max(1, Math.round(sr / rate));
	var chans = [];
	for (var c = 0; c < audio.numberOfChannels; c++) chans.push(audio.getChannelData(c));
	var n = Math.ceil(audio.length / hop), rms = new Float32Array(n), low = new Float32Array(n);
	var a = 1 - Math.exp(-2 * Math.PI * 160 / sr), y1 = 0, y2 = 0;
	for (var f = 0; f < n; f++) {
		var s = 0, l = 0, end = Math.min(audio.length, (f + 1) * hop);
		for (var i = f * hop; i < end; i++) {
			var x = 0;
			for (var k = 0; k < chans.length; k++) x += chans[k][i];
			x /= chans.length;
			y1 += a * (x - y1); y2 += a * (y1 - y2);
			s += x * x; l += y2 * y2;
		}
		var cnt = Math.max(1, end - f * hop);
		rms[f] = Math.sqrt(s / cnt); low[f] = Math.sqrt(l / cnt);
	}
	normalise(rms); normalise(low);
	return { rms: rms, low: low, rate: rate };
}
function normalise(arr) {
	var m = 0;
	for (var i = 0; i < arr.length; i++) if (arr[i] > m) m = arr[i];
	if (m > 0) for (var j = 0; j < arr.length; j++) arr[j] /= m;
}

// ---- About box -------------------------------------------------------------------------------

/** fills `host` with the wordmark and a short burst; returns a function that tears it down */
function about(host) {
	host.classList.add("gg-boot-hero");
	host.innerHTML = '<canvas class="gg-boot__canvas"></canvas>' + logoHtml("gg-about-gold");
	var mark = host.querySelector(".gg-boot__mark"), word = host.querySelector(".gg-boot__wordmark");
	var canvas = host.querySelector(".gg-boot__canvas");
	var anims = [], scene = null, timer = 0;
	if (!reducedMotion()) scene = runScene(canvas, host, mark, ABOUT, "about", function(m) {
		if (m.type == "error") stop();
	});
	if (!scene) {
		canvas.remove();
		host.classList.add("gg-boot-hero--static");
		return function() {};
	}
	var t0 = absNow() + 60;
	scene.start(t0);
	function at(target, frames, sec, dur, easing) {
		anims.push(target.animate(frames, { delay: t0 + sec * 1000 - absNow(), duration: dur, easing: easing, fill: "both" }));
	}
	at(mark, [{ opacity: 0 }, { opacity: 1 }], ABOUT.logoHit - 0.02, 200, "ease-out");
	at(word, [{ opacity: 0, transform: "scale(1.06)", filter: "blur(6px)" }, { opacity: 1, transform: "scale(1)", filter: "blur(0px)" }],
		ABOUT.logoHit - 0.04, 380, "cubic-bezier(0.16, 1, 0.3, 1)");
	// the scene fades out once it has settled, leaving the static glow behind the logo
	at(canvas, [{ opacity: 1 }, { opacity: 0 }], ABOUT.end - 0.6, 600, "ease-in-out");
	timer = setTimeout(stop, ABOUT.end * 1000 + 120);
	function stop() {
		clearTimeout(timer);
		if (scene) { scene.dispose(); scene = null; }
		if (canvas.parentNode) canvas.parentNode.removeChild(canvas);
	}
	return function() {
		stop();
		for (var i = 0; i < anims.length; i++) anims[i].cancel();
	};
}

try {
	boot();
} catch (e) {
	console.error("boot animation failed:", e);
	var left = document.getElementById("gg-boot");
	if (left) left.parentNode.removeChild(left);
	api.state = "done";
	done();
}
})();
