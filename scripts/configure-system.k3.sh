#!/bin/bash
# Configure the SpacemiT K3 Pico-ITX for the Sionna-RK CPU-only port.

set -euo pipefail

if [ "$(uname -m)" != "riscv64" ]; then
    echo "Error: this configuration is intended for a riscv64 host." >&2
    exit 1
fi

export SIONNA_RK_CPU_ONLY=1
export CUDA_VISIBLE_DEVICES=""
export NVIDIA_VISIBLE_DEVICES=void

sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    git \
    ninja-build \
    pkg-config \
    python3 \
    python3-pip \
    python3-venv \
    libcjson-dev \
    libzmq3-dev

echo "K3 CPU-only prerequisites installed."
echo "CUDA, TensorRT, NVIDIA Container Runtime, and L4T packages were not installed."
