
#include "SoilSensorManager.h"
#include <app/util/attribute-storage.h>
#include <app/reporting/reporting.h>
#include <platform/CHIPDeviceLayer.h>
#include <lib/support/logging/CHIPLogging.h>
#include <lib/core/CHIPError.h>
#include <platform/PlatformManager.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(SoilSensorManager);

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;
using namespace chip::app::Clusters::SoilMeasurement;

SoilSensorManager SoilSensorManager::sInstance;

CHIP_ERROR SoilSensorManager::Init()
{
    mSensorInstance = Platform::New<Instance>(1);
    VerifyOrReturnError(mSensorInstance != nullptr, CHIP_ERROR_NO_MEMORY);

    ReturnErrorOnFailure(mSensorInstance->Init());

    // Sätt dummy-värde vid uppstart
    Attributes::SoilMoistureMeasuredValue::TypeInfo::Type dummyValue = 42;
    ReturnErrorOnFailure(mSensorInstance->SetSoilMeasuredValue(dummyValue));

    ChipLogProgress(NotSpecified, "SoilSensorManager initialized with dummy value");

    return CHIP_NO_ERROR;
}
