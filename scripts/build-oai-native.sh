#!/bin/bash
# Build the patched OpenAirInterface gNB and nrUE natively without NVIDIA GPU dependencies.

set -euo pipefail

usage() {
    echo "Usage: $0 [--install-deps] [openairinterface5g_dir]"
}

install_deps=0
oai_dir=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --install-deps)
            install_deps=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            if [ -n "$oai_dir" ]; then
                usage >&2
                exit 1
            fi
            oai_dir="$1"
            shift
            ;;
    esac
done

project_root=$(realpath "$(dirname "${BASH_SOURCE[0]}")/..")
oai_dir=$(realpath -sm "${oai_dir:-${project_root}/ext/openairinterface5g}")

if [ ! -x "${oai_dir}/cmake_targets/build_oai" ]; then
    echo "Error: OpenAirInterface source is missing at ${oai_dir}." >&2
    echo "Run ./scripts/quickstart-oai.sh --no-build first." >&2
    exit 1
fi

if [ ! -e "${oai_dir}/plugins" ]; then
    ln -s "${project_root}/plugins" "${oai_dir}/plugins"
fi

export SIONNA_RK_CPU_ONLY=1
export CUDA_VISIBLE_DEVICES=""
export NVIDIA_VISIBLE_DEVICES=void

build_args=(
    -w SIMU
    --gNB
    --nrUE
    --cmake-opt -DENABLE_PLUGINS=ON
    --cmake-opt -DENABLE_CUDA=OFF
    --cmake-opt -DENABLE_DGX_OPTIMIZATIONS=OFF
    --cmake-opt -DAVX2=OFF
    --cmake-opt -DAVX512=OFF
)

if [ "$install_deps" = "1" ]; then
    build_args=(-I "${build_args[@]}")
fi

echo "Building OpenAirInterface natively in CPU-only mode"
echo "OAI source: ${oai_dir}"
"${oai_dir}/cmake_targets/build_oai" "${build_args[@]}"
