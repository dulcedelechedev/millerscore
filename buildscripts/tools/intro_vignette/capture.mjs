// Renders the boot vignette frame by frame in headless Chrome (GGBoot.freeze(t)) and saves PNGs.
// usage: node capture.mjs <outDir> [fps=30] [width=960] [height=540] [dpr=1.5] [t1,t2,... to capture only those times]
// Chrome: $CHROME, else the default Windows install path. The GPU is used when there is one.
import http from "node:http";
import fs from "node:fs";
import path from "node:path";
import { spawn } from "node:child_process";
import os from "node:os";

const here = path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, "$1"));
const [outDir, fpsA, wA, hA, dprA, onlyT] = process.argv.slice(2);
const fps = +(fpsA || 30), W = +(wA || 960), H = +(hA || 540), DPR = +(dprA || 1.5);
fs.mkdirSync(outDir, { recursive: true });

const types = { ".html": "text/html", ".js": "text/javascript", ".css": "text/css", ".json": "application/json", ".mp3": "audio/mpeg" };
const server = http.createServer((req, res) => {
	const p = path.join(here, decodeURIComponent(new URL(req.url, "http://x").pathname));
	fs.readFile(p, (err, data) => {
		if (err) { res.writeHead(404); res.end(); return; }
		res.writeHead(200, { "content-type": types[path.extname(p)] || "application/octet-stream" });
		res.end(data);
	});
});
await new Promise(r => server.listen(0, "127.0.0.1", r));
const port = server.address().port;

const chromePath = process.env.CHROME || "C:/Program Files/Google/Chrome/Application/chrome.exe";
const profile = path.join(os.tmpdir(), "millerscore-intro-capture");
const dbgPort = 9333;
const chrome = spawn(chromePath, [
	"--headless=new", `--remote-debugging-port=${dbgPort}`, `--user-data-dir=${profile}`,
	`--window-size=${W},${H}`, "--autoplay-policy=no-user-gesture-required", "--mute-audio",
	"--enable-unsafe-swiftshader", "--hide-scrollbars", "about:blank",
], { stdio: "ignore" });

const sleep = ms => new Promise(r => setTimeout(r, ms));
let targets;
for (let i = 0; i < 50; i++) {
	try { targets = await (await fetch(`http://127.0.0.1:${dbgPort}/json/list`)).json(); break; } catch { await sleep(200); }
}
const page = targets.find(t => t.type == "page");
const ws = new WebSocket(page.webSocketDebuggerUrl);
await new Promise(r => ws.addEventListener("open", r));
let seq = 0;
const pending = new Map();
ws.addEventListener("message", e => {
	const m = JSON.parse(e.data);
	if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
	if (m.method == "Runtime.consoleAPICalled") console.log("[page]", m.params.args.map(a => a.value).join(" "));
	if (m.method == "Runtime.exceptionThrown") console.log("[page error]", JSON.stringify(m.params.exceptionDetails.exception?.description || m.params.exceptionDetails.text));
});
function send(method, params = {}) {
	const id = ++seq;
	ws.send(JSON.stringify({ id, method, params }));
	return new Promise(r => pending.set(id, r));
}
async function evaluate(expr) {
	const r = await send("Runtime.evaluate", { expression: expr, awaitPromise: true, returnByValue: true });
	if (r.result?.exceptionDetails) throw new Error(JSON.stringify(r.result.exceptionDetails));
	return r.result?.result?.value;
}

try {
	await send("Runtime.enable");
	await send("Page.enable");
	await send("Emulation.setDeviceMetricsOverride", { width: W, height: H, deviceScaleFactor: DPR, mobile: false });
	await send("Page.navigate", { url: `http://127.0.0.1:${port}/index.html?boot=webgl` });

	let info = null;
	for (let i = 0; i < 200; i++) {
		info = await evaluate("window.GGBoot && GGBoot.t0 != null ? JSON.stringify({mode: GGBoot.mode, renderer: GGBoot.renderer, hdr: GGBoot.hdr, cues: GGBoot.cues}) : null").catch(() => null);
		if (info) break;
		await sleep(100);
	}
	if (!info) throw new Error("boot animation never started");
	await evaluate("GGBoot.freeze(0)");
	info = JSON.parse(info);
	console.log("mode", info.mode, "renderer", info.renderer, "hdr", info.hdr);
	if (info.mode != "webgl") throw new Error("scene fell back to static");

	const end = info.cues.end;
	const times = onlyT != null ? onlyT.split(",").map(Number) : Array.from({ length: Math.round(end * fps) + 1 }, (_, i) => i / fps);
	for (let i = 0; i < times.length; i++) {
		const t = times[i];
		// the worker redraws the frozen time on its own frames; give it a few
		await evaluate(`GGBoot.freeze(${t}); new Promise(r => setTimeout(r, 180))`);
		const shot = await send("Page.captureScreenshot", { format: "png" });
		const name = onlyT != null ? `t${t.toFixed(2)}.png` : `f${String(i).padStart(4, "0")}.png`;
		fs.writeFileSync(path.join(outDir, name), Buffer.from(shot.result.data, "base64"));
		if (i % 30 == 0) console.log(`frame ${i}/${times.length - 1} t=${t.toFixed(3)}`);
	}
	fs.writeFileSync(path.join(outDir, "cues.json"), JSON.stringify({ fps, width: W, height: H, dpr: DPR, cues: info.cues }, null, 1));
} finally {
	ws.close();
	chrome.kill();
	server.close();
}
