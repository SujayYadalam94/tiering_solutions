import argparse
import csv
import io
import os
import sys
import time
from pathlib import Path

import matplotlib.pyplot as plt
import pulp

TRACES_DIR = Path(__file__).parent / "traces"
FIGURES_DIR = Path(__file__).parent / "figures"

AGGREGATE_PERIOD = 5

def _iter_csv_rows(path):
    """Yield parsed row dicts from path, discarding all rows before the last PHASE CHANGED! marker."""
    with open(path, newline="") as f:
        lines = f.readlines()
    if not lines:
        return
    header = lines[0]
    last_marker = -1
    for i, line in enumerate(lines[1:], 1):
        if line.strip() == "PHASE CHANGED!":
            last_marker = i
    relevant = [header] + (lines[last_marker + 1:] if last_marker >= 0 else lines[1:])
    for row in csv.DictReader(io.StringIO("".join(relevant))):
        yield _parse_row(row)

def _parse_row(row):
    return {
        "dram_accesses": int(row["cur_dram_acc"]),
        "cxl_accesses": int(row["cur_cxl_acc"]),
        "dram_lat": float(row["obs_dram_lat_cyc"]),
        "cxl_lat": float(row["obs_cxl_lat_cyc"]),
        "mlp": float(row["MLP"]),
        "measured_stalls": int(row["measured_stalls"]),
        "dram_size": int(row["current_dram_bytes"]),
    }

def _drop_after_size_change(rows):
    """Drop the 2 rows immediately following a DRAM size change."""
    result = []
    skip = 0
    prev_size = None
    for row in rows:
        size = row["dram_size"]
        if prev_size is not None and size != prev_size:
            skip = 2
        if skip > 0:
            skip -= 1
        else:
            result.append({k: v for k, v in row.items() if k != "dram_size"})
        prev_size = size
    return result

def parse_csv(path):
    return _drop_after_size_change(list(_iter_csv_rows(path)))

def read_csv(path):
    """Return all data rows after the last phase change from a CSV metrics file."""
    try:
        return _drop_after_size_change(list(_iter_csv_rows(path)))
    except (FileNotFoundError, KeyError):
        return []

def preprocess(timesteps):
    result = []
    for ts in timesteps:
        dram_acc = ts["dram_accesses"]
        cxl_acc = ts["cxl_accesses"]
        dram_lat = ts["dram_lat"]
        cxl_lat = ts["cxl_lat"]

        if ts["measured_stalls"] < 1000 ** 3:  # filter out very low-stall points that are likely noise
            continue
        if dram_acc == 0 and cxl_acc == 0:
            continue
        if (dram_acc * dram_lat + cxl_acc * cxl_lat) < ts["measured_stalls"]:
            continue
        if ts["mlp"] <= 1.0:
            ts["mlp"] = 1.0
        result.append({**ts, "mlp": max(ts["mlp"], 1.0)})
    return result

def aggregate(timesteps, period=AGGREGATE_PERIOD):
    """Group consecutive rows into blocks of `period`, summing terms."""
    aggregated = []
    for i in range(0, len(timesteps), period):
        block = timesteps[i:i+period]

        if len(block) < period:
            # Pad the last block by repeating its last element.
            for _ in range(period - len(block)):
                block.append(block[-1])

        agg_ts = {
            "cxl_term": sum(ts["cxl_accesses"] * ts["cxl_lat"] / ts["mlp"] for ts in block),
            "dram_term": sum(ts["dram_accesses"] * ts["dram_lat"] / ts["mlp"] for ts in block),
            "measured_stalls": sum(ts["measured_stalls"] for ts in block),
        }
        aggregated.append(agg_ts)
    return aggregated

def plot_stalls(timesteps, alpha_c, alpha_d, beta, epsilon, workload: str = ""):
    measured, estimated = [], []
    for ts in timesteps:
        est = alpha_c * ts["cxl_term"] + alpha_d * ts["dram_term"] + beta
        measured.append(ts["measured_stalls"])
        estimated.append(est)

    scale = 1e9
    xs = range(len(timesteps))

    fig, ax = plt.subplots(figsize=(10, 4))
    ax.plot(xs, [m / scale for m in measured], marker="o", label="measured")
    ax.plot(xs, [e / scale for e in estimated], marker="x", linestyle="--", label="estimated")
    ax.fill_between(
        xs,
        [(e - epsilon) / scale for e in estimated],
        [(e + epsilon) / scale for e in estimated],
        alpha=0.15,
        label="±ε",
    )
    ax.set_xlabel("Timestep")
    ax.set_ylabel("Stalls (×10⁹)")
    ax.set_title(f"Measured vs Estimated Stalls — {workload}")
    ax.legend()
    ax.grid(True, alpha=0.3)

    FIGURES_DIR.mkdir(exist_ok=True)
    out = FIGURES_DIR / f"{workload}_stalls.png"
    fig.savefig(out, dpi=150, bbox_inches="tight")
    plt.close(fig)
    print(f"saved figure → {out}")


def solve(timesteps, workload: str = "", plot=False):
    """
    Fit: stalls = (alpha_c * Ac*Lc + alpha_d * Ad*Ld) / MLP + beta ± epsilon.
    Lexicographic: 1) minimize epsilon, 2) minimize |beta|.
    """
    if not timesteps:
        return None

    solver = pulp.PULP_CBC_CMD(msg=False)

    # Pass 1: minimize epsilon
    prob = pulp.LpProblem("fit_stalls_eps", pulp.LpMinimize)
    alpha_c = pulp.LpVariable("alpha_c", lowBound=0, upBound=1)
    alpha_d = pulp.LpVariable("alpha_d", lowBound=0, upBound=1)
    beta = pulp.LpVariable("beta", lowBound=0)
    epsilon = pulp.LpVariable("epsilon", lowBound=0)
    prob += epsilon

    for t, ts in enumerate(timesteps):
        cxl_term = ts["cxl_term"]
        dram_term = ts["dram_term"]
        m = ts["measured_stalls"]
        estimate = alpha_c * cxl_term + alpha_d * dram_term + beta
        prob += estimate - m <= epsilon, f"upper_{t}"
        prob += m - estimate <= epsilon, f"lower_{t}"
        prob += beta <= alpha_c * cxl_term + alpha_d * dram_term, f"beta_bound_{t}"

    prob.solve(solver)
    if pulp.LpStatus[prob.status] != "Optimal":
        return None
    eps_star = pulp.value(epsilon)
    eps_tol = max(1.0, 1e-6 * abs(eps_star))
    prob += epsilon <= eps_star + eps_tol, "fix_epsilon"
    prob.setObjective(beta)
    prob.solve(solver)
    if pulp.LpStatus[prob.status] != "Optimal":
        return None

    alpha_c_val = pulp.value(alpha_c)
    alpha_d_val = pulp.value(alpha_d)
    beta_val = pulp.value(beta)
    epsilon_val = pulp.value(epsilon)

    result = {
        "alpha_c": alpha_c_val,
        "alpha_d": alpha_d_val,
        "beta": beta_val,
        "epsilon": epsilon_val,
        "status": pulp.LpStatus[prob.status]
    }

    if plot:
        plot_stalls(timesteps, result["alpha_c"], result["alpha_d"], result["beta"], result["epsilon"], workload=workload)
    return result


def write_params(path, alpha_dram, alpha_cxl, beta, epsilon):
    """Atomically write LP params so the C reader never sees a partial file."""
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        f.write(f"alpha_dram={alpha_dram:.6f}\n")
        f.write(f"alpha_cxl={alpha_cxl:.6f}\n")
        f.write(f"beta={beta:.2f}\n")
        f.write(f"epsilon={epsilon:.2f}\n")
    os.replace(tmp, path)


def run_daemon(csv_path, params_path, interval_s=30):
    print(f"[daemon] starting: csv={csv_path} params={params_path} interval={interval_s}s",
          flush=True)
    while True:
        raw = read_csv(csv_path)
        clean = preprocess(raw)
        if len(clean) < 10:
            print(f"[daemon] only {len(clean)} clean rows after filtering — skipping fit",
                  flush=True)
        else:
            agg = aggregate(clean)
            result = solve(agg)
            if result is None:
                print("[daemon] LP solve returned no optimal solution — skipping write",
                      flush=True)
            else:
                # Convert beta and epsilon to single data point units
                result["beta"] /= AGGREGATE_PERIOD
                result["epsilon"] /= AGGREGATE_PERIOD

                write_params(params_path,
                             alpha_dram=result["alpha_d"],
                             alpha_cxl=result["alpha_c"],
                             beta=result["beta"],
                             epsilon=result["epsilon"])
                print(f"[daemon] wrote params: alpha_dram={result['alpha_d']:.6f}"
                      f" alpha_cxl={result['alpha_c']:.6f}"
                      f" beta={result['beta']:.2f}"
                      f" epsilon={result['epsilon']:.2f}",
                      flush=True)
        time.sleep(interval_s)


def main():
    parser = argparse.ArgumentParser(
        description="Fit stall model: (alpha_c*Ac*Lc + alpha_d*Ad*Ld)/MLP + beta")

    subparsers = parser.add_subparsers(dest="mode")

    # Daemon mode
    daemon_p = subparsers.add_parser("daemon", help="Run as a periodic fitting daemon")
    daemon_p.add_argument("--csv", default="/tmp/arms_metrics.csv",
                          help="Path to the live metrics CSV (default: /tmp/arms_metrics.csv)")
    daemon_p.add_argument("--params", default="/tmp/cf_model_params",
                          help="Path to write LP params (default: /tmp/cf_model_params)")
    daemon_p.add_argument("--interval", type=int, default=30,
                          help="Fit interval in seconds (default: 30)")

    # One-shot workload mode (original behaviour)
    available = sorted(p.stem.replace("_metrics", "") for p in TRACES_DIR.glob("*_metrics.csv")) \
        if TRACES_DIR.exists() else []
    fit_p = subparsers.add_parser("fit", help="Fit a single workload trace and plot")
    fit_p.add_argument("workload",
                       choices=available if available else None,
                       metavar="WORKLOAD",
                       help=f"Available: {', '.join(available) if available else '(none found)'}")
    fit_p.add_argument("--num-points", type=int, default=1000,
                       help="Number of points to use for fitting")

    args = parser.parse_args()

    if args.mode == "daemon":
        run_daemon(args.csv, args.params, args.interval)

    elif args.mode == "fit":
        timesteps = preprocess(parse_csv(TRACES_DIR / f"{args.workload}_metrics.csv"))
        print(f"parsed {len(timesteps)} timesteps")
        if not timesteps:
            print("no timesteps parsed; nothing to solve")
            return

        if len(timesteps) > args.num_points:
            timesteps = timesteps[:args.num_points]

        timesteps = aggregate(timesteps)
        result = solve(timesteps, workload=args.workload, plot=True)
        if result is None:
            print("status: Infeasible")
            return

        # Convert beta and epsilon to single data point units
        result["beta"] /= AGGREGATE_PERIOD
        result["epsilon"] /= AGGREGATE_PERIOD

        print(f"status:  {result['status']}")
        print(f"alpha_c = {result['alpha_c']:.6f}")
        print(f"alpha_d = {result['alpha_d']:.6f}")
        print(f"beta    = {result['beta']:.2f}")
        print(f"epsilon = {result['epsilon']:.2f}")

    else:
        parser.print_help()
        sys.exit(1)


if __name__ == "__main__":
    main()
