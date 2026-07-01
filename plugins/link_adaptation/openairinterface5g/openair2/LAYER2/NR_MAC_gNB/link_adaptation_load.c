/*
SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
SPDX-License-Identifier: Apache-2.0
*/
#include "common/config/config_userapi.h"
#include "common/utils/LOG/log.h"
#include "common/utils/load_module_shlib.h"
#include "link_adaptation_extern.h"

// @TODO: Q: can this be inside the loader function?
/* link_adaptation_arg is used to initialize the config module so that the loader works as expected */
static char *link_adaptation_arg[64]={"link_adaptation_default",NULL};

int load_link_adaptation_lib( char *version, link_adaptation_interface_t *interface )
{
    char *ptr = (char*)config_get_if();
    char libname[64] = "link_adaptation";

    if (ptr == NULL) {  // config module possibly not loaded
        uniqCfg = load_configmodule( 1, link_adaptation_arg, CONFIG_ENABLECMDLINEONLY );
        logInit();
    }

    // function description array for the shlib loader
    loader_shlibfunc_t shlib_fdesc[] = { {.fname = "link_adaptation_init" },
                                         {.fname = "link_adaptation_shutdown" },
                                         {.fname = "link_adaptation_get_mcs_from_bler" }};

    int ret;
    ret = load_module_version_shlib( libname, version, shlib_fdesc, sizeofArray(shlib_fdesc), NULL );
    AssertFatal((ret >= 0), "Error loading link adaptation library");

    // assign loaded functions to the interface
    interface->init = (link_adaptation_initfunc_t *)shlib_fdesc[0].fptr;
    interface->shutdown = (link_adaptation_shutdownfunc_t *)shlib_fdesc[1].fptr;
    interface->get_mcs_from_bler = (link_adaptation_mcsfunc_t *)shlib_fdesc[2].fptr;

    AssertFatal( interface->init() == 0, "Error starting link adaptation library %s %s\n", libname, version );

    return 0;
}

int free_link_adaptation_lib( link_adaptation_interface_t *link_adaptation_interface )
{
    return link_adaptation_interface->shutdown();
}
// END marker-plugin-load
