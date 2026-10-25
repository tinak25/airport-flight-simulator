#!/usr/bin/env python3
"""Draws the charts in charts/ from results/benchmarks.csv."""
import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parent.parent
CSV = ROOT / "results" / "benchmarks.csv"
OUT = ROOT / "charts"

BLUE, ORANGE, GREEN = "#2a78d6", "#eb6834", "#1baf7a"
INK, MUTED, GRID, SURFACE = "#0b0b0b", "#52514e", "#e4e3df", "#fcfcfb"

plt.rcParams.update({
    "figure.facecolor": SURFACE, "axes.facecolor": SURFACE, "savefig.facecolor": SURFACE,
    "axes.edgecolor": GRID, "axes.labelcolor": MUTED, "xtick.color": MUTED, "ytick.color": MUTED,
    "axes.spines.top": False, "axes.spines.right": False, "axes.grid": True, "axes.grid.axis": "y",
    "grid.color": GRID, "grid.linewidth": 0.8, "font.size": 10, "axes.titlesize": 12,
    "axes.titleweight": "bold", "axes.titlecolor": INK, "axes.titlelocation": "left",
})


def load():
    with open(CSV) as f:
        machine = f.readline().lstrip("# ").strip()
        rows = list(csv.DictReader(f))
    for r in rows:
        r["seconds"] = float(r["seconds"])
        for k in ("ranks", "consumers", "work"):
            r[k] = int(r[k])
    return machine, rows


def footnote(fig, machine):
    fig.text(0.01, 0.01, f"Measured on {machine}.", color=MUTED, fontsize=8)


def lock_chart(rows, machine):
    data = [r for r in rows if r["experiment"] == "lock"]
    seq = next(r["seconds"] for r in data if r["version"] == "sequential")
    consumers = [1, 2, 4]
    modes = [("held", "Lock held during work (original bug)", ORANGE),
             ("shared", "Lock only around the update", BLUE),
             ("local", "Per-thread stats, merged at the end", GREEN)]
    fig, ax = plt.subplots(figsize=(8, 4.6))
    width = 0.26
    for i, (mode, label, color) in enumerate(modes):
        xs = [c_i + (i - 1) * width for c_i in range(len(consumers))]
        ys = [next(r["seconds"] for r in data if r["lock"] == mode and r["consumers"] == c) for c in consumers]
        bars = ax.bar(xs, ys, width * 0.92, color=color, label=label)
        for b, y in zip(bars, ys):
            ax.text(b.get_x() + b.get_width() / 2, y + 0.03, f"{y:.2f}", ha="center", va="bottom",
                    fontsize=8, color=INK)
    ax.axhline(seq, color=MUTED, linestyle="--", linewidth=1)
    ax.text(len(consumers) - 0.5, seq + 0.03, f"sequential {seq:.2f}s", ha="right", va="bottom",
            fontsize=8, color=MUTED)
    ax.set_xticks(range(len(consumers)), [f"{c} consumer{'s' if c > 1 else ''}" for c in consumers])
    ax.set_ylabel("Seconds (lower is better)")
    ax.set_title("Holding the lock while working cancels out every extra thread")
    ax.legend(frameon=False, loc="upper center", bbox_to_anchor=(0.5, -0.1), ncol=3, fontsize=8)
    fig.tight_layout(rect=(0, 0.04, 1, 1))
    footnote(fig, machine)
    fig.savefig(OUT / "lock_strategies.png", dpi=160)
    plt.close(fig)


def granularity_chart(rows, machine):
    data = [r for r in rows if r["experiment"] == "granularity"]
    works = sorted({r["work"] for r in data})
    speedups = []
    for w in works:
        s = next(r["seconds"] for r in data if r["work"] == w and r["version"] == "sequential")
        c = next(r["seconds"] for r in data if r["work"] == w and r["version"] == "concurrent")
        speedups.append(s / c)
    fig, ax = plt.subplots(figsize=(7.5, 4.3))
    xs = range(len(works))
    ax.plot(xs, speedups, color=BLUE, marker="o", linewidth=2)
    for x, y in zip(xs, speedups):
        ax.text(x, y + 0.06, f"{y:.2f}x", ha="center", va="bottom", fontsize=9, color=INK)
    ax.axhline(1, color=MUTED, linestyle="--", linewidth=1)
    ax.text(0, 1.03, "no gain", fontsize=8, color=MUTED, va="bottom")
    ax.set_xticks(list(xs), [f"{w:,}" for w in works])
    ax.set_xlabel("CPU work per flight (loop iterations)")
    ax.set_ylabel("Speed-up of 2 consumer threads vs sequential")
    ax.set_ylim(0, max(2.2, max(speedups) + 0.4))
    ax.set_title("Threads only pay off when each flight carries enough work")
    fig.tight_layout(rect=(0, 0.04, 1, 1))
    footnote(fig, machine)
    fig.savefig(OUT / "granularity.png", dpi=160)
    plt.close(fig)


def mpi_chart(rows, machine):
    data = [r for r in rows if r["experiment"] == "mpi"]
    order = [("sequential", 1, 0, "Sequential"), ("mpi", 1, 0, "MPI, 1 rank"), ("mpi", 2, 0, "MPI, 2 ranks"),
             ("hybrid", 1, 2, "Hybrid, 1 rank x 2 threads"), ("hybrid", 2, 1, "Hybrid, 2 ranks x 1 thread")]
    labels, ys, colors = [], [], []
    for version, ranks, cons, label in order:
        ys.append(next(r["seconds"] for r in data if r["version"] == version and r["ranks"] == ranks
                       and r["consumers"] == cons))
        labels.append(label)
        colors.append(MUTED if version == "sequential" else (BLUE if version == "mpi" else GREEN))
    fig, ax = plt.subplots(figsize=(8, 4.3))
    bars = ax.barh(labels[::-1], ys[::-1], color=colors[::-1], height=0.6)
    for b, y in zip(bars, ys[::-1]):
        ax.text(y + 0.02, b.get_y() + b.get_height() / 2, f"{y:.2f}s  ({ys[0] / y:.2f}x)", va="center",
                fontsize=9, color=INK)
    ax.grid(axis="x", color=GRID)
    ax.grid(axis="y", visible=False)
    ax.set_xlabel("Seconds (lower is better); speed-up vs sequential in brackets")
    ax.set_xlim(0, max(ys) * 1.3)
    ax.set_title("Same report, split across processes and threads")
    fig.tight_layout(rect=(0, 0.04, 1, 1))
    footnote(fig, machine)
    fig.savefig(OUT / "mpi_and_hybrid.png", dpi=160)
    plt.close(fig)


if __name__ == "__main__":
    OUT.mkdir(exist_ok=True)
    machine, rows = load()
    lock_chart(rows, machine)
    granularity_chart(rows, machine)
    mpi_chart(rows, machine)
    print("charts written to", OUT)
