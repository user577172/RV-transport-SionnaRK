#
# SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#
# Sionna Research Kit - plugins Build Configuration
#

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

option(ENABLE_CUDA "Build NVIDIA CUDA/TensorRT plugins" OFF)

# RISC-V targets in this port are CPU-only. Keep this guard here as a second
# line of defence in case a caller accidentally passes -DENABLE_CUDA=ON.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(riscv64|riscv)$")
  if(ENABLE_CUDA)
    message(WARNING "CUDA was requested on RISC-V; forcing ENABLE_CUDA=OFF")
  endif()
  set(ENABLE_CUDA OFF CACHE BOOL "Build NVIDIA CUDA/TensorRT plugins" FORCE)
  set(ENABLE_DGX_OPTIMIZATIONS OFF CACHE BOOL "Use DGX-specific fast paths" FORCE)
endif()

if (ENABLE_CUDA)
  find_package(CUDAToolkit REQUIRED)
  enable_language(CUDA)
  if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES "87;110;121")
  endif()
endif()

set(PLUGINS_SRC
    common/src/plugins.c
)

if (ENABLE_CUDA)
  add_subdirectory(neural_demapper)
  add_subdirectory(neural_receiver)
  add_subdirectory(ldpc_cuda)
  add_subdirectory(channel_emulation)
else()
  message(STATUS "CPU-only plugin build: CUDA LDPC, TensorRT neural plugins, and CUDA channel emulation are disabled")
  list(APPEND PLUGINS_SRC common/src/cpu_only_stubs.c)
endif()
add_subdirectory(data_acquisition)
add_subdirectory(link_adaptation)

include_directories(${CMAKE_BINARY_DIR}/openair2/RRC/NR/MESSAGES)
include_directories(${CMAKE_BINARY_DIR}/openair2/RRC/LTE/MESSAGES)
include_directories(${CMAKE_BINARY_DIR})

add_library(plugins_plugins STATIC ${PLUGINS_SRC} common/src/mac_plugins.c ${MAC_PLUGINS_SRC})
add_dependencies(plugins_plugins generate_T asn1_nr_rrc_hdrs gen_nr_rrc_hdrs asn1_lte_rrc_hdrs gen_lte_rrc_hdrs)
target_link_libraries(nr-uesoftmodem PUBLIC plugins_plugins)
target_link_libraries(nr-softmodem PUBLIC plugins_plugins)
