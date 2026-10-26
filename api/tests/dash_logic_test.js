/*
 * Headless tests of the dashboard logic with no DOM, run with node.
 *
 * Copyright 2026 Nokia
 * Licensed under the BSD 3-Clause Clear License
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 * Covers what is easy to get wrong: ordering, gaps, derived rates and picking the sample
 * under the cursor. The rendering is checked by eye; this is not.
 */
"use strict";

const assert = require("assert");
const D = require("../fikore_api/static/dash.js");

function point(measurement, tags, fields, t) {
  return { measurement: measurement, tags: tags, fields: fields, ts_ns: t * 1e9 };
}

function testSeriesOrdering() {
  const s = new D.Series();
  s.push(3, 30);
  s.push(1, 10);
  s.push(2, 20);
  assert.deepStrictEqual(s.t, [1, 2, 3]);
  assert.deepStrictEqual(s.v, [10, 20, 30]);

  // Same window twice: the later value wins, no duplicate x.
  s.push(2, 99);
  assert.deepStrictEqual(s.t, [1, 2, 3]);
  assert.strictEqual(s.v[1], 99);

  s.prune(2);
  assert.deepStrictEqual(s.t, [2, 3]);
  assert.deepStrictEqual(s.last(), { t: 3, v: 30 });
}

function testNearestRespectsTolerance() {
  const s = new D.Series();
  s.push(10, 1);
  s.push(20, 2);
  assert.strictEqual(s.nearest(10.4, 1).v, 1);
  assert.strictEqual(s.nearest(19.8, 1).v, 2);
  // Nothing close enough: null rather than the least bad guess.
  assert.strictEqual(s.nearest(15, 1), null);
  assert.strictEqual(new D.Series().nearest(1, 1), null);
}

function testGapsBecomeNull() {
  const store = new D.Store(30);
  // UE 0 reports throughout; UE 1 stops at t=3, as a detached UE does.
  for (let t = 1; t <= 6; t++) {
    store.ingest(point("ue_pdcp", { ue_id: "0", tx_dir: "dl" }, { throughput_mbps_mean: t }, t));
    if (t <= 3) {
      store.ingest(point("ue_pdcp", { ue_id: "1", tx_dir: "dl" }, { throughput_mbps_mean: 10 * t }, t));
    }
  }

  const data = store.panelData("tp_dl", ["0", "1"]);
  const xs = data[0], ue0 = data[1], ue1 = data[2];
  assert.deepStrictEqual(xs, [1, 2, 3, 4, 5, 6]);
  assert.deepStrictEqual(ue0, [1, 2, 3, 4, 5, 6]);
  // A gap, not a zero: that is the whole point.
  assert.deepStrictEqual(ue1, [10, 20, 30, null, null, null]);
  assert.ok(!ue1.includes(0));
}

function testDerivedRates() {
  const store = new D.Store(30);
  store.ingest(point("ue_queue", { ue_id: "0", tx_dir: "dl" }, {
    final_accept_packets_sum: 70,
    final_accept_ce_packets_sum: 20,
    final_drop_packets_sum: 10
  }, 1));

  assert.strictEqual(store.bucket("cong_dl")["0"].last().v, 0.2);
  assert.strictEqual(store.bucket("drop_dl")["0"].last().v, 0.1);

  // No packets in the window: no rate at all, instead of a misleading zero.
  store.ingest(point("ue_queue", { ue_id: "1", tx_dir: "dl" }, {
    final_accept_packets_sum: 0,
    final_accept_ce_packets_sum: 0,
    final_drop_packets_sum: 0
  }, 1));
  assert.strictEqual(store.bucket("cong_dl")["1"], undefined);
}

function testLatencyIsConvertedToMs() {
  const store = new D.Store(30);
  store.ingest(point("ue_pdcp", { ue_id: "0", tx_dir: "ul" }, {
    throughput_mbps_mean: 5, latency_s_mean: 0.0031, ip_latency_s_mean: 0.0125
  }, 1));
  assert.ok(Math.abs(store.bucket("lat_ul")["0"].last().v - 3.1) < 1e-9);
  assert.ok(Math.abs(store.bucket("iplat_ul")["0"].last().v - 12.5) < 1e-9);
}

function testMobilityTrailAndCursorLookup() {
  const store = new D.Store(30);
  for (let t = 1; t <= 4; t++) {
    store.ingest(point("ue_mobility", { ue_id: "0" }, { x: 100 * t, y: -50 * t, distance_m: t }, t));
  }

  const trail = store.trail("0");
  assert.strictEqual(trail.length, 4);
  assert.deepStrictEqual(trail[0], { t: 1, x: 100, y: -50 });
  assert.deepStrictEqual(trail[3], { t: 4, x: 400, y: -200 });

  // The instant under the cursor maps to the window that actually reported it.
  const at = store.positionAt("0", 2.9);
  assert.deepStrictEqual({ x: at.x, y: at.y }, { x: 300, y: -150 });
  // Far outside any window: nothing to draw.
  assert.strictEqual(store.positionAt("0", 40), null);
  assert.strictEqual(store.positionAt("7", 2), null);
}

function testStoreTracksUesAndWindow() {
  const store = new D.Store(5);
  for (let t = 1; t <= 10; t++) {
    store.ingest(point("ue_pdcp", { ue_id: String(t % 3), tx_dir: "dl" }, { throughput_mbps_mean: t }, t));
  }
  assert.deepStrictEqual(store.ueIds(), ["0", "1", "2"]);
  assert.strictEqual(store.latestT, 10);
  assert.ok(Math.abs(store.step - 1) < 0.3);

  store.pruneAll();
  const oldest = store.bucket("tp_dl")["1"].t[0];
  assert.ok(oldest >= 4, "pruning keeps only the window, got " + oldest);
}

function testControlAndRuntimePanels() {
  const store = new D.Store(30);
  store.ingest(point("emulator_runtime", {}, {
    step_time_ms_mean: 0.12, mac_step_time_ms_mean: 0.1, ue_step_time_ms_mean: 0.02, active_ues_last: 21
  }, 1));
  store.ingest(point("control", {}, { applied_sum: 3, rejected_sum: 1, blocked_ttis_sum: 0 }, 1));

  assert.strictEqual(store.bucket("rt_total")["step"].last().v, 0.12);
  assert.strictEqual(store.bucket("rt_split")["mac"].last().v, 0.1);
  assert.strictEqual(store.bucket("control")["applied"].last().v, 3);
  assert.strictEqual(store.bucket("control")["rejected"].last().v, 1);
}

function testCursorBus() {
  const bus = new D.CursorBus();
  const seen = [];
  bus.subscribe(function (t) { seen.push(t); });

  bus.publish(12.5);
  bus.publish(12.5);        // same instant, no repeated notification
  bus.publish(null);        // back to the live head
  assert.deepStrictEqual(seen, [12.5, null]);
}

function testColoursAreStablePerUe() {
  assert.strictEqual(D.ueColour("3"), D.ueColour(3));
  assert.notStrictEqual(D.ueColour(0), D.ueColour(1));
  // Wraps around rather than running out.
  assert.strictEqual(D.ueColour(0), D.ueColour(D.PALETTE.length));
}

function testMapColourScaleClipsTails() {
  const scale = D.colourScale(-158, -101);      // p1 and p99 of a real fading map
  const low = scale(-170);                      // below p1, saturated
  const high = scale(1.71);                     // the edge value, saturated
  assert.deepStrictEqual(low, scale(-158));
  assert.deepStrictEqual(high, scale(-101));
  // And it is monotonic in between, so it reads as a magnitude.
  assert.ok(scale(-120)[0] > scale(-150)[0]);

  const sorted = [-170, -158, -150, -142, -120, -101, 1.71];
  assert.strictEqual(D.percentile(sorted, 0), -170);
  assert.strictEqual(D.percentile(sorted, 1), 1.71);
  assert.strictEqual(D.percentile(sorted, 0.5), -142);
}

const tests = [
  testSeriesOrdering,
  testNearestRespectsTolerance,
  testGapsBecomeNull,
  testDerivedRates,
  testLatencyIsConvertedToMs,
  testMobilityTrailAndCursorLookup,
  testStoreTracksUesAndWindow,
  testControlAndRuntimePanels,
  testCursorBus,
  testColoursAreStablePerUe,
  testMapColourScaleClipsTails
];

let failed = 0;
tests.forEach(function (t) {
  try {
    t();
    console.log("  ok   " + t.name);
  } catch (e) {
    failed++;
    console.log("  FAIL " + t.name + ": " + e.message);
  }
});

console.log(failed === 0 ? "dash_logic_test: " + tests.length + " ok" : "dash_logic_test: " + failed + " failed");
process.exit(failed === 0 ? 0 : 1);
