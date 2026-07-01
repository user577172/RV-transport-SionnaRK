/*
SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
SPDX-License-Identifier: Apache-2.0
*/

#include "common/utils/LOG/log.h"
#include "link_adaptation_orig.h"

// Required initialization function
int32_t link_adaptation_init(void) {
    LOG_I(MAC, "Initializing link adaptation module\n");
    return 0;
}

// Required shutdown function
int32_t link_adaptation_shutdown(void) {
    LOG_I(MAC, "Shutting down link adaptation module\n");
    return 0;
}

int link_adaptation_get_mcs_from_bler(const NR_bler_options_t *bler_options,
                      const NR_mac_dir_stats_t *stats,
                      NR_bler_stats_t *bler_stats,
                      int max_mcs,
                      frame_t frame)
{
  /* first call: everything is zero. Initialize to sensible default */
  if (bler_stats->last_frame == 0 && bler_stats->mcs == 0) {
    bler_stats->last_frame = frame;
    bler_stats->mcs = 9;
    bler_stats->bler = (bler_options->lower + bler_options->upper) / 2.0f;
  }
  int diff = frame - bler_stats->last_frame;
  if (diff < 0) // wrap around
    diff += 1024;

  max_mcs = min(max_mcs, bler_options->max_mcs);
  const uint8_t old_mcs = min(bler_stats->mcs, max_mcs);
  if (diff < BLER_UPDATE_FRAME)
    return old_mcs; // no update

  // last update is longer than x frames ago
  const int num_dl_sched = (int)(stats->rounds[0] - bler_stats->rounds[0]);
  const int num_dl_retx = (int)(stats->rounds[1] - bler_stats->rounds[1]);
  const float bler_window = num_dl_sched > 0 ? (float) num_dl_retx / num_dl_sched : bler_stats->bler;
  bler_stats->bler = BLER_FILTER * bler_stats->bler + (1 - BLER_FILTER) * bler_window;

  int new_mcs = old_mcs;
  if (bler_stats->bler < bler_options->lower && old_mcs < max_mcs && num_dl_sched > 3)
    new_mcs += 1;
  else if ((bler_stats->bler > bler_options->upper && old_mcs > 6) // above threshold
      || (num_dl_sched <= 3 && old_mcs > 9))                                // no activity
    new_mcs -= 1;
  // else we are within threshold boundaries

  bler_stats->last_frame = frame;
  bler_stats->mcs = new_mcs;
  memcpy(bler_stats->rounds, stats->rounds, sizeof(stats->rounds));
  LOG_D(MAC, "frame %4d MCS %d -> %d (num_dl_sched %d, num_dl_retx %d, BLER wnd %.3f avg %.6f)\n",
        frame, old_mcs, new_mcs, num_dl_sched, num_dl_retx, bler_window, bler_stats->bler);
  return new_mcs;
}