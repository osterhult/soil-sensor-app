#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <cstddef>
#include <cstdint>
#include <cinttypes>

#include <platform/CHIPDeviceLayer.h>
#include <app/server/AppDelegate.h>
#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <data-model-providers/codegen/Instance.h>
#include "matter/SoilDeviceInfoProvider.h"
#include "app/AppTask.h"
#include "app/factory_reset.h"
#include "cfg/app_config.h"
#include "connectivity/ble_manager.h"
#include "matter/access_manager.h"
#include "matter/ep0_im_sanitizer.h"
#include "matter/ep0_metadata_filter.h"
#include "matter/server_runtime.h"
#include "sensors/soil_moisture_sensor.h"
#include <platform/nrfconnect/DeviceInstanceInfoProviderImpl.h>
#include <platform/CHIPDeviceEvent.h>
#include <platform/internal/BLEManager.h>
#include <platform/DeviceInstanceInfoProvider.h>
#include <DeviceInfoProviderImpl.h>
#include <setup_payload/OnboardingCodesUtil.h>
#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>
#include <credentials/FabricTable.h>
#include <lib/core/ErrorStr.h>
#include <lib/core/CHIPError.h>
#include <lib/support/logging/CHIPLogging.h>
#include <zephyr/sys/util.h>

#include <platform/ConfigurationManager.h>

#ifdef CONFIG_PM
#include <zephyr/pm/pm.h>
#include <zephyr/pm/state.h>
#endif

#ifndef CONFIG_CHIP_ACL_MAX_ENTRIES_PER_FABRIC
#define CONFIG_CHIP_ACL_MAX_ENTRIES_PER_FABRIC 4
#endif

#ifndef CONFIG_CHIP_ACL_MAX_SUBJECTS_PER_ENTRY
#define CONFIG_CHIP_ACL_MAX_SUBJECTS_PER_ENTRY 3
#endif

#ifndef CONFIG_CHIP_ACL_MAX_TARGETS_PER_ENTRY
#define CONFIG_CHIP_ACL_MAX_TARGETS_PER_ENTRY 3
#endif

LOG_MODULE_REGISTER(soil_app, LOG_LEVEL_INF);
using namespace chip;
using namespace chip::DeviceLayer;

using chip::FabricIndex;
using chip::Server;

namespace matter {
namespace cluster_overrides {
void RegisterIdentifyRevisionOverride(chip::EndpointId endpoint);
} // namespace cluster_overrides
} // namespace matter



// Example DeviceInfo provider instance (used to print onboarding info)
static chip::DeviceLayer::DeviceInfoProviderImpl gExampleDeviceInfoProvider;

extern "C" int main(void)
{
    printk("boot\n");
    LOG_INF("Soil sensor app started.");

    // Do not load settings yet; BLE Manager enables BT and will register
    // its settings handlers. We load settings right after CHIP stack init.

    // Start Matter immediately (no pre-wifi wait!)
    int settingsStatus = cfg::app_config::InitSettings();
    if (settingsStatus) {
        LOG_ERR("settings_subsys_init failed: %d", settingsStatus);
        return 0;
    }

    CHIP_ERROR err = PlatformMgr().InitChipStack();
    if (err != CHIP_NO_ERROR) {
        LOG_ERR("InitChipStack failed: %s (%" CHIP_ERROR_FORMAT ")", chip::ErrorStr(err), err.Format());
        return 0;
    }

    // Provide development Device Attestation Credentials (DAC) for commissioning
    Credentials::SetDeviceAttestationCredentialsProvider(Credentials::Examples::GetExampleDACProvider());

    cfg::app_config::ConfigureBasicInformation();
    DeviceLayer::SetDeviceInfoProvider(&gExampleDeviceInfoProvider);

    // Register handlers for factory reset prep and BLE-related platform events
    PlatformMgr().AddEventHandler(::app::factory_reset::FactoryResetEventHandler, 0);
    PlatformMgr().AddEventHandler(connectivity::ble_manager::AppEventHandler, 0);

    CHIP_ERROR appTaskErr = AppTask::Instance().StartApp();
    if (appTaskErr != CHIP_NO_ERROR)
    {
        LOG_ERR("AppTask start failed: %s (%" CHIP_ERROR_FORMAT ")", chip::ErrorStr(appTaskErr), appTaskErr.Format());
        return 0;
    }

    // Load Zephyr settings now that CHIP stack (and BT) are initialized.
    cfg::app_config::LoadSettingsIfEnabled();

// (Wi‑Fi commissioning registration moved after Server init)

    // Use standard CHIP BLE advertising (service data in ADV, name in scan response).
    // Only set a distinctive device name for easier discovery.
    connectivity::ble_manager::ConfigureDeviceName();

    // Defer enabling BLE advertising until after Server init and event loop start
    // to avoid races where a central connects before rendezvous is fully ready.

    chip::CommonCaseDeviceServerInitParams initParams;
    err = initParams.InitializeStaticResourcesBeforeServerInit();
    if (err != CHIP_NO_ERROR) {
        LOG_ERR("Init static server resources failed: %ld", (long)err.AsInteger());
        return -3;
    }
    // Provide a data model provider for the server (required by recent CHIP)
    chip::app::DataModel::Provider * baseProvider =
        chip::app::CodegenDataModelProviderInstance(initParams.persistentStorageDelegate);
    static matter::ep0::MetadataFilter sMetadataFilter(*baseProvider);
    initParams.dataModelProvider = &sMetadataFilter;
    initParams.appDelegate       = &matter::access_manager::CommissioningCapacityDelegate();

    chip::Server & server = chip::Server::GetInstance();

    err = server.Init(initParams);

    if (err != CHIP_NO_ERROR) { LOG_ERR("Matter Server init failed: %ld", (long)err.AsInteger()); return -2; }

    matter::ep0::Register();
    matter::cluster_overrides::RegisterIdentifyRevisionOverride(/*endpoint=*/1);

    matter::server_runtime::InitEventLogging();
    matter::server_runtime::ConfigureDynamicMrp();

    CHIP_ERROR managementErr = matter::access_manager::InitManagementClusters();
    if (managementErr != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "Management cluster init failed: %s", chip::ErrorStr(managementErr));
    }

    matter::access_manager::EnsureAccessControlReady();
    matter::access_manager::AssertRootAccessControlReady();
    matter::access_manager::InitializeFabricHandlers(server);

    matter::access_manager::OpenCommissioningWindowIfNeeded(server);

    (void) chip::System::SystemClock().SetClock_RealTime(chip::System::SystemClock().GetMonotonicMicroseconds64());


    matter::server_runtime::InitWifiCommissioningCluster();

    // Do not set RealTime from Last Known Good Time here; CASE session handling
    // will fall back appropriately and update LKG as needed during commissioning.

    if (IS_ENABLED(CONFIG_SOIL_ENDPOINT))
    {
        sensors::soil_moisture_sensor::Init();
    }

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

    connectivity::ble_manager::EnableAdvertising();

    while (true) { k_sleep(K_SECONDS(5)); }
}
