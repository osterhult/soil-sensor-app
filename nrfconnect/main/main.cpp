#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

// (No direct Zephyr net/shell usage in current app)

#include <cstddef>
#include <cstdint>
#include <cinttypes>

#include <platform/CHIPDeviceLayer.h>
#include <app/server/AppDelegate.h>
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
#include <credentials/FabricTable.h>
#include <credentials/GroupDataProviderImpl.h>
#include <lib/core/Optional.h>
#include <lib/core/ErrorStr.h>
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
#include <transport/Session.h>
#include <transport/SessionManager.h>
#include <transport/SecureSession.h>
#include <zephyr/settings/settings.h>
#include <string.h>

#include <lib/support/CodeUtils.h>
#include <lib/support/LinkedList.h>
#include <lib/support/logging/CHIPLogging.h>

#include <messaging/ReliableMessageProtocolConfig.h>

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

constexpr chip::NodeId kCaseAdminSubjectId = 0x0000000000000002ULL;

constexpr chip::System::Clock::Milliseconds32 kEp0AclKeepAliveInterval = chip::System::Clock::Milliseconds32(1500);
constexpr chip::System::Clock::Milliseconds32 kFabricEvictionDelay     = chip::System::Clock::Milliseconds32(60000);
constexpr chip::System::Clock::Milliseconds32 kFabricEvictionRetryDelay = chip::System::Clock::Milliseconds32(30000);

bool gEp0AclSubscriptionActive      = false;
uint8_t gEp0AclSubscriptionRefCount = 0;
bool gEp0AclKeepAliveArmed          = false;

#ifdef CONFIG_PM
bool gEp0AclPmLockActive = false;
#endif

chip::Optional<chip::FabricIndex> gPendingFabricEviction;

chip::Credentials::GroupDataProviderImpl gGroupDataProvider;

using chip::Optional;

static void PreventDeepSleepForEp0Subscription();
static void AllowDeepSleepForEp0Subscription();
static void StartEp0AclKeepAliveTimer();
static void StopEp0AclKeepAliveTimer();
static void Ep0AclKeepAliveTimer(chip::System::Layer * layer, void * context);
static void OnEp0AclSubscriptionAdded();
static void OnEp0AclSubscriptionRemoved();
static bool IsEp0AclSubscription(chip::app::ReadHandler & handler);
static void EnsureCaseAdminEntryWork(intptr_t context);
static void ScheduleEnsureCaseAdminEntry(chip::FabricIndex fabricIndex);
static void ScheduleFabricEviction(chip::FabricIndex victim,
                                   chip::System::Clock::Milliseconds32 delay = kFabricEvictionDelay);
static void FabricEvictionTimerHandler(chip::System::Layer * layer, void * context);
static void FabricEvictionWork(intptr_t context);
static void InitEventLogging();
static void EnsureAccessControlReady();
static void AssertRootAccessControlReady();
static void NotifyAclChanged();

class Ep0AclSubscriptionCallback final : public chip::app::ReadHandler::ApplicationCallback
{
public:
    CHIP_ERROR OnSubscriptionRequested(chip::app::ReadHandler & handler,
                                       chip::Transport::SecureSession & session) override
    {
        (void) session;
        if (IsEp0AclSubscription(handler))
        {
            constexpr uint16_t kEp0AclMaxIntervalSeconds = 30;
            CHIP_ERROR intervalErr                        = handler.SetMaxReportingInterval(kEp0AclMaxIntervalSeconds);
            if (intervalErr != CHIP_NO_ERROR)
            {
                ChipLogProgress(AppServer, "ACL max interval err: %s", chip::ErrorStr(intervalErr));
            }

            intervalErr = handler.SetMinReportingIntervalForTests(0);
            if (intervalErr != CHIP_NO_ERROR)
            {
                ChipLogProgress(AppServer, "ACL min interval err: %s", chip::ErrorStr(intervalErr));
            }
        }
        return CHIP_NO_ERROR;
    }

    void OnSubscriptionEstablished(chip::app::ReadHandler & handler) override
    {
        if (IsEp0AclSubscription(handler))
        {
            OnEp0AclSubscriptionAdded();
        }
    }

    void OnSubscriptionTerminated(chip::app::ReadHandler & handler) override
    {
        if (IsEp0AclSubscription(handler))
        {
            OnEp0AclSubscriptionRemoved();
        }
    }
};

Ep0AclSubscriptionCallback gEp0AclSubscriptionCallback;

static size_t GetFabricCapacity(const chip::FabricTable &)
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

struct SessionCountContext
{
    FabricIndex fabric;
    size_t count;
};

static size_t CountCaseSessionsForFabric(FabricIndex fabricIndex)
{
    SessionCountContext context{ fabricIndex, 0 };
    auto & sessionManager = Server::GetInstance().GetSecureSessionManager();

    (void) sessionManager.ForEachSessionHandle(&context, [](void * ctx, chip::SessionHandle & handle) -> bool {
        auto * countCtx = static_cast<SessionCountContext *>(ctx);
        auto * secure   = handle->AsSecureSession();
        if ((secure != nullptr) && secure->IsCASESession() && (secure->GetFabricIndex() == countCtx->fabric))
        {
            countCtx->count++;
        }
        return true;
    });

    return context.count;
}

static bool HasActiveCaseSessionOnFabric(FabricIndex fabricIndex)
{
    return CountCaseSessionsForFabric(fabricIndex) > 0;
}

static void InitEventLogging()
{
    auto & eventManagement = chip::app::EventManagement::GetInstance();
    (void) eventManagement;
}

static void NotifyAclChanged()
{
    using namespace chip::app;
    namespace AccessControlCluster = chip::app::Clusters::AccessControl;

    auto * engine = InteractionModelEngine::GetInstance();
    if (engine == nullptr)
    {
        return;
    }

    AttributePathParams paths[] = {
        AttributePathParams(0, AccessControlCluster::Id, AccessControlCluster::Attributes::Acl::Id),
        AttributePathParams(0, AccessControlCluster::Id,
                            AccessControlCluster::Attributes::AccessControlEntriesPerFabric::Id),
        AttributePathParams(0, AccessControlCluster::Id,
                            AccessControlCluster::Attributes::SubjectsPerAccessControlEntry::Id),
        AttributePathParams(0, AccessControlCluster::Id,
                            AccessControlCluster::Attributes::TargetsPerAccessControlEntry::Id),
    };

    for (auto & path : paths)
    {
        (void) engine->GetReportingEngine().SetDirty(path);
    }
}

static void EnsureAccessControlReady()
{
    size_t entryCount = 0;
    CHIP_ERROR err     = chip::Access::GetAccessControl().GetEntryCount(entryCount);
    VerifyOrDie(err == CHIP_NO_ERROR);
}

static void AssertRootAccessControlReady()
{
    VerifyOrDie(emberAfContainsServer(0, chip::app::Clusters::AccessControl::Id));
    NotifyAclChanged();
}

static void PreventDeepSleepForEp0Subscription()
{
#ifdef CONFIG_PM
#if defined(PM_STATE_STANDBY)
    constexpr pm_state_t kSleepState = PM_STATE_STANDBY;
#elif defined(PM_STATE_RUNTIME_IDLE)
    constexpr pm_state_t kSleepState = PM_STATE_RUNTIME_IDLE;
#else
    constexpr pm_state_t kSleepState = PM_STATE_SOFT_OFF;
#endif
#ifdef PM_ALL_SUBSTATES
    constexpr uint8_t kSubstate = PM_ALL_SUBSTATES;
#else
    constexpr uint8_t kSubstate = 0;
#endif

    if (!gEp0AclPmLockActive)
    {
        int rc = pm_policy_state_lock_get(kSleepState, kSubstate);
        if (rc == 0)
        {
            gEp0AclPmLockActive = true;
        }
        else
        {
            LOG_WRN("Failed to acquire PM policy lock (%d)", rc);
        }
    }
#endif
}

static void AllowDeepSleepForEp0Subscription()
{
#ifdef CONFIG_PM
#if defined(PM_STATE_STANDBY)
    constexpr pm_state_t kSleepState = PM_STATE_STANDBY;
#elif defined(PM_STATE_RUNTIME_IDLE)
    constexpr pm_state_t kSleepState = PM_STATE_RUNTIME_IDLE;
#else
    constexpr pm_state_t kSleepState = PM_STATE_SOFT_OFF;
#endif
#ifdef PM_ALL_SUBSTATES
    constexpr uint8_t kSubstate = PM_ALL_SUBSTATES;
#else
    constexpr uint8_t kSubstate = 0;
#endif

    if (gEp0AclPmLockActive)
    {
        pm_policy_state_lock_put(kSleepState, kSubstate);
        gEp0AclPmLockActive = false;
    }
#endif
}

static void Ep0AclKeepAliveTimer(chip::System::Layer * layer, void * context)
{
    (void) context;

    if (!gEp0AclSubscriptionActive)
    {
        gEp0AclKeepAliveArmed = false;
        return;
    }

    // Keep rescheduling to prevent deep sleep; no payload is required.
    CHIP_ERROR err = layer->StartTimer(kEp0AclKeepAliveInterval, Ep0AclKeepAliveTimer, nullptr);
    if (err != CHIP_NO_ERROR)
    {
        gEp0AclKeepAliveArmed = false;
        ChipLogError(AppServer, "Failed to re-arm ACL keep-alive timer: %s", chip::ErrorStr(err));
    }
}

static void StartEp0AclKeepAliveTimer()
{
    if (gEp0AclKeepAliveArmed)
    {
        return;
    }

    CHIP_ERROR err = chip::DeviceLayer::SystemLayer().StartTimer(kEp0AclKeepAliveInterval, Ep0AclKeepAliveTimer, nullptr);
    if (err == CHIP_NO_ERROR)
    {
        gEp0AclKeepAliveArmed = true;
    }
    else
    {
        ChipLogError(AppServer, "Failed to arm ACL keep-alive timer: %s", chip::ErrorStr(err));
    }
}

static void StopEp0AclKeepAliveTimer()
{
    if (!gEp0AclKeepAliveArmed)
    {
        return;
    }

    (void) chip::DeviceLayer::SystemLayer().CancelTimer(Ep0AclKeepAliveTimer, nullptr);
    gEp0AclKeepAliveArmed = false;
}

static void OnEp0AclSubscriptionAdded()
{
    if (gEp0AclSubscriptionRefCount == UINT8_MAX)
    {
        return;
    }

    gEp0AclSubscriptionRefCount++;
    if (gEp0AclSubscriptionActive)
    {
        return;
    }

    gEp0AclSubscriptionActive = true;
    PreventDeepSleepForEp0Subscription();
    StartEp0AclKeepAliveTimer();
}

static void OnEp0AclSubscriptionRemoved()
{
    if (gEp0AclSubscriptionRefCount == 0)
    {
        return;
    }

    gEp0AclSubscriptionRefCount--;
    if (gEp0AclSubscriptionRefCount > 0)
    {
        return;
    }

    gEp0AclSubscriptionActive = false;
    StopEp0AclKeepAliveTimer();
    AllowDeepSleepForEp0Subscription();
}

static bool IsEp0AclSubscription(chip::app::ReadHandler & handler)
{
    const chip::SingleLinkedListNode<chip::app::AttributePathParams> * pathNode = handler.GetAttributePathList();
    if (pathNode == nullptr)
    {
        return false;
    }

    for (auto current = pathNode; current != nullptr; current = current->mpNext)
    {
        const auto & path = current->mValue;
        if (path.HasWildcardClusterId())
        {
            continue;
        }

        if (path.mClusterId != chip::app::Clusters::AccessControl::Id)
        {
            continue;
        }

        if (!path.HasWildcardEndpointId() && (path.mEndpointId != chip::EndpointId{ 0 }))
        {
            continue;
        }

        return true;
    }

    return false;
}

static CHIP_ERROR CloseCaseSessionsForFabric(FabricIndex fabricIndex)
{
    Server::GetInstance().GetSecureSessionManager().ExpireAllSessionsForFabric(fabricIndex);
    return CHIP_NO_ERROR;
}

static bool HasActiveSubscriptionOnFabric(FabricIndex fabricIndex)
{
    auto * im = chip::app::InteractionModelEngine::GetInstance();
    return (im != nullptr) && im->FabricHasAtLeastOneActiveSubscription(fabricIndex);
}

static Optional<FabricIndex> PickEvictionCandidate(const chip::FabricTable & table, Optional<FabricIndex> protectedFabric)
{
    for (auto it = table.cbegin(); it != table.cend(); ++it)
    {
        const chip::FabricInfo & info = *it;
        if (!info.IsInitialized())
        {
            continue;
        }

        FabricIndex candidate = info.GetFabricIndex();
        if (protectedFabric.HasValue() && (candidate == protectedFabric.Value()))
        {
            continue;
        }

        if (!HasActiveCaseSessionOnFabric(candidate) && !HasActiveSubscriptionOnFabric(candidate))
        {
            return chip::MakeOptional(candidate);
        }
    }

    FabricIndex fallback            = chip::kUndefinedFabricIndex;
    size_t lowestSessionCount       = SIZE_MAX;

    for (auto it = table.cbegin(); it != table.cend(); ++it)
    {
        const chip::FabricInfo & info = *it;
        if (!info.IsInitialized())
        {
            continue;
        }

        FabricIndex candidate = info.GetFabricIndex();
        if (protectedFabric.HasValue() && (candidate == protectedFabric.Value()))
        {
            continue;
        }

        if (HasActiveCaseSessionOnFabric(candidate))
        {
            continue;
        }

        size_t sessionCount = 0;
        if (HasActiveSubscriptionOnFabric(candidate))
        {
            continue;
        }
        if (sessionCount < lowestSessionCount)
        {
            lowestSessionCount = sessionCount;
            fallback           = candidate;
        }
    }

    if (fallback != chip::kUndefinedFabricIndex)
    {
        return chip::MakeOptional(fallback);
    }

    return Optional<FabricIndex>();
}

static CHIP_ERROR EnsureFreeFabricSlot(Optional<FabricIndex> protectedFabric)
{
    auto & fabricTable = Server::GetInstance().GetFabricTable();

    const size_t capacity    = GetFabricCapacity(fabricTable);
    const size_t fabricCount = fabricTable.FabricCount();

    if ((capacity == 0) || (fabricCount < capacity))
    {
        return CHIP_NO_ERROR;
    }

    ChipLogProgress(AppServer, "Fabric guard: %u/%u slots", static_cast<unsigned>(fabricCount),
                    static_cast<unsigned>(capacity));

    if (gEp0AclSubscriptionActive)
    {
        return CHIP_ERROR_NO_MEMORY;
    }

    Optional<FabricIndex> victim = PickEvictionCandidate(fabricTable, protectedFabric);
    VerifyOrReturnError(victim.HasValue(), CHIP_ERROR_NO_MEMORY);

    if (gPendingFabricEviction.HasValue() && (gPendingFabricEviction.Value() == victim.Value()))
    {
        return CHIP_NO_ERROR;
    }

    if (gPendingFabricEviction.HasValue() && (gPendingFabricEviction.Value() != victim.Value()))
    {
        (void) chip::DeviceLayer::SystemLayer().CancelTimer(FabricEvictionTimerHandler,
                                                            reinterpret_cast<void *>(static_cast<uintptr_t>(gPendingFabricEviction.Value())));
    }

    gPendingFabricEviction.SetValue(victim.Value());
    ScheduleFabricEviction(victim.Value());

    return CHIP_NO_ERROR;
}

static void ScheduleFabricEviction(chip::FabricIndex victim, chip::System::Clock::Milliseconds32 delay)
{
    if (victim == chip::kUndefinedFabricIndex)
    {
        return;
    }

    (void) chip::DeviceLayer::SystemLayer().CancelTimer(FabricEvictionTimerHandler,
                                                        reinterpret_cast<void *>(static_cast<uintptr_t>(victim)));

    CHIP_ERROR err = chip::DeviceLayer::SystemLayer().StartTimer(delay, FabricEvictionTimerHandler,
                                                                reinterpret_cast<void *>(static_cast<uintptr_t>(victim)));
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "Fabric guard: schedule timer fail (0x%02x): %s", victim, chip::ErrorStr(err));
        gPendingFabricEviction.ClearValue();
        return;
    }

}

static void FabricEvictionTimerHandler(chip::System::Layer * layer, void * context)
{
    (void) layer;
    auto victim = static_cast<chip::FabricIndex>(reinterpret_cast<uintptr_t>(context));
    chip::DeviceLayer::PlatformMgr().ScheduleWork(FabricEvictionWork, static_cast<intptr_t>(victim));
}

static void FabricEvictionWork(intptr_t context)
{
    chip::FabricIndex victim = static_cast<chip::FabricIndex>(context);

    if (!gPendingFabricEviction.HasValue() || (gPendingFabricEviction.Value() != victim))
    {
        return;
    }

    if (gEp0AclSubscriptionActive)
    {
        ScheduleFabricEviction(victim, kFabricEvictionRetryDelay);
        return;
    }

    if (HasActiveCaseSessionOnFabric(victim) || HasActiveSubscriptionOnFabric(victim))
    {
        ScheduleFabricEviction(victim, kFabricEvictionRetryDelay);
        return;
    }

    (void) CloseCaseSessionsForFabric(victim);

    CHIP_ERROR err = Server::GetInstance().GetFabricTable().Delete(victim);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "Fabric guard: delete fail (0x%02x): %s", victim, chip::ErrorStr(err));
        ScheduleFabricEviction(victim, kFabricEvictionRetryDelay);
        return;
    }

    NotifyAclChanged();
    gPendingFabricEviction.ClearValue();
}

static void ScheduleEnsureCaseAdminEntry(chip::FabricIndex fabricIndex)
{
    if (fabricIndex == chip::kUndefinedFabricIndex)
    {
        return;
    }

    chip::DeviceLayer::PlatformMgr().ScheduleWork(EnsureCaseAdminEntryWork, static_cast<intptr_t>(fabricIndex));
}

static void EnsureCaseAdminEntryWork(intptr_t context)
{
    chip::FabricIndex fabricIndex = static_cast<chip::FabricIndex>(context);
    if (fabricIndex == chip::kUndefinedFabricIndex)
    {
        return;
    }

    auto & accessControl = chip::Access::GetAccessControl();

    size_t entryCount = 0;
    CHIP_ERROR err    = accessControl.GetEntryCount(fabricIndex, entryCount);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "ACL bootstrap err f=0x%02x: %s", fabricIndex, chip::ErrorStr(err));
        return;
    }

    if (entryCount > 0)
    {
        NotifyAclChanged();
        return;
    }

    chip::Access::AccessControl::Entry newEntry;
    err = accessControl.PrepareEntry(newEntry);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "ACL bootstrap err f=0x%02x: %s", fabricIndex, chip::ErrorStr(err));
        return;
    }

    err = newEntry.SetFabricIndex(fabricIndex);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "ACL bootstrap err f=0x%02x: %s", fabricIndex, chip::ErrorStr(err));
        return;
    }

    err = newEntry.SetPrivilege(chip::Access::Privilege::kAdminister);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "ACL bootstrap err f=0x%02x: %s", fabricIndex, chip::ErrorStr(err));
        return;
    }

    err = newEntry.SetAuthMode(chip::Access::AuthMode::kCase);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "ACL bootstrap err f=0x%02x: %s", fabricIndex, chip::ErrorStr(err));
        return;
    }

    err = newEntry.AddSubject(nullptr, kCaseAdminSubjectId);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "ACL bootstrap err f=0x%02x: %s", fabricIndex, chip::ErrorStr(err));
        return;
    }

    size_t newIndex = 0;
    err             = accessControl.CreateEntry(nullptr, fabricIndex, &newIndex, newEntry);
    if (err != CHIP_NO_ERROR)
    {
        if (err != CHIP_ERROR_INCORRECT_STATE)
        {
            ChipLogError(AppServer, "ACL bootstrap err f=0x%02x: %s", fabricIndex, chip::ErrorStr(err));
        }
        return;
    }

    NotifyAclChanged();
}

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

        size_t entriesPerFabric  = CONFIG_CHIP_ACL_MAX_ENTRIES_PER_FABRIC;
        size_t subjectsPerEntry   = CONFIG_CHIP_ACL_MAX_SUBJECTS_PER_ENTRY;
        size_t targetsPerEntry    = CONFIG_CHIP_ACL_MAX_TARGETS_PER_ENTRY;

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
bool gRegisteredIMHooks = false;
bool gFabricDelegateRegistered = false;

void RegisterIMHooksOnce()
{
    if (gRegisteredIMHooks)
    {
        return;
    }

    chip::app::AttributeAccessInterfaceRegistry::Instance().Register(&gAclLimitsAttrAccess);
    chip::app::InteractionModelEngine::GetInstance()->RegisterReadHandlerAppCallback(&gEp0AclSubscriptionCallback);

    gRegisteredIMHooks = true;
}

class CaseAdminAclFabricDelegate final : public chip::FabricTable::Delegate
{
public:
    void OnFabricCommitted(const chip::FabricTable & fabricTable, chip::FabricIndex fabricIndex) override
    {
        (void) fabricTable;
        ScheduleEnsureCaseAdminEntry(fabricIndex);
    }
};

CaseAdminAclFabricDelegate gCaseAdminAclFabricDelegate;

CHIP_ERROR InitManagementClusters()
{
    auto & server = chip::Server::GetInstance();

    gGroupDataProvider.SetStorageDelegate(&server.GetPersistentStorage());
    gGroupDataProvider.SetSessionKeystore(server.GetSessionKeystore());

    CHIP_ERROR err = gGroupDataProvider.Init();
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "GroupDataProvider init failed: %" CHIP_ERROR_FORMAT, err.Format());
        return err;
    }

    chip::Credentials::SetGroupDataProvider(&gGroupDataProvider);

    return CHIP_NO_ERROR;
}

class CommissioningCapacityDelegate final : public AppDelegate
{
public:
    void OnCommissioningWindowOpened() override
    {
        Optional<FabricIndex> protectedFabric;
        const auto openerFabric = Server::GetInstance().GetCommissioningWindowManager().GetOpenerFabricIndex();
        if (!openerFabric.IsNull())
        {
            protectedFabric.SetValue(openerFabric.Value());
        }

        CHIP_ERROR err = EnsureFreeFabricSlot(protectedFabric);
        if (err == CHIP_ERROR_NO_MEMORY)
        {
            ChipLogProgress(AppServer, "Fabric guard: no free slot");
        }
        else if (err != CHIP_NO_ERROR)
        {
            ChipLogError(AppServer, "Fabric guard: ensure failed: %" CHIP_ERROR_FORMAT, err.Format());
        }
    }

    void OnCommissioningSessionEstablishmentStarted() override
    {
        Optional<FabricIndex> protectedFabric;
        const auto openerFabric = Server::GetInstance().GetCommissioningWindowManager().GetOpenerFabricIndex();
        if (!openerFabric.IsNull())
        {
            protectedFabric.SetValue(openerFabric.Value());
        }

        CHIP_ERROR err = EnsureFreeFabricSlot(protectedFabric);
        if (err == CHIP_ERROR_NO_MEMORY)
        {
            ChipLogProgress(AppServer, "Fabric guard: full, no eviction");
        }
        else if (err != CHIP_NO_ERROR)
        {
            ChipLogError(AppServer, "Fabric guard: ensure failed: %" CHIP_ERROR_FORMAT, err.Format());
        }
    }
};

CommissioningCapacityDelegate gCommissioningCapacityDelegate;

static bool gPluginsInitialized = false;

static void InitializeGeneratedPluginsOnce()
{
    if (gPluginsInitialized)
    {
        return;
    }

    MATTER_PLUGINS_INIT;
    gPluginsInitialized = true;
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
    initParams.appDelegate       = &gCommissioningCapacityDelegate;

    err = chip::Server::GetInstance().Init(initParams);

    if (err != CHIP_NO_ERROR) { LOG_ERR("Matter Server init failed: %ld", (long)err.AsInteger()); return -2; }

    InitEventLogging();
    EnsureAccessControlReady();
    AssertRootAccessControlReady();

    RegisterIMHooksOnce();
    NotifyAclChanged();

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
        ChipLogError(AppServer, "Management cluster init failed: %" CHIP_ERROR_FORMAT, managementErr.Format());
    }

    InitializeGeneratedPluginsOnce();

    (void) chip::System::SystemClock().SetClock_RealTime(chip::System::SystemClock().GetMonotonicMicroseconds64());


    // --- ACL init & commissioning window hygiene ---
    {
        auto & server = chip::Server::GetInstance();

        (void) chip::Access::GetAccessControl();

        if (!gFabricDelegateRegistered)
        {
            CHIP_ERROR delegateErr = server.GetFabricTable().AddFabricDelegate(&gCaseAdminAclFabricDelegate);
            if (delegateErr != CHIP_NO_ERROR)
            {
                ChipLogError(AppServer, "Failed to register fabric ACL delegate: %s", chip::ErrorStr(delegateErr));
            }
            else
            {
                gFabricDelegateRegistered = true;
            }
        }

        for (auto it = server.GetFabricTable().cbegin(); it != server.GetFabricTable().cend(); ++it)
        {
            const chip::FabricInfo & info = *it;
            if (!info.IsInitialized())
            {
                continue;
            }
            ScheduleEnsureCaseAdminEntry(info.GetFabricIndex());
        }

        // Only close an *existing* window on already-commissioned devices.
        // For first-boot (no fabrics), keep the default window open for PASE.
        if (server.GetFabricTable().FabricCount() > 0) {
            // Keep default commissioning window state; do not force-close here.
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
