#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

// (No direct Zephyr net/shell usage in current app)

#include <cstddef>
#include <cstdint>
#include <cinttypes>

#include <platform/CHIPDeviceLayer.h>
#include <app/server/AppDelegate.h>
#include <app/server/CommissioningWindowManager.h>
#include <app/server/Dnssd.h>
#include <app/server/Server.h>
#include <app/CASESessionManager.h>
#include <data-model-providers/codegen/Instance.h>
#include <app/clusters/basic-information/BasicInformationCluster.h>
#include "SoilDeviceInfoProvider.h"
#include "AppTask.h"
#include <platform/nrfconnect/DeviceInstanceInfoProviderImpl.h>
#include <platform/CHIPDeviceEvent.h>
#include <platform/internal/BLEManager.h>
#include <platform/DeviceInstanceInfoProvider.h>
#include <DeviceInfoProviderImpl.h>
#include <setup_payload/OnboardingCodesUtil.h>
#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>
#include <credentials/FabricTable.h>
#include <credentials/GroupDataProvider.h>
#include <credentials/GroupDataProviderImpl.h>
#include <lib/core/Optional.h>
#include <lib/core/ErrorStr.h>
#include <lib/core/CHIPError.h>
#include <access/AccessControl.h>
// CHIP support utilities
#include <lib/support/Span.h>
// Network Commissioning (Wi‑Fi) integration for nRF Connect
#include <app/clusters/network-commissioning/CodegenInstance.h>
#include <app/PluginApplicationCallbacks.h>
#include <app-common/zap-generated/cluster-objects.h>
#include <app/AttributeAccessInterface.h>
#include <app/AttributeAccessInterfaceRegistry.h>
#include <app/AttributePathParams.h>
#include <app/AttributeValueEncoder.h>
#include <app/EventManagement.h>
#include <app/InteractionModelEngine.h>
#include <app/ReadHandler.h>
#include <app/reporting/Engine.h>
#include <app/util/attribute-storage.h>
#include <platform/nrfconnect/wifi/NrfWiFiDriver.h>
#include <protocols/secure_channel/SessionResumptionStorage.h>
#include <transport/Session.h>
#include <transport/SessionManager.h>
#include <transport/SecureSession.h>
#include <zephyr/settings/settings.h>
#include <string.h>

#include <vector>

#include <lib/support/CodeUtils.h>
#include <lib/support/LinkedList.h>
#include <lib/support/logging/CHIPLogging.h>

#include <lib/dnssd/Advertiser.h>

#include <messaging/ReliableMessageProtocolConfig.h>

#include <platform/ConfigurationManager.h>
#include <platform/KeyValueStoreManager.h>

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

using chip::FabricIndex;
using chip::Server;



// Example DeviceInfo provider instance (used to print onboarding info)
static chip::DeviceLayer::DeviceInfoProviderImpl gExampleDeviceInfoProvider;

namespace
{

// Guard to prevent duplicate registration / init across reboots
static bool sOneTimeMatterInitDone = false;
static bool sFactoryResetScheduled  = false;

chip::Credentials::GroupDataProviderImpl gGroupDataProvider;

static size_t MatterMaxFabrics();
static bool EnsureFabricSlot();
static void FactoryResetEventHandler(const chip::DeviceLayer::ChipDeviceEvent * event, intptr_t arg);
static void DoFactoryResetLikeNordic();
static void FullCommissioningCleanup();

class AccessControlLimitsAttrAccess : public chip::app::AttributeAccessInterface
{
public:
    AccessControlLimitsAttrAccess() :
        chip::app::AttributeAccessInterface(chip::Optional<chip::EndpointId>::Missing(),
                                            chip::app::Clusters::AccessControl::Id)
    {}

    CHIP_ERROR Read(const chip::app::ConcreteReadAttributePath & path,
                    chip::app::AttributeValueEncoder & encoder) override
    {
        using namespace chip::app::Clusters::AccessControl::Attributes;

        auto & ac = chip::Access::GetAccessControl();

        size_t entriesPerFabric = CONFIG_CHIP_ACL_MAX_ENTRIES_PER_FABRIC;
        size_t subjectsPerEntry = CONFIG_CHIP_ACL_MAX_SUBJECTS_PER_ENTRY;
        size_t targetsPerEntry  = CONFIG_CHIP_ACL_MAX_TARGETS_PER_ENTRY;

        (void) ac.GetMaxEntriesPerFabric(entriesPerFabric);
        (void) ac.GetMaxSubjectsPerEntry(subjectsPerEntry);
        (void) ac.GetMaxTargetsPerEntry(targetsPerEntry);

        if (path.mAttributeId == AccessControlEntriesPerFabric::Id)
        {
            return encoder.Encode(static_cast<uint16_t>(entriesPerFabric));
        }

        if (path.mAttributeId == SubjectsPerAccessControlEntry::Id)
        {
            return encoder.Encode(static_cast<uint16_t>(subjectsPerEntry));
        }

        if (path.mAttributeId == TargetsPerAccessControlEntry::Id)
        {
            return encoder.Encode(static_cast<uint16_t>(targetsPerEntry));
        }

        return CHIP_NO_ERROR;
    }
};

AccessControlLimitsAttrAccess gAclLimitsAttrAccess;

static bool gPluginsInitialized = false;

static void InitEventLogging()
{
    auto & eventManagement = chip::app::EventManagement::GetInstance();
    (void) eventManagement;
}

static CHIP_ERROR InitManagementClusters()
{
    auto & server = chip::Server::GetInstance();

    gGroupDataProvider.SetStorageDelegate(&server.GetPersistentStorage());
    gGroupDataProvider.SetSessionKeystore(server.GetSessionKeystore());

    CHIP_ERROR err = gGroupDataProvider.Init();
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "GroupDataProvider init failed: %s", chip::ErrorStr(err));
        return err;
    }

    chip::Credentials::SetGroupDataProvider(&gGroupDataProvider);
    return CHIP_NO_ERROR;
}

static void InitializeGeneratedPluginsOnce()
{
    if (gPluginsInitialized)
    {
        return;
    }

    MATTER_PLUGINS_INIT;
    gPluginsInitialized = true;
}

static bool IsBenignNotFound(CHIP_ERROR err)
{
    return (err == CHIP_NO_ERROR) || (err == CHIP_ERROR_NOT_FOUND) || (err == CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND) ||
        (err == CHIP_ERROR_KEY_NOT_FOUND);
}

static size_t MatterMaxFabrics()
{
#if defined(CHIP_DEVICE_CONFIG_MAX_FABRICS)
    return CHIP_DEVICE_CONFIG_MAX_FABRICS;
#elif defined(CONFIG_CHIP_MAX_FABRICS)
    return CONFIG_CHIP_MAX_FABRICS;
#elif defined(CHIP_CONFIG_MAX_FABRICS)
    return CHIP_CONFIG_MAX_FABRICS;
#else
    return 5;
#endif
}

struct FabricSessionCheckContext
{
    FabricIndex fabric;
    bool hasActive;
};

static bool SessionIteratorForFabric(void * context, chip::SessionHandle & handle)
{
    auto * data = static_cast<FabricSessionCheckContext *>(context);
    if (handle->IsActiveSession() && handle->GetFabricIndex() == data->fabric)
    {
        data->hasActive = true;
        return false;
    }
    return true;
}

static bool FabricHasActiveSessions(FabricIndex fabricIndex)
{
    FabricSessionCheckContext ctx{ fabricIndex, false };
    (void) chip::Server::GetInstance().GetSecureSessionManager().ForEachSessionHandle(&ctx, SessionIteratorForFabric);
    return ctx.hasActive;
}

static bool FabricHasActiveSubscription(FabricIndex fabricIndex)
{
    auto * imEngine = chip::app::InteractionModelEngine::GetInstance();
    return (imEngine != nullptr) && imEngine->FabricHasAtLeastOneActiveSubscription(fabricIndex);
}

static bool TryEvictIdleFabric()
{
    auto & server      = chip::Server::GetInstance();
    auto & fabricTable = server.GetFabricTable();

    for (auto it = fabricTable.begin(); it != fabricTable.end(); ++it)
    {
        const chip::FabricInfo & info = *it;
        if (!info.IsInitialized())
        {
            continue;
        }

        FabricIndex candidate = info.GetFabricIndex();
        if (FabricHasActiveSessions(candidate) || FabricHasActiveSubscription(candidate))
        {
            continue;
        }

        server.GetSecureSessionManager().ExpireAllSessionsForFabric(candidate);
        if (auto * resumption = server.GetSessionResumptionStorage(); resumption != nullptr)
        {
            (void) resumption->DeleteAll(candidate);
        }
        if (auto * groups = server.GetGroupDataProvider(); groups != nullptr)
        {
            (void) groups->RemoveFabric(candidate);
        }

        CHIP_ERROR err = fabricTable.Delete(candidate);
        if (err != CHIP_NO_ERROR && err != CHIP_ERROR_NOT_FOUND)
        {
            ChipLogError(AppServer, "Failed to evict fabric 0x%02x: %s", candidate, chip::ErrorStr(err));
            return false;
        }

        ChipLogProgress(AppServer, "Evicted idle fabric 0x%02x to free a slot", candidate);
        return true;
    }

    return false;
}

static bool EnsureFabricSlot()
{
    auto & server      = chip::Server::GetInstance();
    auto & fabricTable = server.GetFabricTable();

    if (fabricTable.FabricCount() < MatterMaxFabrics())
    {
        return true;
    }

    bool evicted = TryEvictIdleFabric();
    if (!evicted)
    {
        ChipLogProgress(AppServer, "No fabric slots available and no idle fabric to evict");
    }
    return evicted;
}

class CommissioningCapacityDelegate final : public ::AppDelegate
{
public:
    void OnCommissioningWindowOpened() override { (void) EnsureFabricSlot(); }
    void OnCommissioningSessionEstablishmentStarted() override { (void) EnsureFabricSlot(); }
};

CommissioningCapacityDelegate gCommissioningCapacityDelegate;

static void FullCommissioningCleanup()
{
    using namespace chip;
    using namespace chip::DeviceLayer;

    Server & server = Server::GetInstance();

    server.GetFailSafeContext().ForceFailSafeTimerExpiry();
    server.GetCommissioningWindowManager().CloseCommissioningWindow();

    if (auto * caseManager = server.GetCASESessionManager(); caseManager != nullptr)
    {
        caseManager->ReleaseAllSessions();
    }

    server.GetSecureSessionManager().ExpireAllSecureSessions();

    std::vector<FabricIndex> fabricsToDelete;
    fabricsToDelete.reserve(MatterMaxFabrics());

    auto & fabricTable = server.GetFabricTable();
    for (auto it = fabricTable.begin(); it != fabricTable.end(); ++it)
    {
        const FabricInfo & info = *it;
        if (info.IsInitialized())
        {
            fabricsToDelete.push_back(info.GetFabricIndex());
        }
    }

    auto * resumption    = server.GetSessionResumptionStorage();
    auto * groupProvider = server.GetGroupDataProvider();

    for (FabricIndex fabricIndex : fabricsToDelete)
    {
        server.GetSecureSessionManager().ExpireAllSessionsForFabric(fabricIndex);

        if (auto * caseManager = server.GetCASESessionManager(); caseManager != nullptr)
        {
            caseManager->ReleaseSessionsForFabric(fabricIndex);
        }

        if (resumption != nullptr)
        {
            CHIP_ERROR err = resumption->DeleteAll(fabricIndex);
            if (!IsBenignNotFound(err) && err != CHIP_ERROR_NOT_IMPLEMENTED)
            {
                ChipLogProgress(AppServer, "Failed to clear resumption cache for fabric 0x%02x: %s", fabricIndex,
                                chip::ErrorStr(err));
            }
        }

        if (groupProvider != nullptr)
        {
            CHIP_ERROR err = groupProvider->RemoveFabric(fabricIndex);
            if (!IsBenignNotFound(err) && err != CHIP_ERROR_NOT_IMPLEMENTED)
            {
                ChipLogProgress(AppServer, "Failed to remove group data for fabric 0x%02x: %s", fabricIndex,
                                chip::ErrorStr(err));
            }
        }

        CHIP_ERROR fabricErr = fabricTable.Delete(fabricIndex);
        if (fabricErr != CHIP_NO_ERROR && fabricErr != CHIP_ERROR_NOT_FOUND)
        {
            ChipLogProgress(AppServer, "Fabric delete failed for index 0x%02x: %s", fabricIndex, chip::ErrorStr(fabricErr));
        }

        (void) Access::GetAccessControl().DeleteAllEntriesForFabric(fabricIndex);
    }

    Access::ResetAccessControlToDefault();

    ChipLogProgress(AppServer, "KeyValueStore clear delegated to platform factory reset");

    ChipLogProgress(AppServer, "FullCommissioningCleanup done.");
}

static void DoFactoryResetLikeNordic()
{
    if (sFactoryResetScheduled)
    {
        return;
    }

    sFactoryResetScheduled = true;
    FullCommissioningCleanup();
    chip::DeviceLayer::ConfigurationMgr().InitiateFactoryReset();
}

static void FactoryResetEventHandler(const chip::DeviceLayer::ChipDeviceEvent * event, intptr_t)
{
    if ((event == nullptr) || (event->Type != chip::DeviceLayer::DeviceEventType::kFactoryReset))
    {
        return;
    }

    DoFactoryResetLikeNordic();
}

} // namespace


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
        constexpr char kDefaultCountryCode[] = CONFIG_CHIP_DEVICE_COUNTRY_CODE;
        static_assert(sizeof(kDefaultCountryCode) > 1, "CONFIG_CHIP_DEVICE_COUNTRY_CODE must not be empty");
        static_assert(sizeof(kDefaultCountryCode) - 1 <= DeviceLayer::ConfigurationManager::kMaxLocationLength,
                      "CONFIG_CHIP_DEVICE_COUNTRY_CODE exceeds the maximum Basic Information country code length");
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

    // Register handlers for factory reset prep and BLE-related platform events
    PlatformMgr().AddEventHandler(FactoryResetEventHandler, 0);
    PlatformMgr().AddEventHandler(AppEventHandler, 0);

    CHIP_ERROR appTaskErr = AppTask::Instance().StartApp();
    if (appTaskErr != CHIP_NO_ERROR)
    {
        LOG_ERR("AppTask start failed: %s (%" CHIP_ERROR_FORMAT ")", chip::ErrorStr(appTaskErr), appTaskErr.Format());
        return 0;
    }

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
    initParams.appDelegate       = &gCommissioningCapacityDelegate;

    chip::Server & server = chip::Server::GetInstance();

    if (!sOneTimeMatterInitDone)
    {
        if (!chip::app::AttributeAccessInterfaceRegistry::Instance().Register(&gAclLimitsAttrAccess))
        {
            LOG_WRN("ACL limits attribute was already registered");
        }
        sOneTimeMatterInitDone = true;
    }

    err = server.Init(initParams);

    if (err != CHIP_NO_ERROR) { LOG_ERR("Matter Server init failed: %ld", (long)err.AsInteger()); return -2; }

    InitEventLogging();

#if CHIP_DEVICE_CONFIG_ENABLE_DYNAMIC_MRP_CONFIG
    {
        using namespace chip::System::Clock::Literals;
        chip::Messaging::ReliableMessageProtocolConfig mrpConfig(2000_ms32, 300_ms32);
        auto mrpOverride = chip::MakeOptional(mrpConfig);
        (void) chip::Messaging::ReliableMessageProtocolConfig::SetLocalMRPConfig(mrpOverride);
    }
#endif

    CHIP_ERROR managementErr = InitManagementClusters();
    if (managementErr != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "Management cluster init failed: %s", chip::ErrorStr(managementErr));
    }

    InitializeGeneratedPluginsOnce();

    if (server.GetFabricTable().FabricCount() == 0)
    {
        if (!EnsureFabricSlot())
        {
            LOG_WRN("Unable to free fabric slot before commissioning window");
        }
        else
        {
            constexpr auto kDefaultCommissioningTimeout = chip::System::Clock::Seconds32(15 * 60);
            CHIP_ERROR windowErr =
                server.GetCommissioningWindowManager().OpenBasicCommissioningWindow(kDefaultCommissioningTimeout);
            if (windowErr != CHIP_NO_ERROR)
            {
                LOG_WRN("OpenBasicCommissioningWindow failed: %s", chip::ErrorStr(windowErr));
            }
        }
    }

    (void) chip::System::SystemClock().SetClock_RealTime(chip::System::SystemClock().GetMonotonicMicroseconds64());


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
