#include <app/reporting/reporting.h>

#include "include/soil_measurement_access.h"

using namespace chip;
using namespace chip::app;
using namespace chip::app::Clusters;

namespace {
constexpr EndpointId kSoilEndpoint = 1;
} // anonymous

SoilAttrAccess::SoilAttrAccess() :
    AttributeAccessInterface(MakeOptional(kSoilEndpoint),
                             SoilMeasurement::Id /*0x0430*/) {}

CHIP_ERROR SoilAttrAccess::Read(const ConcreteReadAttributePath & path,
                                AttributeValueEncoder & encoder)
{
    if (path.mAttributeId ==
        SoilMeasurement::Attributes::SoilMoistureMeasuredValue::Id)
    {
        return encoder.Encode(
            DataModel::Nullable<uint16_t>(ReadSoilMoistureSensor()));
    }
    // Let the framework handle the other attributes (FeatureMap etc.)
    return CHIP_NO_ERROR;
}

void SoilAttrAccess::ReportIfChanged(uint16_t newPercent)
{
    if (newPercent == mLastPercent) return;

    mLastPercent = newPercent;

    MatterReportingAttributeChangeCallback(
        kSoilEndpoint,
        SoilMeasurement::Id,
        SoilMeasurement::Attributes::SoilMoistureMeasuredValue::Id);
}

// ---------- global singleton ----------
SoilAttrAccess gSoilAttrAccess;