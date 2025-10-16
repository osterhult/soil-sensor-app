#include <app/AttributeAccessInterface.h>
#include <app/AttributeAccessInterfaceRegistry.h>
#include <app/AttributeValueEncoder.h>
#include <app/ConcreteAttributePath.h>
#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/util/attribute-storage.h>
#include <app/util/attribute-table.h>
#include <lib/core/CHIPError.h>
#include <protocols/interaction_model/Constants.h>
#include <lib/support/logging/CHIPLogging.h>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;

namespace {

class GkmClusterRevisionOverride : public AttributeAccessInterface
{
public:
    GkmClusterRevisionOverride() : AttributeAccessInterface(MakeOptional<EndpointId>(0), GroupKeyManagement::Id) {}

    CHIP_ERROR Read(const ConcreteReadAttributePath & path, AttributeValueEncoder & encoder) override
    {
        constexpr AttributeId kClusterRevisionId = Globals::Attributes::ClusterRevision::Id;

        if (path.mAttributeId == kClusterRevisionId)
        {
            uint16_t rev = 2;
            ChipLogProgress(AppServer, "Override: EP%u GroupKeyManagement ClusterRevision -> %u",
                            static_cast<unsigned>(path.mEndpointId), rev);
            return encoder.Encode(rev);
        }
        return CHIP_NO_ERROR;
    }
};

GkmClusterRevisionOverride gGkmRevOverride;

} // namespace

extern "C" void MatterAppPlatform_RegisterGkmRevisionOverride()
{
    if (!AttributeAccessInterfaceRegistry::Instance().Register(&gGkmRevOverride))
    {
        ChipLogError(AppServer, "GroupKeyManagement revision override already registered");
        return;
    }

    constexpr EndpointId kEndpoint           = 0;
    constexpr ClusterId kCluster             = GroupKeyManagement::Id;
    constexpr AttributeId kClusterRevisionId = Globals::Attributes::ClusterRevision::Id;
    uint16_t rev                              = 2;
    auto status = emberAfWriteAttribute(kEndpoint, kCluster, kClusterRevisionId, reinterpret_cast<uint8_t *>(&rev),
                                        ZCL_INT16U_ATTRIBUTE_TYPE);
    if (status != Protocols::InteractionModel::Status::Success)
    {
        ChipLogError(AppServer, "Failed to seed GroupKeyManagement ClusterRevision attribute (%u)", static_cast<unsigned>(status));
    }
}
