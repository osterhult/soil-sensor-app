// // main/soil_measurement_nrf.cpp
// #include <app/util/attribute-storage.h>
// #include <app/reporting/reporting.h>
// #include <platform/CHIPDeviceLayer.h>
// #include <zephyr/kernel.h>

// // Generated headers for the SoilMeasurement cluster in 1.5
// #include <clusters/SoilMeasurement/Metadata.h>

// #include "include/SoilSensorManager.h"

// using namespace chip;
// using namespace chip::app;
// using namespace chip::app::Clusters;
// using namespace chip::DeviceLayer;

// namespace {
// constexpr EndpointId kEndpoint = 1;
// constexpr uint32_t kUpdateMs = 2000;

// SoilSensorManager gSensor;

// // The generated per-endpoint server wrapper for 0x0430 in 1.5:
// using Soil   = chip::app::Clusters::SoilMeasurement;
// using Limits = Soil::Structs::MeasurementAccuracyStruct::Type;
// using Range  = Soil::Structs::MeasurementAccuracyRangeStruct::Type;

// Soil::Instance * sInstance = nullptr;

// // Limits: 0–100% with a single accuracy range covering the whole span.
// // The MeasurementAccuracy* structs are shared across clusters;
// // fields generally include Min/MaxMeasuredValue and an accuracy ranges list. 
// // (In energy clusters the range has range_min/range_max & percent_* fields.)
// Range   kRangeList[1];
// DataModel::List<const Range> kRanges(kRangeList);
// Limits  kLimits;

// void BuildLimits()
// {
//     // 0 .. 100 percent range
//     kLimits.minMeasuredValue = 0;
//     kLimits.maxMeasuredValue = 100;
//     // Provide one accuracy range; percent_* are typically in 100ths.
//     kRangeList[0].rangeMin        = 0;
//     kRangeList[0].rangeMax        = 100;
//     kRangeList[0].percentTypical  = MakeOptional(static_cast<uint16_t>(200)); // 2.00%
//     kRangeList[0].percentMax      = MakeOptional(static_cast<uint16_t>(500)); // 5.00%
//     kLimits.accuracyRanges        = kRanges;
//     kLimits.measured              = true;
//     // If MeasurementType is exposed for this cluster, you can set it if needed.
//     // kLimits.measurementType = Clusters::detail::MeasurementTypeEnum::kPercentage;
// }

// //void TimerHandler(System::Layer *, void *, System::Error)
// void TimerHandler(chip::System::Layer *, void *)
// {
//     const uint8_t pct = gSensor.Sample(); // 0..100
//     if (sInstance)
//     {
//         // Set attribute 0x0001 and trigger reporting if it changed:
//         sInstance->SetSoilMeasuredValue(static_cast<Percent>(pct));
//     }

//     //SystemLayer().StartTimer(System::Clock::Milliseconds32(kUpdateMs), TimerHandler, nullptr);
//     chip::System::SystemLayer().StartTimer(chip::System::Clock::Milliseconds32(kUpdateMs),
//                                        TimerHandler, nullptr);
// }
// } // namespace

// extern "C" void emberAfSoilMeasurementClusterInitCallback(EndpointId endpointId)
// {
//     static Soil::Instance instance(endpointId);
//     sInstance = &instance;

//     gSensor.Init(/* ADC ch */ 0);

//     BuildLimits();
//     VerifyOrDie(sInstance->Init(kLimits) == CHIP_NO_ERROR);

//     // Prime 0x0001 to avoid “NullValue” in TC-SOIL-2.2
//     const uint8_t first = gSensor.Sample();
//     sInstance->SetSoilMeasuredValue(static_cast<Percent>(first));

//     // Periodic updates
//     SystemLayer().StartTimer(System::Clock::Milliseconds32(kUpdateMs), TimerHandler, nullptr);
// }

/*
 * Minimal soil measurement loop for nRF + Matter (no stub include).
 * - Reads SAADC channel 0 from DT node-label: zephyr_user
 * - Maps raw ADC to Percent100ths (0..10000)
 * - Updates SoilMeasurement cluster via generated attribute accessor
 */

#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>

#include <app/server/Server.h>
#include <platform/CHIPDeviceLayer.h>
#include <lib/support/logging/CHIPLogging.h>

// Generated attribute accessors (path provided by zzz_generated/app-common in your compile flags)
#include <app-common/zap-generated/attributes/Accessors.h>

using namespace chip;
using namespace chip::DeviceLayer;

// ========== App configuration ==========
static constexpr EndpointId kSoilEndpoint = 1;     // adjust if your endpoint differs
static constexpr uint32_t   kUpdateMs     = 1000;  // measurement interval

// Expecting in your board overlay:
//
// / {
//   zephyr_user: adc_channels {
//     compatible = "zephyr,user";
//     io-channels = <&adc 0>, <&adc 1>;
//   };
// };
//
static const struct adc_dt_spec kAdcCh[] = {
    ADC_DT_SPEC_GET_BY_IDX(DT_NODELABEL(zephyr_user), 0),
    ADC_DT_SPEC_GET_BY_IDX(DT_NODELABEL(zephyr_user), 1),
};

static int16_t sSampleBuf; // buffer for one sample

// Forward decls exported to main.cpp
extern "C" CHIP_ERROR InitSoilSensor();
extern "C" void       StartMeasurementLoop();

// Local fwd decls
static void        TimerHandler(System::Layer * aLayer, void * aAppState);
static CHIP_ERROR  ReadChannelPercent100ths(const adc_dt_spec & ch, uint16_t & outPercent100ths);

// ---------- Public (called from main.cpp) ----------
extern "C" CHIP_ERROR InitSoilSensor()
{
    // Configure each ADC channel declared in DT
    for (const auto & ch : kAdcCh)
    {
        if (!device_is_ready(ch.dev)) {
            ChipLogError(AppServer, "ADC device not ready");
            return CHIP_ERROR_INCORRECT_STATE;
        }
        const int rc = adc_channel_setup_dt(&ch);
        if (rc) {
            ChipLogError(AppServer, "adc_channel_setup_dt() failed: %d", rc);
            return CHIP_ERROR_INTERNAL;
        }
    }

    ChipLogProgress(AppServer, "Soil sensor ADC initialized (%zu channels)", sizeof(kAdcCh) / sizeof(kAdcCh[0]));

    // Optionally set sane initial attribute value (e.g., null or 0%)
    // Here we set 0%:
    (void) chip::app::Clusters::SoilMeasurement::Attributes::SoilMoistureMeasuredValue::Set(
        kSoilEndpoint, static_cast<uint16_t>(0));

    return CHIP_NO_ERROR;
}

extern "C" void StartMeasurementLoop()
{
    // Start periodic timer
    SystemLayer().StartTimer(System::Clock::Milliseconds32(kUpdateMs), TimerHandler, nullptr);
    ChipLogProgress(AppServer, "Soil measurement loop started (%u ms)", (unsigned) kUpdateMs);
}

// ---------- Periodic task ----------
static void TimerHandler(System::Layer * /*aLayer*/, void * /*aAppState*/)
{
    // Read channel 0 (change to kAdcCh[1] if you want the second probe)
    uint16_t percent100ths = 0;

    if (ReadChannelPercent100ths(kAdcCh[0], percent100ths) == CHIP_NO_ERROR)
    {
        // Write to SoilMeasurement.MeasuredValue
        // const CHIP_ERROR err =
        //     chip::app::Clusters::SoilMeasurement::Attributes::SoilMoistureMeasuredValue::Set(kSoilEndpoint, percent100ths);
        const CHIP_ERROR err = 
            chip::app::Clusters::SoilMeasurement::Attributes::SoilMoistureMeasuredValue::Set(kSoilEndpoint, percent100ths, chip::app::Clusters::Attributes::Accessors::MarkAttributeDirty::kYes);

        if (err != CHIP_NO_ERROR) {
            ChipLogError(AppServer, "Set SoilMoistureMeasuredValue failed: %s", ErrorStr(err));
        } else {
            ChipLogProgress(AppServer, "Soil moisture = %u.%02u%%",
                            (unsigned)(percent100ths / 100),
                            (unsigned)(percent100ths % 100));
        }
    }

    // Re-arm timer
    SystemLayer().StartTimer(System::Clock::Milliseconds32(kUpdateMs), TimerHandler, nullptr);
}

// ---------- ADC helper ----------
static CHIP_ERROR ReadChannelPercent100ths(const adc_dt_spec & ch, uint16_t & outPercent100ths)
{
    adc_sequence sequence = {
        .buffer      = &sSampleBuf,
        .buffer_size = sizeof(sSampleBuf),
    };

    int rc = adc_sequence_init_dt(&ch, &sequence);
    if (rc) {
        ChipLogError(AppServer, "adc_sequence_init_dt() failed: %d", rc);
        return CHIP_ERROR_INTERNAL;
    }

    rc = adc_read(ch.dev, &sequence);
    if (rc) {
        ChipLogError(AppServer, "adc_read() failed: %d", rc);
        return CHIP_ERROR_INTERNAL;
    }

    int32_t raw = sSampleBuf;
    if (raw < 0) raw = 0;

    // Simple linear mapping (12-bit SAADC assumed). Calibrate/replace as needed.
    constexpr int32_t kMaxCounts = 4095;
    int32_t pct100ths = (raw * 10000) / kMaxCounts;

    if (pct100ths < 0) pct100ths = 0;
    if (pct100ths > 10000) pct100ths = 10000;

    outPercent100ths = static_cast<uint16_t>(pct100ths);
    return CHIP_NO_ERROR;
}