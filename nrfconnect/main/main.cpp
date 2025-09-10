#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/wifi_mgmt.h>

#if defined(CONFIG_DK_LIBRARY)
#include <dk_buttons_and_leds.h>
#endif

#include <platform/CHIPDeviceLayer.h>
#include <app/server/Server.h>
#include <data-model-providers/codegen/Instance.h>
#include <platform/nrfconnect/DeviceInstanceInfoProviderImpl.h>
#include <platform/DeviceInstanceInfoProvider.h>
#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>
// CHIP support utilities
#include <lib/support/Span.h>
// For custom BLE advertising payload
#include <platform/Zephyr/BLEManagerImpl.h>
#include <ble/CHIPBleServiceData.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>

LOG_MODULE_REGISTER(soil_app, LOG_LEVEL_INF);
using namespace chip;
using namespace chip::DeviceLayer;

static K_SEM_DEFINE(s_net_ready, 0, 1);
// static struct net_mgmt_event_callback s_wifi_cb;
// static struct net_mgmt_event_callback s_ipv6_cb;

// static void wifi_event_handler(struct net_mgmt_event_callback *cb,
//                                uint32_t event, struct net_if *iface)
// {
//     if (event == NET_EVENT_WIFI_CONNECT_RESULT) {
//         const struct wifi_status *st = (const struct wifi_status *)cb->info;
//         if (st && st->status == 0) { k_sem_give(&s_net_ready); }
//     }
// }

// static void ipv6_event_handler(struct net_mgmt_event_callback *cb,
//                                uint32_t event, struct net_if *iface)
// {
//     if (event == NET_EVENT_IPV6_ADDR_ADD) {
//         if (iface) { k_sem_give(&s_net_ready); }
//     }
// }

extern "C" int main(void)
{
    printk("boot\n");
    LOG_INF("Soil sensor app started.");

    // Init NVS/settings etc (if you have it)
    // settings_subsys_init();

    // Start Matter immediately (no pre-wifi wait!)
    CHIP_ERROR err = PlatformMgr().InitChipStack();
    if (err != CHIP_NO_ERROR) {
        LOG_ERR("InitChipStack failed: %d", err.AsInteger());
        return 0;
    }

    // Provide development Device Attestation Credentials (DAC) for commissioning
    Credentials::SetDeviceAttestationCredentialsProvider(Credentials::Examples::GetExampleDACProvider());

    // Register DeviceInfoProvider (removes "DeviceInfoProvider is not registered" log)
    DeviceLayer::SetDeviceInstanceInfoProvider(&DeviceLayer::DeviceInstanceInfoProviderMgrImpl());

    // Provide custom BLE advertising that includes both the 16-bit Service UUID list and Service Data.
    // This improves central-side discovery (e.g., CoreBluetooth) when filtering by service UUID.
    {
        static uint8_t advFlags = BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR;
        Ble::ChipBLEDeviceIdentificationInfo idInfo;
        ConfigurationMgr().GetBLEDeviceIdentificationInfo(idInfo);

        struct __attribute__((packed)) AdvSvcData
        {
            uint8_t uuid[2];
            Ble::ChipBLEDeviceIdentificationInfo info;
        };

        static AdvSvcData svc{};
        svc.uuid[0] = 0xF6; // 0xFFF6 LE
        svc.uuid[1] = 0xFF;
        svc.info    = idInfo;

        static bt_data adv[] = {
            BT_DATA(BT_DATA_FLAGS, &advFlags, sizeof(advFlags)),
            BT_DATA_BYTES(BT_DATA_UUID16_ALL, 0xF6, 0xFF),
            BT_DATA(BT_DATA_SVC_DATA16, &svc, sizeof(svc)),
        };

        const char * name = bt_get_name();
        static bt_data scanRsp[1];
        scanRsp[0] = BT_DATA(BT_DATA_NAME_COMPLETE, name, (uint8_t)strlen(name));

        DeviceLayer::Internal::BLEMgrImpl().SetCustomAdvertising(
            chip::Span<bt_data>(adv, sizeof(adv) / sizeof(adv[0])));
        DeviceLayer::Internal::BLEMgrImpl().SetCustomScanResponse(
            chip::Span<bt_data>(scanRsp, sizeof(scanRsp) / sizeof(scanRsp[0])));
    }

    // Start BLE advertising so the commissioner can send Wi-Fi creds
    chip::DeviceLayer::ConnectivityMgr().SetBLEAdvertisingEnabled(true);
    
    chip::CommonCaseDeviceServerInitParams initParams;
    err = initParams.InitializeStaticResourcesBeforeServerInit();
    if (err != CHIP_NO_ERROR) {
        LOG_ERR("Init static server resources failed: %ld", (long)err.AsInteger());
        return -3;
    }
    // Provide a data model provider for the server (required by recent CHIP)
    initParams.dataModelProvider = chip::app::CodegenDataModelProviderInstance(initParams.persistentStorageDelegate);

    err = chip::Server::GetInstance().Init(initParams);

    if (err != CHIP_NO_ERROR) { LOG_ERR("Matter Server init failed: %ld", (long)err.AsInteger()); return -2; }

    ConnectivityMgr().SetBLEAdvertisingEnabled(true);
    LOG_INF("Matter server started; BLE advertising enabled.");

    while (true) { k_sleep(K_SECONDS(5)); }
}



// extern "C" int main(void)
// {

//     LOG_INF("Soil sensor app started.");

// #if defined(CONFIG_DK_LIBRARY)
//     dk_leds_init();
//     for (int i = 0; i < 4; ++i) { dk_set_led_on(DK_LED1); k_msleep(80); dk_set_led_off(DK_LED1); k_msleep(80); }
// #endif

//     /* Wait for Wi-Fi connect or any IPv6 on a non-loopback iface */
//     net_mgmt_init_event_callback(&s_wifi_cb, wifi_event_handler, NET_EVENT_WIFI_CONNECT_RESULT);
//     net_mgmt_add_event_callback(&s_wifi_cb);

//     net_mgmt_init_event_callback(&s_ipv6_cb, ipv6_event_handler, NET_EVENT_IPV6_ADDR_ADD);
//     net_mgmt_add_event_callback(&s_ipv6_cb);

//     LOG_INF("Waiting for Wi-Fi or IPv6 address...");
//     if (k_sem_take(&s_net_ready, K_SECONDS(60)) != 0) {
//         LOG_WRN("No Wi-Fi/IPv6 within 60s; delaying Matter startup.");
//         /* If you want to hard-require network, return here. */
//         /* return -1; */
//     }

//     CHIP_ERROR err = PlatformMgr().InitChipStack();
//     if (err != CHIP_NO_ERROR) { LOG_ERR("InitChipStack failed: %ld", (long)err.AsInteger()); return -1; }

//     Credentials::SetDeviceAttestationCredentialsProvider(Credentials::Examples::GetExampleDACProvider());

//     chip::ServerInitParams params;
//     err = chip::Server::GetInstance().Init(params);
//     if (err != CHIP_NO_ERROR) { LOG_ERR("Matter Server init failed: %ld", (long)err.AsInteger()); return -2; }

//     ConnectivityMgr().SetBLEAdvertisingEnabled(true);
//     LOG_INF("Matter server started; BLE advertising enabled.");

//     while (true) { k_sleep(K_SECONDS(5)); }
// }
