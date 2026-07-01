/*
SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
SPDX-License-Identifier: Apache-2.0
*/

#include "common/utils/LOG/log.h"
#include "link_adaptation_log.h"

#include <stdio.h>
#include <time.h>

static FILE *log_file_in = NULL;
static FILE *log_file_out = NULL;

static const char *LOG_FILE_IN = "/opt/oai-gnb/plugins/link_adaptation/link_adaptation_stats_in.csv";
static const char *LOG_FILE_OUT = "/opt/oai-gnb/plugins/link_adaptation/link_adaptation_stats_out.csv";

// Logging implementation of get_mcs_from_bler
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

    // Log statistics to file at the start of each update window
    if (log_file_in) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        fprintf(log_file_in, "%ld.%09ld,%d,%d,%d,%d,%d,%.6f\n",
                ts.tv_sec, ts.tv_nsec, frame, max_mcs, old_mcs,
                (int)(stats->rounds[0] - bler_stats->rounds[0]),
                (int)(stats->rounds[1] - bler_stats->rounds[1]),
                bler_stats->bler);
        fflush(log_file_in);
    }

    // START marker-bler-ewma
    // last update is longer than x frames ago
    const int num_dl_sched = (int)(stats->rounds[0] - bler_stats->rounds[0]);
    const int num_dl_retx = (int)(stats->rounds[1] - bler_stats->rounds[1]);
    const float bler_window = num_dl_sched > 0 ? (float) num_dl_retx / num_dl_sched : bler_stats->bler;
    bler_stats->bler = BLER_FILTER * bler_stats->bler + (1 - BLER_FILTER) * bler_window;
    // END marker-bler-ewma

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

    if (log_file_out) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        fprintf(log_file_out, "%ld.%09ld,%d,%d,%d,%d,%d,%d,%.6f,%.6f\n",
                ts.tv_sec, ts.tv_nsec, frame, max_mcs, old_mcs, new_mcs,
                num_dl_sched, num_dl_retx, bler_window, bler_stats->bler);
        fflush(log_file_out);
    }

    LOG_D(MAC, "LOG: frame %4d MCS %d -> %d (num_dl_sched %d, num_dl_retx %d, BLER wnd %.3f avg %.6f)\n",
          frame, old_mcs, new_mcs, num_dl_sched, num_dl_retx, bler_window, bler_stats->bler);
    return new_mcs;
}

// Initialize the link adaptation module
int32_t link_adaptation_init(void)
{
    LOG_I(MAC, "Initializing logging link adaptation module\n");
    
    // Open log files (optional — non-fatal if directory is not writable)
    log_file_in = fopen(LOG_FILE_IN, "w");
    if (!log_file_in) {
        LOG_W(MAC, "Could not open input log file %s (logging disabled)\n", LOG_FILE_IN);
    }
    log_file_out = fopen(LOG_FILE_OUT, "w");
    if (!log_file_out) {
        LOG_W(MAC, "Could not open output log file %s (logging disabled)\n", LOG_FILE_OUT);
    }
    
    // Write CSV headers
    if (log_file_in) {
        fprintf(log_file_in, "timestamp,frame,max_mcs,old_mcs,num_dl_sched,num_dl_retx,bler_avg\n");
        fflush(log_file_in);
    }
    if (log_file_out) {
        fprintf(log_file_out, "timestamp,frame,max_mcs,old_mcs,new_mcs,num_dl_sched,num_dl_retx,bler_window,bler_avg\n");
        fflush(log_file_out);
    }
    
    return 0;
}

// Shutdown the link adaptation module
int32_t link_adaptation_shutdown(void)
{
    LOG_I(MAC, "Shutting down logging link adaptation module\n");
    
    if (log_file_in) {
        fclose(log_file_in);
        log_file_in = NULL;
    }
    
    if (log_file_out) {
        fclose(log_file_out);
        log_file_out = NULL;
    }
    
    return 0;
} 