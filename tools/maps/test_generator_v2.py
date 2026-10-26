#!/usr/bin/env python3
"""Unit tests for deterministic v2 map generation."""

from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import numpy as np

from build_manifest import build_manifest
from generator_v2 import (
    GENERATOR_NAME,
    GENERATOR_VERSION,
    MAP_SEMANTIC_VERSION,
    correlated_gaussian,
    generate_components,
    generate_map,
    los_probability,
    pathloss_map,
    write_map,
)


class GeneratorV2Test(unittest.TestCase):
    def test_same_seed_is_identical(self) -> None:
        first = generate_map(
            "URBAN_MICROCELL", 2.38, 42, cell_number=33)
        second = generate_map(
            "URBAN_MICROCELL", 2.38, 42, cell_number=33)
        self.assertEqual(first, second)

    def test_different_seed_changes_realization(self) -> None:
        first = generate_map(
            "URBAN_MICROCELL", 2.38, 42, cell_number=33)
        second = generate_map(
            "URBAN_MICROCELL", 2.38, 43, cell_number=33)
        self.assertNotEqual(first["map"], second["map"])

    def test_components_match_serialized_map(self) -> None:
        components = generate_components(
            "URBAN_MICROCELL", 2.38, 42, cell_number=33)
        payload = generate_map(
            "URBAN_MICROCELL", 2.38, 42, cell_number=33)
        np.testing.assert_array_equal(
            components.link_gain_db,
            np.asarray(payload["map"]),
        )

    def test_production_metadata_is_complete(self) -> None:
        payload = generate_map(
            "URBAN_MICROCELL", 2.38, 42, cell_number=33)
        metadata = payload["metadata"]
        self.assertEqual(metadata["semantic_version"], MAP_SEMANTIC_VERSION)
        self.assertEqual(
            metadata["generator"],
            {"name": GENERATOR_NAME, "version": GENERATOR_VERSION},
        )
        self.assertEqual(metadata["seed"], 42)
        self.assertEqual(metadata["grid_origin"], "explicit-center-cell")
        self.assertEqual(
            metadata["los_probability_model"],
            "3gpp-tr-38.901-v18.1.0-clause-7.4.2-umi",
        )

    def test_independent_abg_golden_vectors(self) -> None:
        cases = [
            ("RURAL_MACROCELL", True, 3.5, 100.0, 86.48136088700551),
            ("URBAN_MICROCELL", False, 26.0, 200.0, 134.03312027380358),
            ("INDOOR_OPEN_OFFICE", False, 3.5, 50.0, 95.94676207475511),
        ]
        for scenario, los, frequency, distance, expected in cases:
            actual = pathloss_map(
                scenario,
                los,
                frequency,
                np.asarray([distance]),
            )[0]
            self.assertAlmostEqual(float(actual), expected, places=10)

    def test_even_v2_grid_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            generate_map(
                "URBAN_MICROCELL", 2.38, 42, cell_number=32)

    def test_shopping_mall_los_is_labelled_legacy(self) -> None:
        payload = generate_map(
            "INDOOR_SHOPPING_MALL", 3.5, 42, cell_number=33)
        self.assertEqual(
            payload["metadata"]["los_probability_model"],
            "legacy-fikore-shopping-mall-heuristic-provenance-pending",
        )

    def test_manifest_preserves_v2_provenance(self) -> None:
        payload = generate_map(
            "URBAN_MICROCELL", 2.38, 42, cell_number=33)
        with tempfile.TemporaryDirectory() as directory:
            path = (
                Path(directory)
                / "macroscopic_fading_map_URBAN_MICROCELL_2.38.json"
            )
            write_map(payload, path)
            manifest = build_manifest(Path(directory))
        self.assertEqual(manifest["schema_version"], 2)
        self.assertEqual(manifest["map_schema"], "v2")
        entry = manifest["maps"][0]
        self.assertEqual(entry["semantic_version"], MAP_SEMANTIC_VERSION)
        self.assertEqual(entry["generator"]["name"], GENERATOR_NAME)
        self.assertEqual(entry["seed"], 42)
        self.assertEqual(
            entry["realization_id"], payload["metadata"]["realization_id"])

    def test_grid_has_explicit_center(self) -> None:
        payload = generate_map(
            "RURAL_MACROCELL", 3.5, 42, cell_number=33)
        self.assertEqual(payload["cell_number"], 33)
        self.assertEqual(
            payload["metadata"]["grid_origin"],
            "explicit-center-cell")
        center = payload["map"][16][16]
        self.assertTrue(np.isfinite(center))

    def test_correlated_field_is_normalized(self) -> None:
        field = correlated_gaussian(
            np.random.default_rng(42), 65, 5.0, 10.0)
        self.assertAlmostEqual(float(np.mean(field)), 0.0, places=10)
        self.assertAlmostEqual(float(np.std(field)), 1.0, places=10)

    def test_correlated_field_has_no_periodic_edge_seam(self) -> None:
        correlations = []
        for seed in range(8):
            field = correlated_gaussian(
                np.random.default_rng(seed), 65, 5.0, 10.0)
            correlations.extend(
                [
                    float(np.corrcoef(field[:, 0], field[:, -1])[0, 1]),
                    float(np.corrcoef(field[0, :], field[-1, :])[0, 1]),
                ]
            )
        self.assertLess(abs(float(np.mean(correlations))), 0.08)

    def test_uma_uses_ue_height_correction(self) -> None:
        distance = np.asarray([200.0])
        normal_ue = los_probability(
            "URBAN_MACROCELL", distance, 1.5)[0]
        high_ue = los_probability(
            "URBAN_MACROCELL", distance, 25.0)[0]
        self.assertLess(normal_ue, high_ue)
        self.assertAlmostEqual(normal_ue, 0.128047, places=5)


if __name__ == "__main__":
    unittest.main()
