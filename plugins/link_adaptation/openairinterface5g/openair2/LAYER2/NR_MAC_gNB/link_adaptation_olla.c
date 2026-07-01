/*
SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
SPDX-License-Identifier: Apache-2.0
*/

#include "common/utils/LOG/log.h"
#include "link_adaptation_olla.h"

#include <stdio.h>
#include <time.h>

// for logging stats

static FILE *log_file_in = NULL;
static FILE *log_file_out = NULL;

static const char *LOG_FILE_IN = "/opt/oai-gnb/plugins/link_adaptation/link_adaptation_stats_in.csv";
static const char *LOG_FILE_OUT = "/opt/oai-gnb/plugins/link_adaptation/link_adaptation_stats_out.csv";

// Functionality inspired by:
#include "openair2/NR_UE_PHY_INTERFACE/NR_Packet_Drop.h"

nr_bler_struct nr_bler_vs_snr_data[NR_NUM_MCS];

#define container_of(ptr, type, member) ({ \
    const typeof(((type *)0)->member) *__mptr = (ptr); \
    (type *)((char *)__mptr - offsetof(type, member)); \
})

// Helper function to get SNR offset for UE with given RNTI
static float* get_ue_snr_offset(rnti_t rnti) {
    // First try to find existing entry for this RNTI
    for (int i = 0; i < MAX_UES; i++) {
        if (olla_ue_data[i].rnti == rnti) {
            return &olla_ue_data[i].snr_offset;
        }
    }
    
    // If not found, find a free slot (RNTI=0)
    for (int i = 0; i < MAX_UES; i++) {
        if (olla_ue_data[i].rnti == 0) {
            olla_ue_data[i].rnti = rnti;
            olla_ue_data[i].snr_offset = 0.0f;
            return &olla_ue_data[i].snr_offset;
        }
    }
    
    // If we get here, we need to reuse a slot
    // Find the oldest entry (could be improved with timestamp)
    olla_ue_data[0].rnti = rnti;
    olla_ue_data[0].snr_offset = 0.0f;
    return &olla_ue_data[0].snr_offset;
}

#define NR_NUM_MCS 29
#define TARGET_BLER 0.1f

static float snr_at_target_bler[NR_NUM_MCS] = {0};

static void compute_snr_at_target_bler(void) {
    for (int mcs = 0; mcs < NR_NUM_MCS; mcs++) {
        snr_at_target_bler[mcs] = -9999.0f; // default: not found
        for (int i = 0; i < nr_bler_vs_snr_data[mcs].length; i++) {
            float bler = nr_bler_vs_snr_data[mcs].bler_table[i][4] / nr_bler_vs_snr_data[mcs].bler_table[i][5];
            if (bler < TARGET_BLER) {
                snr_at_target_bler[mcs] = nr_bler_vs_snr_data[mcs].bler_table[i][0]; // SNR in dB
                break;
            }
        }
    }
}

// START marker-illa-simple
// ILLA (Inner Loop Link Adaptation)
// Selects max MCS that achieves BLER target for given SNR
// max MCS s.t. BLER(SNR, MCS) < TARGET_BLER
static int illa_select_mcs(float effective_snr, int max_mcs, int min_mcs) {
    int selected_mcs = min_mcs;
    for (int mcs = max_mcs; mcs >= min_mcs; mcs--) {
        if (effective_snr > snr_at_target_bler[mcs]) {
            selected_mcs = mcs;
            break;
        }
    }
    return selected_mcs;
}
// END marker-illa-simple

// Read in each MCS file and build BLER-SINR-TB table
void init_nr_bler_table_file(const char *awgn_results_dir)
{
  memset(nr_bler_vs_snr_data, 0, sizeof(nr_bler_vs_snr_data));

  for (unsigned int i = 0; i < NR_NUM_MCS; i++) {
    char fName[1024];
    #if OLLA_BLER_TABLE_MIMO2x2
        snprintf(fName, sizeof(fName), "%s/mcs%u_cdlc_mimo2x2_dl.csv", awgn_results_dir, i);
    #else
        snprintf(fName, sizeof(fName), "%s/mcs%u_awgn_5G.csv", awgn_results_dir, i);
    #endif
    FILE *pFile = fopen(fName, "r");
    if (!pFile) {
      LOG_E(NR_MAC, "%s: open %s: %s\n", __func__, fName, strerror(errno));
      continue;
    }
    size_t bufSize = 1024;
    char *line = NULL;
    char *token;
    char *temp = NULL;
    int nlines = 0;
    while (getline(&line, &bufSize, pFile) > 0) {
      if (!strncmp(line, "SNR", 3)) {
        continue;
      }

      if (nlines > NR_NUM_SINR) {
        LOG_E(NR_MAC, "BLER FILE ERROR - num lines greater than expected - file: %s\n", fName);
        abort();
      }

      token = strtok_r(line, ";", &temp);
      int ncols = 0;
      while (token != NULL) {
        if (ncols > NUM_BLER_COL) {
          LOG_E(NR_MAC, "BLER FILE ERROR - num of cols greater than expected\n");
          abort();
        }

        nr_bler_vs_snr_data[i].bler_table[nlines][ncols] = strtof(token, NULL);
        ncols++;

        token = strtok_r(NULL, ";", &temp);
      }
      nlines++;
    }
    nr_bler_vs_snr_data[i].length = nlines;
    fclose(pFile);
  }
}


int32_t link_adaptation_init(void)
{
    LOG_I(MAC, "Initializing SNR-based OLLA link adaptation module\n");
    // Load SNR/BLER tables
    #if OLLA_BLER_TABLE_MIMO2x2
        init_nr_bler_table_file(OLLA_BLER_TABLE_MIMO2x2_PATH);
    #else
        init_nr_bler_table_file(OLLA_BLER_TABLE_AWGN_PATH);
    #endif
    compute_snr_at_target_bler();
    for (int i = 0; i < MAX_UES; i++) {
        olla_ue_data[i].rnti = 0;
        olla_ue_data[i].snr_offset = 0.0f; // This is now in SNR dB
    }

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
        fprintf(log_file_in, "timestamp,frame,rnti,max_mcs,old_mcs,num_dl_sched,num_dl_retx,bler_avg\n");
        fflush(log_file_in);
    }
    if (log_file_out) {
        fprintf(log_file_out, "timestamp,frame,rnti,max_mcs,old_mcs,new_mcs,reported_snr,effective_snr,num_dl_sched,num_dl_retx,bler_window,bler_avg\n");
        fflush(log_file_out);
    }
    return 0;
}


// Main OLLA Feedback Loop
int link_adaptation_get_mcs_from_bler(const NR_bler_options_t *bler_options,
                                     const NR_mac_dir_stats_t *stats,
                                     NR_bler_stats_t *bler_stats,
                                     int max_mcs,
                                     frame_t frame)
{
    // First call: everything is zero. Initialize to sensible defaults so we
    // don't fall through with old_mcs=0 and a zero'd rounds window.
    if (bler_stats->last_frame == 0 && bler_stats->mcs == 0) {
        bler_stats->last_frame = frame;
        bler_stats->mcs = 9;
        bler_stats->bler = (bler_options->lower + bler_options->upper) / 2.0f;
    }

    // Only update OLLA every BLER_UPDATE_FRAME frames (default: update every frame)
    int diff = frame - bler_stats->last_frame;
    if (diff < 0) // wrap around
        diff += 1024;

    // Recover the UE pointer from the DL stats pointer. The OAI dispatcher
    // guarantees we are only invoked from the DL scheduler, so
    // stats == &UE->mac_stats.dl holds.
    NR_mac_stats_t *mac_stats = container_of(stats, NR_mac_stats_t, dl);
    NR_UE_info_t *UE = container_of(mac_stats, NR_UE_info_t, mac_stats);
    rnti_t rnti = UE->rnti;

    // Determine max mcs that can be scheduled
    NR_UE_DL_BWP_t *current_BWP = &UE->current_DL_BWP;
    const int max_mcs_table = current_BWP->mcsTableIdx == 1 ? 27 : 28;
    const int max_mcs_allowed = min(max_mcs_table, bler_options->max_mcs);

    // @TODO what's the MCS in the first transmission?
    const uint8_t old_mcs = min(bler_stats->mcs, max_mcs_allowed);
    if (diff < BLER_UPDATE_FRAME)
        return old_mcs; // no update

    // Log statistics to file at the start of each update window
    if (log_file_in) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        fprintf(log_file_in, "%ld.%09ld,%d,%d,%d,%d,%d,%d,%.6f\n",
                ts.tv_sec, ts.tv_nsec, frame, rnti, max_mcs, old_mcs,
                (int)(stats->rounds[0] - bler_stats->rounds[0]),
                (int)(stats->rounds[1] - bler_stats->rounds[1]),
                bler_stats->bler);
        fflush(log_file_in);
    }

    // Now get the current SNR offset [dB] for the user
    float *olla_snr_offset = get_ue_snr_offset(rnti); // This is now in SNR dB

    // Number of scheduled transmissions since last OLLA update
    const int num_dl_sched = (int)(stats->rounds[0] - bler_stats->rounds[0]);
    // Number of retransmissions since last OLLA update (corresponds to # NACKs)
    const int num_dl_retx = (int)(stats->rounds[1] - bler_stats->rounds[1]);

    // Calculate number of ACKs and NACKs
    const int num_nacks = num_dl_retx;
    const int num_acks = num_dl_sched - num_dl_retx;
    
    // Update OLLA offset based on ACKs and NACKs
    *olla_snr_offset -= num_nacks * OLLA_STEP_SIZE;  // NACKs decrease offset
    *olla_snr_offset += num_acks * OLLA_STEP_SIZE * (OLLA_TARGET_BLER / (1.0f - OLLA_TARGET_BLER));  // ACKs increase offset
    
    // Limit the offset range
    if (*olla_snr_offset > 30.0f) *olla_snr_offset = 30.0f;
    if (*olla_snr_offset < -30.0f) *olla_snr_offset = -30.0f;

    // max_mcs assumed to come from CQI report
    max_mcs = min(max_mcs, bler_options->max_mcs);

    // SNR threshold for this max_mcs
    // float reported_snr = (max_mcs < NR_NUM_MCS) ? snr_at_target_bler[max_mcs] : snr_at_target_bler[9];
    float reported_snr = snr_at_target_bler[max_mcs];
    float effective_snr = reported_snr + *olla_snr_offset;

    // ILLA: find the largest MCS for which effective_snr > snr_at_target_bler[mcs]
    int new_mcs = illa_select_mcs(effective_snr, max_mcs_allowed, OLLA_MIN_MCS);

    // Update stats
    const float bler_window = num_dl_sched > 0 ? (float) num_dl_retx / num_dl_sched : bler_stats->bler;
    bler_stats->bler = BLER_FILTER * bler_stats->bler + (1 - BLER_FILTER) * bler_window;
    bler_stats->last_frame = frame;
    bler_stats->mcs = new_mcs;
    memcpy(bler_stats->rounds, stats->rounds, sizeof(stats->rounds));

    if (log_file_out) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        fprintf(log_file_out, "%ld.%09ld,%d,%d,%d,%d,%d,%.3f,%.3f,%d,%d,%.6f,%.6f\n",
                ts.tv_sec, ts.tv_nsec, frame, rnti, max_mcs, old_mcs, new_mcs, reported_snr, effective_snr,
                num_dl_sched, num_dl_retx, bler_window, bler_stats->bler);
        fflush(log_file_out);
    }

    LOG_D(MAC, "OLLA: RNTI %04x frame %4d baseSNR %.2f dB, offset %.2f dB, effSNR %.2f dB, MCS %d -> %d, BLER %.3f\n",
          rnti, frame, reported_snr, *olla_snr_offset, effective_snr, old_mcs, new_mcs, bler_stats->bler);
    
    return new_mcs;
}

// Shutdown the link adaptation module
int32_t link_adaptation_shutdown(void)
{
    LOG_I(MAC, "Shutting down OLLA link adaptation module\n");
    
    // Print stats for active UEs
    LOG_I(MAC, "OLLA statistics at shutdown:");
    for (int i = 0; i < MAX_UES; i++) {
        if (olla_ue_data[i].rnti != 0) {
            LOG_I(MAC, "  UE index %d: final offset %.2f\n", i, olla_ue_data[i].snr_offset);
        }
    }

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