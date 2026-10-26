#!/usr/bin/env python3
"""Verify hashes in the committed PHY Model V2 evidence manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MANIFEST = (
    ROOT / "docs" / "baselines" / "phy-model-v2-evidence-manifest.json"
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def check(path_text: str, expected: str) -> None:
    path = ROOT / path_text
    if not path.is_file():
        raise ValueError(f"evidence file is missing: {path_text}")
    actual = sha256(path)
    if actual != expected:
        raise ValueError(
            f"evidence hash mismatch for {path_text}: "
            f"expected {expected}, got {actual}")


def commit_exists(commit: str) -> bool:
    return subprocess.run(
        ["git", "cat-file", "-e", f"{commit}^{{commit}}"],
        cwd=ROOT,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    ).returncode == 0


def resolve_commit(
    commit: str,
    rebased_equivalents: dict[str, str],
) -> str:
    if commit_exists(commit):
        return commit
    equivalent = rebased_equivalents.get(commit)
    if equivalent is None or not commit_exists(equivalent):
        raise ValueError(
            f"evidence source commit is unavailable: {commit}")
    return equivalent


def check_git_blob(
    commit: str,
    path_text: str,
    expected: str,
    rebased_equivalents: dict[str, str],
) -> None:
    source_commit = resolve_commit(commit, rebased_equivalents)
    payload = subprocess.check_output(
        ["git", "show", f"{source_commit}:{path_text}"],
        cwd=ROOT,
    )
    actual = hashlib.sha256(payload).hexdigest()
    if actual != expected:
        raise ValueError(
            f"git blob hash mismatch for {source_commit}:{path_text}: "
            f"expected {expected}, got {actual}")


def stable_patch_id(commit: str) -> str:
    patch = subprocess.check_output(
        ["git", "show", "--pretty=format:", commit],
        cwd=ROOT,
    )
    result = subprocess.check_output(
        ["git", "patch-id", "--stable"],
        cwd=ROOT,
        input=patch,
        text=False,
    ).decode().strip()
    if not result:
        raise ValueError(f"commit has no stable patch id: {commit}")
    return result.split()[0]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.resolve().read_text())
    checked = 0
    rebase_mapping = None
    rebased_equivalents: dict[str, str] = {}
    rebase_source_map = manifest.get("rebase_source_map")
    if rebase_source_map is not None:
        check(
            rebase_source_map["path"],
            rebase_source_map["sha256"],
        )
        checked += 1
        rebase_mapping = json.loads(
            (ROOT / rebase_source_map["path"]).read_text()
        )
        rebased_equivalents = {
            entry["old_commit"]: entry["rebased_commit"]
            for entry in rebase_mapping["mappings"]
        }

    for key in ("production_catalog", "production_manifest"):
        artifact = manifest["map_generation"][key]
        check(artifact["path"], artifact["sha256"])
        checked += 1

    map_manifest_path = (
        ROOT / manifest["map_generation"]["production_manifest"]["path"]
    )
    map_manifest = json.loads(map_manifest_path.read_text())
    for entry in map_manifest["maps"]:
        check(
            f"include/maps_scenarios/{entry['file']}",
            entry["sha256"],
        )
        checked += 1

    for profile in manifest["canonical_profiles"]:
        check(profile["path"], profile["validated_sha256"])
        checked += 1

    for batch in manifest["run_batches"]:
        for path_key, hash_key in (
            ("manifest", "manifest_sha256"),
            ("summary", "summary_sha256"),
            ("paired_summary", "paired_summary_sha256"),
        ):
            if path_key in batch:
                check(batch[path_key], batch[hash_key])
                checked += 1
        if "manifest" in batch:
            run_manifest = json.loads(
                (ROOT / batch["manifest"]).read_text())
            if run_manifest.get("source_sha") != batch["source_sha"]:
                raise ValueError(
                    f"run source mismatch for {batch['id']}")
            for run in run_manifest.get("runs", []):
                check(
                    run["rendered_config"],
                    run["rendered_config_sha256"],
                )
                checked += 1
                if "map_source_git_commit" in run:
                    check_git_blob(
                        run["map_source_git_commit"],
                        run["map_source_git_path"],
                        run["map_sha256"],
                        rebased_equivalents,
                    )
                else:
                    check(
                        f"include/maps_scenarios/{run['map_file']}",
                        run["map_sha256"],
                    )
                checked += 1

    harq_packages = []
    if manifest.get("harq_evidence") is not None:
        harq_packages.append(manifest["harq_evidence"])
    harq_packages.extend(
        manifest.get("historical_harq_evidence", []))
    for harq_evidence in harq_packages:
        check(harq_evidence["path"], harq_evidence["sha256"])
        checked += 1
        harq_manifest = json.loads(
            (ROOT / harq_evidence["path"]).read_text()
        )
        for artifact in harq_manifest["artifacts"]:
            check(artifact["path"], artifact["sha256"])
            checked += 1
        for campaign in harq_manifest["campaigns"]:
            check(campaign["manifest"], campaign["manifest_sha256"])
            checked += 1
            campaign_manifest = json.loads(
                (ROOT / campaign["manifest"]).read_text()
            )
            if (
                campaign_manifest["source_sha"]
                != campaign["source_sha"]
            ):
                raise ValueError(
                    f"HARQ run source mismatch for {campaign['label']}"
                )
            for run in campaign_manifest["runs"]:
                check(
                    run["rendered_config"],
                    run["rendered_config_sha256"],
                )
                checked += 1
                check(
                    f"include/maps_scenarios/{run['map_file']}",
                    run["map_sha256"],
                )
                checked += 1

    scheduler_packages = []
    if manifest.get("scheduler_evidence") is not None:
        scheduler_packages.append(manifest["scheduler_evidence"])
    scheduler_packages.extend(
        manifest.get("historical_scheduler_evidence", []))
    for scheduler_evidence in scheduler_packages:
        check(
            scheduler_evidence["path"],
            scheduler_evidence["sha256"],
        )
        checked += 1
        scheduler_manifest = json.loads(
            (ROOT / scheduler_evidence["path"]).read_text()
        )
        resolve_commit(
            scheduler_manifest["source_sha"],
            rebased_equivalents,
        )
        for artifact in scheduler_manifest["artifacts"]:
            check(artifact["path"], artifact["sha256"])
            checked += 1

    if rebase_mapping is not None:
        for entry in rebase_mapping["mappings"]:
            observed = stable_patch_id(entry["rebased_commit"])
            if observed != entry["stable_patch_id"]:
                raise ValueError(
                    "rebased patch-id mismatch for "
                    f"{entry['rebased_commit']}"
                )
            old_exists = commit_exists(entry["old_commit"])
            if (
                old_exists
                and stable_patch_id(entry["old_commit"])
                != entry["stable_patch_id"]
            ):
                raise ValueError(
                    "pre-rebase patch-id mismatch for "
                    f"{entry['old_commit']}"
                )

    analysis_metadata = json.loads(
        (
            ROOT
            / "docs/baselines/phy-v2-production-analysis-metadata.json"
        ).read_text()
    )
    if (
        analysis_metadata.get("analyzer_source_sha")
        != manifest["source"]["analyzer_source_sha"]
    ):
        raise ValueError("analysis metadata source SHA is stale")

    manuscript = (
        ROOT / "docs/phy-model-evolution-external-review.md"
    ).read_text()
    implementation_match = re.search(
        r"\*\*Model implementation source:\*\* `([0-9a-f]{40})`",
        manuscript,
    )
    profile_match = re.search(
        r"\*\*Packet-profile source:\*\* `([0-9a-f]{40})`",
        manuscript,
    )
    runtime_match = re.search(
        r"\*\*Runtime-benchmark source:\*\* `([0-9a-f]{40})`",
        manuscript,
    )
    if (
        implementation_match is None
        or implementation_match.group(1)
        != manifest["source"]["implementation_source_sha"]
    ):
        raise ValueError("manuscript implementation source SHA is stale")
    if (
        profile_match is None
        or profile_match.group(1)
        != manifest["source"]["packet_profile_source_sha"]
    ):
        raise ValueError("manuscript profile source SHA is stale")
    if (
        runtime_match is None
        or runtime_match.group(1)
        != manifest["source"]["runtime_benchmark_source_sha"]
    ):
        raise ValueError("manuscript runtime source SHA is stale")
    subprocess.check_call(
        [
            "git",
            "cat-file",
            "-e",
            f"{manifest['source']['map_catalog_source_sha']}^{{commit}}",
        ],
        cwd=ROOT,
    )

    identifiers = set()
    for artifact in manifest["committed_evidence"]:
        identifier = artifact["id"]
        if identifier in identifiers:
            raise ValueError(f"duplicate evidence identifier: {identifier}")
        identifiers.add(identifier)
        check(artifact["path"], artifact["sha256"])
        checked += 1

    if manifest["pending_evidence"]:
        raise ValueError(
            f"evidence remains pending: {manifest['pending_evidence']}")
    print(f"verified {checked} PHY Model V2 evidence artifacts")


if __name__ == "__main__":
    main()
