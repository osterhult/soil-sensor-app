#include "ep0_metadata_filter.h"

#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/ConcreteAttributePath.h>
#include <lib/support/CodeUtils.h>
#include <protocols/interaction_model/Constants.h>

namespace matter {
namespace ep0 {
namespace {
constexpr chip::EndpointId kRootEndpoint             = 0;
constexpr chip::ClusterId kBasicInformationClusterId = chip::app::Clusters::BasicInformation::Id;
} // namespace

MetadataFilter::MetadataFilter(chip::app::DataModel::Provider & inner) : mInner(&inner) {}

bool MetadataFilter::ShouldFilter(chip::EndpointId endpointId, chip::ClusterId clusterId, chip::AttributeId attributeId) const
{
    static_cast<void>(endpointId);
    static_cast<void>(clusterId);
    static_cast<void>(attributeId);
    return false;
}

CHIP_ERROR MetadataFilter::Startup(chip::app::DataModel::InteractionModelContext context)
{
    return mInner->Startup(context);
}

CHIP_ERROR MetadataFilter::Shutdown()
{
    return mInner->Shutdown();
}

chip::app::DataModel::ActionReturnStatus
MetadataFilter::ReadAttribute(const chip::app::DataModel::ReadAttributeRequest & request,
                              chip::app::AttributeValueEncoder & encoder)
{
    constexpr chip::EndpointId kGroupKeyMgmtEndpoint = 0;
    constexpr chip::ClusterId kGroupKeyMgmtCluster   = chip::app::Clusters::GroupKeyManagement::Id;
    constexpr chip::AttributeId kClusterRevisionId   = chip::app::Clusters::Globals::Attributes::ClusterRevision::Id;

    if ((request.path.mEndpointId == kGroupKeyMgmtEndpoint) && (request.path.mClusterId == kGroupKeyMgmtCluster) &&
        (request.path.mAttributeId == kClusterRevisionId))
    {
        CHIP_ERROR err = encoder.Encode(static_cast<uint16_t>(2));
        if (err != CHIP_NO_ERROR)
        {
            return err;
        }
        return chip::Protocols::InteractionModel::Status::Success;
    }

    return mInner->ReadAttribute(request, encoder);
}

chip::app::DataModel::ActionReturnStatus
MetadataFilter::WriteAttribute(const chip::app::DataModel::WriteAttributeRequest & request,
                               chip::app::AttributeValueDecoder & decoder)
{
    return mInner->WriteAttribute(request, decoder);
}

void MetadataFilter::ListAttributeWriteNotification(const chip::app::ConcreteAttributePath & aPath,
                                                    chip::app::DataModel::ListWriteOperation opType)
{
    mInner->ListAttributeWriteNotification(aPath, opType);
}

std::optional<chip::app::DataModel::ActionReturnStatus> MetadataFilter::InvokeCommand(
    const chip::app::DataModel::InvokeRequest & request, chip::TLV::TLVReader & input_arguments,
    chip::app::CommandHandler * handler)
{
    return mInner->InvokeCommand(request, input_arguments, handler);
}

CHIP_ERROR MetadataFilter::Endpoints(chip::ReadOnlyBufferBuilder<chip::app::DataModel::EndpointEntry> & builder)
{
    return mInner->Endpoints(builder);
}

CHIP_ERROR MetadataFilter::SemanticTags(
    chip::EndpointId endpointId,
    chip::ReadOnlyBufferBuilder<chip::app::DataModel::ProviderMetadataTree::SemanticTag> & builder)
{
    return mInner->SemanticTags(endpointId, builder);
}

CHIP_ERROR MetadataFilter::DeviceTypes(chip::EndpointId endpointId,
                                       chip::ReadOnlyBufferBuilder<chip::app::DataModel::DeviceTypeEntry> & builder)
{
    return mInner->DeviceTypes(endpointId, builder);
}

CHIP_ERROR MetadataFilter::ClientClusters(chip::EndpointId endpointId,
                                          chip::ReadOnlyBufferBuilder<chip::ClusterId> & builder)
{
    return mInner->ClientClusters(endpointId, builder);
}

CHIP_ERROR MetadataFilter::ServerClusters(
    chip::EndpointId endpointId, chip::ReadOnlyBufferBuilder<chip::app::DataModel::ServerClusterEntry> & builder)
{
    return mInner->ServerClusters(endpointId, builder);
}

CHIP_ERROR MetadataFilter::EventInfo(const chip::app::ConcreteEventPath & path,
                                     chip::app::DataModel::EventEntry & eventInfo)
{
    return mInner->EventInfo(path, eventInfo);
}

#if CHIP_CONFIG_USE_ENDPOINT_UNIQUE_ID
CHIP_ERROR MetadataFilter::EndpointUniqueID(chip::EndpointId endpointId, chip::MutableCharSpan & EndpointUniqueId)
{
    return mInner->EndpointUniqueID(endpointId, EndpointUniqueId);
}
#endif

CHIP_ERROR MetadataFilter::Attributes(const chip::app::ConcreteClusterPath & path,
                                      chip::ReadOnlyBufferBuilder<chip::app::DataModel::AttributeEntry> & builder)
{
    CHIP_ERROR err = mInner->Attributes(path, builder);
    ReturnErrorOnFailure(err);

    if ((path.mEndpointId != kRootEndpoint) || (path.mClusterId != kBasicInformationClusterId))
    {
        return CHIP_NO_ERROR;
    }

    auto buffer = builder.TakeBuffer();
    if (buffer.data() == nullptr)
    {
        return CHIP_NO_ERROR;
    }

    ReturnErrorOnFailure(builder.EnsureAppendCapacity(buffer.size()));

    for (size_t i = 0; i < buffer.size(); ++i)
    {
        const auto & entry = buffer.data()[i];
        if (!ShouldFilter(path.mEndpointId, path.mClusterId, entry.attributeId))
        {
            ReturnErrorOnFailure(builder.Append(entry));
        }
    }

    return CHIP_NO_ERROR;
}

CHIP_ERROR MetadataFilter::GeneratedCommands(const chip::app::ConcreteClusterPath & path,
                                             chip::ReadOnlyBufferBuilder<chip::CommandId> & builder)
{
    return mInner->GeneratedCommands(path, builder);
}

CHIP_ERROR MetadataFilter::AcceptedCommands(
    const chip::app::ConcreteClusterPath & path,
    chip::ReadOnlyBufferBuilder<chip::app::DataModel::AcceptedCommandEntry> & builder)
{
    return mInner->AcceptedCommands(path, builder);
}

void MetadataFilter::Temporary_ReportAttributeChanged(const chip::app::AttributePathParams & path)
{
    mInner->Temporary_ReportAttributeChanged(path);
}

} // namespace ep0
} // namespace matter
