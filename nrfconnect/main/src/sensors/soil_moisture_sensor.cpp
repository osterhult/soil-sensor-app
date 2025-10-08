#include "sensors/soil_moisture_sensor.h"

#include <app-common/zap-generated/cluster-objects.h>
#include <app/clusters/soil-measurement-server/soil-measurement-cluster.h>
#include <app/server-cluster/ServerClusterInterfaceRegistry.h>
#include <data-model-providers/codegen/CodegenDataModelProvider.h>
#include <crypto/RandUtils.h>
#include <data-model-providers/codegen/Instance.h>
#include <lib/core/Optional.h>
#include <platform/CHIPDeviceLayer.h>
#include <system/SystemClock.h>

namespace sensors
{
namespace soil_moisture_sensor
{

namespace
{

static chip::app::LazyRegisteredServerCluster<chip::app::Clusters::SoilMeasurementCluster> sSoilCluster;
constexpr chip::EndpointId kSoilEndpoint = 1;
constexpr uint32_t kSoilUpdateMs         = 5000;
uint8_t gSoilLast                        = 101; // invalid sentinel so first update always changes

void SoilUpdateTimer(chip::System::Layer * layer, void *)
{
    uint8_t v = static_cast<uint8_t>(chip::Crypto::GetRandU16() % 101);
    if (v == gSoilLast)
    {
        v = static_cast<uint8_t>((v + 1) % 101);
    }
    chip::app::DataModel::Nullable<chip::Percent> measured;
    measured.SetNonNull(v);
    (void) sSoilCluster.Cluster().SetSoilMoistureMeasuredValue(measured);
    gSoilLast = v;
    if (layer)
    {
        (void) layer->StartTimer(chip::System::Clock::Milliseconds32(kSoilUpdateMs), SoilUpdateTimer, nullptr);
    }
}

} // namespace

void Init()
{
    using LimitsType = chip::app::Clusters::SoilMeasurement::Attributes::SoilMoistureMeasurementLimits::TypeInfo::Type;
    using RangeType  = chip::app::Clusters::Globals::Structs::MeasurementAccuracyRangeStruct::Type;

    static const RangeType kRanges[] = {
        {
            .rangeMin   = 0,
            .rangeMax   = 100,
            .percentMax = chip::MakeOptional(static_cast<chip::Percent100ths>(5)) // 5%
        },
    };

    const LimitsType limits = {
        .measurementType  = chip::app::Clusters::Globals::MeasurementTypeEnum::kSoilMoisture,
        .measured         = true,
        .minMeasuredValue = 0,
        .maxMeasuredValue = 100,
        .accuracyRanges   = chip::app::DataModel::List<const RangeType>(kRanges),
    };

    sSoilCluster.Create(kSoilEndpoint, limits);
    (void) chip::app::CodegenDataModelProvider::Instance().Registry().Register(sSoilCluster.Registration());

    SoilUpdateTimer(nullptr, nullptr);
    (void) chip::DeviceLayer::SystemLayer().StartTimer(chip::System::Clock::Milliseconds32(kSoilUpdateMs), SoilUpdateTimer, nullptr);
}

} // namespace soil_moisture_sensor
} // namespace sensors
