// nrfconnect/main/soil_measurement_nrf.cpp
//
// Simple SoilMeasurement reader using AttributeAccessInterface.
// No Attributes::<...>::Set(...) calls -> avoids all the Set/MarkAttributeDirty issues.

#include <app/AttributeAccessInterface.h>
#include <app/ConcreteAttributePath.h>
#include <app/reporting/reporting.h>              // MatterReportingAttributeChangeCallback
#include <lib/core/DataModelTypes.h>
#include <platform/CHIPDeviceLayer.h>
#include <system/SystemClock.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(soil_measurement_nrf, CONFIG_CHIP_APP_LOG_LEVEL);

using namespace chip;
using namespace chip::app;
using namespace chip::DeviceLayer;

// ---- Try to include ANY generated ids headers (paths vary by build) ----
#if __has_include(<zap-generated/app-common/ids/Clusters.h>)
  #include <zap-generated/app-common/ids/Clusters.h>
#elif __has_include(<app-common/zap-generated/ids/Clusters.h>)
  #include <app-common/zap-generated/ids/Clusters.h>
#elif __has_include(<zap-generated/ids/Clusters.h>)
  #include <zap-generated/ids/Clusters.h>
#else
  #error "Couldn't find generated Clusters.h (zap-generated*/ids/Clusters.h)."
#endif

#if __has_include(<zap-generated/app-common/ids/Attributes.h>)
  #include <zap-generated/app-common/ids/Attributes.h>
#elif __has_include(<app-common/zap-generated/ids/Attributes.h>)
  #include <app-common/zap-generated/ids/Attributes.h>
#elif __has_include(<zap-generated/ids/Attributes.h>)
  #include <zap-generated/ids/Attributes.h>
#else
  #error "Couldn't find generated Attributes.h (zap-generated*/ids/Attributes.h)."
#endif
// -----------------------------------------------------------------------

// ---- Detect whichever AttributeAccess registration API your CHIP provides
#if __has_include(<app/util/attribute-storage.h>)
  #include <app/util/attribute-storage.h>   // registerAttributeAccessOverride / RegisterAttributeAccessOverride
  #define HAVE_ATTR_STORAGE 1
#endif

#if __has_include(<app/AttributeAccessInterfaceRegistry.h>)
  #include <app/AttributeAccessInterfaceRegistry.h> // AttributeAccessInterfaceRegistry::Instance().Register(...)
  #define HAVE_ATTR_IF_REGISTRY 1
#endif

#if __has_include(<app/EmberAfAttributeAccessRegistry.h>)
  #include <app/EmberAfAttributeAccessRegistry.h>   // EmberAfAttributeAccessRegistry::Instance().Register(...)
  #define HAVE_EMBER_AF_REGISTRY 1
#endif
// -----------------------------------------------------------------------

namespace {

constexpr EndpointId kSoilEndpoint = 1;      // endpoint that hosts your SoilMeasurement cluster
constexpr uint32_t   kUpdateMs     = 5000;   // periodic "new reading" cadence

static uint16_t gSoilPctX100 = 3000;         // 0..10000 -> 0.00..100.00%

static uint16_t ReadSoilMoisturePercentX100()
{
    // TODO: replace with real sensor read
    gSoilPctX100 = static_cast<uint16_t>((gSoilPctX100 + 123) % 10001);
    return gSoilPctX100;
}

// Provide read access for SoilMoistureMeasuredValue
class SoilAttrAccess : public AttributeAccessInterface
{
public:
    SoilAttrAccess()
        : AttributeAccessInterface(Optional<EndpointId>::Missing(), Clusters::SoilMeasurement::Id) {}

    CHIP_ERROR Read(const ConcreteReadAttributePath & path, AttributeValueEncoder & encoder) override
    {
        if (path.mAttributeId == Clusters::SoilMeasurement::Attributes::SoilMoistureMeasuredValue::Id)
        {
            // Encode current value as uint16 (Percent100ths)
            return encoder.Encode(static_cast<uint16_t>(gSoilPctX100));
        }
        return CHIP_IM_GLOBAL_STATUS(UnsupportedAttribute);
    }
};

SoilAttrAccess gSoilAttrAccess;

inline void NotifyReport(EndpointId ep)
{
    MatterReportingAttributeChangeCallback(
        ep,
        Clusters::SoilMeasurement::Id,
        Clusters::SoilMeasurement::Attributes::SoilMoistureMeasuredValue::Id);
}

static void TimerHandler(System::Layer * layer, void *)
{
    (void) layer->StartTimer(System::Clock::Milliseconds32(kUpdateMs), TimerHandler, nullptr);
    const uint16_t v = ReadSoilMoisturePercentX100();
    NotifyReport(kSoilEndpoint);
    LOG_INF("Soil moisture updated: %u.%02u%%", v / 100, v % 100);
}

} // namespace

extern "C" {

void emberAfSoilMeasurementClusterInitCallback(EndpointId endpoint)
{
    if (endpoint != kSoilEndpoint)
        return;

// --- Robust registration across CHIP forks/versions ---
#if defined(HAVE_ATTR_IF_REGISTRY)
    AttributeAccessInterfaceRegistry::Instance().Register(&gSoilAttrAccess);
#elif defined(HAVE_EMBER_AF_REGISTRY)
    EmberAfAttributeAccessRegistry::Instance().Register(&gSoilAttrAccess);
#elif defined(HAVE_ATTR_STORAGE)
    // Legacy/CHIP trees usually expose this in the global namespace.
    registerAttributeAccessOverride(&gSoilAttrAccess);
#else
    #error "No attribute access registration API found. Ensure one of: \
    app/util/attribute-storage.h, app/AttributeAccessInterfaceRegistry.h, \
    or app/EmberAfAttributeAccessRegistry.h is available."
#endif


#if defined(HAVE_ATTR_IF_REGISTRY)
    AttributeAccessInterfaceRegistry::Instance().Register(&gSoilAttrAccess);
#elif defined(HAVE_EMBER_AF_REGISTRY)
    EmberAfAttributeAccessRegistry::Instance().Register(&gSoilAttrAccess);
#elif !defined(HAVE_ATTR_STORAGE)
    #error "No attribute access registration API found. Please ensure one of: app/util/attribute-storage.h, app/AttributeAccessInterfaceRegistry.h, or app/EmberAfAttributeAccessRegistry.h is available."
#endif

    (void) SystemLayer().StartTimer(System::Clock::Milliseconds32(kUpdateMs), TimerHandler, nullptr);
}

void emberAfSoilMeasurementClusterShutdownCallback(EndpointId endpoint)
{
    if (endpoint != kSoilEndpoint)
        return;
    (void) SystemLayer().CancelTimer(TimerHandler, nullptr);
}

} // extern "C"