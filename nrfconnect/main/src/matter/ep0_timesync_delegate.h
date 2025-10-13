#pragma once

#include <app/AttributeAccessInterface.h>
#include <app/AttributeValueDecoder.h>
#include <app/AttributeValueEncoder.h>
#include <app/ConcreteAttributePath.h>
#include <app/clusters/time-synchronization-server/time-synchronization-server.h>
#include <lib/core/CHIPError.h>
#include <lib/core/Optional.h>
#include <protocols/interaction_model/StatusCode.h>

namespace matter
{
namespace ep0
{

class TimeSyncDelegate : public chip::app::AttributeAccessInterface
{
public:
    static TimeSyncDelegate & Instance();

    CHIP_ERROR Read(const chip::app::ConcreteReadAttributePath & path,
                    chip::app::AttributeValueEncoder & encoder) override;

    CHIP_ERROR Write(const chip::app::ConcreteDataAttributePath & path,
                     chip::app::AttributeValueDecoder & decoder) override;

    chip::Protocols::InteractionModel::Status HandleSetUtcTimeCommand(
        const chip::app::Clusters::TimeSynchronization::Commands::SetUTCTime::DecodableType & commandData);

private:
    TimeSyncDelegate();

    chip::Optional<uint64_t> mUtcTime; // chip epoch microseconds
};

CHIP_ERROR RegisterTimeSyncDelegate();

} // namespace ep0
} // namespace matter
