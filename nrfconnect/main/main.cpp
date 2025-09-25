#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

// (No direct Zephyr net/shell usage in current app)

#include <platform/CHIPDeviceLayer.h>
#include <app/server/Server.h>
#include <data-model-providers/codegen/Instance.h>
#include <app/clusters/basic-information/BasicInformationCluster.h>
#include "SoilDeviceInfoProvider.h"
#include <platform/nrfconnect/DeviceInstanceInfoProviderImpl.h>
#include <platform/CHIPDeviceEvent.h>
#include <platform/internal/BLEManager.h>
#include <platform/DeviceInstanceInfoProvider.h>
#include <DeviceInfoProviderImpl.h>
#include <setup_payload/OnboardingCodesUtil.h>
#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>
#include <lib/core/ErrorStr.h>
#include <access/AccessControl.h>
// CHIP support utilities
#include <lib/support/Span.h>
// Network Commissioning (Wi‑Fi) integration for nRF Connect
#include <app/clusters/network-commissioning/CodegenInstance.h>
#include <platform/nrfconnect/wifi/NrfWiFiDriver.h>
#include <zephyr/settings/settings.h>
#include <string.h>

// Soil Measurement cluster (server) integration
#include <app/server-cluster/ServerClusterInterfaceRegistry.h>
#include <app/clusters/soil-measurement-server/soil-measurement-cluster.h>
#include <clusters/BasicInformation/Attributes.h>
#include <clusters/SoilMeasurement/Attributes.h>
#include <clusters/shared/Structs.h>
// System clock (to set Real Time from Last Known Good Time)
#include <system/SystemClock.h>
// Random utils for generating demo measurements
#include <crypto/RandUtils.h>

LOG_MODULE_REGISTER(soil_app, LOG_LEVEL_INF);
using namespace chip;
using namespace chip::DeviceLayer;
using namespace chip::app;
using namespace chip::app::Clusters;



// Minimal resolver required by your AccessControl::Init(delegate, resolver) API.
class NoopDeviceTypeResolver : public chip::Access::AccessControl::DeviceTypeResolver {
public:
    bool IsDeviceTypeOnEndpoint(chip::DeviceTypeId /*deviceType*/,
                                chip::EndpointId /*endpoint*/) override
    {
        return false;
    }
};

static NoopDeviceTypeResolver gNoopResolver;



// Example DeviceInfo provider instance (used to print onboarding info)
static chip::DeviceLayer::DeviceInfoProviderImpl gExampleDeviceInfoProvider;

#if defined(CONFIG_CHIP_WIFI)
// Register Network Commissioning cluster (Wi‑Fi) on endpoint 0 using nRF Wi‑Fi driver
static chip::app::Clusters::NetworkCommissioning::Instance sWiFiCommissioningInstance(
    0, &(chip::DeviceLayer::NetworkCommissioning::NrfWiFiDriver::Instance()));
#endif

// Soil Measurement cluster instance on endpoint 1
static chip::app::LazyRegisteredServerCluster<SoilMeasurementCluster> sSoilCluster;
static constexpr EndpointId kSoilEndpoint = 1;
static constexpr uint32_t kSoilUpdateMs   = 5000;
static uint8_t gSoilLast = 101; // invalid sentinel so first update always changes

static void SoilUpdateTimer(System::Layer * layer, void *)
{
    uint8_t v = static_cast<uint8_t>(chip::Crypto::GetRandU16() % 101);
    if (v == gSoilLast)
    {
        v = static_cast<uint8_t>((v + 1) % 101);
    }
    DataModel::Nullable<Percent> measured;
    measured.SetNonNull(v);
    (void) sSoilCluster.Cluster().SetSoilMoistureMeasuredValue(measured);
    gSoilLast = v;
    if (layer)
    {
        (void) layer->StartTimer(System::Clock::Milliseconds32(kSoilUpdateMs), SoilUpdateTimer, nullptr);
    }
}
// (Removed legacy net_mgmt callbacks; not used)

static void AppEventHandler(const chip::DeviceLayer::ChipDeviceEvent * event, intptr_t /* arg */)
{
    using namespace chip::DeviceLayer;
    switch (event->Type)
    {
    case DeviceEventType::kCHIPoBLEAdvertisingChange:
        LOG_INF("BLE adv change: result=%d enabled=%d adv=%d conns=%u",
                (int) event->CHIPoBLEAdvertisingChange.Result, (int) DeviceLayer::Internal::BLEMgr().IsAdvertisingEnabled(),
                (int) DeviceLayer::Internal::BLEMgr().IsAdvertising(), DeviceLayer::Internal::BLEMgr().NumConnections());
        break;
    case DeviceEventType::kCHIPoBLEConnectionEstablished:
        LOG_INF("BLE connection established");
        break;
    case DeviceEventType::kCHIPoBLEConnectionClosed:
        LOG_INF("BLE connection closed");
        break;
    default:
        break;
    }
}

extern "C" int main(void)
{
    printk("boot\n");
    LOG_INF("Soil sensor app started.");

    // Do not load settings yet; BLE Manager enables BT and will register
    // its settings handlers. We load settings right after CHIP stack init.

    // Start Matter immediately (no pre-wifi wait!)
    int settingsStatus = settings_subsys_init();
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

    // Register providers that some clusters expect
    DeviceLayer::SetDeviceInstanceInfoProvider(&DeviceLayer::SoilDeviceInstanceInfoProvider::Instance());
    BasicInformationCluster::Instance().OptionalAttributes()
        .Set<BasicInformation::Attributes::ManufacturingDate::Id>()
        .Set<BasicInformation::Attributes::PartNumber::Id>()
        .Set<BasicInformation::Attributes::ProductURL::Id>()
        .Set<BasicInformation::Attributes::ProductLabel::Id>()
        .Set<BasicInformation::Attributes::SerialNumber::Id>()
        .Set<BasicInformation::Attributes::ProductAppearance::Id>();
    {
        constexpr char kDefaultCountryCode[] = "SE";
        char countryCode[DeviceLayer::ConfigurationManager::kMaxLocationLength + 1] = {};
        size_t codeLen                                                               = 0;
        CHIP_ERROR locationErr = ConfigurationMgr().GetCountryCode(countryCode, sizeof(countryCode), codeLen);

        if ((locationErr != CHIP_NO_ERROR) || (codeLen != sizeof(kDefaultCountryCode) - 1))
        {
            // Only populate a default when nothing has been provisioned yet.
            if (ConfigurationMgr().StoreCountryCode(kDefaultCountryCode, sizeof(kDefaultCountryCode) - 1) != CHIP_NO_ERROR)
            {
                LOG_WRN("Failed to persist default country code");
            }
        }

        uint16_t year;
        uint8_t month;
        uint8_t day;
        constexpr char kManufacturingDate[] = "2024-01-15";
        CHIP_ERROR mfgErr = SoilDeviceInstanceInfoProvider::Instance().GetManufacturingDate(year, month, day);
        if ((mfgErr != CHIP_NO_ERROR) || (year != 2024) || (month != 1) || (day != 15))
        {
            (void) ConfigurationMgr().StoreManufacturingDate(kManufacturingDate, strlen(kManufacturingDate));
        }
    }
    DeviceLayer::SetDeviceInfoProvider(&gExampleDeviceInfoProvider);

    // Register a handler for BLE-related and other platform events
    PlatformMgr().AddEventHandler(AppEventHandler, 0);

    // Load Zephyr settings now that CHIP stack (and BT) are initialized.
    if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
        (void) settings_load();
    }

// (Wi‑Fi commissioning registration moved after Server init)

    // Use standard CHIP BLE advertising (service data in ADV, name in scan response).
    // Only set a distinctive device name for easier discovery.
    {
        Ble::ChipBLEDeviceIdentificationInfo idInfo;
        ConfigurationMgr().GetBLEDeviceIdentificationInfo(idInfo);
        uint16_t disc = idInfo.GetDeviceDiscriminator();
        char advName[20];
        snprintk(advName, sizeof(advName), "SoilSensor-%03X", (unsigned) disc);
        (void) DeviceLayer::Internal::BLEMgr().SetDeviceName(advName);
    }

    // Defer enabling BLE advertising until after Server init and event loop start
    // to avoid races where a central connects before rendezvous is fully ready.

    chip::CommonCaseDeviceServerInitParams initParams;
    err = initParams.InitializeStaticResourcesBeforeServerInit();
    if (err != CHIP_NO_ERROR) {
        LOG_ERR("Init static server resources failed: %ld", (long)err.AsInteger());
        return -3;
    }
    // Provide a data model provider for the server (required by recent CHIP)
    initParams.dataModelProvider = chip::app::CodegenDataModelProviderInstance(initParams.persistentStorageDelegate);

    err = chip::Server::GetInstance().Init(initParams);

    if (err != CHIP_NO_ERROR) { LOG_ERR("Matter Server init failed: %ld", (long)err.AsInteger()); return -2; }


   // --- ACL init & commissioning window hygiene ---
    {
        auto & server = chip::Server::GetInstance();

        // AccessControl init (your working version)
        auto & ac = chip::Access::GetAccessControl();
        CHIP_ERROR acErr = ac.Init(/*delegate*/ nullptr, gNoopResolver);
        if (acErr != CHIP_NO_ERROR && acErr != CHIP_ERROR_INCORRECT_STATE) {
            LOG_ERR("AccessControl init failed: %ld", (long) acErr.AsInteger());
        }

        // Only close an *existing* window on already-commissioned devices.
        // For first-boot (no fabrics), keep the default window open for PASE.
        if (server.GetFabricTable().FabricCount() > 0) {
            server.GetCommissioningWindowManager().CloseCommissioningWindow();
        }
    }   
    // --- end ACL init ---

#if defined(CONFIG_CHIP_WIFI)
    // Ensure Wi‑Fi commissioning cluster is registered
    (void) sWiFiCommissioningInstance.Init();
#endif

    // Do not set RealTime from Last Known Good Time here; CASE session handling
    // will fall back appropriately and update LKG as needed during commissioning.

    // Register Soil Measurement cluster (endpoint 1) with limits
    {
        using LimitsType = SoilMeasurement::Attributes::SoilMoistureMeasurementLimits::TypeInfo::Type;
        using RangeType  = chip::app::Clusters::Globals::Structs::MeasurementAccuracyRangeStruct::Type;

        static const RangeType kRanges[] = {
            {
                .rangeMin   = 0,
                .rangeMax   = 100,
                .percentMax = MakeOptional(static_cast<Percent100ths>(5)) // 5%
            },
        };

        const LimitsType limits = {
            .measurementType  = chip::app::Clusters::Globals::MeasurementTypeEnum::kSoilMoisture,
            .measured         = true,
            .minMeasuredValue = 0,
            .maxMeasuredValue = 100,
            .accuracyRanges   = DataModel::List<const RangeType>(kRanges),
        };

        sSoilCluster.Create(kSoilEndpoint, limits);
        (void) app::CodegenDataModelProvider::Instance().Registry().Register(sSoilCluster.Registration());

        // Set an initial random measured value and start periodic updates
        SoilUpdateTimer(nullptr, nullptr);
        (void) SystemLayer().StartTimer(System::Clock::Milliseconds32(kSoilUpdateMs), SoilUpdateTimer, nullptr);
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

    ConnectivityMgr().SetBLEAdvertisingEnabled(true);
    LOG_INF("Matter server started; BLE advertising enabled.");

    while (true) { k_sleep(K_SECONDS(5)); }
}
