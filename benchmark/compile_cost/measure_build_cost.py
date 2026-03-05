#!/usr/bin/env python3

import argparse
import os
import pathlib
import statistics
import subprocess
import sys
import time
import shutil
import tempfile

TARGETS = [
    ("buildcost_etch_tdfa", "etch_tdfa"),
    ("buildcost_etch_tnfa", "etch_tnfa"),
    ("buildcost_ctre", "ctre"),
    ("buildcost_std_regex", "std_regex"),
]


def run(cmd, cwd, env, quiet=False):
    stdout = subprocess.DEVNULL if quiet else None
    stderr = subprocess.STDOUT if quiet else None
    subprocess.run(cmd, cwd=cwd, env=env, check=True, stdout=stdout, stderr=stderr)


def find_binary(buildir: pathlib.Path, target: str) -> pathlib.Path:
    candidates = []
    for path in buildir.rglob("*"):
        if not path.is_file():
            continue
        if path.name == target or path.name == f"{target}.exe":
            candidates.append(path)
    if not candidates:
        raise FileNotFoundError(f"cannot locate binary for target {target}")
    candidates.sort(key=lambda p: p.stat().st_mtime, reverse=True)
    return candidates[0]


def main() -> int:
    parser = argparse.ArgumentParser(description="Measure compile-time and binary-size cost for regex engines")
    parser.add_argument("--repeat", type=int, default=3, help="number of repeated clean builds per target")
    parser.add_argument("--quiet", action="store_true", help="suppress xmake output")
    args = parser.parse_args()

    root = pathlib.Path(__file__).resolve().parents[2]
    env = os.environ.copy()
    run_root = pathlib.Path(tempfile.mkdtemp(prefix="etch-buildcost-"))

    print("Build-cost benchmark (clean build, release, ccache disabled)")
    print(f"repeat={args.repeat}")
    print()
    print(f"{'engine':<12}{'compile_s(avg)':>16}{'compile_s(min)':>16}{'compile_s(max)':>16}{'binary_bytes':>16}")
    print("-" * 76)

    try:
        for target, label in TARGETS:
            elapsed = []
            binary_size = None
            for i in range(args.repeat):
                buildir = run_root / f"{target}_{i}"
                if buildir.exists():
                    shutil.rmtree(buildir, ignore_errors=True)
                buildir.parent.mkdir(parents=True, exist_ok=True)

                run(["xmake",
                     "f",
                     "-y",
                     "-m",
                     "release",
                     "--benchmark=y",
                     "--test=n",
                     "--ccache=n",
                     f"--builddir={buildir}"],
                    cwd=root,
                    env=env,
                    quiet=args.quiet)
                t0 = time.perf_counter()
                run(["xmake", "-y", "-b", target], cwd=root, env=env, quiet=args.quiet)
                dt = time.perf_counter() - t0
                elapsed.append(dt)

                binary = find_binary(buildir, target)
                binary_size = binary.stat().st_size

            avg_t = statistics.mean(elapsed)
            min_t = min(elapsed)
            max_t = max(elapsed)
            print(f"{label:<12}{avg_t:>16.2f}{min_t:>16.2f}{max_t:>16.2f}{binary_size:>16d}")
    finally:
        # Restore default project builddir/config so later normal builds are not polluted.
        run(["xmake", "f", "-y", "-m", "release", "--benchmark=y", "--test=y"], cwd=root, env=env, quiet=True)
        shutil.rmtree(run_root, ignore_errors=True)

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as exc:
        print(f"command failed: {exc}", file=sys.stderr)
        raise
