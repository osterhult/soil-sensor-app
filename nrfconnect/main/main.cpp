
#include <app/server/OnboardingCodesUtil.h>
#include <app/server/Server.h>
#include <lib/support/logging/CHIPLogging.h>
#include "SoilSensorManager.h"

using namespace chip;
using namespace chip::DeviceLayer;

int main()
{
    VerifyOrDie(PlatformMgr().InitChipStack() == CHIP_NO_ERROR);
    VerifyOrDie(PlatformMgr().StartEventLoopTask() == CHIP_NO_ERROR);

    InitServer();
    PrintOnboardingCodes(GetLocalNodeId());

    SoilSensorManager::GetInstance().Init();

    return 0;
}
