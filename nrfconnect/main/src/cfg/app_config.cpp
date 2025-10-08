#include "cfg/app_config.h"

#include "matter/SoilDeviceInfoProvider.h"

#include <app/clusters/basic-information/BasicInformationCluster.h>
#include <platform/ConfigurationManager.h>
#include <platform/CHIPDeviceLayer.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <cstring>

LOG_MODULE_DECLARE(soil_app, LOG_LEVEL_INF);

namespace cfg
{
namespace app_config
{

int InitSettings()
{
    return settings_subsys_init();
}

void LoadSettingsIfEnabled()
{
    if (IS_ENABLED(CONFIG_BT_SETTINGS))
    {
        (void) settings_load();
    }
}

void ConfigureBasicInformation()
{
    chip::DeviceLayer::SetDeviceInstanceInfoProvider(&chip::DeviceLayer::SoilDeviceInstanceInfoProvider::Instance());
    chip::app::Clusters::BasicInformationCluster::Instance().OptionalAttributes()
        .Set<chip::app::Clusters::BasicInformation::Attributes::ManufacturingDate::Id>()
        .Set<chip::app::Clusters::BasicInformation::Attributes::PartNumber::Id>()
        .Set<chip::app::Clusters::BasicInformation::Attributes::ProductURL::Id>()
        .Set<chip::app::Clusters::BasicInformation::Attributes::ProductLabel::Id>()
        .Set<chip::app::Clusters::BasicInformation::Attributes::SerialNumber::Id>()
        .Set<chip::app::Clusters::BasicInformation::Attributes::ProductAppearance::Id>();

    constexpr char kDefaultCountryCode[] = CONFIG_CHIP_DEVICE_COUNTRY_CODE;
    static_assert(sizeof(kDefaultCountryCode) > 1, "CONFIG_CHIP_DEVICE_COUNTRY_CODE must not be empty");
    static_assert(sizeof(kDefaultCountryCode) - 1 <= chip::DeviceLayer::ConfigurationManager::kMaxLocationLength,
                  "CONFIG_CHIP_DEVICE_COUNTRY_CODE exceeds the maximum Basic Information country code length");
    char countryCode[chip::DeviceLayer::ConfigurationManager::kMaxLocationLength + 1] = {};
    size_t codeLen                                                               = 0;
    CHIP_ERROR locationErr = chip::DeviceLayer::ConfigurationMgr().GetCountryCode(countryCode, sizeof(countryCode), codeLen);

    if ((locationErr != CHIP_NO_ERROR) || (codeLen != sizeof(kDefaultCountryCode) - 1))
    {
        // Only populate a default when nothing has been provisioned yet.
        if (chip::DeviceLayer::ConfigurationMgr().StoreCountryCode(kDefaultCountryCode, sizeof(kDefaultCountryCode) - 1) !=
            CHIP_NO_ERROR)
        {
            LOG_WRN("Failed to persist default country code");
        }
    }

    uint16_t year;
    uint8_t month;
    uint8_t day;
    constexpr char kManufacturingDate[] = "2024-01-15";
    CHIP_ERROR mfgErr = chip::DeviceLayer::SoilDeviceInstanceInfoProvider::Instance().GetManufacturingDate(year, month, day);
    if ((mfgErr != CHIP_NO_ERROR) || (year != 2024) || (month != 1) || (day != 15))
    {
        (void) chip::DeviceLayer::ConfigurationMgr().StoreManufacturingDate(kManufacturingDate, strlen(kManufacturingDate));
    }
}

} // namespace app_config
} // namespace cfg
