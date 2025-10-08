#include "connectivity/ble_manager.h"

#include <lib/support/logging/CHIPLogging.h>
#include <platform/ConfigurationManager.h>
#include <platform/internal/BLEManager.h>
#include <platform/nrfconnect/DeviceInstanceInfoProviderImpl.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_DECLARE(soil_app, LOG_LEVEL_INF);

namespace connectivity
{
namespace ble_manager
{

void ConfigureDeviceName()
{
    chip::Ble::ChipBLEDeviceIdentificationInfo idInfo;
    chip::DeviceLayer::ConfigurationMgr().GetBLEDeviceIdentificationInfo(idInfo);
    uint16_t disc = idInfo.GetDeviceDiscriminator();
    char advName[20];
    snprintk(advName, sizeof(advName), "SoilSensor-%03X", static_cast<unsigned>(disc));
    (void) chip::DeviceLayer::Internal::BLEMgr().SetDeviceName(advName);
}

void EnableAdvertising()
{
    chip::DeviceLayer::ConnectivityMgr().SetBLEAdvertisingEnabled(true);
    LOG_INF("Matter server started; BLE advertising enabled.");
}

void AppEventHandler(const chip::DeviceLayer::ChipDeviceEvent * event, intptr_t)
{
    switch (event->Type)
    {
    case chip::DeviceLayer::DeviceEventType::kCHIPoBLEAdvertisingChange:
        LOG_INF("BLE adv change: result=%d enabled=%d adv=%d conns=%u",
                static_cast<int>(event->CHIPoBLEAdvertisingChange.Result),
                static_cast<int>(chip::DeviceLayer::Internal::BLEMgr().IsAdvertisingEnabled()),
                static_cast<int>(chip::DeviceLayer::Internal::BLEMgr().IsAdvertising()),
                chip::DeviceLayer::Internal::BLEMgr().NumConnections());
        break;
    case chip::DeviceLayer::DeviceEventType::kCHIPoBLEConnectionEstablished:
        LOG_INF("BLE connection established");
        break;
    case chip::DeviceLayer::DeviceEventType::kCHIPoBLEConnectionClosed:
        LOG_INF("BLE connection closed");
        break;
    default:
        break;
    }
}

} // namespace ble_manager
} // namespace connectivity
