/*
SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
SPDX-License-Identifier: Apache-2.0
*/
#ifndef _LINK_ADAPTATION_MCS_HIST_OLLA_H__
#define _LINK_ADAPTATION_MCS_HIST_OLLA_H__

#include "common/platform_types.h"
#include "openair2/LAYER2/NR_MAC_gNB/nr_mac_gNB.h"

// Constants for BLER-based adaptation
#define BLER_UPDATE_FRAME 10
#define BLER_FILTER 0.9f

// OLLA-specific parameters
#define OLLA_STEP_SIZE 1.0f
#define OLLA_TARGET_BLER 0.1f  // 10% target BLER

// Replace the OLLA_BLER_TABLE_MIMO2x2 macro with two path macros
// #define OLLA_BLER_TABLE_MIMO2x2 0  // Set to 1 for 2x2 MIMO, 0 for SISO/AWGN
// #define OLLA_BLER_TABLE_AWGN_PATH "AWGN_results"
// #define OLLA_BLER_TABLE_MIMO2x2_PATH "AWGN_MIMO2x2_results"
#define ILLA_DATA_TABLE_DIR "plugins/link_adaptation/data/AWGN_results"
#define ILLA_DATA_TABLE_FILE "bler_sigma_fit.csv"

#define SE_ZERO (120 * 2 / 1024.0f)

// Add at the top, after other OLLA macros
#define OLLA_MIN_MCS 3

// Store OLLA offset for each UE (using RNTI as identifier)
typedef struct {
    rnti_t rnti;    // RNTI=0 indicates unused slot
    float snr_offset;
    uint64_t last_access_time; // Timestamp for LRU replacement
} olla_ue_data_t;

#define MAX_UES 64
static olla_ue_data_t olla_ue_data[MAX_UES] = {0};

int32_t link_adaptation_init(void);
int32_t link_adaptation_shutdown(void);

// Function declarations for the default link adaptation implementation
int link_adaptation_get_mcs_from_bler(const NR_bler_options_t *bler_options,
                                     const NR_mac_dir_stats_t *stats,
                                     NR_bler_stats_t *bler_stats,
                                     int max_mcs,
                                     frame_t frame);

typedef struct {
    int num_acks;
    int num_nacks;
    int cumltv_tbs_ack;
} ack_nack_stats_t;

// data type for BLER vs SNR sigma fit table
#define MAX_LINES 481
#define NUM_COLS 7
#define NUM_MCS_TABLES_DEFINED 2
#define MIN_MCS_IDX 3
#define MAX_MCS_IDX 28
#define SIGMA_FIT_NOT_IMPLEMENTED 0   // Keep 0 for memset; sentinel on cbs_num_info_bits

typedef struct {
    int cbs_num_info_bits;
    float sigmoid_center_db;
    float sigmoid_scale_db;
} bler_sigma_fit_data_t;

// Function to print MCS history to console
ack_nack_stats_t print_mcs_history(NR_mcs_history_t *mcs_hist, rnti_t rnti);

#endif // _LINK_ADAPTATION_MCS_HIST_H__
