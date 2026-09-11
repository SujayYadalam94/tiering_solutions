#!/usr/bin/env python3
"""Find C++ Arrow/Parquet without installing or modifying the Python environment."""

import shlex
import shutil
import subprocess
import sys
from pathlib import Path


def flags(kind):
    if shutil.which("pkg-config") and subprocess.run(
        ["pkg-config", "--exists", "arrow", "parquet"], check=False
    ).returncode == 0:
        return subprocess.check_output(
            ["pkg-config", "--" + kind, "arrow", "parquet"], text=True
        ).strip()

    import pyarrow

    if kind == "cflags":
        return "-isystem " + shlex.quote(pyarrow.get_include())
    result = []
    for name in ("parquet", "arrow"):
        for directory in map(Path, pyarrow.get_library_dirs()):
            # Wheels ship versioned libraries without the unversioned symlinks.
            candidates = sorted(directory.glob(f"lib{name}.so.*"))
            unversioned = directory / f"lib{name}.so"
            library = unversioned if unversioned.exists() else next(iter(candidates), None)
            if library is not None:
                result.extend([str(library), f"-Wl,-rpath,{directory}"])
                break
        else:
            raise RuntimeError(f"lib{name} not found in PyArrow's library directories")
    return shlex.join(result)


if __name__ == "__main__":
    try:
        if len(sys.argv) != 2 or sys.argv[1] not in ("cflags", "libs"):
            raise ValueError("usage: parquet_flags.py cflags|libs")
        print(flags(sys.argv[1]))
    except Exception as error:
        print(f"Arrow/Parquet development files unavailable: {error}. "
              "Install arrow/parquet via pkg-config, or select a PyArrow Python "
              "with make PARQUET_PYTHON=/path/to/python.", file=sys.stderr)
        sys.exit(1)
