import csv

import matplotlib.pyplot as plt


trials = []
scalar_times = []
simd_times = []

with open("SIMD_results.csv", newline="") as file:
    reader = csv.DictReader(file)
    for row in reader:
        if row["trial"] == "summary":
            continue

        trials.append(int(row["trial"]))
        scalar_times.append(float(row["scalar_seconds"]))
        simd_times.append(float(row["simd_seconds"]))

plt.figure(figsize=(10, 6))
plt.plot(trials, scalar_times, label="Scalar", linewidth=2)
plt.plot(trials, simd_times, label="SIMD", linewidth=2)

plt.xlabel("Trial")
plt.ylabel("Runtime (seconds)")
plt.title("Scalar vs SIMD Runtime Per Trial")
plt.legend()
plt.grid(True, alpha=0.3)
plt.tight_layout()

plt.savefig("runtime_plot.png", dpi=200)
print("Saved runtime_plot.png")
