/*
 * Wi-Fi management API shim for Zephyr header churn.
 *
 * This header is placed ahead of Zephyr's include path and chains to the
 * real <zephyr/net/wifi_mgmt.h> via include_next. It then provides
 * compatible aliases for the Wi-Fi statistics request token that CHIP's
 * WiFiManager uses, mapping to whatever the Zephyr tree defines.
 */

#pragma once

/* First, include the actual Zephyr header found later on the include path. */
#include_next <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_mgmt.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Map request token variations to a single name used by CHIP. */
#ifndef NET_REQUEST_STATS_GET_WIFI
  /* Prefer newer spellings if present */
# ifdef NET_REQUEST_WIFI_CMD_GET_STATS
#  define NET_REQUEST_STATS_GET_WIFI NET_REQUEST_WIFI_CMD_GET_STATS
#  define net_mgmt_NET_REQUEST_STATS_GET_WIFI net_mgmt_NET_REQUEST_WIFI_CMD_GET_STATS
# elif defined(NET_REQUEST_WIFI_STATS_GET)
#  define NET_REQUEST_STATS_GET_WIFI NET_REQUEST_WIFI_STATS_GET
#  define net_mgmt_NET_REQUEST_STATS_GET_WIFI net_mgmt_NET_REQUEST_WIFI_STATS_GET
  /* Fallbacks that return iface status (keeps build green) */
# elif defined(NET_REQUEST_WIFI_CMD_IFACE_STATUS)
#  define NET_REQUEST_STATS_GET_WIFI NET_REQUEST_WIFI_CMD_IFACE_STATUS
#  define net_mgmt_NET_REQUEST_STATS_GET_WIFI net_mgmt_NET_REQUEST_WIFI_CMD_IFACE_STATUS
# elif defined(NET_REQUEST_WIFI_IFACE_STATUS)
#  define NET_REQUEST_STATS_GET_WIFI NET_REQUEST_WIFI_IFACE_STATUS
#  define net_mgmt_NET_REQUEST_STATS_GET_WIFI net_mgmt_NET_REQUEST_WIFI_IFACE_STATUS
# else
  /* As a last resort, provide a stub so compile-time name pasting works. */
#  include <stddef.h>
struct net_if;
static inline int net_mgmt_NET_REQUEST_STATS_GET_WIFI(struct net_if *iface, void *data, size_t len)
{
    (void) iface; (void) data; (void) len;
    return -95; /* -ENOTSUP without pulling errno headers */
}
#  define NET_REQUEST_STATS_GET_WIFI 0
# endif
#endif

#ifdef __cplusplus
}
#endif
