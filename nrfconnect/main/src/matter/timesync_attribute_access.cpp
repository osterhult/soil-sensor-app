#include "matter/timesync_attribute_access.h"

#include <app/AttributeAccessInterfaceRegistry.h>
#include <app/util/attribute-storage.h>
#include <app-common/zap-generated/attributes/Accessors.h>
#include <app-common/zap-generated/cluster-objects.h>
#include <lib/support/CodeUtils.h>

using namespace chip;
using namespace chip::app;

namespace matter { namespace timesync {

static TimeSyncAttrAccess gTimeSyncAttrAccess;

void RegisterTimeSyncAttrAccess()
{
    AttributeAccessInterfaceRegistry::Instance().Register(&gTimeSyncAttrAccess);
}

CHIP_ERROR TimeSyncAttrAccess::Read(const ConcreteReadAttributePath & path, AttributeValueEncoder & encoder)
{
    using namespace chip::app::Clusters::TimeSynchronization;
    using namespace chip::app::Clusters::TimeSynchronization::Attributes;

    if (path.mClusterId != Clusters::TimeSynchronization::Id)
    {
        return CHIP_IM_GLOBAL_STATUS(InvalidAction);
    }

    switch (path.mAttributeId)
    {
    case UTCTime::Id:
        return encoder.EncodeNull();
    case Granularity::Id:
        return encoder.Encode(static_cast<uint8_t>(0));
    case TimeSource::Id:
        return encoder.Encode(static_cast<uint8_t>(0));
    case TimeZone::Id:
        return encoder.EncodeList([](const auto & listEncoder) -> CHIP_ERROR { return CHIP_NO_ERROR; });
    case DSTOffset::Id:
        return encoder.EncodeList([](const auto & listEncoder) -> CHIP_ERROR { return CHIP_NO_ERROR; });
    case FeatureMap::Id:
        return CHIP_ERROR_NOT_FOUND;
    case AttributeList::Id: {
        static constexpr AttributeId kAttrs[] = {
            UTCTime::Id,
            Granularity::Id,
            TimeSource::Id,
            TimeZone::Id,
            DSTOffset::Id,
            FeatureMap::Id,
            ClusterRevision::Id,
            GeneratedCommandList::Id,
            AcceptedCommandList::Id,
            AttributeList::Id,
        };
        return encoder.EncodeList([&](const auto & listEncoder) -> CHIP_ERROR {
            for (AttributeId attr : kAttrs)
            {
                ReturnErrorOnFailure(listEncoder.Encode(attr));
            }
            return CHIP_NO_ERROR;
        });
    }
    case ClusterRevision::Id:
        return encoder.Encode(static_cast<uint16_t>(2));
    default:
        return CHIP_ERROR_NOT_FOUND;
    }
}

}} // namespace matter::timesync
