#!/bin/bash

if [ "${SIONNA_RK_CPU_ONLY:-1}" = "1" ]; then
    echo "NVIDIA MPS is disabled in CPU-only mode."
    exit 0
fi


echo "Stopping MPS Server"
echo "quit" | nvidia-cuda-mps-control
