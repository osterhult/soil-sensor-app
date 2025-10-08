#pragma once

#include <platform/CHIPDeviceLayer.h>

namespace app
{
namespace factory_reset
{

void FactoryResetEventHandler(const chip::DeviceLayer::ChipDeviceEvent * event, intptr_t arg);

} // namespace factory_reset
} // namespace app

