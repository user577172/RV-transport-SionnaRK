/*
SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
SPDX-License-Identifier: Apache-2.0
*/

#include "common/utils/LOG/log.h"
#include "link_adaptation_mcs_hist_log.h"

#include <stdio.h>
#include <time.h>

static FILE *log_file_out = NULL;

static const char *LOG_FILE_OUT = "/opt/oai-gnb/plugins/link_adaptation/link_adaptation_stats_out.csv";

#define container_of(ptr, type, member) ({ \
    const typeof(((type *)0)->member) *__mptr = (ptr); \
    (type *)((char *)__mptr - offsetof(type, member)); \
})

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

    // Get the UE structure from stats pointer (works for both DL and UL callers)
    NR_mac_stats_t *mac_stats_dl = container_of(stats, NR_mac_stats_t, dl);
    NR_mac_stats_t *mac_stats_ul = container_of(stats, NR_mac_stats_t, ul);
    NR_UE_info_t *UE_dl = container_of(mac_stats_dl, NR_UE_info_t, mac_stats);
    NR_UE_info_t *UE_ul = container_of(mac_stats_ul, NR_UE_info_t, mac_stats);
    NR_UE_info_t *UE = (UE_dl->rnti != 0 && UE_dl->rnti <= 0xFFF0) ? UE_dl : UE_ul;
    rnti_t rnti = UE->rnti;
    // Print MCS history for this UE
    ack_nack_stats_t ack_nack_stats = print_mcs_history(&UE->UE_sched_ctrl.mcs_history, rnti);
    // Get number of ACKs and NACKs from ACK/NACK-MCS-History List
    int num_acks = ack_nack_stats.num_acks;
    int num_nacks = ack_nack_stats.num_nacks;

    max_mcs = min(max_mcs, bler_options->max_mcs);
    const uint8_t old_mcs = min(bler_stats->mcs, max_mcs);
    if (diff < BLER_UPDATE_FRAME){
        if (log_file_out) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            fprintf(log_file_out, "%ld.%09ld,%d,%d,%d,%d,%d,%d,%d,%d,%.6f\n",
                    ts.tv_sec, ts.tv_nsec, frame, rnti, max_mcs, old_mcs, old_mcs,
                    num_acks, num_nacks, ack_nack_stats.cumltv_tbs_ack, bler_stats->bler);
            fflush(log_file_out);
        }
        return old_mcs; // no update
    }
    
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

    if (log_file_out) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        fprintf(log_file_out, "%ld.%09ld,%d,%d,%d,%d,%d,%d,%d,%d,%.6f\n",
                ts.tv_sec, ts.tv_nsec, frame, rnti, max_mcs, old_mcs, new_mcs,
                num_acks, num_nacks, ack_nack_stats.cumltv_tbs_ack, bler_stats->bler);
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
    
    // Open output log file (optional — non-fatal if directory is not writable)
    log_file_out = fopen(LOG_FILE_OUT, "w");
    if (!log_file_out) {
        LOG_W(MAC, "Could not open output log file %s (logging disabled)\n", LOG_FILE_OUT);
    }
    
    // Write CSV headers
    if (log_file_out) {
        fprintf(log_file_out, "timestamp,frame,rnti,max_mcs,old_mcs,new_mcs,num_acks,num_nacks,ack_tb,bler_avg\n");
        fflush(log_file_out);
    }
    
    return 0;
}

// Shutdown the link adaptation module
int32_t link_adaptation_shutdown(void)
{
    LOG_I(MAC, "Shutting down logging link adaptation module\n");
    
    if (log_file_out) {
        fclose(log_file_out);
        log_file_out = NULL;
    }
    
    return 0;
} 

// Function to print MCS history to console
ack_nack_stats_t print_mcs_history(NR_mcs_history_t *mcs_hist, rnti_t rnti) {
    ack_nack_stats_t stats = {.num_acks = 0, .num_nacks = 0, .cumltv_tbs_ack=0};
    if (!mcs_hist || mcs_hist->count == 0) {
        LOG_I(MAC, "UE %04x: No MCS history available\n", rnti);
        return stats;
    }

    LOG_I(MAC, "\nMCS History for UE %04x (count: %d):\n", rnti, mcs_hist->count);
    LOG_I(MAC, "----------------------------------------\n");
    LOG_I(MAC, "Index | MCS | Success | Frame.Slot | TB Size\n");
    LOG_I(MAC, "----------------------------------------\n");

    // Start from the oldest entry (head) and move forward
    int idx = mcs_hist->head;
    
    for (int i = 0; i < mcs_hist->count; i++) {
        const NR_mcs_history_entry_t *entry = &mcs_hist->entries[idx];
        uint32_t frame = entry->timestamp >> 16;
        uint32_t slot = entry->timestamp & 0xFFFF;
        stats.num_acks += entry->success;

        // update 1st iter HARQ normalized throughput statistic
        stats.cumltv_tbs_ack += entry->success ? entry->tb_size * entry->spectral_efficiency / SE_ZERO : 0;
        
        LOG_I(MAC, "%5d | %3d | %7s | %4d.%2d | %8d\n",
              i,
              entry->mcs,
              entry->success ? "ACK" : "NACK",
              frame,
              slot,
              entry->tb_size);
        
        // Move to next entry in FIFO order
        idx = (idx + 1) % MAX_MCS_HISTORY;
    }
    stats.num_nacks = mcs_hist->count - stats.num_acks;
    LOG_I(MAC, "----------------------------------------\n");
    LOG_I(MAC, "%5d ACK   %5d NACK \n", stats.num_acks, stats.num_nacks);
    LOG_I(MAC, "----------------------------------------\n");

    // Clear the history after printing
    mcs_hist->head = 0;
    mcs_hist->tail = 0;
    mcs_hist->count = 0;

    return stats;
} 