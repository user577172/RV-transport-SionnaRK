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
models_dir=$(realpath ${default_dir}/../../../ext/neural_rx/onnx_models)

# shared helpers for resolving trtexec and the FP16 autocast (v11) path
source "${default_dir}/../../common/build-trt-common.sh"

# defaults
plan_file="${default_dir}/../models/nrx_oai.plan"
onnx_file="${models_dir}/nrx_oai.onnx"

# Fall back to the ONNX model bundled with the plugin when the re-export
# location under ext/neural_rx is not available (e.g. on host builds where the
# neural_rx submodule is not checked out).
if [ ! -f "$onnx_file" ]; then
    onnx_file="${default_dir}/../models/nrx_oai.onnx"
fi

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
            echo "  --plan PATH   Output plan file path (default: plugins/neural_receiver/models/nrx_oai.plan)"
            echo "  --onnx PATH   Input ONNX file path (default: ../../../ext/neural_rx/onnx_models/nrx_oai.onnx)"
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
    # build flags / IO format overrides were removed, so we first autocast the
    # ONNX to FP16. The FP16 types (and the int32 DMRS inputs) come from the
    # converted model itself.
    activate_build_venv || exit 1
    fp16_onnx="${onnx_file%.onnx}.fp16.onnx"

    # ModelOpt autocast runs a reference inference to choose per-node precision.
    # The receiver's five inputs (incl. the int32 DMRS index tensors) need a
    # correctly shaped, valid sample, so we generate calibration data first.
    calib_data=$(mktemp --suffix=.npz)
    trap 'rm -f "${calib_data}"' EXIT
    python3 "${default_dir}/make_calibration_data.py" "${calib_data}" || exit 1

    # Keep the nr_preprocessing subgraph in FP32: it round-trips tensor
    # dimensions through float math, and FP16 loses integer exactness for the
    # larger dimensions (>2048), which corrupts a dynamic Reshape at build time.
    autocast_fp16 "${onnx_file}" "${fp16_onnx}" --calibration_data "${calib_data}" --nodes_to_exclude ".*nr_preprocessing.*" || exit 1

    # 24 PRBs
    "$TRTEXEC" --onnx="${fp16_onnx}" --saveEngine="${plan_file}" --minShapes=rx_slot:1x288x13x1x2,h_hat:1x432x1x1x2 --optShapes=rx_slot:1x288x13x1x2,h_hat:1x432x1x1x2 --maxShapes=rx_slot:1x288x13x1x2,h_hat:1x432x1x1x2

    # 51 PRBs
    # requires re-export of ONNX model via /ext/neural_rx/scripts/export_onnx.py and modify the number of PRBs in the config file n_size_bwp_eval=51
    # Note that MAX_BLOCK_LEN is currently hardcoded in the receiver plugin.
    #"$TRTEXEC" --onnx=${fp16_onnx} --saveEngine=${plan_file} --minShapes=rx_slot:1x612x13x1x2,h_hat:1x918x1x1x2 --optShapes=rx_slot:1x612x13x1x2,h_hat:1x918x1x1x2 --maxShapes=rx_slot:1x612x13x1x2,h_hat:1x918x1x1x2
else
    # TensorRT v10 and earlier.
    # 24 PRBs
    "$TRTEXEC" --fp16 --onnx="${onnx_file}" --saveEngine="${plan_file}" --minShapes=rx_slot:1x288x13x1x2,h_hat:1x432x1x1x2 --optShapes=rx_slot:1x288x13x1x2,h_hat:1x432x1x1x2 --maxShapes=rx_slot:1x288x13x1x2,h_hat:1x432x1x1x2 --inputIOFormats=fp16:chw,fp16:chw,fp16:chw,int32:chw,int32:chw --outputIOFormats=fp16:chw

    # 51 PRBs
    # requires re-export of ONNX model via /ext/neural_rx/scripts/export_onnx.py and modify the number of PRBs in the config file n_size_bwp_eval=51
    # Note that MAX_BLOCK_LEN is currently hardcoded in the receiver plugin.
    #"$TRTEXEC" --fp16 --onnx="${onnx_file}" --saveEngine="${plan_file}" --minShapes=rx_slot:1x612x13x1x2,h_hat:1x918x1x1x2 --optShapes=rx_slot:1x612x13x1x2,h_hat:1x918x1x1x2 --maxShapes=rx_slot:1x612x13x1x2,h_hat:1x918x1x1x2 --inputIOFormats=fp16:chw,fp16:chw,fp16:chw,int32:chw,int32:chw --outputIOFormats=fp16:chw
fi
