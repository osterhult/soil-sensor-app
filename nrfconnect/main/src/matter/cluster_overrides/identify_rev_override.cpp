#include <app/AttributeAccessInterface.h>
#include <app/AttributeAccessInterfaceRegistry.h>
#include <app/ConcreteAttributePath.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <lib/support/logging/CHIPLogging.h>

namespace matter {
namespace cluster_overrides {
namespace {

constexpr chip::EndpointId kDefaultEndpoint = 1;
constexpr chip::ClusterId kClusterId        = chip::app::Clusters::Identify::Id;
constexpr chip::AttributeId kClusterRevision = 0xFFFD;

class IdentifyRevisionOverride final : public chip::app::AttributeAccessInterface
{
public:
    explicit IdentifyRevisionOverride(chip::EndpointId endpoint) :
        chip::app::AttributeAccessInterface(chip::MakeOptional(endpoint), kClusterId), mEndpoint(endpoint)
    {}

    CHIP_ERROR Read(const chip::app::ConcreteReadAttributePath & path,
                    chip::app::AttributeValueEncoder & encoder) override
    {
        if (path.mEndpointId != mEndpoint || path.mClusterId != kClusterId)
        {
            return CHIP_NO_ERROR;
        }

        if (path.mAttributeId == kClusterRevision)
        {
            return encoder.Encode(static_cast<uint16_t>(6));
        }

        return CHIP_NO_ERROR;
    }

private:
    chip::EndpointId mEndpoint;
};

IdentifyRevisionOverride & GetInterface(chip::EndpointId endpoint)
{
    static IdentifyRevisionOverride sDefaultInterface(kDefaultEndpoint);
    if (endpoint == kDefaultEndpoint)
    {
        return sDefaultInterface;
    }

    static IdentifyRevisionOverride sAltInterface(endpoint);
    return sAltInterface;
}

} // namespace

void RegisterIdentifyRevisionOverride(chip::EndpointId endpoint)
{
    IdentifyRevisionOverride & iface = GetInterface(endpoint);

    if (!chip::app::AttributeAccessInterfaceRegistry::Instance().Register(&iface))
    {
        ChipLogError(Zcl, "Identify revision override already registered for endpoint %u", static_cast<unsigned>(endpoint));
    }
}

} // namespace cluster_overrides
} // namespace matter

