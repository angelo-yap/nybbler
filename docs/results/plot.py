import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
from data import data

classes = ["bitwise", "arithmetic", "shift", "compare"]
widths = ["i1", "i2", "i4"]

def get(cls, w, idx):
    for row in data:
        if row[0] == cls and row[1] == w:
            return row[idx]
    return None

# --- Figure 1: static instruction-count ratio, faceted by class, grouped by width ---
fig, axes = plt.subplots(1, 4, figsize=(14, 4), sharey=True)
fig.suptitle("Static instruction-count ratio (baseline / nybbler), x86-64 AVX2 (cross-compiled)", fontsize=11)
for ax, cls in zip(axes, classes):
    vals = [get(cls, w, 2) for w in widths]
    bars = ax.bar(widths, vals, color="#4C72B0")
    ax.set_title(cls)
    ax.set_ylim(0, 70)
    ax.axhline(1.0, color="gray", linewidth=0.8, linestyle="--")
    for b, v in zip(bars, vals):
        ax.text(b.get_x() + b.get_width()/2, v + 1, f"{v:.1f}x", ha="center", fontsize=9)
axes[0].set_ylabel("ratio (higher = better)")
plt.tight_layout()
plt.savefig("static_ratio.png", dpi=150)
plt.close()

# --- Figure 2: wall-clock speedup, faceted by class, grouped by width ---
fig, axes = plt.subplots(1, 4, figsize=(14, 4), sharey=True)
fig.suptitle("Wall-clock speedup (baseline ns/v / lowered ns/v), native arm64 (Apple M5)", fontsize=11)
for ax, cls in zip(axes, classes):
    vals = [get(cls, w, 3) for w in widths]
    bars = ax.bar(widths, vals, color="#DD8452")
    ax.set_title(cls)
    ax.set_ylim(0, 50)
    ax.axhline(1.0, color="gray", linewidth=0.8, linestyle="--")
    for b, v in zip(bars, vals):
        ax.text(b.get_x() + b.get_width()/2, v + 1, f"{v:.2f}x", ha="center", fontsize=9)
axes[0].set_ylabel("speedup (higher = better)")
plt.tight_layout()
plt.savefig("wallclock_speedup.png", dpi=150)
plt.close()

# --- Figure 3: side-by-side comparison, both datasets, all 12 cells ---
fig, ax = plt.subplots(figsize=(13, 5))
labels = [f"{c}\n{w}" for c in classes for w in widths]
static_vals = [get(c, w, 2) for c in classes for w in widths]
wc_vals = [get(c, w, 3) for c in classes for w in widths]
x = np.arange(len(labels))
width = 0.38
ax.bar(x - width/2, static_vals, width, label="static ratio (x86-64 AVX2, cross-compiled)", color="#4C72B0")
ax.bar(x + width/2, wc_vals, width, label="wall-clock speedup (arm64 native, Apple M5)", color="#DD8452")
ax.set_xticks(x)
ax.set_xticklabels(labels, fontsize=8)
ax.axhline(1.0, color="gray", linewidth=0.8, linestyle="--")
ax.set_ylabel("ratio / speedup (higher = better)")
ax.set_title("Static vs. wall-clock results by class x width\n(two different ISAs -- see methodology note, not directly comparable 1:1)")
ax.legend(fontsize=9)
plt.tight_layout()
plt.savefig("combined_comparison.png", dpi=150)
plt.close()

print("done")
