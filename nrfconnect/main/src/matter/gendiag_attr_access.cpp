#include "matter/gendiag_attr_access.h"

#include <app-common/zap-generated/attributes/Accessors.h>
#include <app-common/zap-generated/cluster-objects.h>
#include <app/AttributeAccessInterfaceRegistry.h>
#include <app/util/attribute-storage.h>
#include <lib/support/CodeUtils.h>
#include <platform/KeyValueStoreManager.h>
#include <system/SystemClock.h>

namespace matter
{
namespace
{
constexpr char kRebootCntKey[] = "soil.gendiag.rebootcnt";
uint16_t sRebootCountCache      = 0;
bool sBootInited                = false;

CHIP_ERROR EnsureRebootCountInitialized()
{
    if (sBootInited)
    {
        return CHIP_NO_ERROR;
    }

    size_t len               = sizeof(sRebootCountCache);
    uint16_t stored          = 0;
    CHIP_ERROR kvErr         = chip::DeviceLayer::PersistedStorage::KeyValueStoreMgr().Get(kRebootCntKey, &stored, len);
    if (kvErr == CHIP_NO_ERROR && len == sizeof(stored))
    {
        sRebootCountCache = static_cast<uint16_t>(stored + 1);
    }
    else
    {
        sRebootCountCache = 1;
    }

    (void) chip::DeviceLayer::PersistedStorage::KeyValueStoreMgr().Put(kRebootCntKey, &sRebootCountCache,
                                                                       sizeof(sRebootCountCache));
    sBootInited = true;
    return CHIP_NO_ERROR;
}
} // namespace

GeneralDiagAttrAccess::GeneralDiagAttrAccess() :
    chip::app::AttributeAccessInterface(chip::MakeOptional<chip::EndpointId>(chip::EndpointId{ 0 }),
                                        chip::app::Clusters::GeneralDiagnostics::Id)
{}

CHIP_ERROR GeneralDiagAttrAccess::Read(const chip::app::ConcreteReadAttributePath & path,
                                       chip::app::AttributeValueEncoder & encoder)
{
    namespace Attr = chip::app::Clusters::GeneralDiagnostics::Attributes;

    VerifyOrReturnError(path.mEndpointId == chip::EndpointId{ 0 }, CHIP_NO_ERROR);
    ReturnErrorOnFailure(EnsureRebootCountInitialized());

    switch (path.mAttributeId)
    {
    case Attr::BootReason::Id: {
        auto reason = chip::app::Clusters::GeneralDiagnostics::BootReasonEnum::kPowerOnReboot;
        return encoder.Encode(reason);
    }
    case Attr::UpTime::Id: {
        const auto millis = chip::System::SystemClock().GetMonotonicMilliseconds64();
        return encoder.Encode(static_cast<uint64_t>(millis.count() / 1000ULL));
    }
    case Attr::RebootCount::Id:
        return encoder.Encode(sRebootCountCache);
    case Attr::ActiveHardwareFaults::Id:
    case Attr::ActiveRadioFaults::Id:
    case Attr::ActiveNetworkFaults::Id:
        return encoder.EncodeEmptyList();
    case Attr::TestEventTriggersEnabled::Id:
        return encoder.Encode(false);
    default:
        return CHIP_NO_ERROR;
    }
}

CHIP_ERROR RegisterGeneralDiagAttrAccess()
{
    static GeneralDiagAttrAccess sAttrAccess;
    static bool sRegistered = false;

    if (sRegistered)
    {
        return CHIP_NO_ERROR;
    }

    auto & registry = chip::app::AttributeAccessInterfaceRegistry::Instance();
    if (!registry.Register(&sAttrAccess))
    {
        // Already registered; treat as success.
        sRegistered = true;
        return CHIP_NO_ERROR;
    }

    sRegistered = true;
    return CHIP_NO_ERROR;
}

} // namespace matter
