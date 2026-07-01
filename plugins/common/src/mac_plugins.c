/*
SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
SPDX-License-Identifier: Apache-2.0
*/
#include "mac_plugins.h"
#include <stddef.h>

// START marker-plugins

link_adaptation_interface_t link_adaptation_interface = {0};

void init_mac_plugins(void)
{
    load_link_adaptation_lib(NULL, &link_adaptation_interface);
}

void free_mac_plugins(void)
{
    free_link_adaptation_lib(&link_adaptation_interface);
}

// END marker-plugins
