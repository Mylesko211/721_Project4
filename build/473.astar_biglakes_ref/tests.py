#!/usr/bin/env python3
"""Automate SVP make cleanrun sweeps in a fixed order.

This script:
1. Activates the SPEC environment once.
2. Runs each make command in order.
3. Stores per-test logs and a CSV summary.
"""

import argparse
import csv
import datetime as dt
import os
import shlex
import subprocess
import sys
import time
from pathlib import Path


ACTIVATE_SCRIPT = "/mnt/designkits/spec_2006_2017/O2_fno_bbreorder/activate.bash"

METRICS_TO_CHECK = [
	"ipc_rate",
	"vpmeas_miss",
	"vpmeas_conf_corr",
	"vpmeas_unconf_incorr",
	"vpmeas_ineligible",
	"vpmeas_eligible",
	"vpmeas_conf_incorr",
	"vpmeas_unconf_corr",
]

# Hardcoded expected metrics from these baseline files:
# 1: stats.26-04-16.22:35:44.log
# 2: stats.26-04-16.22:36:23.log
# 3: stats.26-04-16.22:37:02.log
# 4: stats.26-04-16.22:37:39.log
# 5: stats.26-04-16.22:38:44.log
# 6: stats.26-04-16.22:39:48.log
# 7: stats.26-04-16.22:40:38.log
# 8: stats.26-04-16.22:41:30.log
# 9: stats.26-04-16.22:42:34.log
# 10: stats.26-04-16.22:43:40.log
# 11: stats.26-04-16.22:44:47.log
HARDCODED_EXPECTED_BY_CASE = {
	1: {
		"ipc_rate": 3.6,
		"vpmeas_miss": 0,
		"vpmeas_conf_corr": 7279896,
		"vpmeas_unconf_incorr": 0,
		"vpmeas_ineligible": 2720105,
		"vpmeas_eligible": 7279896,
		"vpmeas_conf_incorr": 0,
		"vpmeas_unconf_corr": 0,
	},
	2: {
		"ipc_rate": 3.72,
		"vpmeas_miss": 425,
		"vpmeas_conf_corr": 4907936,
		"vpmeas_unconf_incorr": 2371535,
		"vpmeas_ineligible": 2720105,
		"vpmeas_eligible": 7279896,
		"vpmeas_conf_incorr": 0,
		"vpmeas_unconf_corr": 0,
	},
	3: {
		"ipc_rate": 3.69,
		"vpmeas_miss": 259,
		"vpmeas_conf_corr": 2436037,
		"vpmeas_unconf_incorr": 1917074,
		"vpmeas_ineligible": 5646631,
		"vpmeas_eligible": 4353370,
		"vpmeas_conf_incorr": 0,
		"vpmeas_unconf_corr": 0,
	},
	4: {
		"ipc_rate": 3.63,
		"vpmeas_miss": 165,
		"vpmeas_conf_corr": 2470311,
		"vpmeas_unconf_incorr": 456050,
		"vpmeas_ineligible": 7073475,
		"vpmeas_eligible": 2926526,
		"vpmeas_conf_incorr": 0,
		"vpmeas_unconf_corr": 0,
	},
	5: {
		"ipc_rate": 2.85,
		"vpmeas_miss": 242,
		"vpmeas_conf_corr": 4198415,
		"vpmeas_unconf_incorr": 2320712,
		"vpmeas_ineligible": 2720105,
		"vpmeas_eligible": 7279896,
		"vpmeas_conf_incorr": 9218,
		"vpmeas_unconf_corr": 751309,
	},
	6: {
		"ipc_rate": 2.54,
		"vpmeas_miss": 254,
		"vpmeas_conf_corr": 4202679,
		"vpmeas_unconf_incorr": 2324949,
		"vpmeas_ineligible": 2720105,
		"vpmeas_eligible": 7279896,
		"vpmeas_conf_incorr": 9222,
		"vpmeas_unconf_corr": 742792,
	},
	7: {
		"ipc_rate": 2.79,
		"vpmeas_miss": 2402925,
		"vpmeas_conf_corr": 1481641,
		"vpmeas_unconf_incorr": 2813041,
		"vpmeas_ineligible": 2720105,
		"vpmeas_eligible": 7279896,
		"vpmeas_conf_incorr": 3921,
		"vpmeas_unconf_corr": 578368,
	},
	8: {
		"ipc_rate": 2.78,
		"vpmeas_miss": 0,
		"vpmeas_conf_corr": 1481636,
		"vpmeas_unconf_incorr": 5218858,
		"vpmeas_ineligible": 2720105,
		"vpmeas_eligible": 7279896,
		"vpmeas_conf_incorr": 4544,
		"vpmeas_unconf_corr": 574858,
	},
	9: {
		"ipc_rate": 2.85,
		"vpmeas_miss": 0,
		"vpmeas_conf_corr": 4195734,
		"vpmeas_unconf_incorr": 2321218,
		"vpmeas_ineligible": 2720105,
		"vpmeas_eligible": 7279896,
		"vpmeas_conf_incorr": 9291,
		"vpmeas_unconf_corr": 753653,
	},
	10: {
		"ipc_rate": 2.83,
		"vpmeas_miss": 0,
		"vpmeas_conf_corr": 4364723,
		"vpmeas_unconf_incorr": 2316167,
		"vpmeas_ineligible": 2720105,
		"vpmeas_eligible": 7279896,
		"vpmeas_conf_incorr": 12336,
		"vpmeas_unconf_corr": 586670,
	},
	11: {
		"ipc_rate": 2.77,
		"vpmeas_miss": 0,
		"vpmeas_conf_corr": 4480816,
		"vpmeas_unconf_incorr": 2306387,
		"vpmeas_ineligible": 2720105,
		"vpmeas_eligible": 7279896,
		"vpmeas_conf_incorr": 18828,
		"vpmeas_unconf_corr": 473865,
	},
}

# Ordered exactly as requested.
SIM_FLAGS_LIST = [
	"--vp-eligible=1,0,1 --vp-perf=1 --mdp=5,0 --perf=0,0,0,1 -t --cbpALG=0 --fq=64 --cp=32 --al=256 --lsq=128 --iq=64 --iqnp=4 --fw=8 --dw=8 --iw=16 --rw=8 -e10000000",
	"--vp-eligible=1,0,1 --vp-svp=300,1,10,10,31 --mdp=4,0 --perf=1,0,0,1 -t --cbpALG=0 --fq=64 --cp=32 --al=256 --lsq=128 --iq=64 --iqnp=4 --fw=8 --dw=8 --iw=16 --rw=8 -e10000000",
    "--vp-eligible=1,0,0 --vp-svp=300,1,10,10,31 --mdp=4,0 --perf=1,0,0,1 -t --cbpALG=0 --fq=64 --cp=32 --al=256 --lsq=128 --iq=64 --iqnp=4 --fw=8 --dw=8 --iw=16 --rw=8 -e10000000",
	"--vp-eligible=0,0,1 --vp-svp=300,1,10,10,31 --mdp=4,0 --perf=1,0,0,1 -t --cbpALG=0 --fq=64 --cp=32 --al=256 --lsq=128 --iq=64 --iqnp=4 --fw=8 --dw=8 --iw=16 --rw=8 -e10000000",
	"--vp-eligible=1,0,1 --vp-svp=300,0,10,10,31 --mdp=5,0 --perf=0,0,0,1 -t --cbpALG=0 --fq=64 --cp=32 --al=256 --lsq=128 --iq=64 --iqnp=4 --fw=8 --dw=8 --iw=16 --rw=8 -e10000000",
	"--vp-eligible=1,0,1 --vp-svp=100,0,10,10,31 --mdp=5,0 --perf=0,0,0,1 -t --cbpALG=0 --fq=64 --cp=32 --al=256 --lsq=128 --iq=64 --iqnp=4 --fw=8 --dw=8 --iw=16 --rw=8 -e10000000",
	"--vp-eligible=1,0,1 --vp-svp=300,0,7,10,31 --mdp=5,0 --perf=0,0,0,1 -t --cbpALG=0 --fq=64 --cp=32 --al=256 --lsq=128 --iq=64 --iqnp=4 --fw=8 --dw=8 --iw=16 --rw=8 -e10000000",
	"--vp-eligible=1,0,1 --vp-svp=300,0,7,0,31 --mdp=5,0 --perf=0,0,0,1 -t --cbpALG=0 --fq=64 --cp=32 --al=256 --lsq=128 --iq=64 --iqnp=4 --fw=8 --dw=8 --iw=16 --rw=8 -e10000000",
	"--vp-eligible=1,0,1 --vp-svp=300,0,8,0,31 --mdp=5,0 --perf=0,0,0,1 -t --cbpALG=0 --fq=64 --cp=32 --al=256 --lsq=128 --iq=64 --iqnp=4 --fw=8 --dw=8 --iw=16 --rw=8 -e10000000",
	"--vp-eligible=1,0,1 --vp-svp=300,0,8,0,15 --mdp=5,0 --perf=0,0,0,1 -t --cbpALG=0 --fq=64 --cp=32 --al=256 --lsq=128 --iq=64 --iqnp=4 --fw=8 --dw=8 --iw=16 --rw=8 -e10000000",
	"--vp-eligible=1,0,1 --vp-svp=300,0,8,0,7 --mdp=5,0 --perf=0,0,0,1 -t --cbpALG=0 --fq=64 --cp=32 --al=256 --lsq=128 --iq=64 --iqnp=4 --fw=8 --dw=8 --iw=16 --rw=8 -e10000000"
]


def get_activated_env(activate_script):
	"""Source the activation script once, then capture exported env."""
	cmd = ["bash", "-lc", "source {} && env -0".format(shlex.quote(activate_script))]
	proc = subprocess.run(cmd, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

	env = os.environ.copy()
	for item in proc.stdout.split(b"\0"):
		if not item:
			continue
		key, _, value = item.partition(b"=")
		if key:
			env[key.decode("utf-8", errors="ignore")] = value.decode("utf-8", errors="ignore")
	return env


def find_latest_stats_file(workdir, existing_names):
	stats_files = sorted(workdir.glob("stats.*.log"), key=lambda p: p.stat().st_mtime)
	new_files = [p for p in stats_files if p.name not in existing_names]
	if new_files:
		return new_files[-1]
	if stats_files:
		return stats_files[-1]
	return None


def parse_stats_metrics(stats_file):
	metrics = {}
	if not stats_file or not stats_file.exists():
		return metrics

	wanted = set(METRICS_TO_CHECK)
	with stats_file.open("r", encoding="utf-8", errors="ignore") as fp:
		for line in fp:
			if ":" not in line:
				continue
			name, value = line.split(":", 1)
			name = name.strip()
			if name not in wanted:
				continue

			tokens = value.strip().split()
			if not tokens:
				continue

			raw = tokens[0]
			try:
				if name == "ipc_rate":
					metrics[name] = float(raw)
				else:
					metrics[name] = int(raw)
			except ValueError:
				continue

	return metrics

def compare_metrics(actual_metrics, expected_metrics, ipc_tol, int_tol):
	mismatches = []
	for metric_name, expected_val in expected_metrics.items():
		if metric_name not in actual_metrics:
			mismatches.append("{} missing in stats".format(metric_name))
			continue

		actual_val = actual_metrics[metric_name]
		tol = ipc_tol if metric_name == "ipc_rate" else int_tol
		diff = abs(float(actual_val) - float(expected_val))
		if diff > tol:
			mismatches.append(
				"{} expected {} got {} (diff {})".format(metric_name, expected_val, actual_val, diff)
			)

	return len(mismatches) == 0, mismatches


def run_case(case_idx, sim_flags, workdir, logs_dir, env, expected_by_case, ipc_tol, int_tol):
	log_file = logs_dir / "case_{:02d}.log".format(case_idx)
	cmd = ["make", "cleanrun", "SIM_FLAGS_EXTRA={}".format(sim_flags)]

	print("\n[{:02d}/{:02d}] Running:".format(case_idx, len(SIM_FLAGS_LIST)))
	print(" ".join(cmd))
	print("Log: {}".format(log_file))

	existing_stats = set(p.name for p in workdir.glob("stats.*.log"))

	start = time.time()
	with log_file.open("w", encoding="utf-8") as fp:
		fp.write("Command: {}\n".format(" ".join(cmd)))
		fp.write("Workdir: {}\n".format(workdir))
		fp.write("=" * 80 + "\n")
		result = subprocess.run(cmd, cwd=workdir, env=env, stdout=fp, stderr=subprocess.STDOUT)

	elapsed = time.time() - start

	stats_file = ""
	actual_metrics = {}
	compare_pass = ""
	compare_failures = ""

	if result.returncode == 0:
		stats_path = find_latest_stats_file(workdir, existing_stats)
		if stats_path:
			stats_file = str(stats_path)
			actual_metrics = parse_stats_metrics(stats_path)

	if expected_by_case is not None:
		expected_metrics = expected_by_case.get(case_idx)
		if expected_metrics is None:
			compare_pass = "NO_EXPECTED"
		else:
			passed, failures = compare_metrics(actual_metrics, expected_metrics, ipc_tol, int_tol)
			compare_pass = "PASS" if passed else "FAIL"
			compare_failures = " | ".join(failures)

	row = {
		"case": case_idx,
		"returncode": result.returncode,
		"seconds": round(elapsed, 2),
		"log": str(log_file),
		"sim_flags": sim_flags,
		"stats_file": stats_file,
		"compare_pass": compare_pass,
		"compare_failures": compare_failures,
	}

	for metric_name in METRICS_TO_CHECK:
		row[metric_name] = actual_metrics.get(metric_name, "")

	if "ipc_rate" in actual_metrics:
		row["IPC"] = actual_metrics["ipc_rate"]
	else:
		row["IPC"] = ""

	return row


def run_parent_build(build_dir, logs_dir, env, jobs):
	log_file = logs_dir / "prebuild.log"
	cmd = ["make", "-j{}".format(jobs)]

	print("\n[prebuild] Running parent build step:")
	print(" ".join(cmd))
	print("Build dir: {}".format(build_dir))
	print("Log: {}".format(log_file))

	start = time.time()
	with log_file.open("w", encoding="utf-8") as fp:
		fp.write("Command: {}\n".format(" ".join(cmd)))
		fp.write("Build dir: {}\n".format(build_dir))
		fp.write("=" * 80 + "\n")
		result = subprocess.run(cmd, cwd=build_dir, env=env, stdout=fp, stderr=subprocess.STDOUT)

	elapsed = time.time() - start
	return {
		"case": "prebuild",
		"returncode": result.returncode,
		"seconds": round(elapsed, 2),
		"log": str(log_file),
		"sim_flags": "make -j<nproc>",
	}


def write_summary(summary_csv, rows):
	fieldnames = [
		"case",
		"returncode",
		"seconds",
		"log",
		"sim_flags",
		"stats_file",
		"compare_pass",
		"compare_failures",
		"IPC",
	]
	fieldnames.extend(METRICS_TO_CHECK)

	with summary_csv.open("w", newline="", encoding="utf-8") as fp:
		writer = csv.DictWriter(fp, fieldnames=fieldnames)
		writer.writeheader()
		writer.writerows(rows)


def parse_args():
	parser = argparse.ArgumentParser(description="Run ordered SVP cleanrun test sweep.")
	parser.add_argument(
		"--workdir",
		default=str(Path(__file__).resolve().parent),
		help="Directory to run make commands in (default: directory of this script).",
	)
	parser.add_argument(
		"--continue-on-error",
		action="store_true",
		help="Continue running all cases even if one fails.",
	)
	parser.add_argument(
		"--activate",
		default=ACTIVATE_SCRIPT,
		help="Path to activation script (default: {}).".format(ACTIVATE_SCRIPT),
	)
	parser.add_argument(
		"--build-dir",
		default=None,
		help="Directory to run prebuild make -j<nproc> in (default: parent of --workdir).",
	)
	parser.add_argument(
		"--skip-parent-build",
		action="store_true",
		help="Skip the parent build step and run test commands directly.",
	)
	parser.add_argument(
		"--ipc-tol",
		type=float,
		default=0.01,
		help="Absolute tolerance for IPC comparison (default: 0.01).",
	)
	parser.add_argument(
		"--int-tol",
		type=float,
		default=0.0,
		help="Absolute tolerance for integer metrics comparison (default: 0).",
	)
	parser.add_argument(
		"--fail-on-compare-mismatch",
		action="store_true",
		help="Treat expected-vs-actual mismatches as test failures.",
	)
	return parser.parse_args()


def main():
	if sys.version_info < (3, 5):
		print("ERROR: tests.py requires Python 3.5+", file=sys.stderr)
		return 2

	args = parse_args()
	workdir = Path(args.workdir).resolve()
	build_dir = Path(args.build_dir).resolve() if args.build_dir else workdir.parent

	if not workdir.exists():
		print("ERROR: workdir does not exist: {}".format(workdir), file=sys.stderr)
		return 2
	if not (workdir / "Makefile").exists():
		print("ERROR: no Makefile in workdir: {}".format(workdir), file=sys.stderr)
		return 2
	if not args.skip_parent_build:
		if not build_dir.exists():
			print("ERROR: build dir does not exist: {}".format(build_dir), file=sys.stderr)
			return 2
		if not (build_dir / "Makefile").exists():
			print("ERROR: no Makefile in build dir: {}".format(build_dir), file=sys.stderr)
			return 2

	stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
	logs_dir = workdir / "test_logs_{}".format(stamp)
	logs_dir.mkdir(parents=True, exist_ok=True)

	print("Workdir: {}".format(workdir))
	print("Build dir: {}".format(build_dir))
	print("Logs dir: {}".format(logs_dir))
	print("Activating environment from: {}".format(args.activate))

	try:
		env = get_activated_env(args.activate)
	except subprocess.CalledProcessError as exc:
		print("ERROR: failed to source activation script.", file=sys.stderr)
		stderr_text = exc.stderr.decode("utf-8", errors="ignore") if exc.stderr else ""
		if stderr_text:
			print(stderr_text, file=sys.stderr)
		return 2

	expected_by_case = HARDCODED_EXPECTED_BY_CASE
	print("Loaded hardcoded expected metrics for {} cases.".format(len(expected_by_case)))

	rows = []
	failed = False

	if not args.skip_parent_build:
		jobs = max(1, os.cpu_count() or 1)
		prebuild = run_parent_build(build_dir, logs_dir, env, jobs)
		rows.append(prebuild)
		pre_rc = int(prebuild["returncode"])
		pre_status = "PASS" if pre_rc == 0 else "FAIL"
		print("Prebuild result: {} (rc={}, {}s)".format(pre_status, pre_rc, prebuild["seconds"]))
		if pre_rc != 0:
			failed = True
			if not args.continue_on_error:
				print("Stopping early due to prebuild failure. Use --continue-on-error to keep going.")
				summary_csv = logs_dir / "summary.csv"
				write_summary(summary_csv, rows)
				print("CSV: {}".format(summary_csv))
				return 1

	for i, flags in enumerate(SIM_FLAGS_LIST, start=1):
		row = run_case(i, flags, workdir, logs_dir, env, expected_by_case, args.ipc_tol, args.int_tol)
		rows.append(row)
		rc = int(row["returncode"])
		status = "PASS" if rc == 0 else "FAIL"
		print("Result: {} (rc={}, {}s)".format(status, rc, row["seconds"]))

		if row.get("stats_file"):
			print("Stats: {}".format(row["stats_file"]))

		if row.get("compare_pass"):
			print("Compare: {}".format(row["compare_pass"]))
			if row.get("compare_failures"):
				print("  {}".format(row["compare_failures"]))
			if args.fail_on_compare_mismatch and row.get("compare_pass") == "FAIL":
				failed = True
				if not args.continue_on_error:
					print("Stopping early due to compare mismatch. Use --continue-on-error to keep going.")
					break

		if rc != 0:
			failed = True
			if not args.continue_on_error:
				print("Stopping early due to failure. Use --continue-on-error to keep going.")
				break

	summary_csv = logs_dir / "summary.csv"
	write_summary(summary_csv, rows)

	print("\nSummary:")
	print("  Ran cases: {}".format(len(rows)))
	print("  Failed: {}".format(sum(1 for r in rows if int(r["returncode"]) != 0)))
	compared = [r for r in rows if r.get("compare_pass") in ("PASS", "FAIL")]
	compare_failures = sum(1 for r in compared if r.get("compare_pass") == "FAIL")
	print("  Compared: {}".format(len(compared)))
	print("  Compare mismatches: {}".format(compare_failures))
	print("  CSV: {}".format(summary_csv))

	return 1 if failed else 0


if __name__ == "__main__":
	raise SystemExit(main())
