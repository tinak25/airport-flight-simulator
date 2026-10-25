#!/usr/bin/env python3
"""Times every version of the simulator and writes results/benchmarks.csv.

Each configuration is run REPEATS times and the median is kept, because a
single run on a shared machine is noisy. Run from the repo root after `make`:

    python3 bench/run_benchmarks.py            # default settings
    MPI_EXTRA=--oversubscribe python3 bench/run_benchmarks.py
"""
import csv
import os
import platform
import statistics
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BIN = ROOT / "bin"
OUT = ROOT / "results" / "benchmarks.csv"
REPEATS = int(os.environ.get("REPEATS", "5"))
MPIRUN = os.environ.get("MPIRUN", "mpirun").split()
MPI_EXTRA = os.environ.get("MPI_EXTRA", "").split()
BASE = ["--terminals", "6", "--flights", "2000", "--seed", "42"]


def run(cmd):
    out = subprocess.run(cmd, capture_output=True, text=True, check=True).stdout
    report = [l for l in out.splitlines() if not l.startswith("==") and not l.startswith("elapsed_seconds")]
    secs = float(next(l for l in out.splitlines() if l.startswith("elapsed_seconds")).split(":")[1])
    return secs, "\n".join(report)


def timed(cmd):
    times, reports = [], set()
    for _ in range(REPEATS):
        t, rep = run(cmd)
        times.append(t)
        reports.add(rep)
    assert len(reports) == 1, f"output changed between runs of {cmd}"
    return statistics.median(times), reports.pop()


def main():
    rows = []
    cores = os.cpu_count()

    def record(experiment, version, ranks, consumers, lock, work, cmd, baseline_report=None):
        secs, rep = timed(cmd)
        if baseline_report is not None:
            assert rep == baseline_report, f"{version} disagrees with sequential"
        rows.append(dict(experiment=experiment, version=version, ranks=ranks, consumers=consumers,
                         lock=lock, work=work, seconds=round(secs, 6)))
        print(f"{experiment:12s} {version:11s} ranks={ranks} consumers={consumers} lock={lock:6s} "
              f"work={work:6d}  {secs:.3f}s", flush=True)
        return rep

    # 1. Lock strategy: does adding consumer threads help?
    work = 100000
    w = ["--work", str(work)]
    seq = record("lock", "sequential", 1, 0, "-", work, [BIN / "sequential", *BASE, *w])
    for lock in ["held", "shared", "local"]:
        for c in [1, 2, 4]:
            record("lock", "concurrent", 1, c, lock, work,
                   [BIN / "concurrent", *BASE, *w, "--consumers", str(c), "--lock", lock], seq)

    # 2. Granularity: how much work per flight before threads pay off?
    for work in [100, 1000, 10000, 100000]:
        w = ["--work", str(work)]
        base = record("granularity", "sequential", 1, 0, "-", work, [BIN / "sequential", *BASE, *w])
        record("granularity", "concurrent", 1, 2, "local", work,
               [BIN / "concurrent", *BASE, *w, "--consumers", "2"], base)

    # 3. MPI and hybrid
    work = 100000
    w = ["--work", str(work)]
    seq = record("mpi", "sequential", 1, 0, "-", work, [BIN / "sequential", *BASE, *w])
    for n in [1, 2]:
        record("mpi", "mpi", n, 0, "-", work, [*MPIRUN, *MPI_EXTRA, "-n", str(n), BIN / "distributed_mpi", *BASE, *w], seq)
    record("mpi", "hybrid", 1, 2, "local", work,
           [*MPIRUN, *MPI_EXTRA, "-n", "1", BIN / "hybrid", *BASE, *w, "--consumers", "2"], seq)
    record("mpi", "hybrid", 2, 1, "local", work,
           [*MPIRUN, *MPI_EXTRA, "-n", "2", BIN / "hybrid", *BASE, *w, "--consumers", "1"], seq)

    OUT.parent.mkdir(exist_ok=True)
    with open(OUT, "w", newline="") as f:
        f.write(f"# {platform.system()} {platform.machine()}, {cores} logical cores, "
                f"median of {REPEATS} runs\n")
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
