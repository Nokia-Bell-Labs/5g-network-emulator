/*
 * FikoRE dashboard — logic with no DOM.
 *
 * Copyright 2026 Nokia
 * Licensed under the BSD 3-Clause Clear License
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Everything here is a pure function or a plain container, on purpose: it is the part that
 * is easy to get wrong (gaps, derived rates, picking the sample under the cursor) and the
 * part that can be tested headlessly with node. The DOM and uPlot live in index.html.
 */
(function () {
  "use strict";

  // Aggregation windows arrive every second by default. Beyond this many windows without a
  // point, a series is treated as absent rather than as slow.
  var GAP_FACTOR = 2.5;

  // Tolerance used to decide whether a series reported *in this same window*. The x axis
  // is built from real sample timestamps, and two measurements of the same window differ
  // only by the milliseconds between their publish calls, so half a window is generous
  // for jitter and still rejects a window that is genuinely missing.
  var MATCH_FRACTION = 0.5;

  // ---------------------------------------------------------------- colours

  // Stable per UE: the same UE keeps its colour across every panel and the map, which is
  // what makes the whole dashboard readable at a glance.
  var PALETTE = [
    "#4e9bd5", "#e2803c", "#5fa855", "#c64c4c", "#9575c7",
    "#8c6d5d", "#d97fb0", "#7f7f7f", "#b9bf3c", "#3fbecd"
  ];

  function ueColour(ueId) {
    var n = parseInt(ueId, 10);
    if (isNaN(n)) n = 0;
    return PALETTE[n % PALETTE.length];
  }

  // ---------------------------------------------------------------- series

  // One (t, v) ring per series, pruned to the window. Kept sorted by t because points can
  // arrive slightly out of order between measurements.
  function Series() {
    this.t = [];
    this.v = [];
  }

  Series.prototype.push = function (t, v) {
    var i = this.t.length;
    while (i > 0 && this.t[i - 1] > t) i--;
    // After the walk, everything before i is <= t, so an existing equal sample sits at i-1.
    if (i > 0 && this.t[i - 1] === t) {
      this.v[i - 1] = v;           // same window twice: the later value wins
      return;
    }
    if (i === this.t.length) {
      this.t.push(t);
      this.v.push(v);
      return;
    }
    this.t.splice(i, 0, t);
    this.v.splice(i, 0, v);
  };

  Series.prototype.prune = function (minT) {
    var cut = 0;
    while (cut < this.t.length && this.t[cut] < minT) cut++;
    if (cut > 0) {
      this.t.splice(0, cut);
      this.v.splice(0, cut);
    }
  };

  Series.prototype.last = function () {
    if (!this.t.length) return null;
    return { t: this.t[this.t.length - 1], v: this.v[this.v.length - 1] };
  };

  // Nearest sample to t, or null when none is within tol. Used by the map: the position
  // reported for a window is the last one of that window, so interpolating between two
  // windows would invent a position the emulator never reported.
  Series.prototype.nearest = function (t, tol) {
    if (!this.t.length) return null;
    var best = -1, bestDist = Infinity;
    for (var i = 0; i < this.t.length; i++) {
      var d = Math.abs(this.t[i] - t);
      if (d < bestDist) { bestDist = d; best = i; }
    }
    if (best < 0 || bestDist > tol) return null;
    return { t: this.t[best], v: this.v[best], index: best };
  };

  // ---------------------------------------------------------------- alignment

  // uPlot wants one shared x array and one y array per series, with null where a series
  // has no value. Absences longer than a gap are left as null so the line breaks instead
  // of pretending a detached UE is transmitting zero.
  function align(seriesByKey, keys, windowS, latestT, step) {
    var xs = [];
    var tol = (step || 1) * MATCH_FRACTION;
    var seen = {};

    keys.forEach(function (k) {
      var s = seriesByKey[k];
      if (!s) return;
      for (var i = 0; i < s.t.length; i++) {
        var t = s.t[i];
        if (t < latestT - windowS) continue;
        if (!seen[t]) { seen[t] = true; xs.push(t); }
      }
    });
    xs.sort(function (a, b) { return a - b; });

    var data = [xs];
    keys.forEach(function (k) {
      var s = seriesByKey[k];
      var col = new Array(xs.length);
      for (var i = 0; i < xs.length; i++) {
        col[i] = null;
        if (!s) continue;
        var hit = s.nearest(xs[i], tol);
        if (hit) col[i] = hit.v;
      }
      data.push(col);
    });
    return data;
  }

  // ---------------------------------------------------------------- store

  // Everything the page knows, fed by the WebSocket. Measurement and tags in, series out.
  function Store(windowS) {
    this.windowS = windowS || 30;
    this.latestT = 0;
    this.step = 1;               // observed spacing between windows, in seconds
    this.ues = {};               // ueId -> true
    this.series = {};            // panel key -> { seriesKey -> Series }
    this.missing = {};           // measurement -> true when never seen
  }

  Store.prototype.bucket = function (panel) {
    if (!this.series[panel]) this.series[panel] = {};
    return this.series[panel];
  };

  Store.prototype.add = function (panel, key, t, v) {
    if (v === undefined || v === null || isNaN(v)) return;
    var bucket = this.bucket(panel);
    if (!bucket[key]) bucket[key] = new Series();
    bucket[key].push(t, v);
  };

  // Influx line protocol point as the collector hands it over.
  Store.prototype.ingest = function (point) {
    var m = point.measurement;
    var tags = point.tags || {};
    var f = point.fields || {};
    var t = point.ts_ns ? point.ts_ns / 1e9 : point.received_at;
    if (!t) return;

    if (t > this.latestT) {
      if (this.latestT > 0) {
        var d = t - this.latestT;
        // Track the window spacing, ignoring jitter and reordering.
        if (d > 0.05 && d < 10) this.step = this.step * 0.8 + d * 0.2;
      }
      this.latestT = t;
    }

    var ue = tags.ue_id;
    var dir = tags.tx_dir;
    if (ue !== undefined) this.ues[ue] = true;

    if (m === "ue_mobility" && ue !== undefined) {
      this.add("pos_x", ue, t, f.x);
      this.add("pos_y", ue, t, f.y);
      this.add("distance", ue, t, f.distance_m);
      return;
    }

    if (m === "emulator_runtime") {
      this.add("rt_total", "step", t, f.step_time_ms_mean);
      this.add("rt_split", "mac", t, f.mac_step_time_ms_mean);
      this.add("rt_split", "ue", t, f.ue_step_time_ms_mean);
      this.add("rt_total", "active_ues", t, f.active_ues_last);
      return;
    }

    if (m === "control") {
      this.add("control", "applied", t, f.applied_sum);
      this.add("control", "rejected", t, f.rejected_sum);
      this.add("control", "blocked_ttis", t, f.blocked_ttis_sum);
      return;
    }

    if (ue === undefined || (dir !== "ul" && dir !== "dl")) return;

    if (m === "ue_phy") {
      this.add("sinr_" + dir, ue, t, f.sinr_db_mean);
      return;
    }

    if (m === "ue_pdcp") {
      this.add("tp_" + dir, ue, t, f.throughput_mbps_mean);
      if (f.latency_s_mean !== undefined) this.add("lat_" + dir, ue, t, f.latency_s_mean * 1000);
      if (f.ip_latency_s_mean !== undefined) this.add("iplat_" + dir, ue, t, f.ip_latency_s_mean * 1000);
      return;
    }

    if (m === "ue_queue") {
      // Derived exactly as the matplotlib dashboard did, from the final verdict counters.
      var accept = f.final_accept_packets_sum || 0;
      var acceptCe = f.final_accept_ce_packets_sum || 0;
      var drop = f.final_drop_packets_sum || 0;
      var total = accept + acceptCe + drop;
      if (total > 0) {
        this.add("cong_" + dir, ue, t, acceptCe / total);
        this.add("drop_" + dir, ue, t, drop / total);
      }
      return;
    }
  };

  Store.prototype.pruneAll = function () {
    var minT = this.latestT - this.windowS * 1.2;
    var self = this;
    Object.keys(this.series).forEach(function (panel) {
      var bucket = self.series[panel];
      Object.keys(bucket).forEach(function (k) { bucket[k].prune(minT); });
    });
  };

  Store.prototype.ueIds = function () {
    return Object.keys(this.ues).sort(function (a, b) { return parseInt(a, 10) - parseInt(b, 10); });
  };

  // Data for a panel, ready for uPlot.setData.
  Store.prototype.panelData = function (panel, keys) {
    return align(this.bucket(panel), keys, this.windowS, this.latestT, this.step);
  };

  // Trail of a UE over the window, for the map.
  Store.prototype.trail = function (ueId) {
    var xs = this.bucket("pos_x")[ueId];
    var ys = this.bucket("pos_y")[ueId];
    if (!xs || !ys || !xs.t.length) return [];
    var out = [];
    for (var i = 0; i < xs.t.length; i++) {
      var y = ys.nearest(xs.t[i], this.step * GAP_FACTOR);
      if (y) out.push({ t: xs.t[i], x: xs.v[i], y: y.v });
    }
    return out;
  };

  // Position of a UE at an instant, or null when no window is close enough.
  Store.prototype.positionAt = function (ueId, t) {
    var xs = this.bucket("pos_x")[ueId];
    var ys = this.bucket("pos_y")[ueId];
    if (!xs || !ys) return null;
    var tol = this.step * GAP_FACTOR;
    var x = xs.nearest(t, tol);
    var y = ys.nearest(t, tol);
    if (!x || !y) return null;
    return { x: x.v, y: y.v, t: x.t };
  };

  // ---------------------------------------------------------------- cursor bus

  // uPlot's cursor.sync only links uPlot instances, and the map is not one. Whoever wants
  // the hovered instant subscribes here; publishing null means "back to the live head".
  function CursorBus() {
    this.t = null;
    this.subs = [];
  }

  CursorBus.prototype.subscribe = function (fn) { this.subs.push(fn); };

  CursorBus.prototype.publish = function (t) {
    if (this.t === t) return;
    this.t = t;
    for (var i = 0; i < this.subs.length; i++) this.subs[i](t);
  };

  // ---------------------------------------------------------------- map colours

  // Percentile clipping, because the fading maps have a long tail: p0 is around -170 dB
  // while p50 is -142, so colouring by min and max leaves the map flat.
  function colourScale(lo, hi) {
    return function (v) {
      var x = (v - lo) / (hi - lo);
      if (!isFinite(x)) x = 0;
      x = x < 0 ? 0 : (x > 1 ? 1 : x);
      // Dark blue to yellow, monotonic in luminance so it reads as a magnitude.
      var r = Math.round(255 * Math.min(1, Math.max(0, 1.6 * x - 0.3)));
      var g = Math.round(255 * Math.min(1, Math.max(0, 1.4 * x - 0.1)));
      var b = Math.round(255 * Math.min(1, Math.max(0, 0.9 - 1.1 * x)));
      return [r, g, b];
    };
  }

  function percentile(sorted, p) {
    if (!sorted.length) return 0;
    var i = Math.min(sorted.length - 1, Math.max(0, Math.round((sorted.length - 1) * p)));
    return sorted[i];
  }

  var api = {
    GAP_FACTOR: GAP_FACTOR,
    MATCH_FRACTION: MATCH_FRACTION,
    PALETTE: PALETTE,
    ueColour: ueColour,
    Series: Series,
    Store: Store,
    CursorBus: CursorBus,
    align: align,
    colourScale: colourScale,
    percentile: percentile
  };

  // The same file is loaded by the page with a <script> tag and by node in the tests.
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  else if (typeof window !== "undefined") window.FikoDash = api;
})();
