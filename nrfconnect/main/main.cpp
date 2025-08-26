#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(soil_app, LOG_LEVEL_INF);

#include <platform/CHIPDeviceLayer.h>
#include <app/server/Server.h>  // keep this one
#include <lib/core/CHIPError.h>

#include "soil_measurement_nrf.h"

using namespace chip;


extern "C" int main(void)
{
    LOG_INF("Soil Sensor starting");

    chip::CommonCaseDeviceServerInitParams initParams;
    CHIP_ERROR err = initParams.InitializeStaticResourcesBeforeServerInit();
    if (err != CHIP_NO_ERROR) {
        LOG_ERR("InitParams failed: %" PRId32, static_cast<int32_t>(err.AsInteger()));
        return -1;
    }

    err = chip::Server::GetInstance().Init(initParams);
    if (err != CHIP_NO_ERROR) {
        LOG_ERR("Server init failed: %" PRId32, static_cast<int32_t>(err.AsInteger()));
        return -2;
    }

    LOG_INF("Matter server is up");
    /* If you need a run loop, add it here, or just return. */
    return 0;
}