#!/usr/bin/env python3
"""Deterministic FikoRE v2 macroscopic-map generator."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from dataclasses import dataclass
from pathlib import Path

import numpy as np

GENERATOR_NAME = "fikore-map-generator"
GENERATOR_VERSION = "2.1.0"
MAP_SEMANTIC_VERSION = "2.1.0"


@dataclass(frozen=True)
class ScenarioParameters:
    los_correlation_m: float
    nlos_correlation_m: float
    los_sigma_db: float
    nlos_sigma_db: float


@dataclass(frozen=True)
class MapComponents:
    cell_size_m: float
    distance_m: np.ndarray
    los_probability: np.ndarray
    los_state: np.ndarray
    los_shadow_db: np.ndarray
    nlos_shadow_db: np.ndarray
    los_pathloss_db: np.ndarray
    nlos_pathloss_db: np.ndarray
    link_gain_db: np.ndarray


SCENARIO_PARAMETERS = {
    "RURAL_MACROCELL": ScenarioParameters(37.0, 120.0, 1.7, 6.7),
    "URBAN_MICROCELL": ScenarioParameters(10.0, 13.0, 4.3, 6.8),
    "URBAN_MACROCELL": ScenarioParameters(37.0, 50.0, 2.4, 5.3),
    "INDOOR_OPEN_OFFICE": ScenarioParameters(10.0, 6.0, 4.3, 7.9),
    "INDOOR_MIXED_OFFICE": ScenarioParameters(10.0, 6.0, 4.3, 7.9),
    "INDOOR_SHOPPING_MALL": ScenarioParameters(10.0, 10.0, 3.3, 4.6),
}

LOS_PROBABILITY_MODELS = {
    "RURAL_MACROCELL": "3gpp-tr-38.901-v18.1.0-clause-7.4.2-rma",
    "URBAN_MICROCELL": "3gpp-tr-38.901-v18.1.0-clause-7.4.2-umi",
    "URBAN_MACROCELL": "3gpp-tr-38.901-v18.1.0-clause-7.4.2-uma",
    "INDOOR_OPEN_OFFICE":
        "3gpp-tr-38.901-v18.1.0-clause-7.4.2-inh-open",
    "INDOOR_MIXED_OFFICE":
        "3gpp-tr-38.901-v18.1.0-clause-7.4.2-inh-mixed",
    "INDOOR_SHOPPING_MALL":
        "legacy-fikore-shopping-mall-heuristic-provenance-pending",
}


def abg_coefficients(
    scenario: str, los: bool, frequency_ghz: float
) -> tuple[float, float, float]:
    if scenario == "RURAL_MACROCELL":
        return (2.16, 32.4, 2.0) if los else (2.75, 32.4, 2.0)
    if scenario == "URBAN_MACROCELL":
        if not los:
            return 3.5, 13.6, 2.4
        return (2.2, 28.0, 2.0) if frequency_ghz < 6 else (1.9, 35.8, 1.9)
    if scenario == "URBAN_MICROCELL":
        if not los:
            return 2.8, 31.4, 2.7
        return (2.27, 27.02, 2.0) if frequency_ghz < 6 else (1.1, 46.8, 2.1)
    if scenario in {"INDOOR_OPEN_OFFICE", "INDOOR_MIXED_OFFICE"}:
        if not los:
            return (4.33, 11.5, 2.0) if frequency_ghz < 6 else (3.9, 19.0, 2.1)
        return (1.87, 32.82, 2.0) if frequency_ghz < 6 else (1.6, 32.9, 1.8)
    if scenario == "INDOOR_SHOPPING_MALL":
        return (1.9, 31.2, 2.2) if los else (2.0, 34.4, 2.3)
    raise ValueError(f"unsupported scenario: {scenario}")


def los_probability(
    scenario: str, distance_m: np.ndarray, ue_height_m: float
) -> np.ndarray:
    distance = np.maximum(distance_m, 1e-9)
    if scenario == "RURAL_MACROCELL":
        return np.where(
            distance <= 10.0, 1.0, np.exp(-(distance - 10.0) / 1000.0))
    if scenario == "URBAN_MICROCELL":
        probability = 18.0 / distance + np.exp(-distance / 36.0) * (
            1.0 - 18.0 / distance)
        return np.where(distance <= 18.0, 1.0, probability)
    if scenario == "URBAN_MACROCELL":
        correction = (
            0.0
            if ue_height_m <= 13.0
            else ((ue_height_m - 13.0) / 10.0) ** 1.5)
        probability = (
            18.0 / distance
            + np.exp(-distance / 63.0) * (1.0 - 18.0 / distance)
        ) * (
            1.0
            + correction
            * 1.25
            * (distance / 100.0) ** 3
            * np.exp(-distance / 150.0)
        )
        return np.where(distance <= 18.0, 1.0, probability)
    if scenario == "INDOOR_OPEN_OFFICE":
        return np.where(
            distance <= 5.0,
            1.0,
            np.where(
                distance <= 49.0,
                np.exp(-(distance - 5.0) / 70.8),
                0.54 * np.exp(-(distance - 49.0) / 211.7),
            ),
        )
    if scenario == "INDOOR_MIXED_OFFICE":
        return np.where(
            distance <= 1.2,
            1.0,
            np.where(
                distance <= 6.5,
                np.exp(-(distance - 1.2) / 4.7),
                0.32 * np.exp(-(distance - 6.5) / 32.6),
            ),
        )
    if scenario == "INDOOR_SHOPPING_MALL":
        return 0.95 * np.exp(-distance / 15.2) + 0.05
    raise ValueError(f"unsupported scenario: {scenario}")


def normal_cdf(values: np.ndarray) -> np.ndarray:
    erf = np.vectorize(math.erf, otypes=[float])
    return 0.5 * (1.0 + erf(values / math.sqrt(2.0)))


def correlated_gaussian(
    rng: np.random.Generator,
    cell_number: int,
    cell_size_m: float,
    correlation_m: float,
) -> np.ndarray:
    # Embed the target covariance in a grid twice as wide, generate a periodic
    # field there, and crop one contiguous cell_number square. Within the crop,
    # Euclidean separations use the intended covariance rather than wrapping
    # opposite production-map edges into artificial neighbors.
    embedding_size = 2 * cell_number
    indices = np.arange(embedding_size)
    wrapped_distance = np.minimum(
        indices, embedding_size - indices) * cell_size_m
    x_grid, y_grid = np.meshgrid(
        wrapped_distance, wrapped_distance)
    covariance = np.exp(
        -np.hypot(x_grid, y_grid) / correlation_m)
    spectrum = np.real(np.fft.fft2(covariance))
    spectrum = np.maximum(spectrum, 0.0)
    white = rng.normal(size=(embedding_size, embedding_size))
    embedded_field = np.real(
        np.fft.ifft2(np.fft.fft2(white) * np.sqrt(spectrum)))
    crop_start = (embedding_size - cell_number) // 2
    crop_end = crop_start + cell_number
    field = embedded_field[
        crop_start:crop_end,
        crop_start:crop_end,
    ]
    field -= float(np.mean(field))
    standard_deviation = float(np.std(field))
    if standard_deviation <= 0.0:
        raise ValueError("generated field has zero variance")
    return field / standard_deviation


def pathloss_map(
    scenario: str,
    los: bool,
    frequency_ghz: float,
    distance_m: np.ndarray,
) -> np.ndarray:
    alpha, beta, gamma = abg_coefficients(
        scenario, los, frequency_ghz)
    distance = np.maximum(distance_m, 1.0)
    return (
        10.0 * alpha * np.log10(distance)
        + beta
        + 10.0 * gamma * math.log10(frequency_ghz)
    )


def parameter_hash(parameters: dict) -> str:
    encoded = json.dumps(
        parameters, sort_keys=True, separators=(",", ":")).encode()
    return hashlib.sha256(encoded).hexdigest()


def generate_components(
    scenario: str,
    frequency_ghz: float,
    seed: int,
    cell_number: int = 291,
    ue_height_m: float = 1.5,
) -> MapComponents:
    if cell_number < 3 or cell_number % 2 == 0:
        raise ValueError(
            "v2 cell_number must be odd and at least 3")
    parameters = SCENARIO_PARAMETERS[scenario]
    cell_size_m = 0.5 * min(
        parameters.los_correlation_m,
        parameters.nlos_correlation_m)
    center = (cell_number - 1) / 2.0
    axis = (np.arange(cell_number) - center) * cell_size_m
    x_grid, y_grid = np.meshgrid(axis, axis)
    distance = np.hypot(x_grid, y_grid)

    seed_sequence = np.random.SeedSequence(seed)
    shadow_los_seed, shadow_nlos_seed, los_state_seed = (
        seed_sequence.spawn(3))
    shadow_los = correlated_gaussian(
        np.random.default_rng(shadow_los_seed),
        cell_number,
        cell_size_m,
        parameters.los_correlation_m,
    ) * parameters.los_sigma_db
    shadow_nlos = correlated_gaussian(
        np.random.default_rng(shadow_nlos_seed),
        cell_number,
        cell_size_m,
        parameters.nlos_correlation_m,
    ) * parameters.nlos_sigma_db

    los_uniform = normal_cdf(
        correlated_gaussian(
            np.random.default_rng(los_state_seed),
            cell_number,
            cell_size_m,
            parameters.los_correlation_m))
    probability = np.clip(
        los_probability(scenario, distance, ue_height_m), 0.0, 1.0)
    los_state = los_uniform <= probability

    los_pathloss = pathloss_map(
        scenario, True, frequency_ghz, distance)
    nlos_pathloss = pathloss_map(
        scenario, False, frequency_ghz, distance)
    macroscopic_los = shadow_los - los_pathloss
    macroscopic_nlos = shadow_nlos - nlos_pathloss
    final_map = np.where(
        los_state, macroscopic_los, macroscopic_nlos)

    return MapComponents(
        cell_size_m=cell_size_m,
        distance_m=distance,
        los_probability=probability,
        los_state=los_state,
        los_shadow_db=shadow_los,
        nlos_shadow_db=shadow_nlos,
        los_pathloss_db=los_pathloss,
        nlos_pathloss_db=nlos_pathloss,
        link_gain_db=final_map,
    )


def generate_map(
    scenario: str,
    frequency_ghz: float,
    seed: int,
    cell_number: int = 291,
    ue_height_m: float = 1.5,
) -> dict:
    parameters = SCENARIO_PARAMETERS[scenario]
    components = generate_components(
        scenario,
        frequency_ghz,
        seed,
        cell_number,
        ue_height_m,
    )
    los_coefficients = abg_coefficients(
        scenario, True, frequency_ghz)
    nlos_coefficients = abg_coefficients(
        scenario, False, frequency_ghz)
    generation_parameters = {
        "scenario": scenario,
        "frequency_ghz": frequency_ghz,
        "seed": seed,
        "cell_number": cell_number,
        "cell_size_m": components.cell_size_m,
        "ue_height_m": ue_height_m,
        "los_abg": {
            "alpha": los_coefficients[0],
            "beta": los_coefficients[1],
            "gamma": los_coefficients[2],
        },
        "nlos_abg": {
            "alpha": nlos_coefficients[0],
            "beta": nlos_coefficients[1],
            "gamma": nlos_coefficients[2],
        },
        "los_shadow_sigma_db": parameters.los_sigma_db,
        "nlos_shadow_sigma_db": parameters.nlos_sigma_db,
        "los_decorrelation_m": parameters.los_correlation_m,
        "nlos_decorrelation_m": parameters.nlos_correlation_m,
        "los_state_model": "correlated-gaussian-cdf-threshold",
        "los_probability_model": LOS_PROBABILITY_MODELS[scenario],
        "coefficient_validity_range": "not-encoded",
    }
    realization_hash = parameter_hash(generation_parameters)
    metadata = {
        **generation_parameters,
        "realization_id": (
            f"{scenario.lower()}-{frequency_ghz:g}-"
            f"seed-{seed}-{realization_hash[:12]}"
        ),
        "generator": {
            "name": GENERATOR_NAME,
            "version": GENERATOR_VERSION,
        },
        "grid_origin": "explicit-center-cell",
        "pathloss_family": "ABG",
        "semantic_version": MAP_SEMANTIC_VERSION,
    }
    return {
        "schema_version": 2,
        "metadata": metadata,
        "cell_number": cell_number,
        "cell_size": components.cell_size_m,
        "map": components.link_gain_db.tolist(),
    }


def write_map(payload: dict, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(
            payload,
            sort_keys=True,
            separators=(",", ":"),
            allow_nan=False,
        )
        + "\n"
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "scenario", choices=sorted(SCENARIO_PARAMETERS))
    parser.add_argument("frequency_ghz", type=float)
    parser.add_argument("--seed", type=int, required=True)
    parser.add_argument("--cell-number", type=int, default=291)
    parser.add_argument("--ue-height-m", type=float, default=1.5)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    payload = generate_map(
        args.scenario,
        args.frequency_ghz,
        args.seed,
        args.cell_number,
        args.ue_height_m,
    )
    write_map(payload, args.output.resolve())
    print(args.output.resolve())


if __name__ == "__main__":
    main()
