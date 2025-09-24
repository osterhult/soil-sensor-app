/*
 *    Copyright (c) 2022 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

/**
 *    @file
 *          Example project configuration file for CHIP.
 *
 *          This is a place to put application or project-specific overrides
 *          to the default configuration values for general CHIP features.
 *
 */

#pragma once

#include "matter_zephyr_compat.h"

// On Zephyr (Wi‑Fi), use Sockets-based Inet layer instead of LwIP.
#ifdef CHIP_SYSTEM_CONFIG_USE_LWIP
#undef CHIP_SYSTEM_CONFIG_USE_LWIP
#endif
#define CHIP_SYSTEM_CONFIG_USE_LWIP 0

#ifndef CHIP_SYSTEM_CONFIG_USE_OPENTHREAD_ENDPOINT
#define CHIP_SYSTEM_CONFIG_USE_OPENTHREAD_ENDPOINT 0
#endif

// Do NOT force CHIP_DEVICE_CONFIG_ENABLE_CHIPOBLE here; the nRF Connect
// platform header defines it based on Kconfig (CONFIG_BT). Rely on Kconfig.


// All clusters app has 3 group endpoints. This needs to defined here so that
// CHIP_CONFIG_MAX_GROUPS_PER_FABRIC is properly configured.
#define CHIP_CONFIG_MAX_GROUP_ENDPOINTS_PER_FABRIC 3

#define CHIP_CONFIG_ENABLE_ACL_EXTENSIONS 1

// Raise or disable long-dispatch warnings to reduce noisy logs from slow Wi‑Fi/BT events
// 0 disables logging; use a higher value to only warn on very long handlers
#ifndef CHIP_DISPATCH_EVENT_LONG_DISPATCH_TIME_WARNING_THRESHOLD_MS
#define CHIP_DISPATCH_EVENT_LONG_DISPATCH_TIME_WARNING_THRESHOLD_MS 300
#endif

// Explicitly declare the configuration version exposed via the Basic Information cluster.
#ifndef CHIP_DEVICE_CONFIG_DEVICE_CONFIGURATION_VERSION
#define CHIP_DEVICE_CONFIG_DEVICE_CONFIGURATION_VERSION 1
#endif
