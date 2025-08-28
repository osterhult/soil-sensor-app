#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include <platform/CHIPDeviceLayer.h>
#include <app/server/Server.h>

#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>


LOG_MODULE_REGISTER(soil_app, LOG_LEVEL_INF);

using namespace chip;
using namespace chip::DeviceLayer;

static uint16_t ReadSoilMoistureFake() { return 512; }

extern "C" int main(void)
{
    k_msleep(500); // give console time to come up

    for (int i = 0; i < 10; ++i) {
        printk("UART printk #%d: hello from main()\n", i);
        LOG_INF("UART LOG_INF #%d: hello from main()", i);
        k_msleep(300);
    }


    CHIP_ERROR err = PlatformMgr().InitChipStack();
    if (err != CHIP_NO_ERROR) {
        LOG_ERR("InitChipStack failed: %" PRId32, static_cast<int32_t>(err.AsInteger()));
        return -1;
    }

    // Dev certificates (only if you're using CONFIG_CHIP_USE_DEV_CERTS=y)
    Credentials::SetDeviceAttestationCredentialsProvider(
        Credentials::Examples::GetExampleDACProvider());

    // Your tree expects a ServerInitParams passed to Init()
    chip::ServerInitParams params; // default-construct; no extra init calls
    err = chip::Server::GetInstance().Init(params);
    if (err != CHIP_NO_ERROR) {
        LOG_ERR("Matter Server init failed: %" PRId32, static_cast<int32_t>(err.AsInteger()));
        return -2;
    }

    // Ensure BLE advertising (pairing autostart should also cover it)
    ConnectivityMgr().SetBLEAdvertisingEnabled(true);

    while (true) {
        LOG_INF("Soil moisture (fake) = %u", ReadSoilMoistureFake());
        k_sleep(K_SECONDS(5));
    }
}