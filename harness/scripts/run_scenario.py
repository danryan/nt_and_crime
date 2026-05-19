#!/usr/bin/env python3
"""Drive a host-simulator binary through a YAML scenario; diff outputs vs golden."""
import argparse
import json
import subprocess
import sys
import yaml
from pathlib import Path


def load_scenario(path: Path) -> dict:
    return yaml.safe_load(path.read_text())


def run_simulator(binary: Path, scenario_json: str, output_dir: Path) -> int:
    output_dir.mkdir(parents=True, exist_ok=True)
    proc = subprocess.run(
        [str(binary), "--scenario-json", "-", "--output-dir", str(output_dir)],
        input=scenario_json,
        text=True,
        capture_output=True,
    )
    if proc.stdout:
        sys.stdout.write(proc.stdout)
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr)
    return proc.returncode


def diff_or_write_golden(actual_dir: Path, scenario: dict, write_golden: bool) -> int:
    expected = scenario.get("expect", {})
    rc = 0
    kind_map = {
        "bus_out": "out_bus.bin",
        "screen": "out_screen.bin",
        "params": "out_params.log",
    }
    for kind, golden_path in expected.items():
        actual = actual_dir / kind_map[kind]
        golden = Path(golden_path)
        if write_golden:
            golden.parent.mkdir(parents=True, exist_ok=True)
            golden.write_bytes(actual.read_bytes())
            print(f"wrote golden: {golden}")
        else:
            diff_kind = kind.split("_")[0] if "_" in kind else kind
            r = subprocess.run(
                [
                    "python3",
                    "harness/scripts/diff_outputs.py",
                    diff_kind,
                    str(golden),
                    str(actual),
                ]
            )
            if r.returncode != 0:
                rc = r.returncode
    return rc


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("scenario", type=Path)
    parser.add_argument("--binary", default=None, type=Path)
    parser.add_argument("--write-golden", action="store_true")
    parser.add_argument("--output-dir", default=Path("out/"), type=Path)
    args = parser.parse_args()

    scenario = load_scenario(args.scenario)
    binary = args.binary or Path(f"build/host/sim_{scenario['plugin']}")
    rc = run_simulator(binary, json.dumps(scenario), args.output_dir)
    if rc != 0:
        return rc

    if args.write_golden:
        return diff_or_write_golden(args.output_dir, scenario, True)

    diff_script = Path("harness/scripts/diff_outputs.py")
    if not diff_script.exists():
        print(
            "warn: diff_outputs.py missing; skipping diff",
            file=sys.stderr,
        )
        return 0

    return diff_or_write_golden(args.output_dir, scenario, False)


if __name__ == "__main__":
    sys.exit(main())
