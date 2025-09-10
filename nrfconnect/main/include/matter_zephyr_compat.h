#pragma once

/* Zephyr networking + Wi-Fi mgmt */
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_stats.h>
#include <zephyr/net/wifi_mgmt.h>

/*
 * Compat layer for Zephyr's Wi-Fi "get stats" request naming churn.
 * We alias the request TOKEN and the net_mgmt-pasted HANDLER name.
 *
 * net_mgmt(X, ...) expands to: net_mgmt_##X(...)
 * so we must also provide: #define net_mgmt_NET_REQUEST_STATS_GET_WIFI <real handler>
 */

 /* Old OpenThread endpoint macro was removed upstream—defuse it if some header defines it */
#ifdef CHIP_SYSTEM_CONFIG_USE_OPEN_THREAD_ENDPOINT
#  undef CHIP_SYSTEM_CONFIG_USE_OPEN_THREAD_ENDPOINT
#endif
#ifndef CHIP_SYSTEM_CONFIG_USE_OPENTHREAD_ENDPOINT
#  define CHIP_SYSTEM_CONFIG_USE_OPENTHREAD_ENDPOINT 0
#endif

#ifndef NET_REQUEST_STATS_GET_WIFI
  /* Newer spellings first */
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
# endif
#endif

/*
 * Legacy ZAP generators sometimes emit SINGLETON attribute mask flags.
 * Newer CHIP trees do not define this bit. Make it a no-op for compatibility.
 */
#ifndef MATTER_ATTRIBUTE_FLAG_SINGLETON
#define MATTER_ATTRIBUTE_FLAG_SINGLETON 0
#endif
