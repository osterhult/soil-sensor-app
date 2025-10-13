#include "matter/ep0_timesync_delegate.h"

#include <app-common/zap-generated/cluster-objects.h>
#include <app/CommandHandler.h>
#include <app/AttributeAccessInterfaceRegistry.h>
#include <app/AttributePathParams.h>
#include <app/InteractionModelEngine.h>
#include <app/reporting/Engine.h>
#include <app/server/Server.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/TimeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <protocols/interaction_model/Constants.h>
#include <system/SystemClock.h>

namespace matter
{
namespace ep0
{

namespace
{
constexpr chip::EndpointId kRootEndpoint = 0;
constexpr uint16_t kClusterRevision      = 2;
constexpr uint32_t kFeatureMap           = 0;

CHIP_ERROR EncodeAttributeIdList(chip::app::AttributeValueEncoder & encoder, const chip::AttributeId * ids, size_t count)
{
    return encoder.EncodeList([ids, count](const auto & listEncoder) -> CHIP_ERROR {
        for (size_t idx = 0; idx < count; ++idx)
        {
            ReturnErrorOnFailure(listEncoder.Encode(ids[idx]));
        }
        return CHIP_NO_ERROR;
    });
}

CHIP_ERROR EncodeEmptyList(chip::app::AttributeValueEncoder & encoder)
{
    return encoder.EncodeList([](const auto & /*listEncoder*/) -> CHIP_ERROR { return CHIP_NO_ERROR; });
}

} // namespace

TimeSyncDelegate & TimeSyncDelegate::Instance()
{
    static TimeSyncDelegate sInstance;
    return sInstance;
}

TimeSyncDelegate::TimeSyncDelegate() :
    chip::app::AttributeAccessInterface(chip::MakeOptional<chip::EndpointId>(kRootEndpoint),
                                        chip::app::Clusters::TimeSynchronization::Id),
    mTimeSource(chip::app::Clusters::TimeSynchronization::TimeSourceEnum::kNone)
{}

CHIP_ERROR TimeSyncDelegate::Read(const chip::app::ConcreteReadAttributePath & path,
                                  chip::app::AttributeValueEncoder & encoder)
{
    using namespace chip::app::Clusters::TimeSynchronization::Attributes;

    if (path.mEndpointId != kRootEndpoint)
    {
        return CHIP_NO_ERROR;
    }

    switch (path.mAttributeId)
    {
    case UTCTime::Id:
        if (mUtcTime.HasValue())
        {
            return encoder.Encode(mUtcTime.Value());
        }
        return encoder.EncodeNull();
    case TimeSource::Id:
        return encoder.Encode(static_cast<uint8_t>(mTimeSource));
    case Granularity::Id:
        return encoder.Encode(static_cast<uint8_t>(chip::app::Clusters::TimeSynchronization::GranularityEnum::kNoTimeGranularity));
    case FeatureMap::Id:
        return encoder.Encode(kFeatureMap);
    case ClusterRevision::Id:
        return encoder.Encode(kClusterRevision);
    case AttributeList::Id: {
        constexpr chip::AttributeId kAttributes[] = {
            UTCTime::Id,
            Granularity::Id,
            TimeSource::Id,
            FeatureMap::Id,
            ClusterRevision::Id,
            AttributeList::Id,
            GeneratedCommandList::Id,
            AcceptedCommandList::Id,
        };
        return EncodeAttributeIdList(encoder, kAttributes, sizeof(kAttributes) / sizeof(kAttributes[0]));
    }
    case AcceptedCommandList::Id:
        return encoder.EncodeList([](const auto & listEncoder) -> CHIP_ERROR {
            return listEncoder.Encode(chip::app::Clusters::TimeSynchronization::Commands::SetUTCTime::Id);
        });
    case GeneratedCommandList::Id:
        return EncodeEmptyList(encoder);
    case TimeZone::Id:
    case DSTOffset::Id:
        return CHIP_IM_GLOBAL_STATUS(UnsupportedAttribute);
    default:
        return CHIP_IM_GLOBAL_STATUS(UnsupportedAttribute);
    }
}

CHIP_ERROR TimeSyncDelegate::Write(const chip::app::ConcreteDataAttributePath & path,
                                   chip::app::AttributeValueDecoder & decoder)
{
    using namespace chip::app::Clusters::TimeSynchronization::Attributes;

    if (path.mEndpointId != kRootEndpoint)
    {
        return CHIP_NO_ERROR;
    }

    return CHIP_IM_GLOBAL_STATUS(UnsupportedWrite);
}

chip::Protocols::InteractionModel::Status TimeSyncDelegate::HandleSetUtcTimeCommand(
    const chip::app::Clusters::TimeSynchronization::Commands::SetUTCTime::DecodableType & commandData)
{
    using namespace chip::Protocols::InteractionModel;

    uint64_t unixEpochMicros = 0;
    if (!chip::ChipEpochToUnixEpochMicros(commandData.UTCTime, unixEpochMicros))
    {
        return Status::InvalidCommand;
    }

    // Accept the command but avoid writing optional attributes that are not exposed on EP0.
    mUtcTime.SetValue(commandData.UTCTime);
    const auto & timeSource = commandData.timeSource;
    mTimeSource             = timeSource.HasValue()
                        ? timeSource.Value()
                        : chip::app::Clusters::TimeSynchronization::TimeSourceEnum::kNone;

    const uint64_t chipEpochSeconds = commandData.UTCTime / chip::kMicrosecondsPerSecond;
    if (chipEpochSeconds <= UINT32_MAX)
    {
        CHIP_ERROR lkgErr = chip::Server::GetInstance().GetFabricTable().SetLastKnownGoodChipEpochTime(
            chip::System::Clock::Seconds32(static_cast<uint32_t>(chipEpochSeconds)));
        if (lkgErr != CHIP_NO_ERROR && lkgErr != CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE)
        {
            ChipLogError(AppServer, "Failed to persist Last Known Good Time: %s", chip::ErrorStr(lkgErr));
        }
    }

    CHIP_ERROR clockErr = chip::System::SystemClock().SetClock_RealTime(
        chip::System::Clock::Microseconds64(unixEpochMicros));
    if (clockErr != CHIP_NO_ERROR && clockErr != CHIP_ERROR_UNSUPPORTED_CHIP_FEATURE)
    {
        ChipLogError(AppServer, "Failed to update real-time clock: %s", chip::ErrorStr(clockErr));
    }

    if (auto * engine = chip::app::InteractionModelEngine::GetInstance(); engine != nullptr)
    {
        using AttributePathParams = chip::app::AttributePathParams;
        AttributePathParams utcPath(kRootEndpoint, chip::app::Clusters::TimeSynchronization::Id,
                                    chip::app::Clusters::TimeSynchronization::Attributes::UTCTime::Id);
        (void) engine->GetReportingEngine().SetDirty(utcPath);

    }

    return Status::Success;
}

CHIP_ERROR RegisterTimeSyncDelegate()
{
    static bool sRegistered = false;
    if (sRegistered)
    {
        return CHIP_NO_ERROR;
    }

    auto & registry = chip::app::AttributeAccessInterfaceRegistry::Instance();
    auto & delegate = TimeSyncDelegate::Instance();
    (void) registry.Register(&delegate);
    sRegistered = true;
    return CHIP_NO_ERROR;
}

} // namespace ep0
} // namespace matter

extern "C" bool emberAfTimeSynchronizationClusterSetUTCTimeCallback(
    chip::app::CommandHandler * commandObj, const chip::app::ConcreteCommandPath & commandPath,
    const chip::app::Clusters::TimeSynchronization::Commands::SetUTCTime::DecodableType & commandData)
{
    const auto status = matter::ep0::TimeSyncDelegate::Instance().HandleSetUtcTimeCommand(commandData);
    commandObj->AddStatus(commandPath, status);
    return true;
}
