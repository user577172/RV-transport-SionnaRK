#!/usr/bin/env python3
#
# SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#
"""Generate a calibration data NPZ for ModelOpt autocast of the neural receiver.

TensorRT v11 builds strongly-typed engines, so the FP16 precision must come from
the ONNX model itself. We obtain the FP16 ONNX with ``modelopt.onnx.autocast``,
which runs a reference inference to decide per-node precision. The neural
receiver has five inputs with shape and value constraints (the DMRS index
tensors drive Gather/Reshape ops), so random inputs make the reference run fail.
This script writes a small, correctly shaped and valid calibration sample.

Shapes/values mirror the 24-PRB configuration used by ``build-trt-plans.sh`` and
the runtime defaults (see plugins/neural_receiver/src/runtime/trt_receiver.cpp).
"""

import argparse

import numpy as np

# 24 PRBs configuration (matches the trtexec --optShapes in build-trt-plans.sh)
NUM_SUBCARRIERS = 288  # 24 PRBs x 12 subcarriers
NUM_PILOTS = 432  # 24 PRBs x 18 pilots
NUM_OFDM_SYMBOLS = 13
NUM_RX_ANT = 1
NUM_TX = 1
# DMRS geometry (see test_receiver.py / runtime defaults)
DMRS_OFDM_POS = [[2, 7, 11]]
DMRS_SUBCARRIER_POS = [[0, 2, 4, 6, 8, 10]]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", help="Path to the calibration NPZ file to write")
    parser.add_argument("--seed", type=int, default=0, help="RNG seed for reproducibility")
    args = parser.parse_args()

    rng = np.random.default_rng(args.seed)

    # Well-conditioned random data: keep h_hat away from zero so the receiver's
    # div_no_nan operations stay finite during the reference run.
    rx_slot = (rng.standard_normal((1, NUM_SUBCARRIERS, NUM_OFDM_SYMBOLS, NUM_RX_ANT, 2)) * 0.3).astype(np.float32)
    h_hat = (rng.standard_normal((1, NUM_PILOTS, NUM_TX, NUM_RX_ANT, 2)) * 0.3 + 1.0).astype(np.float32)

    np.savez(
        args.output,
        rx_slot=rx_slot,
        h_hat=h_hat,
        active_dmrs_ports=np.ones((1, NUM_TX), dtype=np.float32),
        dmrs_ofdm_pos=np.array(DMRS_OFDM_POS, dtype=np.int32),
        dmrs_subcarrier_pos=np.array(DMRS_SUBCARRIER_POS, dtype=np.int32),
    )
    print(f"Wrote receiver calibration data to {args.output}")


if __name__ == "__main__":
    main()
