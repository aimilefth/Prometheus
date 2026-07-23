#!/usr/bin/env python3
"""Generate Prometheus GEMM builds from an already-fixed Vitis template.

One-time template assumptions:
  * HLS top: kernel_nlp
  * HLS files: src/output.cpp and src/output_2.h
  * kernel argument order: C, A, B
  * Makefile, hls.cfg, system.cfg, Tcl, and XRT code already use that interface

Configure GEMM_TRIPLETS and the resource-budget globals near the top of this
file. The only per-design template edits made here are:
  * GEMM_I, GEMM_J, GEMM_K in both host_visible.h files
  * data packing/tiling and result unpacking in host/tb_gemm.cpp

Add these marker blocks to template_build/host/tb_gemm.cpp:

  // PROMETHEUS_DSE_BEGIN_DATA_LAYOUT
  // PROMETHEUS_DSE_END_DATA_LAYOUT

  // PROMETHEUS_DSE_BEGIN_RESULT_LAYOUT
  // PROMETHEUS_DSE_END_RESULT_LAYOUT

The first block goes before XRT buffer creation. The second goes after the
kernel result has been copied back into C_new_0.
"""

from __future__ import annotations

import argparse
import re
import shlex
import shutil
import subprocess
import sys
import textwrap
from pathlib import Path


DSE_DIR = Path(__file__).resolve().parent
PROMETHEUS_ROOT = DSE_DIR.parent
TEMPLATE_DIR = DSE_DIR / "template_build"
INPUT_DIR = DSE_DIR / "inputs"
PROMETHEUS_OUTPUT_DIR = DSE_DIR / "generated"
BUILD_DIR = DSE_DIR / "builds"

# ---------------------------------------------------------------------------
# DSE configuration
# ---------------------------------------------------------------------------
# GEMM dimensions are (I, J, K), where A is I x K, B is K x J, and C is I x J.
GEMM_TRIPLETS: list[tuple[int, int, int]] = [
    (64, 64, 64),
    (512, 64, 512),
    (512, 512, 64),
    (3072, 64, 32),
    (3072, 128, 64),
    (3072, 256, 128),
    (3072, 512, 256),
    (3072, 1024, 512),
    (3072, 1024, 1024),
    (3072, 1024, 3072),
    (3072, 1024, 4096),
    (3072, 2048, 1024),
    (3072, 2048, 4096),
    (3072, 3072, 1024),
    (3072, 4096, 1024),
    (3072, 4096, 2048),
    (3072, 4096, 4096),
    (2816, 3072, 8192),
]
# Prometheus/FPGA resource budgets. Set an optional budget to None to omit the
# corresponding command-line option and let Prometheus use its default.
SLR = 1
DSP = 1968
BRAM: int | None = None
FF: int | None = None
LUT: int | None = None
ON_CHIP_MEM_SIZE: int | None = None # See if here i could add the urams


DATA_BEGIN = "// PROMETHEUS_DSE_BEGIN_DATA_LAYOUT"
DATA_END = "// PROMETHEUS_DSE_END_DATA_LAYOUT"
RESULT_BEGIN = "// PROMETHEUS_DSE_BEGIN_RESULT_LAYOUT"
RESULT_END = "// PROMETHEUS_DSE_END_RESULT_LAYOUT"


class DseError(RuntimeError):
    pass


def point_name(point: tuple[int, int, int]) -> str:
    i, j, k = point
    return f"gemm_I{i}_J{j}_K{k}"


def make_input_kernel(point: tuple[int, int, int]) -> str:
    i_size, j_size, k_size = point
    return textwrap.dedent(
        f"""\
        void kernel_gemm(float A[{i_size}][{k_size}], float B[{k_size}][{j_size}], float C[{i_size}][{j_size}]) {{
          int i, j, k;

          for (i = 0; i < {i_size}; ++i) {{
            for (j = 0; j < {j_size}; ++j) {{
              C[i][j] = 0.0f;
            }}
          }}

          for (i = 0; i < {i_size}; ++i) {{
            for (k = 0; k < {k_size}; ++k) {{
              for (j = 0; j < {j_size}; ++j) {{
                C[i][j] += A[i][k] * B[k][j];
              }}
            }}
          }}
        }}
        """
    )


def run_prometheus(
    point: tuple[int, int, int],
    force: bool,
    reuse: bool,
) -> Path:
    name = point_name(point)
    input_file = INPUT_DIR / f"{name}.c"
    output_dir = PROMETHEUS_OUTPUT_DIR / name

    INPUT_DIR.mkdir(parents=True, exist_ok=True)
    PROMETHEUS_OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    input_file.write_text(make_input_kernel(point), encoding="utf-8")

    required = [
        output_dir / "src" / "output.cpp",
        output_dir / "src" / "output_2.h",
        output_dir / "src" / "host.cpp",
    ]
    if reuse and all(path.is_file() for path in required):
        print(f"[gemm-dse] reusing {output_dir}")
        return output_dir

    if output_dir.exists():
        if not force:
            raise DseError(
                f"{output_dir} already exists; use --force or --reuse-prometheus"
            )
        shutil.rmtree(output_dir)

    command = [
        sys.executable,
        str(PROMETHEUS_ROOT / "main.py"),
        "--file",
        str(input_file),
        "--SLR",
        str(SLR),
        "--DSP",
        str(DSP),
        "--code_generation",
        "--folder",
        str(output_dir),
        "--ap_multiple_burst",
    ]

    optional_budgets = {
        "--BRAM": BRAM,
        "--FF": FF,
        "--LUT": LUT,
        "--ON_CHIP_MEM_SIZE": ON_CHIP_MEM_SIZE,
    }
    for option, value in optional_budgets.items():
        if value is not None:
            command.extend([option, str(value)])

    print("[gemm-dse] " + shlex.join(command), flush=True)
    subprocess.run(command, cwd=PROMETHEUS_ROOT, check=True)

    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        raise DseError("Prometheus did not generate:\n  " + "\n  ".join(missing))
    return output_dir


def replace_define(text: str, name: str, value: int) -> str:
    pattern = re.compile(rf"(?m)^\s*#\s*define\s+{name}\b.*$")
    replacement = f"#define {name} {value}"
    if pattern.search(text):
        return pattern.sub(replacement, text, count=1)
    return replacement + "\n" + text


def update_host_visible(path: Path, point: tuple[int, int, int]) -> None:
    if not path.is_file():
        raise DseError(f"Missing template file: {path}")

    i_size, j_size, k_size = point
    text = path.read_text(encoding="utf-8")
    text = replace_define(text, "GEMM_I", i_size)
    text = replace_define(text, "GEMM_J", j_size)
    text = replace_define(text, "GEMM_K", k_size)
    path.write_text(text, encoding="utf-8")


def extract_layout_blocks(host_cpp: str) -> tuple[str, str]:
    setup_start = re.search(r"(?m)^\s*float\s+A_ori\b", host_cpp)
    setup_end = re.search(r"(?m)^\s*cl_int\s+err\s*;", host_cpp)
    result_start = re.search(
        r"(?m)^\s*kernel_gemm\s*\(\s*A_ori\s*,\s*B_ori\s*,\s*C_ori\s*\)\s*;",
        host_cpp,
    )
    result_end = re.search(
        r'(?m)^\s*printf\s*\(\s*"C-simulation passed!\\n"\s*\)\s*;',
        host_cpp,
    )

    if not setup_start or not setup_end or setup_start.start() >= setup_end.start():
        raise DseError("Could not extract data layout from generated host.cpp")
    if not result_start or not result_end or result_start.start() >= result_end.start():
        raise DseError("Could not extract result layout from generated host.cpp")

    setup = textwrap.dedent(host_cpp[setup_start.start() : setup_end.start()]).strip()
    result = textwrap.dedent(host_cpp[result_start.start() : result_end.start()]).strip()
    return setup, result


def replace_marker_block(text: str, begin: str, end: str, body: str) -> str:
    pattern = re.compile(
        rf"(?ms)^(?P<indent>[ \t]*){re.escape(begin)}\s*$"
        rf".*?"
        rf"^(?P=indent){re.escape(end)}\s*$"
    )
    match = pattern.search(text)
    if not match:
        raise DseError(f"Missing marker block: {begin} ... {end}")

    indent = match.group("indent")
    replacement = (
        f"{indent}{begin}\n"
        f"{textwrap.indent(body, indent)}\n"
        f"{indent}{end}"
    )
    return text[: match.start()] + replacement + text[match.end() :]


def update_host_testbench(template_host: Path, generated_host: Path) -> None:
    if not template_host.is_file():
        raise DseError(f"Missing template file: {template_host}")

    host_text = template_host.read_text(encoding="utf-8")
    generated_text = generated_host.read_text(encoding="utf-8")
    setup, result = extract_layout_blocks(generated_text)

    host_text = replace_marker_block(host_text, DATA_BEGIN, DATA_END, setup)
    host_text = replace_marker_block(host_text, RESULT_BEGIN, RESULT_END, result)
    template_host.write_text(host_text, encoding="utf-8")


def create_build(
    point: tuple[int, int, int],
    prometheus_output: Path,
    force: bool,
) -> Path:
    name = point_name(point)
    build_dir = BUILD_DIR / name

    if not TEMPLATE_DIR.is_dir():
        raise DseError(f"Template directory not found: {TEMPLATE_DIR}")
    if build_dir.exists():
        if not force:
            raise DseError(f"{build_dir} already exists; use --force")
        shutil.rmtree(build_dir)

    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    shutil.copytree(TEMPLATE_DIR, build_dir)

    src_dir = build_dir / "src"
    src_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy2(prometheus_output / "src" / "output.cpp", src_dir / "output.cpp")
    shutil.copy2(prometheus_output / "src" / "output_2.h", src_dir / "output_2.h")

    update_host_visible(build_dir / "host" / "host_visible.h", point)
    update_host_visible(build_dir / "benchmark_host" / "host_visible.h", point)
    update_host_testbench(
        build_dir / "host" / "tb_gemm.cpp",
        prometheus_output / "src" / "host.cpp",
    )
    return build_dir


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Generate the GEMM points configured in GEMM_TRIPLETS."
    )
    parser.add_argument("--reuse-prometheus", action="store_true")
    parser.add_argument("--force", action="store_true")
    parser.add_argument(
        "--stop-on-error",
        action="store_true",
        help="Stop immediately when one GEMM point fails. By default, continue.",
    )
    args = parser.parse_args()

    if not (PROMETHEUS_ROOT / "main.py").is_file():
        parser.error(f"Prometheus main.py not found in {PROMETHEUS_ROOT}")

    if not GEMM_TRIPLETS:
        parser.error("GEMM_TRIPLETS is empty")

    for point in GEMM_TRIPLETS:
        if len(point) != 3 or min(point) <= 0:
            parser.error(f"Invalid GEMM triplet: {point!r}")

    for name, value in {
        "SLR": SLR,
        "DSP": DSP,
        "BRAM": BRAM,
        "FF": FF,
        "LUT": LUT,
        "ON_CHIP_MEM_SIZE": ON_CHIP_MEM_SIZE,
    }.items():
        if value is not None and value <= 0:
            parser.error(f"{name} must be positive or None")

    succeeded: list[str] = []
    failed: list[tuple[str, str]] = []

    for point in dict.fromkeys(GEMM_TRIPLETS):
        name = point_name(point)
        print(f"[gemm-dse] generating {name}", flush=True)

        try:
            generated = run_prometheus(
                point, args.force, args.reuse_prometheus
            )
            build = create_build(point, generated, args.force)
        except (DseError, OSError, subprocess.CalledProcessError) as exc:
            message = f"{type(exc).__name__}: {exc}"
            failed.append((name, message))
            print(
                f"[gemm-dse] FAILED {name}: {message}",
                file=sys.stderr,
                flush=True,
            )

            if args.stop_on_error:
                break

            continue
        except Exception as exc:
            # Catch unexpected per-design failures without swallowing
            # KeyboardInterrupt or SystemExit.
            message = f"{type(exc).__name__}: {exc}"
            failed.append((name, message))
            print(
                f"[gemm-dse] FAILED {name}: {message}",
                file=sys.stderr,
                flush=True,
            )

            if args.stop_on_error:
                break

            continue

        succeeded.append(name)
        print(f"[gemm-dse] created {build}", flush=True)

    print(
        f"[gemm-dse] complete: "
        f"{len(succeeded)} succeeded, {len(failed)} failed",
        flush=True,
    )

    if failed:
        print("[gemm-dse] failed designs:", file=sys.stderr)
        for name, message in failed:
            print(f"  - {name}: {message}", file=sys.stderr)

    # All designs are attempted, but return non-zero if any point failed.
    return 2 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())