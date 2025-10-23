#include "app/icdm/IcdmAttrAccess.h"

#include <app/AttributeAccessInterface.h>
#include <app/AttributeAccessInterfaceRegistry.h>
#include <app/AttributeValueEncoder.h>
#include <app/ConcreteAttributePath.h>
#include <app/util/attribute-storage.h>
#include <lib/core/CHIPError.h>
#include <lib/core/Optional.h>
#include <lib/support/CodeUtils.h>
#include <protocols/interaction_model/StatusCode.h>
#include <zephyr/logging/log.h>
#include <inttypes.h>

LOG_MODULE_DECLARE(soil_app, LOG_LEVEL_INF);

using namespace chip;
using namespace chip::app;

namespace {

constexpr EndpointId kIcdmEndpoint = 0x00;      // Endpoint 0
constexpr ClusterId kIcdmCluster   = 0x0046;    // ICD Management cluster

constexpr AttributeId kIdleModeIntervalId      = 0x0000;
constexpr AttributeId kActiveModeIntervalId    = 0x0001;
constexpr AttributeId kActiveModeThresholdId   = 0x0002;
constexpr AttributeId kCompatActiveInterval    = 0x0006;
constexpr AttributeId kCompatActiveThreshold   = 0x0007;
constexpr AttributeId kGeneratedCommandListId  = 0xFFF8;
constexpr AttributeId kAcceptedCommandListId   = 0xFFF9;
constexpr AttributeId kEventListId             = 0xFFFA;
constexpr AttributeId kAttributeListId         = 0xFFFB;
constexpr AttributeId kFeatureMapId            = 0xFFFC;
constexpr AttributeId kClusterRevisionId       = 0xFFFD;
constexpr AttributeId kAttributeListContents[] = { kIdleModeIntervalId,     kActiveModeIntervalId,    kActiveModeThresholdId,
                                                   kCompatActiveInterval,   kCompatActiveThreshold,   kFeatureMapId,
                                                   kClusterRevisionId,      kAttributeListId,         kEventListId,
                                                   kAcceptedCommandListId,  kGeneratedCommandListId };

constexpr uint32_t kIdleModeSeconds     = 300;
constexpr uint32_t kActiveModeMs        = 500;
constexpr uint32_t kThresholdMs         = 500;
constexpr uint32_t kFeatureMap          = 0;
// TODO: replace with generated cluster revision constant if/when exposed
constexpr uint16_t kIcdmClusterRevision = 3;

class IcdmAttrAccess : public AttributeAccessInterface
{
public:
    IcdmAttrAccess() : AttributeAccessInterface(MakeOptional(kIcdmEndpoint), kIcdmCluster) {}

    CHIP_ERROR Read(const ConcreteReadAttributePath & path, AttributeValueEncoder & encoder) override
    {
        const AttributeId attr = path.mAttributeId;
        LOG_INF("ICDM Read attr=0x%04" PRIx32, static_cast<uint32_t>(attr));

        switch (attr)
        {
        case kIdleModeIntervalId:
            // LOG_INF("ICDM IdleModeInterval read");
            return encoder.Encode(kIdleModeSeconds);
        case kActiveModeIntervalId:
            // LOG_INF("ICDM ActiveModeInterval read");
            return encoder.Encode(kActiveModeMs);
        case kCompatActiveInterval:
            // LOG_INF("ICDM ActiveModeInterval compat read");
            return encoder.Encode(kActiveModeMs);
        case kActiveModeThresholdId:
            // LOG_INF("ICDM ActiveModeThreshold read");
            return encoder.Encode(kThresholdMs);
        case kCompatActiveThreshold:
            // LOG_INF("ICDM ActiveModeThreshold compat read");
            return encoder.Encode(kThresholdMs);
        case kFeatureMapId:
            return encoder.Encode(kFeatureMap);
        case kClusterRevisionId:
            return encoder.Encode(kIcdmClusterRevision);
        case kAttributeListId:
            return encoder.EncodeList([](const auto & listEncoder) -> CHIP_ERROR {
                for (AttributeId attrId : kAttributeListContents)
                {
                    ReturnErrorOnFailure(listEncoder.Encode(attrId));
                }
                return CHIP_NO_ERROR;
            });
        case kEventListId:
            return encoder.EncodeList([](const auto &) -> CHIP_ERROR { return CHIP_NO_ERROR; });
        case kAcceptedCommandListId:
            return encoder.EncodeList([](const auto &) -> CHIP_ERROR { return CHIP_NO_ERROR; });
        case kGeneratedCommandListId:
            return encoder.EncodeList([](const auto &) -> CHIP_ERROR { return CHIP_NO_ERROR; });
        default:
            return CHIP_IM_GLOBAL_STATUS(UnsupportedAttribute);
        }
    }
};

static IcdmAttrAccess gIcdmAttrAccess;

} // namespace

extern "C" void AppInit_RegisterIcdmAttrAccess()
{
    AttributeAccessInterfaceRegistry::Instance().Register(&gIcdmAttrAccess);
}
