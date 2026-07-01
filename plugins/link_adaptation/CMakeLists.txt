#
# SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#
# Link Adaptation Tutorial - CMakeLists.txt
#
# This tutorial demonstrates link adaptation algorithms including OLLA
# (Outer Loop Link Adaptation) and MCS histogram-based approaches.
#

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

# Source directory for link adaptation plugins
set(LA_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/openairinterface5g/openair2/LAYER2/NR_MAC_gNB)

#############################################################################
# Register MAC Plugin Loader with Parent
#############################################################################

set(MAC_PLUGINS_SRC ${MAC_PLUGINS_SRC} ${LA_SRC_DIR}/link_adaptation_load.c PARENT_SCOPE)

#############################################################################
# Include Directories for ASN.1 headers
#############################################################################

include_directories(${CMAKE_BINARY_DIR}/openair2/RRC/NR/MESSAGES)
include_directories(${CMAKE_BINARY_DIR}/openair2/RRC/LTE/MESSAGES)
include_directories(${CMAKE_BINARY_DIR})

#############################################################################
# Helper macro for ASN.1 dependencies
#############################################################################

macro(add_la_asn1_deps target)
  add_dependencies(${target} asn1_nr_rrc_hdrs)
  add_dependencies(${target} gen_nr_rrc_hdrs)
  add_dependencies(${target} asn1_lte_rrc_hdrs)
  add_dependencies(${target} gen_lte_rrc_hdrs)
  target_link_libraries(${target} PUBLIC asn1_nr_rrc_hdrs)
  target_link_libraries(${target} PUBLIC asn1_lte_rrc_hdrs)
  add_dependencies(${target} generate_T)
endmacro()

#############################################################################
# Link Adaptation Libraries
#############################################################################

# Original link adaptation (reference implementation)
add_library(link_adaptation_orig MODULE ${LA_SRC_DIR}/link_adaptation_orig.c)
add_library(link_adaptation MODULE ${LA_SRC_DIR}/link_adaptation_orig.c)
add_la_asn1_deps(link_adaptation_orig)
add_la_asn1_deps(link_adaptation)
add_dependencies(nr-softmodem link_adaptation_orig link_adaptation)

# Logging link adaptation (for data collection)
add_library(link_adaptation_log MODULE ${LA_SRC_DIR}/link_adaptation_log.c)
add_la_asn1_deps(link_adaptation_log)
add_dependencies(nr-softmodem link_adaptation_log)

# OLLA (Outer Loop Link Adaptation)
add_library(link_adaptation_olla MODULE ${LA_SRC_DIR}/link_adaptation_olla.c)
add_la_asn1_deps(link_adaptation_olla)
add_dependencies(nr-softmodem link_adaptation_olla)

# MCS Histogram OLLA (advanced OLLA with HARQ feedback history)
add_library(link_adaptation_mcs_hist_olla MODULE ${LA_SRC_DIR}/link_adaptation_mcs_hist_olla.c)
add_la_asn1_deps(link_adaptation_mcs_hist_olla)
add_dependencies(nr-softmodem link_adaptation_mcs_hist_olla)

# MCS Histogram Logging
add_library(link_adaptation_mcs_hist_log MODULE ${LA_SRC_DIR}/link_adaptation_mcs_hist_log.c)
add_la_asn1_deps(link_adaptation_mcs_hist_log)
add_dependencies(nr-softmodem link_adaptation_mcs_hist_log)

