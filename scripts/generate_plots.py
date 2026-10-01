import csv
import os
import math
import statistics

RESULTS = "results"
OUT = "plots"
os.makedirs(OUT, exist_ok=True)

RUNS = {
    "FCFS 4T": "fcfs_4t.csv",
    "SJF 4T": "sjf_4t.csv",
    "RR 4T": "rr_4t.csv",
    "DRR 4T": "drr_4t.csv",
    "FCFS 1T": "fcfs_1t.csv",
    "RR 1T": "rr_1t.csv",
}

def load_csv(path):
    rows = []
    with open(path, newline="") as f:
        for r in csv.DictReader(f):
            r["bytes"] = int(r["bytes"])
            r["arrival_ns"] = int(r["arrival_ns"])
            r["start_ns"] = int(r["start_ns"])
            r["finish_ns"] = int(r["finish_ns"])
            rows.append(r)
    return rows

def percentile(values, p):
    values = sorted(values)
    if not values:
        return 0
    k = max(1, math.ceil(p * len(values)))
    return values[k - 1]

def metrics(rows):
    waiting = [r["start_ns"] - r["arrival_ns"] for r in rows]
    response = [r["finish_ns"] - r["arrival_ns"] for r in rows]

    min_arrival = min(r["arrival_ns"] for r in rows)
    max_finish = max(r["finish_ns"] for r in rows)
    throughput = len(rows) / ((max_finish - min_arrival) / 1e9)

    return {
        "p50": percentile(waiting, 0.50),
        "p99": percentile(waiting, 0.99),
        "throughput": throughput,
        "response": response,
    }

data = {}
for name, filename in RUNS.items():
    data[name] = load_csv(os.path.join(RESULTS, filename))

# Use matplotlib only for plotting.
import matplotlib.pyplot as plt

names = list(RUNS.keys())
m = {name: metrics(data[name]) for name in names}

# 1. Waiting time
x = list(range(len(names)))
p50 = [m[n]["p50"] / 1e6 for n in names]
p99 = [m[n]["p99"] / 1e6 for n in names]

plt.figure(figsize=(10, 6))
plt.plot(x, p50, marker="o", label="p50")
plt.plot(x, p99, marker="o", label="p99")
plt.xticks(x, names, rotation=25)
plt.ylabel("Waiting time (ms)")
plt.title("A28: Waiting Time")
plt.legend()
plt.tight_layout()
plt.savefig(os.path.join(OUT, "a28_waiting_time.png"), dpi=200)
plt.close()

# 2. Throughput
throughput = [m[n]["throughput"] for n in names]

plt.figure(figsize=(10, 6))
plt.bar(x, throughput)
plt.xticks(x, names, rotation=25)
plt.ylabel("Throughput (requests/s)")
plt.title("A28: Throughput")
plt.tight_layout()
plt.savefig(os.path.join(OUT, "a28_throughput.png"), dpi=200)
plt.close()

# 3. Reference 4-thread slowdown
ref_names = ["FCFS 4T", "SJF 4T", "RR 4T", "DRR 4T"]
classes = {
    "Small": (0, 4096),
    "Medium": (4096, 65536),
    "Large": (65536, 10**12),
}

def slowdown_stats(rows, lo, hi):
    vals = []
    for r in rows:
        if lo <= r["bytes"] < hi:
            response = r["finish_ns"] - r["arrival_ns"]
            vals.append(response / r["bytes"])
    return percentile(vals, 0.50), percentile(vals, 0.99)

for size_name, (lo, hi) in classes.items():
    med = []
    p99v = []

    for name in ref_names:
        a, b = slowdown_stats(data[name], lo, hi)
        med.append(a)
        p99v.append(b)

    x2 = list(range(len(ref_names)))

    plt.figure(figsize=(10, 6))
    width = 0.35
    plt.bar([i - width/2 for i in x2], med, width, label="Median")
    plt.bar([i + width/2 for i in x2], p99v, width, label="P99")
    plt.xticks(x2, ref_names)
    plt.ylabel("Normalized slowdown (ns/byte)")
    plt.title(f"Reference 4T: {size_name} workload slowdown")
    plt.legend()
    plt.tight_layout()
    plt.savefig(
        os.path.join(OUT, f"slowdown_{size_name.lower()}.png"),
        dpi=200
    )
    plt.close()

# 4. RR vs DRR forfeited bytes
rr = data["RR 4T"]
drr = data["DRR 4T"]

size_ranges = {
    "Medium": (4096, 65536),
    "Large": (65536, 10**12),
}

labels = []
rr_forfeit = []
drr_forfeit = []

for label, (lo, hi) in size_ranges.items():
    labels.append(label)

    rr_forfeit.append(
        sum(
            int(r["forfeited_bytes"])
            for r in rr
            if lo <= r["bytes"] < hi
        )
    )

    drr_forfeit.append(
        sum(
            int(r["forfeited_bytes"])
            for r in drr
            if lo <= r["bytes"] < hi
        )
    )

x3 = list(range(len(labels)))

plt.figure(figsize=(8, 6))
width = 0.35
plt.bar([i - width/2 for i in x3], rr_forfeit, width, label="RR")
plt.bar([i + width/2 for i in x3], drr_forfeit, width, label="DRR")
plt.xticks(x3, labels)
plt.ylabel("Forfeited bytes")
plt.title("A29: RR vs DRR forfeited bytes")
plt.legend()
plt.tight_layout()
plt.savefig(os.path.join(OUT, "rr_vs_drr_forfeiture.png"), dpi=200)
plt.close()

print("Plots generated in:", OUT)
for f in sorted(os.listdir(OUT)):
    print(f)
