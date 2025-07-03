
#pragma once

#include <app-common/zap-generated/cluster-objects.h>
#include <app/util/attribute-storage.h>
#include <app/AttributeAccessInterface.h>
#include <app/server/Server.h>
#include <lib/core/CHIPError.h>
#include <platform/PlatformManager.h>
//#include <app/clusters/SoilMeasurement/SoilMeasurement.h>

class SoilSensorManager
{
public:
    static SoilSensorManager & GetInstance() { return sInstance; }
    CHIP_ERROR Init();

private:
    static SoilSensorManager sInstance;
    chip::app::Clusters::SoilMeasurement::Instance * mSensorInstance = nullptr;
};
