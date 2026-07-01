/*
SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
SPDX-License-Identifier: Apache-2.0
*/
#ifndef _LINK_ADAPTATION_EXTERN_H__
#define _LINK_ADAPTATION_EXTERN_H__

// START marker-plugin-extern
#include "link_adaptation_defs.h"

/**
 * @brief Link adaptation interface structure
 *
 * Defines the interface for dynamic library loading of link adaptation modules.
 * This structure contains function pointers to the core link adaptation functions
 * and is used by the plugin system to load different link adaptation algorithms
 * (OLLA, etc.) at runtime.
 */
typedef struct link_adaptation_interface_s {
    link_adaptation_initfunc_t     *init;           // Initialization function
    link_adaptation_shutdownfunc_t *shutdown;       // Shutdown function
    link_adaptation_mcsfunc_t      *get_mcs_from_bler; // Main MCS selection function
} link_adaptation_interface_t;

// Global access point for the plugin interface
extern link_adaptation_interface_t link_adaptation_interface;

/**
 * @brief Load link adaptation library
 *
 * Dynamically loads a link adaptation library based on the specified version.
 * The library should export the required functions (init, shutdown, get_mcs_from_bler)
 * and populate the interface structure with the appropriate function pointers.
 *
 * @param version Version string identifying which link adaptation algorithm to load
 * @param interface Pointer to interface structure to populate with function pointers
 * @return 0 on success, negative value on error
 */
int load_link_adaptation_lib(char *version, link_adaptation_interface_t *interface);

/**
 * @brief Free link adaptation library
 *
 * Unloads the currently loaded link adaptation library and cleans up
 * any associated resources. Should be called before loading a different
 * library or when shutting down the system.
 *
 * @param interface Pointer to interface structure to clean up
 * @return 0 on success, negative value on error
 */
int free_link_adaptation_lib(link_adaptation_interface_t *interface);

// Function declarations that will be loaded from the library

/**
 * @brief Initialize link adaptation module
 *
 * Initializes the currently loaded link adaptation module and prepares
 * it for operation. This function pointer is populated by the library
 * loading mechanism.
 *
 * @return 0 on success, negative value on error
 */
link_adaptation_initfunc_t     link_adaptation_init;

/**
 * @brief Shutdown link adaptation module
 *
 * Cleans up resources and performs any necessary cleanup operations
 * for the currently loaded link adaptation module.
 *
 * @return 0 on success
 */
link_adaptation_shutdownfunc_t link_adaptation_shutdown;

/**
 * @brief Get MCS from BLER statistics
 *
 * Main link adaptation function that selects the optimal MCS based on
 * current channel conditions, historical performance, and ACK/NACK feedback.
 * This function pointer is populated by the library loading mechanism.
 *
 * @param bler_options BLER configuration options
 * @param stats MAC layer statistics
 * @param bler_stats BLER statistics structure
 * @param max_mcs Maximum allowed MCS
 * @param frame Current frame number
 * @return Selected MCS value
 */
link_adaptation_mcsfunc_t      link_adaptation_get_mcs_from_bler;

#endif // _LINK_ADAPTATION_EXTERN_H__
