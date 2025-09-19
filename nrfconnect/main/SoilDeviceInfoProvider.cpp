#include "SoilDeviceInfoProvider.h"

#include <lib/support/CHIPMemString.h>
#include <lib/support/CodeUtils.h>

#include <cstring>

namespace chip {
namespace DeviceLayer {

namespace {
constexpr char kPartNumber[]    = "GH-SS-01";
constexpr char kProductUrl[]    = "https://www.grimsholmgreen.se/products/soil-sensor";
constexpr char kProductLabel[]  = "Soil Sensor Model 1";
constexpr auto kProductFinish   = app::Clusters::BasicInformation::ProductFinishEnum::kMatte;
constexpr auto kProductColor    = app::Clusters::BasicInformation::ColorEnum::kWhite;
} // namespace

SoilDeviceInstanceInfoProvider & SoilDeviceInstanceInfoProvider::Instance()
{
    static SoilDeviceInstanceInfoProvider sInstance(ConfigurationManagerImpl::GetDefaultInstance());
    return sInstance;
}

CHIP_ERROR SoilDeviceInstanceInfoProvider::CopyLiteral(const char * literal, char * buf, size_t bufSize)
{
    VerifyOrReturnError(literal != nullptr && buf != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    const size_t required = std::strlen(literal) + 1;
    VerifyOrReturnError(required <= bufSize, CHIP_ERROR_BUFFER_TOO_SMALL);
    Platform::CopyString(buf, bufSize, literal);
    return CHIP_NO_ERROR;
}

CHIP_ERROR SoilDeviceInstanceInfoProvider::GetPartNumber(char * buf, size_t bufSize)
{
    return CopyLiteral(kPartNumber, buf, bufSize);
}

CHIP_ERROR SoilDeviceInstanceInfoProvider::GetProductURL(char * buf, size_t bufSize)
{
    return CopyLiteral(kProductUrl, buf, bufSize);
}

CHIP_ERROR SoilDeviceInstanceInfoProvider::GetProductLabel(char * buf, size_t bufSize)
{
    return CopyLiteral(kProductLabel, buf, bufSize);
}

CHIP_ERROR SoilDeviceInstanceInfoProvider::GetProductFinish(app::Clusters::BasicInformation::ProductFinishEnum * finish)
{
    VerifyOrReturnError(finish != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    *finish = kProductFinish;
    return CHIP_NO_ERROR;
}

CHIP_ERROR SoilDeviceInstanceInfoProvider::GetProductPrimaryColor(app::Clusters::BasicInformation::ColorEnum * primaryColor)
{
    VerifyOrReturnError(primaryColor != nullptr, CHIP_ERROR_INVALID_ARGUMENT);
    *primaryColor = kProductColor;
    return CHIP_NO_ERROR;
}

} // namespace DeviceLayer
} // namespace chip
