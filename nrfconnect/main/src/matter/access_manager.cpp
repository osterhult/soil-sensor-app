#include "matter/access_manager.h"

#include <access/AccessControl.h>
#include <app/CASESessionManager.h>
#include <app/EventManagement.h>
#include <app/InteractionModelEngine.h>
#include <app/reporting/Engine.h>
#include <app/server/AppDelegate.h>
#include <app/server/Server.h>
#include <app/util/attribute-storage.h>
#include <credentials/FabricTable.h>
#include <credentials/GroupDataProvider.h>
#include <credentials/GroupDataProviderImpl.h>
#include <lib/core/NodeId.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/KeyValueStoreManager.h>
#include <platform/Zephyr/KeyValueStoreManagerImpl.h>
#include <system/SystemClock.h>
#include <protocols/secure_channel/SessionResumptionStorage.h>
#include <transport/Session.h>
#include <transport/SessionManager.h>
#include <vector>
#include <zephyr/settings/settings.h>

LOG_MODULE_DECLARE(soil_app, LOG_LEVEL_INF);

namespace matter
{
namespace access_manager
{

namespace
{

bool sFabricDelegateRegistered = false;
bool sAclListenerRegistered    = false;

chip::Credentials::GroupDataProviderImpl gGroupDataProvider;

size_t MatterMaxFabrics()
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

void NotifyAclChanged()
{
    auto * engine = chip::app::InteractionModelEngine::GetInstance();
    if (engine == nullptr)
    {
        return;
    }

    using namespace chip::app::Clusters::AccessControl::Attributes;

    chip::app::AttributePathParams paths[] = {
        chip::app::AttributePathParams(0, chip::app::Clusters::AccessControl::Id, Acl::Id),
        chip::app::AttributePathParams(0, chip::app::Clusters::AccessControl::Id, AccessControlEntriesPerFabric::Id),
        chip::app::AttributePathParams(0, chip::app::Clusters::AccessControl::Id, SubjectsPerAccessControlEntry::Id),
        chip::app::AttributePathParams(0, chip::app::Clusters::AccessControl::Id, TargetsPerAccessControlEntry::Id),
    };

    for (auto & path : paths)
    {
        (void) engine->GetReportingEngine().SetDirty(path);
    }
}

bool IsBenignNotFound(CHIP_ERROR err)
{
    return (err == CHIP_NO_ERROR) || (err == CHIP_ERROR_NOT_FOUND) || (err == CHIP_ERROR_PERSISTED_STORAGE_VALUE_NOT_FOUND) ||
        (err == CHIP_ERROR_KEY_NOT_FOUND);
}

struct FabricSessionCheckContext
{
    chip::FabricIndex fabric;
    bool hasActive;
};

bool SessionIteratorForFabric(void * context, chip::SessionHandle & handle)
{
    auto * data = static_cast<FabricSessionCheckContext *>(context);
    if (handle->IsActiveSession() && handle->GetFabricIndex() == data->fabric)
    {
        data->hasActive = true;
        return false;
    }
    return true;
}

bool FabricHasActiveSessions(chip::FabricIndex fabricIndex)
{
    FabricSessionCheckContext ctx{ fabricIndex, false };
    (void) chip::Server::GetInstance().GetSecureSessionManager().ForEachSessionHandle(&ctx, SessionIteratorForFabric);
    return ctx.hasActive;
}

bool FabricHasActiveSubscription(chip::FabricIndex fabricIndex)
{
    auto * imEngine = chip::app::InteractionModelEngine::GetInstance();
    return (imEngine != nullptr) && imEngine->FabricHasAtLeastOneActiveSubscription(fabricIndex);
}

bool TryEvictIdleFabric()
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

        chip::FabricIndex candidate = info.GetFabricIndex();
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

        (void) chip::Access::GetAccessControl().DeleteAllEntriesForFabric(candidate);
        NotifyAclChanged();

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

constexpr chip::NodeId kCaseAdminSubjectId = 0x0000000000000002ULL;

CHIP_ERROR EnsureCaseAdminEntryForFabric(chip::FabricIndex fabricIndex)
{
    if (fabricIndex == chip::kUndefinedFabricIndex)
    {
        return CHIP_NO_ERROR;
    }

    auto & accessControl = chip::Access::GetAccessControl();

    size_t entryCount = 0;
    CHIP_ERROR err    = accessControl.GetEntryCount(fabricIndex, entryCount);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "ACL seed: failed to get entry count for fabric 0x%02x: %s", fabricIndex, chip::ErrorStr(err));
        return err;
    }

    if (entryCount > 0)
    {
        return CHIP_NO_ERROR;
    }

    chip::Access::AccessControl::Entry newEntry;
    err = accessControl.PrepareEntry(newEntry);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "ACL seed: failed preparing entry for fabric 0x%02x: %s", fabricIndex, chip::ErrorStr(err));
        return err;
    }

    (void) newEntry.SetFabricIndex(fabricIndex);
    (void) newEntry.SetPrivilege(chip::Access::Privilege::kAdminister);
    (void) newEntry.SetAuthMode(chip::Access::AuthMode::kCase);

    err = newEntry.AddSubject(nullptr, kCaseAdminSubjectId);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "ACL seed: failed to add subject for fabric 0x%02x: %s", fabricIndex, chip::ErrorStr(err));
        return err;
    }

    size_t newIndex = 0;
    err             = accessControl.CreateEntry(nullptr, fabricIndex, &newIndex, newEntry);
    if (err == CHIP_ERROR_INCORRECT_STATE)
    {
        return CHIP_NO_ERROR;
    }
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(AppServer, "ACL seed: create entry failed for fabric 0x%02x: %s", fabricIndex, chip::ErrorStr(err));
        return err;
    }

    ChipLogProgress(AppServer, "ACL seed: created CASE Admin entry for fabric index 0x%02x (ACL index %zu)", fabricIndex, newIndex);
    NotifyAclChanged();
    return CHIP_NO_ERROR;
}

class AccessControlEntryListener final : public chip::Access::AccessControl::EntryListener
{
public:
    void OnEntryChanged(const chip::Access::SubjectDescriptor *, chip::FabricIndex, size_t,
                        const chip::Access::AccessControl::Entry *, ChangeType) override
    {
        NotifyAclChanged();
    }
};

AccessControlEntryListener gAccessControlEntryListener;

class CaseAdminAclFabricDelegate final : public chip::FabricTable::Delegate
{
public:
    void OnFabricCommitted(const chip::FabricTable &, chip::FabricIndex fabricIndex) override
    {
        CHIP_ERROR err = EnsureCaseAdminEntryForFabric(fabricIndex);
        if (err != CHIP_NO_ERROR)
        {
            ChipLogError(AppServer, "ACL seed: failed for fabric 0x%02x: %s", fabricIndex, chip::ErrorStr(err));
        }
    }
};

CaseAdminAclFabricDelegate gCaseAdminAclFabricDelegate;

class CommissioningCapacityDelegate final : public ::AppDelegate
{
public:
    void OnCommissioningWindowOpened() override { (void) EnsureFabricSlot(); }
    void OnCommissioningSessionEstablishmentStarted() override { (void) EnsureFabricSlot(); }
};

CommissioningCapacityDelegate gCommissioningCapacityDelegate;

} // namespace

CHIP_ERROR InitManagementClusters()
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

void EnsureAccessControlReady()
{
    size_t entryCount = 0;
    VerifyOrDie(chip::Access::GetAccessControl().GetEntryCount(entryCount) == CHIP_NO_ERROR);
}

void AssertRootAccessControlReady()
{
    VerifyOrDie(emberAfContainsServer(0, chip::app::Clusters::AccessControl::Id));
    NotifyAclChanged();
}

bool EnsureFabricSlot()
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

::AppDelegate & CommissioningCapacityDelegate()
{
    return gCommissioningCapacityDelegate;
}

void InitializeFabricHandlers(chip::Server & server)
{
    auto & accessControl = chip::Access::GetAccessControl();
    if (!sAclListenerRegistered)
    {
        accessControl.AddEntryListener(gAccessControlEntryListener);
        sAclListenerRegistered = true;
    }

    if (!sFabricDelegateRegistered)
    {
        CHIP_ERROR delegateErr = server.GetFabricTable().AddFabricDelegate(&gCaseAdminAclFabricDelegate);
        if (delegateErr == CHIP_NO_ERROR)
        {
            sFabricDelegateRegistered = true;
        }
        else if (delegateErr != CHIP_ERROR_NO_MEMORY && delegateErr != CHIP_ERROR_INCORRECT_STATE)
        {
            ChipLogError(AppServer, "Failed to register fabric ACL delegate: %s", chip::ErrorStr(delegateErr));
        }
    }

    for (auto it = server.GetFabricTable().begin(); it != server.GetFabricTable().end(); ++it)
    {
        const chip::FabricInfo & info = *it;
        if (!info.IsInitialized())
        {
            continue;
        }

        CHIP_ERROR ensureErr = EnsureCaseAdminEntryForFabric(info.GetFabricIndex());
        if (ensureErr != CHIP_NO_ERROR)
        {
            ChipLogError(AppServer, "EnsureCaseAdminEntryForFabric failed for fabric 0x%02x: %s",
                         static_cast<unsigned>(info.GetFabricIndex()), chip::ErrorStr(ensureErr));
        }
    }
}

void OpenCommissioningWindowIfNeeded(chip::Server & server)
{
    if (server.GetFabricTable().FabricCount() == 0)
    {
        if (!EnsureFabricSlot())
        {
            LOG_WRN("Unable to free fabric slot before commissioning window");
            return;
        }

        constexpr auto kDefaultCommissioningTimeout = chip::System::Clock::Seconds32(15 * 60);
        CHIP_ERROR windowErr = server.GetCommissioningWindowManager().OpenBasicCommissioningWindow(kDefaultCommissioningTimeout);
        if (windowErr != CHIP_NO_ERROR)
        {
            LOG_WRN("OpenBasicCommissioningWindow failed: %s", chip::ErrorStr(windowErr));
        }
    }
}

CHIP_ERROR DoFullMatterWipe()
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
    CHIP_ERROR firstError = CHIP_NO_ERROR;

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
    auto * caseManager   = server.GetCASESessionManager();

    for (FabricIndex fabricIndex : fabricsToDelete)
    {
        server.GetSecureSessionManager().ExpireAllSessionsForFabric(fabricIndex);

        if (caseManager != nullptr)
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
                if (firstError == CHIP_NO_ERROR)
                {
                    firstError = err;
                }
            }
        }

        if (groupProvider != nullptr)
        {
            CHIP_ERROR err = groupProvider->RemoveFabric(fabricIndex);
            if (!IsBenignNotFound(err) && err != CHIP_ERROR_NOT_IMPLEMENTED)
            {
                ChipLogProgress(AppServer, "Failed to remove group data for fabric 0x%02x: %s", fabricIndex,
                                chip::ErrorStr(err));
                if (firstError == CHIP_NO_ERROR)
                {
                    firstError = err;
                }
            }
        }

        CHIP_ERROR fabricErr = fabricTable.Delete(fabricIndex);
        if (fabricErr != CHIP_NO_ERROR && fabricErr != CHIP_ERROR_NOT_FOUND)
        {
            ChipLogProgress(AppServer, "Fabric delete failed for index 0x%02x: %s", fabricIndex, chip::ErrorStr(fabricErr));
            if (firstError == CHIP_NO_ERROR)
            {
                firstError = fabricErr;
            }
        }

        (void) Access::GetAccessControl().DeleteAllEntriesForFabric(fabricIndex);
    }

    Access::ResetAccessControlToDefault();
    NotifyAclChanged();

    server.Shutdown();

    CHIP_ERROR kvsErr = PersistedStorage::KeyValueStoreMgrImpl().DoFactoryReset();
    if (kvsErr != CHIP_NO_ERROR)
    {
        ChipLogProgress(AppServer, "KeyValueStore factory reset failed: %s", chip::ErrorStr(kvsErr));
        if (firstError == CHIP_NO_ERROR)
        {
            firstError = kvsErr;
        }
    }

    app::EventManagement::DestroyEventManagement();

    server.GetFailSafeContext().DisarmFailSafe();

    PlatformMgr().ScheduleWork([](intptr_t) {
        int rc = settings_save();
        if (rc != 0)
        {
            LOG_WRN("settings_save failed: %d", rc);
        }
    });

    ChipLogProgress(AppServer, "DoFullMatterWipe completed");

    return firstError;
}

} // namespace access_manager
} // namespace matter
