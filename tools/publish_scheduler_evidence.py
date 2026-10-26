#!/usr/bin/env python3
"""Publish compact scheduler-family benchmark evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FILES = ("results.csv", "report.md", "metadata.json", "failures.csv")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--replace", action="store_true")
    args = parser.parse_args()
    source = args.source.resolve()
    output = args.output.resolve()
    if output.exists():
        if not args.replace:
            raise SystemExit(
                f"refusing to replace existing output: {output}")
        shutil.rmtree(output)

    metadata = json.loads((source / "metadata.json").read_text())
    if metadata["source_dirty"]:
        raise SystemExit("scheduler evidence source was dirty")
    if metadata["failed_cases"] != 0:
        raise SystemExit("scheduler benchmark contains failed cases")
    if (source / "failures.csv").read_text():
        raise SystemExit("scheduler failure file is not empty")

    output.mkdir(parents=True)
    artifacts = []
    for name in FILES:
        destination = output / name
        shutil.copyfile(source / name, destination)
        artifacts.append(
            {
                "path": str(destination.relative_to(ROOT)),
                "sha256": sha256(destination),
            }
        )
    evidence = {
        "schema_version": 1,
        "source_sha": metadata["source_sha"],
        "raw_runtime_samples_committed": False,
        "artifacts": artifacts,
    }
    (output / "evidence.json").write_text(
        json.dumps(evidence, indent=2, sort_keys=True) + "\n"
    )
    print(output)


if __name__ == "__main__":
    main()
