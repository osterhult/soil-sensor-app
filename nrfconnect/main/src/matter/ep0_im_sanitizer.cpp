#include "ep0_im_sanitizer.h"

#include <app-common/zap-generated/cluster-objects.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/AttributeAccessInterfaceRegistry.h>
#include <app/AttributeValueEncoder.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <lib/support/CodeUtils.h>
#include <lib/support/Span.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/ConfigurationManager.h>
#include <protocols/interaction_model/Constants.h>

namespace matter {
namespace ep0 {
namespace {

using chip::app::AttributeValueEncoder;
using chip::app::ConcreteReadAttributePath;

constexpr chip::EndpointId kEp0 = 0;

constexpr chip::ClusterId kBasicInfoCluster    = chip::app::Clusters::BasicInformation::Id;
constexpr chip::ClusterId kDescriptorCluster   = chip::app::Clusters::Descriptor::Id;
constexpr chip::ClusterId kTimeSyncCluster     = chip::app::Clusters::TimeSynchronization::Id;
constexpr chip::ClusterId kIcdCluster          = static_cast<chip::ClusterId>(0x0046);
constexpr chip::ClusterId kGroupKeyMgmtCluster = chip::app::Clusters::GroupKeyManagement::Id;

constexpr chip::AttributeId kFeatureMapId       = 0xFFFC;
constexpr chip::AttributeId kClusterRevisionId  = 0xFFFD;
constexpr chip::AttributeId kGeneratedCmdListId = 0xFFF8;
constexpr chip::AttributeId kAcceptedCmdListId  = 0xFFF9;
constexpr chip::AttributeId kAttributeListId    = 0xFFFB;

constexpr chip::AttributeId kBasicInfoHiddenAttr = 0x0018;
constexpr chip::AttributeId kBasicInfoUniqueId   = 0x0012;

constexpr chip::AttributeId kTimeSyncUtcTime     = 0x0000;
constexpr chip::AttributeId kTimeSyncGranularity = 0x0001;
constexpr chip::AttributeId kTimeSyncTimeSource  = 0x0002;

constexpr chip::AttributeId kIcdIdleModeDuration    = 0x0000;
constexpr chip::AttributeId kIcdActiveModeDuration  = 0x0001;
constexpr chip::AttributeId kIcdActiveModeThreshold = 0x0002;

constexpr chip::AttributeId kDescriptorDeviceTypeList = 0x0000;
constexpr chip::AttributeId kDescriptorServerList     = 0x0001;
constexpr chip::AttributeId kDescriptorClientList     = 0x0002;
constexpr chip::AttributeId kDescriptorPartsList      = 0x0003;

constexpr chip::CommandId kTimeSyncSetUtcTime = 0x0000;

constexpr uint16_t kIcdClusterRevision  = 3;
constexpr uint16_t kGkmClusterRevision  = 2;
constexpr uint16_t kDescriptorClusterRevision = 2;

constexpr chip::DeviceTypeId kRootDeviceType = static_cast<chip::DeviceTypeId>(0x0016);

constexpr chip::ClusterId kRootServerClusters[] = {
    chip::app::Clusters::Descriptor::Id,
    chip::app::Clusters::AccessControl::Id,
    chip::app::Clusters::BasicInformation::Id,
    chip::app::Clusters::LocalizationConfiguration::Id,
    chip::app::Clusters::GeneralCommissioning::Id,
    chip::app::Clusters::NetworkCommissioning::Id,
    chip::app::Clusters::GeneralDiagnostics::Id,
    chip::app::Clusters::AdministratorCommissioning::Id,
    chip::app::Clusters::OperationalCredentials::Id,
    chip::app::Clusters::GroupKeyManagement::Id,
    chip::app::Clusters::TimeSynchronization::Id,
    kIcdCluster,
};

constexpr chip::EndpointId kRootPartsList[] = { 1 };

template <typename T>
CHIP_ERROR EncodeSimpleList(AttributeValueEncoder & aEncoder, const T * values, size_t count)
{
    return aEncoder.EncodeList([&](auto && encoder) -> CHIP_ERROR {
        for (size_t i = 0; i < count; ++i)
        {
            ReturnErrorOnFailure(encoder.Encode(values[i]));
        }
        return CHIP_NO_ERROR;
    });
}

CHIP_ERROR EncodeCommandList(AttributeValueEncoder & aEncoder, const chip::CommandId * cmds, size_t count)
{
    return aEncoder.EncodeList([&](auto && encoder) -> CHIP_ERROR {
        for (size_t i = 0; i < count; ++i)
        {
            ReturnErrorOnFailure(encoder.Encode(cmds[i]));
        }
        return CHIP_NO_ERROR;
    });
}

CHIP_ERROR EncodeEmptyList(AttributeValueEncoder & aEncoder)
{
    return aEncoder.EncodeList([](auto && /*encoder*/) -> CHIP_ERROR { return CHIP_NO_ERROR; });
}

} // namespace

AttrListSanitizer::AttrListSanitizer(chip::ClusterId clusterId) :
    AttributeAccessInterface(chip::MakeOptional(kEp0), clusterId), mClusterId(clusterId)
{}

bool AttrListSanitizer::IsEp0(const ConcreteReadAttributePath & aPath) const
{
    return aPath.mEndpointId == kEp0;
}

CHIP_ERROR AttrListSanitizer::Read(const ConcreteReadAttributePath & aPath, AttributeValueEncoder & aEncoder)
{
    if (!IsEp0(aPath) || aPath.mClusterId != mClusterId)
    {
        return CHIP_NO_ERROR;
    }

    if (mClusterId == kBasicInfoCluster)
    {
        if (aPath.mAttributeId == kBasicInfoHiddenAttr)
        {
            return CHIP_IM_GLOBAL_STATUS(UnsupportedAttribute);
        }

        if (aPath.mAttributeId == kBasicInfoUniqueId)
        {
            char uniqueId[chip::DeviceLayer::ConfigurationManager::kMaxUniqueIDLength + 1] = {};
            CHIP_ERROR err                                                                    =
                chip::DeviceLayer::ConfigurationMgr().GetUniqueId(uniqueId, sizeof(uniqueId));

            if (err == CHIP_NO_ERROR && uniqueId[0] != '\0')
            {
                return aEncoder.Encode(chip::CharSpan(uniqueId, strlen(uniqueId)));
            }

            static constexpr char kFallbackUniqueId[] = "soil-sensor-uid";
            return aEncoder.Encode(chip::CharSpan(kFallbackUniqueId, strlen(kFallbackUniqueId)));
        }

        if (aPath.mAttributeId == kAttributeListId)
        {
            constexpr chip::AttributeId kAttributes[] = {
                0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007, 0x0008, 0x0009,
                0x000A, 0x0012, 0x0013, 0x0015, 0x0016,
                kGeneratedCmdListId, kAcceptedCmdListId, kAttributeListId, kClusterRevisionId,
            };

            return EncodeSimpleList(aEncoder, kAttributes, sizeof(kAttributes) / sizeof(kAttributes[0]));
        }

        return CHIP_NO_ERROR;
    }

    if (mClusterId == kDescriptorCluster)
    {
        switch (aPath.mAttributeId)
        {
        case kDescriptorDeviceTypeList: {
            using DeviceTypeStruct = chip::app::Clusters::Descriptor::Structs::DeviceTypeStruct::Type;
            DeviceTypeStruct deviceType;
            deviceType.deviceType = kRootDeviceType;
            deviceType.revision   = 1;
            return aEncoder.EncodeList([&](auto && encoder) -> CHIP_ERROR {
                ReturnErrorOnFailure(encoder.Encode(deviceType));
                return CHIP_NO_ERROR;
            });
        }
        case kDescriptorServerList:
            return EncodeSimpleList(aEncoder, kRootServerClusters, sizeof(kRootServerClusters) / sizeof(kRootServerClusters[0]));
        case kDescriptorClientList:
            return EncodeEmptyList(aEncoder);
        case kDescriptorPartsList:
            return EncodeSimpleList(aEncoder, kRootPartsList, sizeof(kRootPartsList) / sizeof(kRootPartsList[0]));
        case kFeatureMapId:
            return aEncoder.Encode(static_cast<uint32_t>(0));
        case kGeneratedCmdListId:
        case kAcceptedCmdListId:
            return EncodeEmptyList(aEncoder);
        case kAttributeListId: {
            constexpr chip::AttributeId kAttributes[] = {
                kDescriptorDeviceTypeList, kDescriptorServerList, kDescriptorClientList, kDescriptorPartsList,
                kGeneratedCmdListId, kAcceptedCmdListId, kAttributeListId, kFeatureMapId, kClusterRevisionId,
            };
            return EncodeSimpleList(aEncoder, kAttributes, sizeof(kAttributes) / sizeof(kAttributes[0]));
        }
        case kClusterRevisionId:
            return aEncoder.Encode(kDescriptorClusterRevision);
        default:
            return CHIP_NO_ERROR;
        }
    }

    if (mClusterId == kTimeSyncCluster)
    {
        switch (aPath.mAttributeId)
        {
        case kTimeSyncUtcTime:
            return aEncoder.EncodeNull();
        case kTimeSyncGranularity:
            return aEncoder.Encode(static_cast<uint8_t>(0));
        case kTimeSyncTimeSource:
            return aEncoder.Encode(static_cast<uint8_t>(0));
        case kFeatureMapId:
            return aEncoder.Encode(static_cast<uint32_t>(0));
        case kAttributeListId: {
            constexpr chip::AttributeId kAttributes[] = {
                kTimeSyncUtcTime, kTimeSyncGranularity, kTimeSyncTimeSource,
                kGeneratedCmdListId, kAcceptedCmdListId, kFeatureMapId, kClusterRevisionId, kAttributeListId,
            };
            return EncodeSimpleList(aEncoder, kAttributes, sizeof(kAttributes) / sizeof(kAttributes[0]));
        }
        case kGeneratedCmdListId:
            return EncodeEmptyList(aEncoder);
        case kAcceptedCmdListId: {
            constexpr chip::CommandId kCommands[] = { kTimeSyncSetUtcTime };
            return EncodeCommandList(aEncoder, kCommands, sizeof(kCommands) / sizeof(kCommands[0]));
        }
        default:
            return CHIP_NO_ERROR;
        }
    }

    if (mClusterId == kIcdCluster)
    {
        switch (aPath.mAttributeId)
        {
        case kIcdIdleModeDuration:
            return aEncoder.Encode(static_cast<uint32_t>(0));
        case kIcdActiveModeDuration:
            return aEncoder.Encode(static_cast<uint32_t>(0));
        case kIcdActiveModeThreshold:
            return aEncoder.Encode(static_cast<uint16_t>(0));
        case kClusterRevisionId:
            return aEncoder.Encode(kIcdClusterRevision);
        case kFeatureMapId:
            return aEncoder.Encode(static_cast<uint32_t>(0));
        case kAttributeListId: {
            constexpr chip::AttributeId kAttributes[] = {
                kIcdIdleModeDuration, kIcdActiveModeDuration, kIcdActiveModeThreshold,
                kGeneratedCmdListId, kAcceptedCmdListId, kFeatureMapId, kClusterRevisionId, kAttributeListId,
            };
            return EncodeSimpleList(aEncoder, kAttributes, sizeof(kAttributes) / sizeof(kAttributes[0]));
        }
        case kGeneratedCmdListId:
        case kAcceptedCmdListId:
            return EncodeEmptyList(aEncoder);
        default:
            return CHIP_NO_ERROR;
        }
    }

    if (mClusterId == kGroupKeyMgmtCluster)
    {
        if (aPath.mAttributeId == kClusterRevisionId)
        {
            return aEncoder.Encode(kGkmClusterRevision);
        }
        return CHIP_NO_ERROR;
    }

    return CHIP_NO_ERROR;
}

void Register()
{
    static AttrListSanitizer sBasicInfo(kBasicInfoCluster);
    static AttrListSanitizer sDescriptor(kDescriptorCluster);
    static AttrListSanitizer sTimeSync(kTimeSyncCluster);
    static AttrListSanitizer sIcd(kIcdCluster);
    static AttrListSanitizer sGkm(kGroupKeyMgmtCluster);

    auto & registry = chip::app::AttributeAccessInterfaceRegistry::Instance();

    if (!registry.Register(&sBasicInfo))
    {
        ChipLogError(Zcl, "BasicInformation sanitizer already registered");
    }
    else
    {
        ChipLogProgress(Zcl, "BasicInformation sanitizer registered");
    }

    if (!registry.Register(&sDescriptor))
    {
        ChipLogError(Zcl, "Descriptor sanitizer already registered");
    }
    else
    {
        ChipLogProgress(Zcl, "Descriptor sanitizer registered");
    }

    if (!registry.Register(&sTimeSync))
    {
        ChipLogError(Zcl, "TimeSync sanitizer already registered");
    }
    else
    {
        ChipLogProgress(Zcl, "TimeSync sanitizer registered");
    }

    if (!registry.Register(&sIcd))
    {
        ChipLogError(Zcl, "ICD sanitizer already registered");
    }
    else
    {
        ChipLogProgress(Zcl, "ICD sanitizer registered");
    }

    if (!registry.Register(&sGkm))
    {
        ChipLogError(Zcl, "GroupKeyManagement sanitizer already registered");
    }
    else
    {
        ChipLogProgress(Zcl, "GroupKeyManagement sanitizer registered");
    }
}

} // namespace ep0
} // namespace matter
