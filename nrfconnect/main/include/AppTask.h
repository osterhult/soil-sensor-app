#pragma once

#include <platform/CHIPDeviceLayer.h>
#include <zephyr/kernel.h> // Required for `struct k_work`

class AppTask
{
public:
    static AppTask & Instance()
    {
        static AppTask sAppTask;
        return sAppTask;
    }

    CHIP_ERROR StartApp();

private:
    CHIP_ERROR Init();
};

// Declare the handler function here (outside the class)
void SensorTimerHandler(struct k_work * work);
