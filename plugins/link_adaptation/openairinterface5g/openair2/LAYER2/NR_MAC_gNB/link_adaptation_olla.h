/*
SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
SPDX-License-Identifier: Apache-2.0
*/
#ifndef _LINK_ADAPTATION_OLLA_H__
#define _LINK_ADAPTATION_OLLA_H__

#include "common/platform_types.h"
#include "openair2/LAYER2/NR_MAC_gNB/nr_mac_gNB.h"

// Constants for BLER-based adaptation
#define BLER_UPDATE_FRAME 10
#define BLER_FILTER 0.9f

// OLLA-specific parameters
#define OLLA_STEP_SIZE 1.0f
#define OLLA_TARGET_BLER 0.1f  // 10% target BLER

// BLER table configuration
#define OLLA_BLER_TABLE_MIMO2x2 0  // Set to 1 for 2x2 MIMO, 0 for SISO/AWGN
#define OLLA_BLER_TABLE_AWGN_PATH "plugins/link_adaptation/data/AWGN_results"
#define OLLA_BLER_TABLE_MIMO2x2_PATH "AWGN_MIMO2x2_results"

// Minimum MCS constraint
#define OLLA_MIN_MCS 3

/**
 * @brief OLLA UE-specific data structure
 *
 * Stores per-UE data for the OLLA algorithm including SNR offset.
 * This focuses on the essential SNR offset tracking.
 */
typedef struct {
    rnti_t rnti;                 // RNTI=0 indicates unused slot
    float snr_offset;            // Adaptive SNR offset in dB
} olla_ue_data_t;

#define MAX_UES 64
static olla_ue_data_t olla_ue_data[MAX_UES] = {0};

/**
 * @brief Initialize the OLLA link adaptation module
 *
 * Sets up the OLLA link adaptation module and prepares the system
 * for operation. Initializes UE data structures and BLER tables.
 *
 * @return 0 on success, negative value on error
 */
int32_t link_adaptation_init(void);

/**
 * @brief Shutdown the OLLA link adaptation module
 *
 * Cleans up resources and prints final statistics for all active UEs.
 *
 * @return 0 on success
 */
int32_t link_adaptation_shutdown(void);

/**
 * @brief Main OLLA link adaptation function
 *
 * Implements the Outer Loop Link Adaptation (OLLA) algorithm to select
 * the optimal MCS based on current channel conditions and ACK/NACK feedback.
 *
 * OLLA is a traditional link adaptation approach that adjusts the SNR offset
 * based on the difference between target and actual BLER. It provides a
 * stable baseline for link adaptation.
 *
 * @param bler_options BLER configuration options
 * @param stats MAC layer statistics
 * @param bler_stats BLER statistics structure
 * @param max_mcs Maximum allowed MCS
 * @param frame Current frame number
 * @return Selected MCS value
 */
int link_adaptation_get_mcs_from_bler(const NR_bler_options_t *bler_options,
                                     const NR_mac_dir_stats_t *stats,
                                     NR_bler_stats_t *bler_stats,
                                     int max_mcs,
                                     frame_t frame);

#endif // _LINK_ADAPTATION_OLLA_H__
