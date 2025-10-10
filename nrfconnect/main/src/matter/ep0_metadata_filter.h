#pragma once

#include <app/data-model-provider/Provider.h>
#include <optional>

namespace matter {
namespace ep0 {

class MetadataFilter final : public chip::app::DataModel::Provider
{
public:
    explicit MetadataFilter(chip::app::DataModel::Provider & inner);

    // DataModel::Provider overrides
    CHIP_ERROR Startup(chip::app::DataModel::InteractionModelContext context) override;
    CHIP_ERROR Shutdown() override;

    chip::app::DataModel::ActionReturnStatus ReadAttribute(const chip::app::DataModel::ReadAttributeRequest & request,
                                                           chip::app::AttributeValueEncoder & encoder) override;
    chip::app::DataModel::ActionReturnStatus WriteAttribute(const chip::app::DataModel::WriteAttributeRequest & request,
                                                            chip::app::AttributeValueDecoder & decoder) override;
    void ListAttributeWriteNotification(const chip::app::ConcreteAttributePath & aPath,
                                        chip::app::DataModel::ListWriteOperation opType) override;
    std::optional<chip::app::DataModel::ActionReturnStatus> InvokeCommand(
        const chip::app::DataModel::InvokeRequest & request, chip::TLV::TLVReader & input_arguments,
        chip::app::CommandHandler * handler) override;

    // ProviderMetadataTree overrides
    CHIP_ERROR Endpoints(chip::ReadOnlyBufferBuilder<chip::app::DataModel::EndpointEntry> & builder) override;
    CHIP_ERROR SemanticTags(chip::EndpointId endpointId,
                            chip::ReadOnlyBufferBuilder<chip::app::DataModel::ProviderMetadataTree::SemanticTag> & builder) override;
    CHIP_ERROR DeviceTypes(chip::EndpointId endpointId,
                           chip::ReadOnlyBufferBuilder<chip::app::DataModel::DeviceTypeEntry> & builder) override;
    CHIP_ERROR ClientClusters(chip::EndpointId endpointId,
                              chip::ReadOnlyBufferBuilder<chip::ClusterId> & builder) override;
    CHIP_ERROR ServerClusters(chip::EndpointId endpointId,
                              chip::ReadOnlyBufferBuilder<chip::app::DataModel::ServerClusterEntry> & builder) override;
    CHIP_ERROR EventInfo(const chip::app::ConcreteEventPath & path,
                         chip::app::DataModel::EventEntry & eventInfo) override;
#if CHIP_CONFIG_USE_ENDPOINT_UNIQUE_ID
    CHIP_ERROR EndpointUniqueID(chip::EndpointId endpointId, chip::MutableCharSpan & EndpointUniqueId) override;
#endif
    CHIP_ERROR Attributes(const chip::app::ConcreteClusterPath & path,
                          chip::ReadOnlyBufferBuilder<chip::app::DataModel::AttributeEntry> & builder) override;
    CHIP_ERROR GeneratedCommands(const chip::app::ConcreteClusterPath & path,
                                 chip::ReadOnlyBufferBuilder<chip::CommandId> & builder) override;
    CHIP_ERROR AcceptedCommands(const chip::app::ConcreteClusterPath & path,
                                chip::ReadOnlyBufferBuilder<chip::app::DataModel::AcceptedCommandEntry> & builder) override;
    void Temporary_ReportAttributeChanged(const chip::app::AttributePathParams & path) override;

private:
    bool ShouldFilter(chip::EndpointId endpointId, chip::ClusterId clusterId, chip::AttributeId attributeId) const;

    chip::app::DataModel::Provider * mInner;
};

} // namespace ep0
} // namespace matter
