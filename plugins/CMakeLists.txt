#
# SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#
# Sionna Research Kit - plugins Build Configuration
#

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

option(ENABLE_CUDA "Enable CUDA package" ON)

if (ENABLE_CUDA)
  find_package(CUDAToolkit REQUIRED)
  enable_language(CUDA)
  # Support multiple CUDA architectures: Orin (sm_87), Thor (sm_110), DGX Spark (sm_121)
  # Can be overridden via -DCMAKE_CUDA_ARCHITECTURES=<arch>; plugins may set CUDA_ARCHITECTURES per target
  if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES "87;110;121")
  endif()
endif()

# Base plugin entry points (subdirectories add to this via PARENT_SCOPE)
set(PLUGINS_SRC
    common/src/plugins.c
)

# Add plugin subdirectories
if (ENABLE_CUDA)
  add_subdirectory(neural_demapper)
  add_subdirectory(neural_receiver)
  add_subdirectory(ldpc_cuda)
  add_subdirectory(channel_emulation)
endif ()
add_subdirectory(data_acquisition)
add_subdirectory(link_adaptation)

# ASN.1 include paths needed by MAC plugin headers (link_adaptation)
include_directories(${CMAKE_BINARY_DIR}/openair2/RRC/NR/MESSAGES)
include_directories(${CMAKE_BINARY_DIR}/openair2/RRC/LTE/MESSAGES)
include_directories(${CMAKE_BINARY_DIR})

# Build plugins plugin library with all sources
add_library(plugins_plugins STATIC ${PLUGINS_SRC} common/src/mac_plugins.c ${MAC_PLUGINS_SRC})
add_dependencies(plugins_plugins generate_T asn1_nr_rrc_hdrs gen_nr_rrc_hdrs asn1_lte_rrc_hdrs gen_lte_rrc_hdrs)
target_link_libraries(nr-uesoftmodem PUBLIC plugins_plugins)
target_link_libraries(nr-softmodem PUBLIC plugins_plugins)
