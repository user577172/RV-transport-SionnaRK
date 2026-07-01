#!/bin/bash
#
# SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

# suppress outputs from pushd and popd
function pushd() {
  command pushd "$@" > /dev/null
}

function popd() {
  command popd "$@" > /dev/null
}

default_dir=$(realpath $(dirname "${BASH_SOURCE[0]}"))
models_dir=$(realpath ${default_dir}/../models)

# shared helpers for resolving trtexec and the FP16 autocast (v11) path
source "${default_dir}/../../common/build-trt-common.sh"

# defaults
plan_file="${models_dir}/neural_demapper.2xfloat16.plan"
onnx_file="${models_dir}/neural_demapper.2xfloat16.onnx"

while [[ $# -gt 0 ]]; do
    case $1 in
        --plan)
            plan_file="$2"
            shift 2
            ;;
        --onnx)
            onnx_file="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--plan PATH] [--onnx PATH]"
            echo "  --plan PATH   Output plan file path (default: models/neural_demapper.2xfloat16.plan)"
            echo "  --onnx PATH   Input ONNX file path (default: models/neural_demapper.2xfloat16.onnx)"
            exit 1
            ;;
    esac
done

# normalize paths
plan_file=$(realpath -sm "$plan_file")
onnx_file=$(realpath -sm "$onnx_file")

echo "Using plan file: $plan_file"
echo "Using ONNX file: $onnx_file"

resolve_trtexec || exit 1
trt_major=$(trt_major_version) || exit 1
echo "Detected TensorRT major version: ${trt_major}"

if [ "$trt_major" -ge 11 ]; then
    # TensorRT v11+: strongly-typed networks are the default and the precision
    # build flags were removed, so we first autocast the ONNX to FP16.
    activate_build_venv || exit 1
    fp16_onnx="${onnx_file%.onnx}.fp16.onnx"
    autocast_fp16 "${onnx_file}" "${fp16_onnx}" || exit 1
    "$TRTEXEC" --onnx="${fp16_onnx}" --saveEngine="${plan_file}" --minShapes=y:1x2 --optShapes=y:64x2 --maxShapes=y:512x2
else
    # TensorRT v10 and earlier.
    "$TRTEXEC" --fp16 --onnx="${onnx_file}" --saveEngine="${plan_file}" --preview=+profileSharing0806 --inputIOFormats=fp16:chw --outputIOFormats=fp16:chw --minShapes=y:1x2 --optShapes=y:64x2 --maxShapes=y:512x2
fi
