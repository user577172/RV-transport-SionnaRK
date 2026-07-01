#ifndef __LINK_ADAPTATION_DEFS_H__
#define __LINK_ADAPTATION_DEFS_H__

#include <stdio.h>
#include <stdint.h>
#include "common/platform_types.h"
// #include <softmodem-common.h>
// #include "assertions.h"
// #include "openair2/RRC/NR/MESSAGES/asn1_msg.h"
#include "openair2/LAYER2/NR_MAC_gNB/nr_mac_gNB.h"

// BLER update configuration
#define BLER_UPDATE_FRAME 10
#define BLER_FILTER 0.9f

/**
 * @brief Function type for link adaptation initialization
 *
 * Initializes the link adaptation module and prepares it for operation.
 * Should be called once before using any link adaptation functions.
 *
 * @return 0 on success, negative value on error
 */
typedef int32_t(link_adaptation_initfunc_t)(void);

/**
 * @brief Function type for link adaptation shutdown
 *
 * Cleans up resources and performs any necessary cleanup operations.
 * Should be called when the link adaptation module is no longer needed.
 *
 * @return 0 on success
 */
typedef int32_t(link_adaptation_shutdownfunc_t)(void);

/**
 * @brief Function type for MCS selection based on BLER
 *
 * Main link adaptation function that selects the optimal MCS based on
 * current channel conditions, historical performance, and ACK/NACK feedback.
 *
 * This function implements the core link adaptation algorithm (OLLA)
 * and is called by the MAC layer to determine the MCS for each transmission.
 *
 * @param bler_options BLER configuration options including target BLER and MCS constraints
 * @param stats MAC layer statistics including transmission history and performance metrics
 * @param bler_stats BLER statistics structure containing current BLER and MCS state
 * @param max_mcs Maximum allowed MCS value (typically from CQI reports)
 * @param frame Current frame number for timing and logging purposes
 * @return Selected MCS value (0-28 for most MCS tables)
 */
typedef int(link_adaptation_mcsfunc_t)(const NR_bler_options_t *bler_options,
                                      const NR_mac_dir_stats_t *stats,
                                      NR_bler_stats_t *bler_stats,
                                      int max_mcs,
                                      frame_t frame);

#endif // __LINK_ADAPTATION_DEFS_H__
