// nrfconnect/main/src/matter/icdm_attr_access.cpp
#include <app/AttributeAccessInterface.h>
#include <app/AttributeAccessInterfaceRegistry.h>
#include <app/AttributeValueEncoder.h>
#include <app/ConcreteAttributePath.h>
#include <app/util/attribute-storage.h>
#include <lib/core/CHIPError.h>

using namespace chip;
using namespace chip::app;

namespace {

constexpr EndpointId kIcdmEndpoint = 0x00;    // Endpoint 0 (bridged node)
constexpr ClusterId kIcdmCluster  = 0x0046;   // ICD Management cluster

constexpr AttributeId kIdleModeInterval     = 0x0000;
constexpr AttributeId kActiveModeInterval   = 0x0006;
constexpr AttributeId kActiveModeThreshold  = 0x0007;

class IcdmAttrAccess : public AttributeAccessInterface
{
public:
    IcdmAttrAccess() : AttributeAccessInterface(Optional<EndpointId>(kIcdmEndpoint), kIcdmCluster) {}

    CHIP_ERROR Read(const ConcreteReadAttributePath & path, AttributeValueEncoder & encoder) override
    {
        switch (path.mAttributeId)
        {
        case kIdleModeInterval: {
            constexpr uint32_t kIdleSeconds = 300; // Within [1, 64800] as per spec
            return encoder.Encode(kIdleSeconds);
        }
        case kActiveModeInterval: {
            constexpr uint32_t kActiveMs = 500; // Meets >= 300 ms requirement
            return encoder.Encode(kActiveMs);
        }
        case kActiveModeThreshold: {
            constexpr uint32_t kThresholdMs = 500; // Meets >= 300 ms requirement
            return encoder.Encode(kThresholdMs);
        }
        default:
            // Defer to generated/default handlers for all other attributes.
            return CHIP_NO_ERROR;
        }
    }
};

IcdmAttrAccess gIcdmAttrAccess;
} // namespace

extern "C" void AppInit_RegisterIcdmAttrAccess()
{
    AttributeAccessInterfaceRegistry::Instance().Register(&gIcdmAttrAccess);
}
