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
#include <platform/CHIPDeviceEvent.h>
#include <platform/internal/BLEManager.h>
#include <platform/DeviceInstanceInfoProvider.h>
#include <DeviceInfoProviderImpl.h>
#include <setup_payload/OnboardingCodesUtil.h>
#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>
// CHIP support utilities
#include <lib/support/Span.h>
// Network Commissioning (Wi‑Fi) integration for nRF Connect
#include <app/clusters/network-commissioning/CodegenInstance.h>
#include <platform/nrfconnect/wifi/NrfWiFiDriver.h>
// For custom BLE advertising payload
#include <platform/Zephyr/BLEManagerImpl.h>
#include <ble/CHIPBleServiceData.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/settings/settings.h>
#include <string.h>

LOG_MODULE_REGISTER(soil_app, LOG_LEVEL_INF);
using namespace chip;
using namespace chip::DeviceLayer;

static K_SEM_DEFINE(s_net_ready, 0, 1);
// Example DeviceInfo provider instance (used to print onboarding info)
static chip::DeviceLayer::DeviceInfoProviderImpl gExampleDeviceInfoProvider;

#if defined(CONFIG_CHIP_WIFI)
// Register Network Commissioning cluster (Wi‑Fi) on endpoint 0 using nRF Wi‑Fi driver
static chip::app::Clusters::NetworkCommissioning::Instance sWiFiCommissioningInstance(
    0, &(chip::DeviceLayer::NetworkCommissioning::NrfWiFiDriver::Instance()));
#endif
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

static void AppEventHandler(const chip::DeviceLayer::ChipDeviceEvent * event, intptr_t /* arg */)
{
    using namespace chip::DeviceLayer;
    switch (event->Type)
    {
    case DeviceEventType::kCHIPoBLEAdvertisingChange:
        LOG_INF("BLE adv change: result=%d enabled=%d adv=%d conns=%u",
                (int) event->CHIPoBLEAdvertisingChange.Result, (int) Internal::BLEMgr().IsAdvertisingEnabled(),
                (int) Internal::BLEMgr().IsAdvertising(), Internal::BLEMgr().NumConnections());
        break;
    case DeviceEventType::kCHIPoBLEConnectionEstablished:
        LOG_INF("BLE connection established");
        break;
    case DeviceEventType::kCHIPoBLEConnectionClosed:
        LOG_INF("BLE connection closed");
        break;
    default:
        break;
    }
}

extern "C" int main(void)
{
    printk("boot\n");
    LOG_INF("Soil sensor app started.");

    // Do not load settings yet; BLE Manager enables BT and will register
    // its settings handlers. We load settings right after CHIP stack init.

    // Start Matter immediately (no pre-wifi wait!)
    CHIP_ERROR err = PlatformMgr().InitChipStack();
    if (err != CHIP_NO_ERROR) {
        LOG_ERR("InitChipStack failed: %d", err.AsInteger());
        return 0;
    }

    // Provide development Device Attestation Credentials (DAC) for commissioning
    Credentials::SetDeviceAttestationCredentialsProvider(Credentials::Examples::GetExampleDACProvider());

    // Register providers that some clusters expect
    DeviceLayer::SetDeviceInstanceInfoProvider(&DeviceLayer::DeviceInstanceInfoProviderMgrImpl());
    DeviceLayer::SetDeviceInfoProvider(&gExampleDeviceInfoProvider);

    // Register a handler for BLE-related and other platform events
    PlatformMgr().AddEventHandler(AppEventHandler, 0);

    // Load Zephyr settings now that CHIP stack (and BT) are initialized.
    if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
        (void) settings_load();
    }

#if defined(CONFIG_CHIP_WIFI)
    // Make Network Commissioning (Wi‑Fi) visible to the data model
    (void) sWiFiCommissioningInstance.Init();
#endif

    // Use standard CHIP BLE advertising (service data in ADV, name in scan response).
    // Only set a distinctive device name for easier discovery.
    {
        Ble::ChipBLEDeviceIdentificationInfo idInfo;
        ConfigurationMgr().GetBLEDeviceIdentificationInfo(idInfo);
        uint16_t disc = idInfo.GetDeviceDiscriminator();
        char advName[20];
        snprintk(advName, sizeof(advName), "SoilSensor-%03X", (unsigned) disc);
        (void) DeviceLayer::Internal::BLEMgr().SetDeviceName(advName);
    }

    // Defer enabling BLE advertising until after Server init and event loop start
    // to avoid races where a central connects before rendezvous is fully ready.

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

    // Print device configuration and onboarding codes to UART (like desktop examples)
    ConfigurationMgr().LogDeviceConfig();
    PrintOnboardingCodes(chip::RendezvousInformationFlags(chip::RendezvousInformationFlag::kBLE));

    // Start CHIP event loop thread (required for BLE and commissioning flows)
    err = PlatformMgr().StartEventLoopTask();
    if (err != CHIP_NO_ERROR) {
        LOG_ERR("StartEventLoopTask failed: %ld", (long)err.AsInteger());
    }

    // Ensure DeviceInfo provider uses persistent storage from the server
    gExampleDeviceInfoProvider.SetStorageDelegate(&chip::Server::GetInstance().GetPersistentStorage());
    DeviceLayer::SetDeviceInfoProvider(&gExampleDeviceInfoProvider);

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

// (duplicate removed) gExampleDeviceInfoProvider already defined above
//     ConnectivityMgr().SetBLEAdvertisingEnabled(true);
//     LOG_INF("Matter server started; BLE advertising enabled.");

//     while (true) { k_sleep(K_SECONDS(5)); }
// }
