#
# SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#
import pytest
import numpy as np
import torch
import sionna as sn
from sionna.phy.fec.ldpc import LDPC5GEncoder

def decode_cuda(compiled_decoder, enc, llr, num_iter):
    """
    Wrapper to call the CUDA decoder.
    Adapts Sionna/Input LLRs to the format expected by the CUDA kernel.
    """
    if isinstance(llr, torch.Tensor):
        llr_np = llr.detach().cpu().numpy()
    else:
        llr_np = np.asarray(llr)

    bs = llr_np.shape[0]
    Zc = enc.z
    k = enc.k
    n = enc.n
    BG = 1 if enc._bg == "bg1" else 2

    num_vn = 68*Zc if BG == 1 else 52*Zc
    parity_start = 22*Zc if BG == 1 else 10*Zc

    llr_ch = np.clip(llr_np/32*127, -127, 127).astype(np.int8)

    llr_input = np.zeros((bs, num_vn), dtype=np.int8)

    # 2*Zc are punctured columns in 5G LDPC
    llr_input[:, 2*Zc:k] = llr_ch[:, :k-2*Zc]

    # Shortened bits are set to max LLR (certainty)
    llr_input[:, k:parity_start] = 127

    llr_input[:, parity_start:parity_start+n-k+2*Zc] = llr_ch[:, k-2*Zc:]

    uhats = np.zeros((bs, k), dtype=np.uint8)

    for i in range(bs):
        u_packed = compiled_decoder.decode(BG, Zc, llr_input[i], k, num_iter)

        unpacked = np.unpackbits(u_packed.view(np.uint8))

        uhats[i] = unpacked[:k]

    return uhats

# Test configurations covering different lifting sets (ils 0-7)
# BG selection per 3GPP 38.212: BG2 if k<=292 OR (k<=3824 AND rate<=0.67) OR rate<=0.25
TEST_CONFIGS = [
    # BG1 configs - different lifting sets (rate > 0.67 for k > 292)
    ("bg1", 440, 550),    # Z from lifting set 0 (powers of 2), rate=0.8
    ("bg1", 660, 825),    # Z from lifting set 1 (multiples of 3), rate=0.8
    ("bg1", 800, 1000),   # Z from lifting set 0, rate=0.8
    # BG2 configs - different lifting sets (k<=292 OR rate<=0.67)
    ("bg2", 292, 584),    # Z from lifting set 1, rate=0.5
    ("bg2", 200, 400),    # Z from lifting set 0, rate=0.5
    ("bg2", 100, 200),    # Z from lifting set 0, rate=0.5
]

@pytest.mark.parametrize("bg,k,n", TEST_CONFIGS)
def test_decoder_identity(compiled_decoder, bg, k, n):
    """
    Test that the decoder works with high SNR (effectively clean channel).
    Tests multiple (k, n) configurations to exercise different lifting factors.
    """
    encoder = LDPC5GEncoder(k, n)

    expected_bg = "bg1" if bg == "bg1" else "bg2"
    assert encoder._bg == expected_bg, f"Expected {expected_bg} but got {encoder._bg}"

    bs = 10
    u = torch.randint(0, 2, (bs, k), dtype=torch.float32)

    c = encoder(u)

    # BPSK: 0 -> +1, 1 -> -1
    x = 1.0 - 2.0 * c

    y = x

    llr_sim = y * 30.0

    llr_t = llr_sim if isinstance(llr_sim, torch.Tensor) else torch.tensor(llr_sim, dtype=torch.float32)

    num_iter = 50

    print(f"Testing: BG={1 if encoder._bg == 'bg1' else 2}, Z={encoder.z}, k={k}, n={n}")

    u_hat = decode_cuda(compiled_decoder, encoder, llr_t, num_iter)

    u_int = u.numpy().astype(np.uint8)

    start_sys = 2 * encoder.z
    sys_errors = np.sum(u_int[:, start_sys:k] != u_hat[:, start_sys:k])

    print(f"Systematic Bit Errors: {sys_errors}/{bs * (k - start_sys)}")

    if sys_errors > 0:
        print(f"Found {sys_errors} systematic bit errors")

    total_errors = np.sum(u_int != u_hat)
    print(f"Total Bit Errors: {total_errors}/{bs * k}")

    assert sys_errors == 0, f"Found {sys_errors} systematic bit errors in clean channel test for {bg} (k={k}, n={n}, Z={encoder.z}) - expected 0"
    assert total_errors == 0, f"Found {total_errors} total bit errors in clean channel test for {bg} (k={k}, n={n}, Z={encoder.z}) - expected 0"

def test_decoder_bler_perf(compiled_decoder):
    """
    Run a small MC simulation to check BLER is reasonable at a specific SNR point.
    Using parameters from the notebook.
    """
    k = 800
    n = 1000
    num_iter = 8

    enc = LDPC5GEncoder(k, n)

    num_bits_per_symbol = 2 # QPSK

    constellation = sn.phy.mapping.Constellation("qam", num_bits_per_symbol)
    mapper = sn.phy.mapping.Mapper(constellation=constellation)
    demapper = sn.phy.mapping.Demapper("maxlog", constellation=constellation)
    awgn_channel = sn.phy.channel.AWGN()
    binary_source = sn.phy.mapping.BinarySource()

    ebno_db = 6.0
    coderate = k/n
    no = sn.phy.utils.ebnodb2no(ebno_db, num_bits_per_symbol=num_bits_per_symbol, coderate=coderate)

    bs = 100
    u = binary_source([bs, k])
    c = enc(u)
    x = mapper(c)
    y = awgn_channel(x, no)
    llr = demapper(y, no)

    # Sionna 2 LLR convention: positive LLR indicates bit is likely 1
    # 5G LDPC expects LLR = log(P(0)/P(1)), so invert
    llr = -llr

    u_hat = decode_cuda(compiled_decoder, enc, llr, num_iter)

    bler = sn.phy.utils.compute_bler(u, torch.tensor(u_hat, dtype=u.dtype, device=u.device))

    assert bler <= .1, f"BLER {bler} at 6dB"
