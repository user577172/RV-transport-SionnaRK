/*
SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
SPDX-License-Identifier: Apache-2.0
*/

#include "common/utils/LOG/log.h"
#include "link_adaptation_mcs_hist_olla.h"

#include <stdio.h>
#include <time.h>

#include <math.h>

// for logging stats

static FILE *log_file_out = NULL;

static const char *LOG_FILE_OUT = "/opt/oai-gnb/plugins/link_adaptation/link_adaptation_stats_out.csv";

// int count_la_call = 0;

// Functionality inspired by:
// #include "openair2/NR_UE_PHY_INTERFACE/NR_Packet_Drop.h"

// nr_bler_struct nr_bler_vs_snr_data[NR_NUM_MCS];

bler_sigma_fit_data_t sigma_fit_data_table_pdsch[NUM_MCS_TABLES_DEFINED][MAX_MCS_IDX+1];

#define container_of(ptr, type, member) ({ \
    const typeof(((type *)0)->member) *__mptr = (ptr); \
    (type *)((char *)__mptr - offsetof(type, member)); \
})

/**
 * @brief Get current timestamp in nanoseconds
 *
 * Returns high-resolution timestamp using realtime clock.
 * Used for both LRU tracking and log file timestamps.
 *
 * @return Timestamp in nanoseconds since Unix epoch
 */
static uint64_t get_current_timestamp_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/**
 * @brief Find least recently used UE data entry
 *
 * Searches through all UE data entries to find the one with
 * the oldest access timestamp for LRU replacement.
 *
 * @return Index of LRU entry (0 to MAX_UES-1)
 */
static int find_lru_ue_entry(void) {
    int lru_idx = 0;
    uint64_t oldest_time = olla_ue_data[0].last_access_time;

    for (int i = 1; i < MAX_UES; i++) {
        if (olla_ue_data[i].last_access_time < oldest_time) {
            oldest_time = olla_ue_data[i].last_access_time;
            lru_idx = i;
        }
    }

    return lru_idx;
}

/**
 * @brief Initialize UE data entry with default values
 *
 * Sets up a UE data entry with default values and current timestamp.
 *
 * @param entry Pointer to UE data entry to initialize
 * @param rnti RNTI to assign to this entry
 */
static void init_olla_ue_entry(olla_ue_data_t* entry, rnti_t rnti) {
    entry->rnti = rnti;
    entry->snr_offset = 0.0f;
    entry->last_access_time = get_current_timestamp_ns();
}

/**
 * @brief Get SNR offset for UE with LRU replacement
 *
 * Searches for existing UE data by RNTI, creates new entry if not found,
 * or reuses least recently used slot if array is full. Implements LRU replacement.
 *
 * @param rnti Radio Network Temporary Identifier of the UE
 * @return Pointer to the UE's SNR offset (never NULL)
 * @note Thread safety: Not thread-safe, should be called from single thread
 * @warning When MAX_UES is exceeded, LRU entry is overwritten without warning
 */
static float* get_ue_snr_offset(rnti_t rnti) {
    // Validate RNTI (should be 1-65535, 0 is reserved for unused slots)
    if (rnti == 0 || rnti > 0xFFFF) {
        LOG_E(MAC, "Invalid RNTI %04x, using default RNTI 1\n", rnti);
        rnti = 1;
    }

    uint64_t current_time = get_current_timestamp_ns();

    // First try to find existing entry for this RNTI
    for (int i = 0; i < MAX_UES; i++) {
        if (olla_ue_data[i].rnti == rnti) {
            // Update access time for LRU tracking
            olla_ue_data[i].last_access_time = current_time;
            return &olla_ue_data[i].snr_offset;
        }
    }

    // If not found, find a free slot (RNTI=0)
    for (int i = 0; i < MAX_UES; i++) {
        if (olla_ue_data[i].rnti == 0) {
            init_olla_ue_entry(&olla_ue_data[i], rnti);
            LOG_D(MAC, "Allocated new OLLA UE data slot %d for RNTI %04x\n", i, rnti);
            return &olla_ue_data[i].snr_offset;
        }
    }

    // If we get here, we need to reuse a slot using LRU replacement
    int lru_idx = find_lru_ue_entry();
    LOG_D(MAC, "OLLA LRU replacement: evicting RNTI %04x from slot %d for new RNTI %04x\n",
          olla_ue_data[lru_idx].rnti, lru_idx, rnti);

    init_olla_ue_entry(&olla_ue_data[lru_idx], rnti);
    return &olla_ue_data[lru_idx].snr_offset;
}


static float snr_at_target_bler[NUM_MCS_TABLES_DEFINED][MAX_MCS_IDX+1] = {0};

// START marker-compute-snr-at-target-bler
/**
 * @brief Compute SNR thresholds for target BLER using sigmoid approximation
 *
 * Pre-computes the SNR values where each MCS achieves the target BLER threshold
 * using the inverse of the sigmoid function:
 * SNR = center_db + scale_db * log(1/BLER_target - 1)
 *
 * @note Must be called after sigma_fit_data_table_pdsch is initialized
 * @warning Sets SNR to -99.0 dB for unimplemented (table, MCS) combinations
 */
static void compute_snr_at_target_bler(void) {
    if (OLLA_TARGET_BLER <= 0.0f || OLLA_TARGET_BLER >= 1.0f) {
        LOG_E(MAC, "Invalid target BLER %.3f, must be in range (0,1)\n", OLLA_TARGET_BLER);
        abort();
    }

    const float log_term = log(1.0f / OLLA_TARGET_BLER - 1.0f);
    if (!isfinite(log_term)) {
        LOG_E(MAC, "Invalid log term for target BLER %.3f\n", OLLA_TARGET_BLER);
        abort();
    }

    for (int mcs_table_idx = 0; mcs_table_idx < NUM_MCS_TABLES_DEFINED; mcs_table_idx++) {
        for (int mcs = 0; mcs <= MAX_MCS_IDX; mcs++) {
            if (sigma_fit_data_table_pdsch[mcs_table_idx][mcs].cbs_num_info_bits != SIGMA_FIT_NOT_IMPLEMENTED) {
                float sigmoid_center_db = sigma_fit_data_table_pdsch[mcs_table_idx][mcs].sigmoid_center_db;
                float sigmoid_scale_db = sigma_fit_data_table_pdsch[mcs_table_idx][mcs].sigmoid_scale_db;

                if (sigmoid_scale_db <= 0.0f) {
                    LOG_E(MAC, "Invalid sigmoid scale parameter (<= 0) for MCS %d, table %d\n",
                          mcs, mcs_table_idx);
                    abort();
                }

                snr_at_target_bler[mcs_table_idx][mcs] = sigmoid_center_db + sigmoid_scale_db * log_term;
            } else {
                snr_at_target_bler[mcs_table_idx][mcs] = -99.0f;
            }
        }
    }
}
// END marker-compute-snr-at-target-bler

/**
 * @brief ILLA (Inner Loop Link Adaptation) - Select maximum MCS for target BLER
 *
 * Selects the highest MCS that achieves the BLER target for the given effective SNR,
 * using pre-computed per-(table, MCS) SNR thresholds derived from the sigmoid fit.
 *
 * @param effective_snr Effective SNR in dB (including OLLA offset)
 * @param mcs_table_idx MCS table index (0 for 64QAM, 1 for 256QAM)
 * @param max_mcs Maximum allowed MCS index
 * @param min_mcs Minimum allowed MCS index (typically MIN_MCS_IDX)
 * @return Selected MCS index, guaranteed to be within [min_mcs, max_mcs]
 */
static int illa_select_mcs(float effective_snr, int mcs_table_idx, int max_mcs, int min_mcs) {
    int max_mcs_allowed = mcs_table_idx == 1 ? 27 : 28;
    if (min_mcs < 0 || min_mcs > MAX_MCS_IDX || max_mcs < 0 || max_mcs > max_mcs_allowed || min_mcs > max_mcs) {
        LOG_W(MAC, "Invalid MCS bounds: min=%d, max=%d, using safe defaults\n", min_mcs, max_mcs);
        min_mcs = MIN_MCS_IDX;
        max_mcs = max_mcs_allowed;
    }

    int selected_mcs = min_mcs;
    for (int mcs = max_mcs; mcs >= min_mcs; mcs--) {
        if (effective_snr > snr_at_target_bler[mcs_table_idx][mcs]) {
            selected_mcs = mcs;
            break;
        }
    }
    return selected_mcs;
}

/**
 * @brief Initialize NR BLER lookup tables from CSV files
 *
 * Reads BLER data from CSV files for each MCS and builds lookup tables
 * containing sigmoid parameters (center and scale) for BLER approximation.
 * The sigmoid parameters are used to compute BLER values and SNR thresholds
 * for link adaptation decisions.
 *
 * @param awgn_results_dir Directory path containing MCS BLER CSV files
 * @param bler_sigma_fit_file_name Name of the CSV file containing sigmoid parameters
 * @note CSV file should contain columns: category,table_index,MCS,CBS_num_info_bits,sigmoid_center_db,sigmoid_scale_db,fit_loss
 * @warning Aborts if file cannot be opened or contains invalid data
 */
void init_bler_sigma_fit_table(const char *awgn_results_dir, const char *bler_sigma_fit_file_name){
    memset(sigma_fit_data_table_pdsch, SIGMA_FIT_NOT_IMPLEMENTED, sizeof(sigma_fit_data_table_pdsch));

    char fName[1024];
    snprintf(fName, sizeof(fName), "%s/%s", awgn_results_dir, bler_sigma_fit_file_name);
    FILE *pFile = fopen(fName, "r");

    if (!pFile) {
        // Fail gracefully: leave the table marked SIGMA_FIT_NOT_IMPLEMENTED
        // (from the memset above) rather than dereferencing a NULL FILE* in
        // getline below. compute_snr_at_target_bler() handles unimplemented
        // entries by assigning a -99 dB threshold.
        LOG_E(NR_MAC, "%s: open %s: %s\n", __func__, fName, strerror(errno));
        return;
    }
    size_t bufSize = 1024;
    char *line = NULL;
    char *token;
    char *temp = NULL;
    int nlines = 0;
    while (getline(&line, &bufSize, pFile) > 0) {
        if (!strncmp(line, "category", 8)) {
            nlines++;
            continue;
        }

        if (nlines >= MAX_LINES) {
            LOG_E(NR_MAC, "BLER FILE ERROR - num lines greater than expected - file: %s\n", fName);
            free(line); // Free allocated memory before abort
            fclose(pFile);
            abort();
        }

        token = strtok_r(line, ",", &temp);

        if(strncmp(token, "PDSCH", 5) != 0){
            // continue if not PDSCH
            nlines++;
            continue;
        }

        int ncols = 0;
        int mcs_table_idx = 0;
        int mcs = 0;
        while (token != NULL) {
            if (ncols >= NUM_COLS) {
                LOG_E(NR_MAC, "BLER FILE ERROR - num of cols greater than expected\n");
                free(line); // Free allocated memory before abort
                fclose(pFile);
                abort();
            }

            switch(ncols){
                case 0:
                    // category
                    break;
                case 1:
                    // table_index
                    mcs_table_idx = strtol(token, NULL, 10) - 1;
                    break;
                case 2:
                    // MCS
                    mcs = strtol(token, NULL, 10);
                    if(mcs<0 || mcs>MAX_MCS_IDX){
                        LOG_E(NR_MAC, "BLER FILE ERROR - MCS index not supported\n");
                        free(line); // Free allocated memory before abort
                        fclose(pFile);
                abort();
                    }
                    break;
                case 3:
                    // CBS_num_info_bits
                    sigma_fit_data_table_pdsch[mcs_table_idx][mcs].cbs_num_info_bits = strtol(token, NULL, 10);
                    break;
                case 4:
                    // sigmoid_center_db
                    sigma_fit_data_table_pdsch[mcs_table_idx][mcs].sigmoid_center_db = strtof(token, NULL);
                    break;
                case 5:
                    // sigmoid_scale_db
                    sigma_fit_data_table_pdsch[mcs_table_idx][mcs].sigmoid_scale_db = strtof(token, NULL);
                    break;
                case 6:
                    // fit loss
                    break;
                default:
                    // should not occur
                    break;
            }
            ncols++;
            token = strtok_r(NULL, ",", &temp);
        }
        nlines++;
    }
    free(line); // Free the allocated line buffer
    fclose(pFile);
}

/**
 * @brief Initialize the link adaptation module
 *
 * Sets up the OLLA link adaptation module by:
 * 1. Loading sigmoid parameters for BLER approximation
 * 2. Computing SNR thresholds for target BLER
 * 3. Initializing UE data structures
 * 4. Opening output log file for statistics
 *
 * @return 0 on success, -1 on error
 * @note Must be called before any link adaptation decisions
 * @warning Aborts if BLER tables cannot be initialized
 */
int32_t link_adaptation_init(void)
{
    LOG_I(MAC, "Initializing SNR-based OLLA link adaptation module\n");
    // Load SNR/BLER tables
    init_bler_sigma_fit_table(ILLA_DATA_TABLE_DIR, ILLA_DATA_TABLE_FILE);
    compute_snr_at_target_bler();
    for (int i = 0; i < MAX_UES; i++) {
        olla_ue_data[i].rnti = 0;
        olla_ue_data[i].snr_offset = 0.0f; // This is now in SNR dB
        olla_ue_data[i].last_access_time = 0;
    }

    // Open output log file (optional — non-fatal if directory is not writable)
    log_file_out = fopen(LOG_FILE_OUT, "w");
    if (!log_file_out) {
        LOG_W(MAC, "Could not open output log file %s (logging disabled)\n", LOG_FILE_OUT);
    }

    // Write CSV headers
    if (log_file_out) {
        fprintf(log_file_out, "timestamp,frame,rnti,max_mcs,old_mcs,new_mcs,reported_snr,effective_snr,num_acks,num_nacks,ack_tb,bler_avg\n");
        fflush(log_file_out);
    }
    return 0;
}

/**
 * @brief Main OLLA Feedback Loop
 *
 * Implements the Outer Loop Link Adaptation (OLLA) algorithm:
 * 1. Updates SNR offset based on ACK/NACK feedback
 * 2. Computes effective SNR by combining reported SNR and offset
 * 3. Selects MCS using ILLA based on effective SNR
 * 4. Updates BLER statistics for monitoring
 *
 * The SNR offset is updated using:
 * - Decrease by OLLA_STEP_SIZE for each NACK
 * - Increase by OLLA_STEP_SIZE * (OLLA_TARGET_BLER/(1-OLLA_TARGET_BLER)) for each ACK
 *
 * @param bler_options Configuration options for BLER-based adaptation
 * @param stats MAC layer statistics for the UE
 * @param bler_stats BLER statistics structure to update
 * @param max_mcs Maximum MCS from CQI report
 * @param frame Current frame number
 * @return Selected MCS index for next transmission
 * @note Updates BLER statistics every BLER_UPDATE_FRAME frames
 * @warning Assumes proper initialization of BLER tables and UE data
 */
int link_adaptation_get_mcs_from_bler(const NR_bler_options_t *bler_options,
                                     const NR_mac_dir_stats_t *stats,
                                     NR_bler_stats_t *bler_stats,
                                     int max_mcs,
                                     frame_t frame)
{
    // Only update OLLA every BLER_UPDATE_FRAME frames (default: update every frame)
    int diff = frame - bler_stats->last_frame;
    if (diff < 0) // wrap around
        diff += 1024;

    // count_la_call+=1;
    // LOG_I(MAC, "LA call %d\n", count_la_call);

    // Recover the UE pointer from the DL stats pointer. The OAI dispatcher
    // (get_mcs_from_bler in gNB_scheduler_primitives.c) routes only DL calls
    // to the plugin, so stats == &UE->mac_stats.dl holds.
    NR_mac_stats_t *mac_stats = container_of(stats, NR_mac_stats_t, dl);
    NR_UE_info_t *UE = container_of(mac_stats, NR_UE_info_t, mac_stats);
    rnti_t rnti = UE->rnti;

    // Determine max mcs that can be scheduled
    NR_UE_DL_BWP_t *current_BWP = &UE->current_DL_BWP;
    const int mcs_table_idx = current_BWP->mcsTableIdx;
    const int max_mcs_table = mcs_table_idx == 1 ? 27 : 28;
    const int max_mcs_allowed = min(max_mcs_table, bler_options->max_mcs);

    if (current_BWP->mcsTableIdx == 2) {
        LOG_E(MAC, "UE %04x uses PDSCH MCS Table 3, currently not supported by OLLA!\n", rnti);
    }

    const uint8_t old_mcs = min(bler_stats->mcs, max_mcs_allowed);

    // The DL scheduler invokes us every DL slot (~500 us), but HARQ feedback
    // only lands every few ms. Between feedback arrivals mcs_history is empty
    // and we have nothing to integrate -- return the last selected MCS without
    // touching the offset, emitting a CSV row, or logging.
    if (UE->UE_sched_ctrl.mcs_history.count == 0 && diff < BLER_UPDATE_FRAME) {
        return old_mcs;
    }

    // Print MCS history for this UE
    ack_nack_stats_t ack_nack_stats = print_mcs_history(&UE->UE_sched_ctrl.mcs_history, rnti);

    // Now get the current SNR offset [dB] for the user
    float *olla_snr_offset = get_ue_snr_offset(rnti); // This is now in SNR dB

    // Get number of ACKs and NACKs from ACK/NACK-MCS-History List
    int num_acks = ack_nack_stats.num_acks;
    int num_nacks = ack_nack_stats.num_nacks;

    // Update OLLA offset based on ACKs and NACKs
    *olla_snr_offset -= num_nacks * OLLA_STEP_SIZE;  // NACKs decrease offset
    *olla_snr_offset += num_acks * OLLA_STEP_SIZE * (OLLA_TARGET_BLER / (1.0f - OLLA_TARGET_BLER));  // ACKs increase offset

    // Limit the offset range. Upper bound widened from 10 to 30 dB to match
    // link_adaptation_olla.c — the tighter bound saturates quickly on clean
    // rfsim channels and leaves no drive room for the integrator.
    if (*olla_snr_offset > 30.0f) *olla_snr_offset = 30.0f;
    if (*olla_snr_offset < -30.0f) *olla_snr_offset = -30.0f;

    // max_mcs assumed to come from CQI report
    max_mcs = min(max_mcs, bler_options->max_mcs);

    // Validate max_mcs to prevent array out-of-bounds access
    if (max_mcs < MIN_MCS_IDX || max_mcs > MAX_MCS_IDX) {
        LOG_W(MAC, "Invalid max_mcs %d for RNTI %04x, clamping to valid range\n", max_mcs, rnti);
        max_mcs = max_mcs < MIN_MCS_IDX ? MIN_MCS_IDX : MAX_MCS_IDX;
    }

    // SNR threshold for this max_mcs
    float reported_snr = snr_at_target_bler[mcs_table_idx][max_mcs_allowed];
    float effective_snr = reported_snr + *olla_snr_offset;

    // ILLA: find the largest MCS for which effective_snr > snr_at_target_bler[mcs]
    int new_mcs = illa_select_mcs(effective_snr, mcs_table_idx, max_mcs_allowed, OLLA_MIN_MCS);

    // Update BLER stats
    if (diff >= BLER_UPDATE_FRAME){
        const int num_dl_sched = (int)(stats->rounds[0] - bler_stats->rounds[0]);
        const int num_dl_retx = (int)(stats->rounds[1] - bler_stats->rounds[1]);
        const float bler_window = num_dl_sched > 0 ? (float) num_dl_retx / num_dl_sched : bler_stats->bler;
        bler_stats->bler = BLER_FILTER * bler_stats->bler + (1 - BLER_FILTER) * bler_window;
        bler_stats->last_frame = frame;
        memcpy(bler_stats->rounds, stats->rounds, sizeof(stats->rounds));
    }

    bler_stats->mcs = new_mcs;

    if (log_file_out) {
        uint64_t timestamp_ns = get_current_timestamp_ns();
        uint64_t sec = timestamp_ns / 1000000000ULL;
        uint64_t nsec = timestamp_ns % 1000000000ULL;
        fprintf(log_file_out, "%ld.%09ld,%d,%d,%d,%d,%d,%.3f,%.3f,%d,%d,%d,%.6f\n",
                sec, nsec, frame, rnti, max_mcs, old_mcs, new_mcs, reported_snr, effective_snr,
                num_acks, num_nacks, ack_nack_stats.cumltv_tbs_ack, bler_stats->bler);
        fflush(log_file_out);
    }

    LOG_D(MAC, "OLLA: RNTI %04x frame %4d baseSNR %.2f dB, offset %.2f dB, effSNR %.2f dB, MCS %d -> %d, BLER %.3f\n",
          rnti, frame, reported_snr, *olla_snr_offset, effective_snr, old_mcs, new_mcs, bler_stats->bler);

    LOG_I(MAC, "Scheduling new MCS %d in frame %d\n\n", new_mcs, frame);
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
            LOG_I(MAC, "  UE index %d (RNTI %04x): final SNR offset %.2f dB\n",
                  i, olla_ue_data[i].rnti, olla_ue_data[i].snr_offset);
        }
    }

    if (log_file_out) {
        fclose(log_file_out);
        log_file_out = NULL;
    }

    return 0;
}

/**
 * @brief Print MCS history and compute ACK/NACK statistics
 *
 * Displays the MCS transmission history for a UE and computes
 * ACK/NACK counts for OLLA feedback.
 *
 * @param mcs_hist Pointer to MCS history structure
 * @param rnti UE's Radio Network Temporary Identifier
 * @return Structure containing ACK/NACK counts and statistics
 * @note Clears the MCS history after processing
 * @warning Function has side effects - modifies mcs_hist
 */
// START marker-print-mcs-history
ack_nack_stats_t print_mcs_history(NR_mcs_history_t *mcs_hist, rnti_t rnti) {
    ack_nack_stats_t stats = {.num_acks = 0, .num_nacks = 0, .cumltv_tbs_ack=0};

    // Input validation
    if (!mcs_hist) {
        LOG_E(MAC, "UE %04x: Null pointer in print_mcs_history\n", rnti);
        return stats;
    }

    if (mcs_hist->count == 0) {
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
// END marker-print-mcs-history
