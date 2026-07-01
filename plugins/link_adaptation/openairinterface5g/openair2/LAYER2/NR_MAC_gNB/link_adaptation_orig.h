/*
SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
SPDX-License-Identifier: Apache-2.0
*/
#ifndef _LINK_ADAPTATION_DEFAULT_H__
#define _LINK_ADAPTATION_DEFAULT_H__

#include "common/platform_types.h"
#include "openair2/LAYER2/NR_MAC_gNB/nr_mac_gNB.h"

// Constants for BLER-based adaptation
#define BLER_UPDATE_FRAME 10
#define BLER_FILTER 0.9f

int32_t link_adaptation_init(void);

int32_t link_adaptation_shutdown(void);

// Function declarations for the default link adaptation implementation
int link_adaptation_get_mcs_from_bler(const NR_bler_options_t *bler_options,
                                     const NR_mac_dir_stats_t *stats,
                                     NR_bler_stats_t *bler_stats,
                                     int max_mcs,
                                     frame_t frame);


#endif // _LINK_ADAPTATION_DEFAULT_H__
