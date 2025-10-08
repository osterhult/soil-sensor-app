#pragma once

#include <platform/CHIPDeviceLayer.h>

namespace connectivity
{
namespace ble_manager
{

void ConfigureDeviceName();
void EnableAdvertising();
void AppEventHandler(const chip::DeviceLayer::ChipDeviceEvent * event, intptr_t arg);

} // namespace ble_manager
} // namespace connectivity

