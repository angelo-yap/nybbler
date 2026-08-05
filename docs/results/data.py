# Combined static (x86-64 AVX2, cross-compiled) + wall-clock (arm64 native, Apple M5) results.
data = [
    # class, width, static_ratio, wallclock_speedup, note
    ("bitwise",    "i1", 1.0,  1.00, ""),
    ("bitwise",    "i2", 1.0,  2.33, ""),
    ("bitwise",    "i4", 1.0,  2.00, ""),
    ("arithmetic", "i1", 1.0,  1.00, ""),
    ("arithmetic", "i2", 60.5, 26.69, ""),
    ("arithmetic", "i4", 30.1, 4.76, ""),
    ("shift",      "i1", 1.0,  1.25, ""),
    ("shift",      "i2", 46.4, 46.10, ""),
    ("shift",      "i4", 9.8,  6.54, ""),
    ("compare",    "i1", 25.1, 41.25, "blended: eq=76.6x static, ne/ult/slt=1.0x"),
    ("compare",    "i2", 54.6, 1.50, "partial: mask bitcast dominates wall-clock"),
    ("compare",    "i4", 20.9, 1.41, "partial: mask bitcast dominates wall-clock"),
]
