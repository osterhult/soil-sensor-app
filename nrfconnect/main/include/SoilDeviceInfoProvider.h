#pragma once

#include <clusters/BasicInformation/Structs.h>
#include <platform/nrfconnect/DeviceInstanceInfoProviderImpl.h>

namespace chip {
namespace DeviceLayer {

class SoilDeviceInstanceInfoProvider : public DeviceInstanceInfoProviderImpl
{
public:
    using DeviceInstanceInfoProviderImpl::DeviceInstanceInfoProviderImpl;

    static SoilDeviceInstanceInfoProvider & Instance();

    CHIP_ERROR GetPartNumber(char * buf, size_t bufSize) override;
    CHIP_ERROR GetProductURL(char * buf, size_t bufSize) override;
    CHIP_ERROR GetProductLabel(char * buf, size_t bufSize) override;
    CHIP_ERROR GetProductFinish(app::Clusters::BasicInformation::ProductFinishEnum * finish) override;
    CHIP_ERROR GetProductPrimaryColor(app::Clusters::BasicInformation::ColorEnum * primaryColor) override;

private:
    static CHIP_ERROR CopyLiteral(const char * literal, char * buf, size_t bufSize);
};

} // namespace DeviceLayer
} // namespace chip
