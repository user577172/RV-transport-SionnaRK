/*
SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
SPDX-License-Identifier: Apache-2.0
*/
#ifndef _LINK_ADAPTATION_LOG_H__
#define _LINK_ADAPTATION_LOG_H__

#include "common/platform_types.h"
#include "openair2/LAYER2/NR_MAC_gNB/nr_mac_gNB.h"

// Constants for BLER-based adaptation
#define BLER_UPDATE_FRAME 10
#define BLER_FILTER 0.9f

#define SE_ZERO (120 * 2 / 1024.0f)

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

// Function to print MCS history to console
ack_nack_stats_t print_mcs_history(NR_mcs_history_t *mcs_hist, rnti_t rnti);

#endif // _LINK_ADAPTATION_LOG_H__
