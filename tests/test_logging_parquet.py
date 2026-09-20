#!/usr/bin/env python3
"""Standalone regression tests; no benchmark, perf events, or privileged setup.

Run with a Python that has PyArrow: python3 tests/test_logging_parquet.py
"""
import csv
import math
import os
from pathlib import Path
import resource
import shlex
import signal
import subprocess
import sys
import tempfile

import pyarrow as pa
import pyarrow.parquet as pq

ROOT = Path(__file__).resolve().parents[1]
MODEL_TIMING_COLUMNS = ["model_feature_aggregation_ns", "model_inference_ns", "model_score_total_ns"]
ARMS_TIMING_COLUMNS = ["arms_feature_aggregation_ns", "arms_scoring_ns", "arms_score_total_ns"]


def run(command, **kwargs):
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, **kwargs)
    if result.returncode:
        raise RuntimeError(f"Command failed: {command}\n{result.stdout}\n{result.stderr}")
    return result


def compiler_flags(kind):
    return shlex.split(run([sys.executable, "scripts/parquet_flags.py", kind]).stdout)


def check_output(binary, directory, count, suffix, model_timing=False, arms_timing=False):
    output = directory / f"rows{count}.parquet"
    requested = output if suffix == ".parquet" else output.with_suffix(suffix)
    reference = directory / f"rows{count}.csv"
    result = run([str(binary), str(requested), str(count), str(reference)],
                 preexec_fn=lambda: os.umask(0o022))
    assert result.stdout.count("Training data log written to") == 1, result
    parquet = pq.ParquetFile(output)
    table = parquet.read()
    assert table.num_rows == count
    assert output.stat().st_mode & 0o777 == 0o644
    assert parquet.num_row_groups == (count + 65535) // 65536
    with reference.open() as stream:
        records = list(csv.reader(stream))
    assert table.column_names == records[0][:-1]  # drop the old trailing-comma artifact
    assert table.schema.field("page").type == pa.uint64()
    assert table.schema.field("ewma_2").type == pa.float32()
    assert table.schema.field("in_dram").type == pa.int64()
    for name in MODEL_TIMING_COLUMNS:
        assert (name in table.column_names) == model_timing
        if model_timing:
            assert table.schema.field(name).type == pa.uint64()
    for name in ARMS_TIMING_COLUMNS:
        assert (name in table.column_names) == arms_timing
        if arms_timing:
            assert table.schema.field(name).type == pa.uint64()
    indices = [i for i in range(count) if i < 2 or i == 65535 or i + 1 == count]
    for index, expected in zip(indices, records[1:], strict=True):
        for field, value in zip(table.schema, expected[:-1], strict=True):
            actual = table[field.name][index].as_py()
            if pa.types.is_floating(field.type):
                wanted = float(value)
                assert (math.isnan(actual) and math.isnan(wanted)) or actual == wanted, (field, actual, wanted)
                if wanted == 0:
                    assert math.copysign(1, actual) == math.copysign(1, wanted)
            else:
                assert actual == int(value), (field, actual, value)
    if count > 1:
        assert table["discounted_reward_90"][0].as_py() == 1.0
    for group in range(parquet.num_row_groups):
        assert parquet.metadata.row_group(group).column(0).compression == "SNAPPY"
    assert not list(directory.glob("*.tmp.*"))
    assert not list(directory.glob("*.log"))


def limit_file_size():
    resource.setrlimit(resource.RLIMIT_FSIZE, (1024, 1024))
    signal.signal(signal.SIGXFSZ, signal.SIG_IGN)


def check_preload_shutdown(cxx, directory):
    library = directory / "preload.so"
    app = directory / "preload-app"
    flags = compiler_flags("cflags")
    libs = compiler_flags("libs")
    run(cxx + ["-std=c++20", "-O1", "-shared", "-fPIC", "-I.", "-DC220G5",
               "-DUSE_MODEL=false", "-DPRINT_TRAINING_DATA=true"] + flags
        + ["tests/parquet_preload_fixture.cpp", "logging_parquet.cpp", "-o", str(library),
           "-ldl", "-pthread"] + libs)
    run(cxx + ["-std=c++20", "-O1"] + flags
        + ["tests/parquet_preload_main.cpp", "-o", str(app)] + libs)
    for mode in ("return", "exit", "cold"):
        output = directory / f"preload-{mode}.parquet"
        env = dict(os.environ, LD_PRELOAD=str(library), LOG_OUTPUT_PATH=str(output))
        result = subprocess.run([str(app), mode], env=env, text=True, capture_output=True,
                                timeout=30)
        assert result.returncode == 7, (mode, result.returncode, result.stdout, result.stderr)
        assert result.stderr.count("PRELOAD_PARQUET_WRITTEN") == 1, result.stderr
        assert result.stderr.index("PRELOAD_PARQUET_WRITTEN") < result.stderr.index("APPLICATION_EXIT_HANDLER")
        assert pq.read_table(output)["step"].to_pylist() == [42]
    # The new exit interception must be absent from non-logging builds.
    hook = directory / "non-logging-hook.o"
    run(cxx + ["-std=c++17", "-O0", "-I.", "-DC220G5", "-DPRINT_TRAINING_DATA=false",
               "-c", "hook/hook.cpp", "-o", str(hook)])
    assert not any(line.endswith(" T exit") for line in run(["nm", str(hook)]).stdout.splitlines())
    print("PASS: real preload shutdown before Arrow teardown (return, exit, cold); exit status preserved")


def main():
    cxx = shlex.split(os.environ.get("CXX", "g++"))
    with tempfile.TemporaryDirectory(prefix="training-parquet-test-") as temporary:
        directory = Path(temporary)
        check_preload_shutdown(cxx, directory)
        for enabled in (False, True):
            binary = directory / f"model-timing-{enabled}"
            run(cxx + ["-std=c++17", "-O1", "-ffunction-sections", "-fdata-sections", "-I.",
                       "-DC220G5", "-DUSE_MODEL=true", "-DPRINT_TRAINING_DATA=true",
                       f"-DMODEL_TIMING_TELEMETRY={'true' if enabled else 'false'}",
                       "tests/model_timing_fixture.cpp", "model.cpp", "page.cpp",
                       "-Wl,--gc-sections", "-pthread", "-o", str(binary)])
            run([str(binary)])
        print("PASS: virtual-step batch timing, identical scores, repeated totals, and disabled telemetry")
        arms_scores = None
        for enabled in (False, True):
            binary = directory / f"arms-timing-{enabled}"
            output = directory / f"arms-timing-{enabled}.parquet"
            run(cxx + ["-std=c++20", "-O1", "-ffunction-sections", "-fdata-sections", "-I.",
                       "-DC220G5", "-DUSE_MODEL=false", "-DPRINT_TRAINING_DATA=true",
                       f"-DARMS_TIMING_TELEMETRY={'true' if enabled else 'false'}"]
                + compiler_flags("cflags")
                + ["tests/arms_timing_fixture.cpp", "model.cpp", "page.cpp", "groups.cpp",
                   "logging_parquet.cpp", "-Wl,--gc-sections", "-pthread", "-lnuma", "-o", str(binary)]
                + compiler_flags("libs"))
            scores = run([str(binary), str(output)]).stdout
            if arms_scores is None:
                arms_scores = scores
            else:
                assert scores == arms_scores
            table = pq.read_table(output)
            assert table.num_rows == 6
            assert all((column in table.column_names) == enabled for column in ARMS_TIMING_COLUMNS)
            checked = subprocess.run([sys.executable, "scripts/arms_timing_summary.py", "--check", str(output)],
                                     cwd=ROOT, text=True, capture_output=True)
            assert (checked.returncode == 0) == enabled, checked.stderr
        print("PASS: real ARMS scoring/logging, score parity, repeated timings, Parquet export, and opt-out")
        feature_reference = None
        for training in (True, False):
            binary = directory / f"no-virtual-step-{training}"
            run(cxx + ["-std=c++17", "-O1", "-ffunction-sections", "-fdata-sections", "-I.",
                       "-DC220G5", "-DUSE_MODEL=true", "-DVIRTUAL_FEATURES_ENABLED=false",
                       f"-DPRINT_TRAINING_DATA={'true' if training else 'false'}",
                       "-DMAX_LOGGED_SAMPLES=64", "tests/no_virtual_step_fixture.cpp",
                       "page.cpp", "groups.cpp", "logging.cpp", "-Wl,--gc-sections", "-pthread",
                       "-lnuma", "-o", str(binary)])
            output = run([str(binary)]).stdout
            features = [line for line in output.splitlines() if line.startswith("FEATURES ")]
            assert len(features) == 6
            if training:
                feature_reference = features
            else:
                assert features == feature_reference
        print("PASS: no-virtual-step models match training features and inference; logging off allocates no log buffer")
        for full in (False, True):
            binary = directory / ("fixture-full" if full else "fixture")
            run(cxx + ["-std=c++20", "-O1", "-ffunction-sections", "-fdata-sections", "-I.",
                       "-DC220G5", "-DUSE_MODEL=false", "-DPRINT_TRAINING_DATA=true",
                       "-DMAX_LOGGED_SAMPLES=65537"] + (["-DTEST_FULL_LOGS"] if full else [])
                + compiler_flags("cflags") + ["tests/logging_parquet_fixture.cpp", "-o", str(binary),
                                              "-Wl,--gc-sections", "-lnuma", "-pthread"]
                + compiler_flags("libs"))
            for count in (0, 1, 7, 65536, 65537):
                check_output(binary, directory, count, ".parquet" if count == 7 else ".log", arms_timing=True)
            # A write error must preserve an earlier completed file and remove
            # only the temporary file created by this writer.
            protected = directory / "protected.parquet"
            protected.write_bytes(b"previous completed log")
            result = run([str(binary), str(protected), "7", str(directory / "ignored.csv")],
                         preexec_fn=limit_file_size)
            assert "Failed writing training Parquet log" in result.stderr
            assert "Training data log written to" not in result.stdout
            assert protected.read_bytes() == b"previous completed log"
            assert not list(directory.glob("*.tmp.*"))
            private = directory / "private.parquet"
            run([str(binary), str(private), "1", str(directory / "ignored.csv")],
                preexec_fn=lambda: os.umask(0o077))
            assert private.stat().st_mode & 0o777 == 0o600
            # Invalid parent directory also fails without reporting success.
            result = run([str(binary), str(protected / "bad.log"), "1", str(directory / "ignored.csv")])
            assert "Failed writing training Parquet log" in result.stderr
            print(f"PASS: {'FULL_LOGS' if full else 'standard'} schema, values, rewards, batches, empty logs, failures")

        for full, enabled in ((False, True), (True, True), (False, False)):
            binary = directory / f"model-schema-{full}-{enabled}"
            run(cxx + ["-std=c++20", "-O1", "-ffunction-sections", "-fdata-sections", "-I.",
                       "-DC220G5", "-DUSE_MODEL=true", "-DPRINT_TRAINING_DATA=true",
                       f"-DMODEL_TIMING_TELEMETRY={'true' if enabled else 'false'}",
                       "-DMAX_LOGGED_SAMPLES=7"] + (["-DTEST_FULL_LOGS"] if full else [])
                + compiler_flags("cflags") + ["tests/logging_parquet_fixture.cpp", "-o", str(binary),
                                              "-Wl,--gc-sections", "-lnuma", "-pthread"]
                + compiler_flags("libs"))
            for count in (0, 7):
                check_output(binary, directory, count, ".parquet", model_timing=enabled)
        print("PASS: model timing Parquet columns, full precision, empty logs, and opt-out schema")

        # No Arrow include paths or libraries, even in an unoptimized build.
        no_logs = directory / "no-logs.o"
        run(cxx + ["-std=c++17", "-O0", "-I.", "-DC220G5", "-DPRINT_TRAINING_DATA=false",
                   "-c", "logging_parquet.cpp", "-o", str(no_logs)])
        symbols = run(["nm", "-u", str(no_logs)]).stdout.lower()
        assert "arrow" not in symbols and "parquet" not in symbols
        dry_run = run(["make", "-Bn", "libraries/C220G5/libhemem-arms_plain.so",
                       "PARQUET_PYTHON=/not-installed/python"]).stdout
        assert "-lparquet" not in dry_run and "libparquet.so" not in dry_run
        assert "-larrow" not in dry_run and "libarrow.so" not in dry_run
        print("PASS: non-logging build does not require Arrow, Parquet, or Python")

        scripts = ["logging.sh", "measurement_common.sh", "run_all_measurements_train_dram_only.sh",
                   "run_all_measurements_train_cxl_only.sh", "run_all_measurements_train_80gb_graphs.sh"]
        for script in scripts:
            run(["bash", "-n", script])
        # Exercise only the scoped output helpers, not any benchmark/setup.
        cleanup_dir = directory / "cleanup"
        cleanup_dir.mkdir()
        for name in ("run1.log", "run1_0.log", "run1.parquet", "run2.parquet"):
            (cleanup_dir / name).write_bytes(b"test output")
        run(["bash", "-c", 'source ./measurement_common.sh; '
             'measurement_require_training_parquet "$1/run1.parquet" && '
             'cleanup_split_log_files "$1/run1.log"', "test", str(cleanup_dir)])
        assert sorted(p.name for p in cleanup_dir.iterdir()) == ["run2.parquet"]
        missing = subprocess.run(["bash", "-c", 'source ./measurement_common.sh; '
                                  'measurement_require_training_parquet "$1/run1.parquet"',
                                  "test", str(cleanup_dir)], cwd=ROOT, capture_output=True)
        assert missing.returncode != 0
        print("PASS: runner syntax, direct-output checks, and scoped rerun cleanup")


if __name__ == "__main__":
    main()
