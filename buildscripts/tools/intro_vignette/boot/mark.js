/**
 * The MillerScore mark: the outlined "M" of the app icon (share/icons/AppIcon/MillerScore_AppIcon.svg,
 * mu_logo.svg), with its four-pointed sparkle at the upper right.
 *
 * One definition feeds the boot overlay (SVG) and the boot scene (the M that draws itself, starting
 * at its lower left and going round the outline), so they always line up.
 * Units: half the M's width is 1, y down, centred on the M's box. Stroke width is GGMark.stroke.
 */
(function(root) {
	"use strict";
	// the outline in mu_logo.svg units (392 x 392 icon)
	var OUTLINE = [78, 291, 78, 112, 127, 115, 196, 217, 265, 115, 314, 112, 314, 291, 259, 291, 259, 204,
		202, 293, 190, 293, 133, 204, 133, 291, 78, 291];
	var SPARKLE = { x: 283, y: 99, r: 28, waist: 8 };
	var CX = 196, CY = 202.5, UNIT = 118;
	var stroke = 22 / UNIT;

	function toMark(x, y) { return [(x - CX) / UNIT, (y - CY) / UNIT]; }

	/** The centre line as [x0, y0, x1, y1, ...], evenly spaced about `step` apart */
	function points(step) {
		var raw = [];
		for (var i = 0; i < OUTLINE.length; i += 2) {
			var p = toMark(OUTLINE[i], OUTLINE[i + 1]);
			raw.push(p[0], p[1]);
		}
		return resample(raw, step || 0.02);
	}

	function resample(raw, step) {
		var n = raw.length / 2, len = [0];
		for (var i = 1; i < n; i++) {
			len.push(len[i - 1] + Math.hypot(raw[i * 2] - raw[i * 2 - 2], raw[i * 2 + 1] - raw[i * 2 - 1]));
		}
		var total = len[n - 1], count = Math.max(2, Math.ceil(total / step) + 1), out = [];
		var j = 1;
		for (var k = 0; k < count; k++) {
			var d = total * k / (count - 1);
			while (j < n - 1 && len[j] < d) j++;
			var t = (d - len[j - 1]) / ((len[j] - len[j - 1]) || 1);
			out.push(raw[j * 2 - 2] + (raw[j * 2] - raw[j * 2 - 2]) * t,
				raw[j * 2 - 1] + (raw[j * 2 + 1] - raw[j * 2 - 1]) * t);
		}
		return out;
	}

	function fmt(v) { return v.toFixed(4); }

	/** SVG path data for the M's outline (closed, so the corners join), scaled by `scale` around (cx, cy) */
	function svgPath(scale, cx, cy) {
		if (scale == null) scale = 1;
		cx = cx || 0; cy = cy || 0;
		var d = "";
		for (var i = 0; i < OUTLINE.length - 2; i += 2) {
			var p = toMark(OUTLINE[i], OUTLINE[i + 1]);
			d += (i ? "L" : "M") + fmt(cx + p[0] * scale) + " " + fmt(cy + p[1] * scale);
		}
		return d + "Z";
	}

	/** SVG path data for the sparkle (filled) */
	function sparklePath(scale, cx, cy) {
		if (scale == null) scale = 1;
		cx = cx || 0; cy = cy || 0;
		var c = toMark(SPARKLE.x, SPARKLE.y), r = SPARKLE.r / UNIT, w = SPARKLE.waist / UNIT;
		var pts = [[0, -r], [w, -w], [r, 0], [w, w], [0, r], [-w, w], [-r, 0], [-w, -w]], d = "";
		for (var i = 0; i < pts.length; i++) {
			d += (i ? "L" : "M") + fmt(cx + (c[0] + pts[i][0]) * scale) + " " + fmt(cy + (c[1] + pts[i][1]) * scale);
		}
		return d + "Z";
	}

	/** A standalone <svg> of the mark; `cls` goes on the element, it uses currentColor */
	function svg(cls) {
		var e = GGMark.extent;
		return '<svg class="' + (cls || "") + '" viewBox="' + (-e) + " " + (-e) + " " + (2 * e) + " " + (2 * e) + '"'
			+ ' aria-hidden="true"><path d="' + svgPath(1) + '" fill="none" stroke="currentColor"'
			+ ' stroke-width="' + stroke + '" stroke-linejoin="round"/>'
			+ '<path d="' + sparklePath(1) + '" fill="currentColor"/></svg>';
	}

	var GGMark = { stroke: stroke, points: points, svgPath: svgPath, sparklePath: sparklePath, svg: svg,
		/** half the side of the square the mark (stroke and sparkle included) fits in, in mark units */
		extent: 1 + stroke / 2 + 0.04 };
	if (root) root.GGMark = GGMark;
	if (typeof document == "undefined" && typeof module == "object" && module.exports) module.exports = GGMark;
})(typeof self != "undefined" ? self : null);
