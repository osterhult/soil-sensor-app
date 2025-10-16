#include <app-common/zap-generated/ids/Clusters.h>
#include <app-common/zap-generated/ids/Attributes.h>
#include <app/util/attribute-table.h>
#include <inttypes.h>
#include <lib/core/CHIPError.h>
#include <lib/support/logging/CHIPLogging.h>
#include <protocols/interaction_model/Constants.h>

namespace {

CHIP_ERROR LogClusterRevision(chip::EndpointId ep, chip::ClusterId cluster, uint16_t expected)
{
    constexpr chip::AttributeId kClusterRevisionId = chip::app::Clusters::Globals::Attributes::ClusterRevision::Id;
    uint16_t rev                                    = 0;
    auto status =
        emberAfReadAttribute(ep, cluster, kClusterRevisionId, reinterpret_cast<uint8_t *>(&rev), sizeof(rev));
    if (status == chip::Protocols::InteractionModel::Status::Success)
    {
        ChipLogProgress(AppServer, "EP%u Cluster 0x%04" PRIx32 " ClusterRevision=%u (expect %u)",
                        static_cast<unsigned>(ep), static_cast<uint32_t>(cluster), rev, expected);
        if (rev != expected)
        {
            ChipLogError(AppServer, "Revision mismatch!");
            return CHIP_ERROR_INCORRECT_STATE;
        }
    }
    else
    {
        ChipLogError(AppServer, "Failed to read ClusterRevision for cluster 0x%04" PRIx32 " (IM status %u)",
                     static_cast<uint32_t>(cluster), static_cast<unsigned>(status));
    }
    return CHIP_NO_ERROR;
}

} // namespace

extern "C" void MatterAppPlatform_RevisionSanityCheck()
{
    // EP0 sanity list — adjust only if specs change.
    LogClusterRevision(0, chip::app::Clusters::GroupKeyManagement::Id, 2);
    LogClusterRevision(0, chip::app::Clusters::AccessControl::Id, 2);
    LogClusterRevision(0, chip::app::Clusters::Descriptor::Id, 3);
    LogClusterRevision(0, chip::app::Clusters::BasicInformation::Id, 5);
    LogClusterRevision(0, chip::app::Clusters::AdministratorCommissioning::Id, 1);
    LogClusterRevision(0, chip::app::Clusters::OperationalCredentials::Id, 2);
    LogClusterRevision(0, chip::app::Clusters::NetworkCommissioning::Id, 2);
    LogClusterRevision(0, chip::app::Clusters::GeneralDiagnostics::Id, 2);
    LogClusterRevision(0, chip::app::Clusters::GeneralCommissioning::Id, 2);
    LogClusterRevision(0, chip::app::Clusters::LocalizationConfiguration::Id, 1);
    LogClusterRevision(0, chip::app::Clusters::TimeSynchronization::Id, 2);
}
