#!/bin/bash
#
# SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#
# Shared helpers for building TensorRT engine plans across TensorRT versions.
#
# TensorRT v11 removed the precision build flags (--fp16/--best/--bf16) and the
# --preview features used in the v10 trtexec command line: strongly-typed
# networks are now the default. The precision therefore has to come from the
# ONNX model itself, which we obtain by running ModelOpt autocast to produce an
# FP16 ONNX before invoking trtexec.
#
# This file is meant to be sourced, not executed.

# Absolute path to the directory containing this helper (plugins/common).
_trt_common_dir=$(realpath "$(dirname "${BASH_SOURCE[0]}")")
# Repository root is two levels up: plugins/common -> plugins -> repo root.
SRK_REPO_ROOT=$(realpath "${_trt_common_dir}/../..")

# Resolve the trtexec binary. Prefer one on PATH (DGX Spark installs it in
# /usr/bin), fall back to the historical Jetson/x86 location.
resolve_trtexec() {
    if command -v trtexec >/dev/null 2>&1; then
        TRTEXEC=$(command -v trtexec)
    elif [ -x /usr/src/tensorrt/bin/trtexec ]; then
        TRTEXEC=/usr/src/tensorrt/bin/trtexec
    else
        echo "ERROR: could not find 'trtexec' on PATH or at /usr/src/tensorrt/bin/trtexec" >&2
        return 1
    fi
    export TRTEXEC
}

# Echo the TensorRT major version reported by trtexec.
# trtexec prints the version as a packed integer whose encoding changed across
# major releases:
#   * TensorRT <= 9  : major*1000  + minor*100 + patch  (e.g. 8.6.0.2 -> "8602")
#   * TensorRT >= 10 : major*10000 + minor*100 + patch  (e.g. 11.1.0   -> "110100")
# so the divisor depends on how many digits the packed value has.
trt_major_version() {
    if [ -z "$TRTEXEC" ]; then
        resolve_trtexec || return 1
    fi
    local version
    version=$("$TRTEXEC" --version 2>/dev/null | grep -oP 'TensorRT v\K[0-9]+' | head -n1)
    if [ -z "$version" ]; then
        echo "ERROR: could not parse TensorRT version from '$TRTEXEC --version'" >&2
        return 1
    fi
    # The old encoding tops out at 9999 (9.9.99) and the new encoding starts at
    # 100000 (10.0.0), so the value's magnitude unambiguously selects the divisor.
    if [ "$version" -ge 100000 ]; then
        echo $((version / 10000))
    elif [ "$version" -ge 1000 ]; then
        echo $((version / 1000))
    else
        # Bare major digit (e.g. "11" -> 11).
        echo "$version"
    fi
}

# Make sure ModelOpt is importable for the v11 plan build. Prefer an already
# active environment, otherwise activate one of the repository virtual
# environments (env/ created by configure-system, or a local .env/).
activate_build_venv() {
    # Already importable (e.g. the user pre-activated a virtual environment).
    if python3 -c "import modelopt" >/dev/null 2>&1; then
        return 0
    fi

    local venv
    for venv in env .env; do
        local venv_activate="${SRK_REPO_ROOT}/${venv}/bin/activate"
        [ -f "$venv_activate" ] || continue
        # Only adopt the venv if it actually provides modelopt.
        if ( # shellcheck disable=SC1090
             source "$venv_activate" && python3 -c "import modelopt" ) >/dev/null 2>&1; then
            echo "Activating virtual environment: ${SRK_REPO_ROOT}/${venv}"
            # shellcheck disable=SC1090
            source "$venv_activate"
            return 0
        fi
    done

    echo "ERROR: the 'modelopt' Python package is required to build TensorRT v11 plans" >&2
    echo "       Install it with 'pip install nvidia-modelopt[onnx]' (it is included in" >&2
    echo "       requirements.txt and installed by scripts/configure-system.dgx-spark.sh)." >&2
    return 1
}

# Convert an ONNX model to FP16 using ModelOpt autocast.
# Usage: autocast_fp16 <input.onnx> <output.fp16.onnx> [extra modelopt args...]
# Extra arguments are forwarded verbatim to modelopt.onnx.autocast (e.g.
# --calibration_data / --nodes_to_exclude for models that need them).
autocast_fp16() {
    local in_onnx="$1"
    local out_onnx="$2"
    shift 2
    echo "Casting ONNX to FP16 with ModelOpt: ${in_onnx} -> ${out_onnx}"
    python3 -m modelopt.onnx.autocast --onnx_path "$in_onnx" --output_path "$out_onnx" -t fp16 "$@"
}
