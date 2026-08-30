#!/usr/bin/env python3
"""Aggregate the 1:1 K3 reproduction of the OAI uplink experiment."""

from __future__ import annotations

import csv
import math
import re
import statistics
import sys
from collections import defaultdict
from datetime import datetime
from pathlib import Path


MODULES = [
    ("FFT/DFT（接收时隙摊销）", "feprx"),
    ("PUSCH 前端", "PUSCH inner-receiver"),
    ("LDPC 解码", "UL segments decoding"),
    ("信道估计（含测量）", "PUSCH channel estimation"),
    ("LDPC 速率恢复", "UL segment rate recovery"),
    ("LDPC 解交织", "UL segment deinterleaving"),
    ("资源提取", "PUSCH resource extraction"),
    ("信道补偿", "PUSCH channel compensation"),
    ("LLR 计算", "PUSCH LLR computation"),
    ("信道测量（估计子步骤）", "PUSCH channel measurement"),
]

NOISE_ORDER = [-6.0, -5.0, -4.5, -4.0, -3.75, -3.5, -3.25, -3.0, -2.75, -2.5]
NATIVE_RE = re.compile(r"^\s*(.*?):\s*([0-9.]+) us;\s*([0-9]+);\s*([0-9.]+) us;")
ROUNDS_RE = re.compile(r"round_trials\s+([0-9]+).*?:([0-9]+).*?:([0-9]+).*?:([0-9]+),\s*DTX\s+([0-9]+)")


def quantile(values: list[float], q: float) -> float:
    values = sorted(values)
    if not values:
        return math.nan
    pos = (len(values) - 1) * q
    lo = math.floor(pos)
    hi = math.ceil(pos)
    if lo == hi:
        return values[lo]
    return values[lo] + (values[hi] - values[lo]) * (pos - lo)


def mean(values: list[float]) -> float:
    return statistics.fmean(values) if values else math.nan


def stdev(values: list[float]) -> float:
    return statistics.stdev(values) if len(values) > 1 else 0.0


def parse_env(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    if not path.exists():
        return result
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            result[key] = value
    return result


def parse_native_run(run_dir: Path) -> dict[str, object]:
    text = (run_dir / "nrL1_stats.log").read_text(encoding="utf-8", errors="replace")
    rounds = ROUNDS_RE.search(text)
    if not rounds:
        raise ValueError(f"No ULSCH round count in {run_dir}")
    round_counts = [int(rounds.group(i)) for i in range(1, 5)]
    tb_count = sum(round_counts)
    native: dict[str, tuple[float, int, float]] = {}
    for line in text.splitlines():
        match = NATIVE_RE.match(line)
        if match:
            native[match.group(1).strip()] = (float(match.group(2)), int(match.group(3)), float(match.group(4)))
    row: dict[str, object] = {
        "run": run_dir.name,
        "tb_count": tb_count,
        "round0": round_counts[0],
        "round1": round_counts[1],
        "round2": round_counts[2],
        "round3": round_counts[3],
        "dtx": int(rounds.group(5)),
    }
    for display, native_name in MODULES:
        if native_name not in native:
            raise ValueError(f"Missing {native_name} in {run_dir}")
        avg_call, calls, max_call = native[native_name]
        row[f"{display}_us_tb"] = avg_call * calls / tb_count
        row[f"{display}_calls_tb"] = calls / tb_count
        row[f"{display}_max_call_us"] = max_call
    meta = parse_env(run_dir / "meta.env")
    row.update({f"meta_{key}": value for key, value in meta.items()})
    if meta.get("start_utc") and meta.get("end_utc"):
        start = datetime.fromisoformat(meta["start_utc"].replace("Z", "+00:00"))
        end = datetime.fromisoformat(meta["end_utc"].replace("Z", "+00:00"))
        row["elapsed_seconds"] = (end - start).total_seconds()
    return row


def write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    if not rows:
        return
    keys: list[str] = []
    for row in rows:
        for key in row:
            if key not in keys:
                keys.append(key)
    with path.open("w", newline="", encoding="utf-8-sig") as stream:
        writer = csv.DictWriter(stream, fieldnames=keys, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def native_summary(rows: list[dict[str, object]]) -> list[dict[str, object]]:
    if not rows:
        return []
    result = []
    for display, _ in MODULES:
        values = [float(row[f"{display}_us_tb"]) for row in rows]
        calls = [float(row[f"{display}_calls_tb"]) for row in rows]
        avg = mean(values)
        sd = stdev(values)
        result.append({
            "模块": display,
            "运行数": len(values),
            "P10": quantile(values, 0.10),
            "P50": quantile(values, 0.50),
            "P90": quantile(values, 0.90),
            "最小": min(values),
            "最大": max(values),
            "均值": avg,
            "标准差": sd,
            "调用/TB": mean(calls),
            "变异系数": sd / avg if avg else math.nan,
        })
    return result


def scan_run(run_dir: Path) -> tuple[dict[str, object], list[dict[str, str]]]:
    with (run_dir / "tb.csv").open(newline="", encoding="utf-8-sig") as stream:
        raw = list(csv.DictReader(stream))
    trimmed = raw[650:-50] if len(raw) > 700 else []
    valid = [row for row in trimmed if int(row["dtx"]) == 0]
    first = [row for row in valid if int(row["harq_round"]) == 0]
    retrans = [row for row in valid if int(row["harq_round"]) > 0]
    passed = [row for row in valid if int(row["crc_ok"]) == 1]
    failed = [row for row in valid if int(row["crc_ok"]) == 0]
    first_failed = [row for row in first if int(row["crc_ok"]) == 0]
    noise = float(parse_env(run_dir / "meta.env")["channel"])
    times = [float(row["ldpc_time_us"]) for row in valid]
    metrics: dict[str, object] = {
        "run": run_dir.name,
        "noise": noise,
        "raw_rows": len(raw),
        "trimmed_rows": len(trimmed),
        "valid_tb": len(valid),
        "first_tb": len(first),
        "crc_pass_tb": len(passed),
        "crc_fail_tb": len(failed),
        "dtx_rows": len(trimmed) - len(valid),
        "snr_mean": mean([float(row["snr_db"]) for row in valid]),
        "final_crc_failure_rate": len(failed) / len(valid) if valid else math.nan,
        "first_bler": len(first_failed) / len(first) if first else math.nan,
        "retransmission_share": len(retrans) / len(valid) if valid else math.nan,
        "iterations_all_mean": mean([float(row["ldpc_iterations_per_cb"]) for row in valid]),
        "iterations_first_mean": mean([float(row["ldpc_iterations_per_cb"]) for row in first]),
        "ldpc_p50_us": quantile(times, 0.50),
        "ldpc_p95_us": quantile(times, 0.95),
    }
    for row in trimmed:
        row["source_run"] = run_dir.name
        row["noise_parameter"] = f"{noise:g}"
    return metrics, trimmed


def scan_summary(run_rows: list[dict[str, object]]) -> list[dict[str, object]]:
    groups: dict[float, list[dict[str, object]]] = defaultdict(list)
    for row in run_rows:
        groups[float(row["noise"])].append(row)
    result = []
    metric_names = [
        "snr_mean", "final_crc_failure_rate", "first_bler", "retransmission_share",
        "iterations_all_mean", "iterations_first_mean", "ldpc_p50_us", "ldpc_p95_us",
    ]
    for noise in NOISE_ORDER:
        runs = groups.get(noise, [])
        item: dict[str, object] = {"noise": noise, "runs": len(runs)}
        for metric in metric_names:
            values = [float(row[metric]) for row in runs if not math.isnan(float(row[metric]))]
            item[f"{metric}_mean"] = mean(values)
            item[f"{metric}_std"] = stdev(values)
        for count in ["valid_tb", "first_tb", "crc_pass_tb", "crc_fail_tb", "dtx_rows"]:
            item[count] = sum(int(row[count]) for row in runs)
        result.append(item)
    return result


def native_markdown(summary: list[dict[str, object]]) -> str:
    lines = [
        "| 模块 | 运行数 | P10 | P50 | P90 | 最小 | 最大 | 均值 | 标准差 | 调用/TB | 变异系数 |",
        "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for row in summary:
        lines.append(
            f"| {row['模块']} | {row['运行数']} | {row['P10']:.3f} | {row['P50']:.3f} | "
            f"{row['P90']:.3f} | {row['最小']:.3f} | {row['最大']:.3f} | {row['均值']:.3f} | "
            f"{row['标准差']:.3f} | {row['调用/TB']:.3f} | {row['变异系数']:.3%} |"
        )
    return "\n".join(lines)


def comparison_markdown(ideal: list[dict[str, object]], awgn: list[dict[str, object]]) -> str:
    lines = [
        "| 模块 | 理想 P50 | 理想 P90 | AWGN P50 | AWGN P90 | P50 变化 | P90 变化 |",
        "|---|---:|---:|---:|---:|---:|---:|",
    ]
    for left, right in zip(ideal, awgn):
        p50 = (float(right["P50"]) / float(left["P50"]) - 1) if float(left["P50"]) else math.nan
        p90 = (float(right["P90"]) / float(left["P90"]) - 1) if float(left["P90"]) else math.nan
        lines.append(
            f"| {left['模块']} | {left['P50']:.3f} | {left['P90']:.3f} | {right['P50']:.3f} | "
            f"{right['P90']:.3f} | {p50:+.3%} | {p90:+.3%} |"
        )
    return "\n".join(lines)


def scan_markdown(summary: list[dict[str, object]]) -> str:
    reliability = [
        "#### 可靠性与实测 SNR",
        "",
        "| 噪声参数 | SNR均值 | SNR标准差 | 最终CRC失败率均值 | 标准差 | 首传BLER均值 | 标准差 | 重传占比均值 | 标准差 |",
        "|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
    ]
    iterations = [
        "#### LDPC 迭代与延迟",
        "",
        "时间单位为 µs/TB。",
        "",
        "| 噪声参数 | 全部尝试迭代均值 | 标准差 | 首传迭代均值 | 标准差 | P50均值 | 标准差 | P95均值 | 标准差 |",
        "|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
    ]
    counts = [
        "#### 样本计数",
        "",
        "| 噪声参数 | 有效尝试TB | 首传TB | CRC通过TB | CRC失败TB | DTX行 |",
        "|---:|---:|---:|---:|---:|---:|",
    ]
    for row in summary:
        noise = float(row["noise"])
        reliability.append(
            f"| {noise:.2f} | {row['snr_mean_mean']:.3f} | {row['snr_mean_std']:.3f} | "
            f"{row['final_crc_failure_rate_mean']:.2%} | {row['final_crc_failure_rate_std']:.2%} | "
            f"{row['first_bler_mean']:.2%} | {row['first_bler_std']:.2%} | "
            f"{row['retransmission_share_mean']:.2%} | {row['retransmission_share_std']:.2%} |"
        )
        iterations.append(
            f"| {noise:.2f} | {row['iterations_all_mean_mean']:.3f} | {row['iterations_all_mean_std']:.3f} | "
            f"{row['iterations_first_mean_mean']:.3f} | {row['iterations_first_mean_std']:.3f} | "
            f"{row['ldpc_p50_us_mean']:.3f} | {row['ldpc_p50_us_std']:.3f} | "
            f"{row['ldpc_p95_us_mean']:.3f} | {row['ldpc_p95_us_std']:.3f} |"
        )
        counts.append(
            f"| {noise:.2f} | {int(row['valid_tb']):,} | {int(row['first_tb']):,} | "
            f"{int(row['crc_pass_tb']):,} | {int(row['crc_fail_tb']):,} | {int(row['dtx_rows']):,} |"
        )
    return "\n\n".join(["\n".join(reliability), "\n".join(iterations), "\n".join(counts)])


def main() -> int:
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} RESULT_ROOT OUTPUT_DIR", file=sys.stderr)
        return 2
    root = Path(sys.argv[1])
    output = Path(sys.argv[2])
    output.mkdir(parents=True, exist_ok=True)

    ideal_dirs = sorted(root.glob("ideal_native_r*"))
    awgn_dirs = sorted(root.glob("awgn_m3p5_native_r*"))
    scan_dirs = sorted(root.glob("scan_*_r*"))
    ideal_runs = [parse_native_run(path) for path in ideal_dirs if (path / "nrL1_stats.log").exists()]
    awgn_runs = [parse_native_run(path) for path in awgn_dirs if (path / "nrL1_stats.log").exists()]
    ideal_summary = native_summary(ideal_runs)
    awgn_summary = native_summary(awgn_runs)

    scan_runs: list[dict[str, object]] = []
    stable_rows: list[dict[str, str]] = []
    for path in scan_dirs:
        if (path / "tb.csv").exists():
            metrics, rows = scan_run(path)
            scan_runs.append(metrics)
            stable_rows.extend(rows)
    channel_summary = scan_summary(scan_runs)

    write_csv(output / "K3_RV64_理想信道_20次逐轮数据.csv", ideal_runs)
    write_csv(output / "K3_RV64_理想信道_20次统计摘要.csv", ideal_summary)
    write_csv(output / "K3_RV64_AWGN_m3p5_20次逐轮数据.csv", awgn_runs)
    write_csv(output / "K3_RV64_AWGN_m3p5_20次统计摘要.csv", awgn_summary)
    write_csv(output / "K3_RV64_AWGN_MCS9_70次逐轮指标.csv", scan_runs)
    write_csv(output / "K3_RV64_AWGN_MCS9_信道质量汇总.csv", channel_summary)
    write_csv(output / "K3_RV64_AWGN_MCS9_稳态逐TB数据.csv", stable_rows)

    report = [
        "<!-- Generated by analyze_k3_ul.py; units are microseconds/TB. -->",
        "## 理想 RFsim 原生计时表",
        "",
        native_markdown(ideal_summary),
        "",
        "## 固定 AWGN 原生计时表",
        "",
        native_markdown(awgn_summary),
        "",
        "## 理想与 AWGN 对比表",
        "",
        comparison_markdown(ideal_summary, awgn_summary),
        "",
        "## MCS 9 信道扫描表",
        "",
        scan_markdown(channel_summary),
    ]
    (output / "K3_RV64_实验结果表.md").write_text("\n".join(report) + "\n", encoding="utf-8")
    print(f"ideal_runs={len(ideal_runs)} awgn_runs={len(awgn_runs)} scan_runs={len(scan_runs)} stable_rows={len(stable_rows)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
