/*
 * Minimal Matter server bootstrap + soil sensor start-up (no stub dependencies)
 */

#include <app/server/Server.h>            // init params are available from this header in your tree
#include <platform/CHIPDeviceLayer.h>
#include <lib/support/logging/CHIPLogging.h>

using namespace chip;
using namespace chip::DeviceLayer;

// Provided by soil_measurement_nrf.cpp
extern "C" CHIP_ERROR InitSoilSensor();
extern "C" void       StartMeasurementLoop();

int main()
{
    // Initialize CHIP stack
    VerifyOrDie(PlatformMgr().InitChipStack() == CHIP_NO_ERROR);

    // Prefer CommonCaseDeviceServerInitParams if available, else fall back to ServerInitParams
    #if defined(CHIP_DEVICE_LAYER_TARGET_NRFCONNECT) && defined(CHIP_CONFIG_KVS_PATH) \
        && !defined(CHIP_SERVER_NO_COMMON_CASE) /* heuristic: common in newer trees */
        // Newer trees keep CommonCaseDeviceServerInitParams inside Server.h
        chip::CommonCaseDeviceServerInitParams initParams;
        (void) initParams.InitializeStaticResourcesBeforeServerInit();
    #else
        chip::ServerInitParams initParams;
    #endif

    VerifyOrDie(Server::GetInstance().Init(initParams) == CHIP_NO_ERROR);

    VerifyOrDie(InitSoilSensor() == CHIP_NO_ERROR);
    StartMeasurementLoop();

    PlatformMgr().RunEventLoop();
    return 0;
}