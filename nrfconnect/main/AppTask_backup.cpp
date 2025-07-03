#include "include/AppTask.h"
#include "include/soil_sensor.h"

#include <app/server/Server.h>
#include <app/clusters/soil-measurement-server/soil-measurement-server.h>
#include <app/util/attribute-storage.h>
#include <app/reporting/reporting.h>
#include <platform/CHIPDeviceLayer.h>
#include <setup_payload/OnboardingCodesUtil.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "gen_config.h"
#include "endpoint_config.h"



// Temporary workaround: manually define missing ZAP-generated constants
#define ZCL_SOIL_MEASUREMENT_CLUSTER_ID 0x0430
#define ZCL_SOIL_MEASUREMENT_MEASURED_VALUE_ATTRIBUTE_ID 0x0001
#define ZCL_UINT16U_ATTRIBUTE_TYPE 0x21

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

using namespace ::chip;
using namespace ::chip::DeviceLayer;

namespace {

    constexpr uint32_t kSensorIntervalMs = 5000;
    K_WORK_DELAYABLE_DEFINE(sSensorWork, SensorTimerHandler);

} // namespace

void SensorTimerHandler(struct k_work * work)
{
    uint16_t soilMoisture = ReadSoilMoistureSensor();
    emberAfWriteServerAttribute(1,
        ZCL_SOIL_MEASUREMENT_CLUSTER_ID,
        ZCL_SOIL_MEASUREMENT_MEASURED_VALUE_ATTRIBUTE_ID,
        reinterpret_cast<uint8_t *>(&soilMoisture),
        ZCL_UINT16U_ATTRIBUTE_TYPE);

    chip::app::reporting::ReportingEngine::GetInstance().ScheduleRun();    
    // chip::app::ReportingEngine::GetInstance().ScheduleRun();
    k_work_schedule(&sSensorWork, K_MSEC(kSensorIntervalMs));
}

CHIP_ERROR AppTask::Init()
{
    LOG_INF("Initializing Matter Soil Sensor");

    // Init Zephyr platform stack
    ReturnErrorOnFailure(PlatformMgr().InitChipStack());

#if defined(CONFIG_NET_L2_OPENTHREAD)
    ReturnErrorOnFailure(ThreadStackMgr().InitThreadStack());
    ConnectivityMgr().SetThreadDeviceType(ConnectivityManager::kThreadDeviceType_MinimalEndDevice);
#endif

    // Start Matter server
    //ReturnErrorOnFailure(Server::GetInstance().Init(nullptr));
    chip::ServerInitParams initParams;
   ReturnErrorOnFailure(Server::GetInstance().Init(initParams));

    // Log onboarding info
    PrintOnboardingCodes(RendezvousInformationFlag::kBLE);

    // Start sensor polling
    k_work_init_delayable(&sSensorWork, SensorTimerHandler);
    k_work_schedule(&sSensorWork, K_NO_WAIT);

    return CHIP_NO_ERROR;
}

CHIP_ERROR AppTask::StartApp()
{
    ReturnErrorOnFailure(Init());
    return PlatformMgr().StartEventLoopTask();
}