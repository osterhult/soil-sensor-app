#include <app/AttributeAccessInterface.h>
#include <app/AttributeAccessInterfaceRegistry.h>
#include <protocols/interaction_model/StatusCode.h>
#include <app/clusters/general-commissioning-server/general-commissioning-server.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;

namespace
{
constexpr EndpointId kRootEp              = 0;
constexpr ClusterId kGenCommCluster       = GeneralCommissioning::Id;
constexpr AttributeId kAttrA000C          = 0x000C;
constexpr AttributeId kAttrListId         = 0xFFFB;
constexpr AttributeId kGeneratedCmdListId = 0xFFB8;
constexpr AttributeId kAcceptedCmdListId  = 0xFFB9;
constexpr AttributeId kFeatureMapId       = 0xFFFC;
constexpr AttributeId kClusterRevisionId  = 0xFFFD;

constexpr AttributeId kAllowedAttrList[] = {
    0x0000, 0x0001, 0x0002, 0x0003, 0x0004, kGeneratedCmdListId, kAcceptedCmdListId, kAttrListId, kFeatureMapId,
    kClusterRevisionId,
};

class GenCommAttrBlocker : public AttributeAccessInterface
{
public:
    GenCommAttrBlocker() : AttributeAccessInterface(MakeOptional(kRootEp), kGenCommCluster) {}

    CHIP_ERROR Read(const ConcreteReadAttributePath & path, AttributeValueEncoder & encoder) override
    {
        if (path.mEndpointId == kRootEp && path.mClusterId == kGenCommCluster && path.mAttributeId == kAttrA000C)
        {
            ChipLogProgress(AppServer, "Blocking General Commissioning attribute 0x000C on EP%u",
                            static_cast<unsigned>(path.mEndpointId));
            return CHIP_IM_GLOBAL_STATUS(UnsupportedAttribute);
        }
        if (path.mEndpointId == kRootEp && path.mClusterId == kGenCommCluster && path.mAttributeId == kAttrListId)
        {
            return encoder.EncodeList([](const auto & listEncoder) -> CHIP_ERROR {
                for (AttributeId attr : kAllowedAttrList)
                {
                    ReturnErrorOnFailure(listEncoder.Encode(attr));
                }
                return CHIP_NO_ERROR;
            });
        }
        return CHIP_NO_ERROR;
    }
};

GenCommAttrBlocker gGenCommAttrBlocker;
} // namespace

extern "C" void RegisterGenCommAttrBlocker()
{
    chip::app::AttributeAccessInterfaceRegistry::Instance().Register(&gGenCommAttrBlocker);
}
