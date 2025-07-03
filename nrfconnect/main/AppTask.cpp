/*
 *  Matter “Soil-Sensor” – application task
 *  Builds on Nordic lighting-app skeleton but trimmed for:
 *   • BLE commissioning only                (no Thread)
 *   • Wi-Fi runtime via nRF7002 + Nwk-Comm
 *   • single endpoint with a Soil-Measurement cluster
 */

#include "include/AppTask.h"
#include "include/soil_sensor.h"
#include "include/soil_measurement_access.h"

#include <app/server/Server.h>
#include <app/InteractionModelEngine.h>
#include <app/AttributeAccessInterfaceRegistry.h>
#include <app/clusters/soil-measurement-server/soil-measurement-server.h>

/* Wi-Fi Network-Commissioning -------------------------------------------- */
#ifdef CONFIG_CHIP_WIFI
#include <app/clusters/network-commissioning/network-commissioning.h>
#include <platform/nrfconnect/wifi/NrfWiFiDriver.h>
#endif

#include <platform/CHIPDeviceLayer.h>
#include <setup_payload/OnboardingCodesUtil.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

using namespace chip;
using namespace chip::app;
using namespace chip::DeviceLayer;

/* ------------------------------------------------------------------------- */
/* Soil-sensor specific helpers                                              */
/* ------------------------------------------------------------------------- */

namespace {
constexpr EndpointId kSoilEndpointId  = 1;
constexpr uint32_t   kSamplePeriodMs  = 5000;

/* Zephyr delayed work for periodic sampling */
K_WORK_DELAYABLE_DEFINE(sSoilWork, SensorTimerHandler);
} // anonymous namespace

/* ------------------------------------------------------------------------- */
/*  Network-Commissioning instance (Wi-Fi only)                              */
/* ------------------------------------------------------------------------- */
#ifdef CONFIG_CHIP_WIFI
static app::Clusters::NetworkCommissioning::Instance sWiFiInstance(
    0,                                               /* endpoint Id (root)   */
    &(NetworkCommissioning::NrfWiFiDriver::Instance())
);
#endif

/* ------------------------------------------------------------------------- */
/*  Periodic sampling                                                         */
/* ------------------------------------------------------------------------- */
void SensorTimerHandler(struct k_work * /*work*/)
{
    uint16_t moisture = ReadSoilMoistureSensor();
    gSoilAttrAccess.ReportIfChanged(moisture);

    InteractionModelEngine::GetInstance()->GetReportingEngine().ScheduleRun();
    k_work_schedule(&sSoilWork, K_MSEC(kSamplePeriodMs));
}

/* ------------------------------------------------------------------------- */
/*  AppTask                                                                   */
/* ------------------------------------------------------------------------- */
CHIP_ERROR AppTask::Init()
{
    LOG_INF("Initializing Matter Soil-Sensor …");

    /* ---- CHIP stack ---------------------------------------------------- */
    ReturnErrorOnFailure(PlatformMgr().InitChipStack());

#if defined(CONFIG_NET_L2_OPENTHREAD)
    /* (Thread completely disabled in prj.conf, but keep guard for safety) */
    ReturnErrorOnFailure(ThreadStackMgr().InitThreadStack());
    ConnectivityMgr().SetThreadDeviceType(
        ConnectivityManager::kThreadDeviceType_MinimalEndDevice);
#endif

    /* ---- CHIP server --------------------------------------------------- */
    chip::ServerInitParams params;
    ReturnErrorOnFailure(Server::GetInstance().Init(params));

    /* Register attribute-access interface for custom Soil-Measurement cluster */
    AttributeAccessInterfaceRegistry::Instance().Register(&gSoilAttrAccess);

#ifdef CONFIG_CHIP_WIFI
    /* Bring up Network-Commissioning cluster (Wi-Fi driver already singleton) */
    sWiFiInstance.Init();
#endif

    PrintOnboardingCodes(RendezvousInformationFlag::kBLE);

    /* Start periodic sensor work immediately */
    k_work_init_delayable(&sSoilWork, SensorTimerHandler);
    k_work_schedule(&sSoilWork, K_NO_WAIT);

    return CHIP_NO_ERROR;
}

CHIP_ERROR AppTask::StartApp()
{
    ReturnErrorOnFailure(Init());
    return PlatformMgr().StartEventLoopTask();
}

