#include "matter/server_runtime.h"

#include <app/EventManagement.h>
#include <messaging/ReliableMessageProtocolConfig.h>

#if CHIP_DEVICE_CONFIG_ENABLE_DYNAMIC_MRP_CONFIG
#include <lib/core/Optional.h>
#include <system/SystemClock.h>
#endif

#if defined(CONFIG_CHIP_WIFI)
#include <app/clusters/network-commissioning/CodegenInstance.h>
#include <platform/nrfconnect/wifi/NrfWiFiDriver.h>
#endif

namespace matter
{
namespace server_runtime
{

void InitEventLogging()
{
    auto & eventManagement = chip::app::EventManagement::GetInstance();
    (void) eventManagement;
}

void ConfigureDynamicMrp()
{
#if CHIP_DEVICE_CONFIG_ENABLE_DYNAMIC_MRP_CONFIG
    using namespace chip::System::Clock::Literals;
    chip::Messaging::ReliableMessageProtocolConfig mrpConfig(2000_ms32, 300_ms32);
    auto mrpOverride = chip::MakeOptional(mrpConfig);
    (void) chip::Messaging::ReliableMessageProtocolConfig::SetLocalMRPConfig(mrpOverride);
#endif
}

void InitWifiCommissioningCluster()
{
#if defined(CONFIG_CHIP_WIFI)
    static chip::app::Clusters::NetworkCommissioning::Instance sWiFiCommissioningInstance(
        0, &(chip::DeviceLayer::NetworkCommissioning::NrfWiFiDriver::Instance()));
    (void) sWiFiCommissioningInstance.Init();
#endif
}

} // namespace server_runtime
} // namespace matter
