#include "app/factory_reset.h"

#include "matter/access_manager.h"

#include <lib/core/CHIPError.h>
#include <lib/core/ErrorStr.h>
#include <lib/support/logging/CHIPLogging.h>
#include <zephyr/kernel.h>

LOG_MODULE_DECLARE(soil_app, LOG_LEVEL_INF);

namespace app
{
namespace factory_reset
{

namespace
{

bool sFactoryResetScheduled = false;

void DoFactoryResetLikeNordic()
{
    if (sFactoryResetScheduled)
    {
        return;
    }

    sFactoryResetScheduled = true;

    CHIP_ERROR wipeErr = matter::access_manager::DoFullMatterWipe();
    if (wipeErr != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "DoFullMatterWipe failed: %s", chip::ErrorStr(wipeErr));
    }

    constexpr uint32_t kResetDelayMs = 150;
    k_msleep(kResetDelayMs);
    NVIC_SystemReset();
}

} // namespace

void FactoryResetEventHandler(const chip::DeviceLayer::ChipDeviceEvent * event, intptr_t)
{
    if ((event == nullptr) || (event->Type != chip::DeviceLayer::DeviceEventType::kFactoryReset))
    {
        return;
    }

    DoFactoryResetLikeNordic();
}

} // namespace factory_reset
} // namespace app

