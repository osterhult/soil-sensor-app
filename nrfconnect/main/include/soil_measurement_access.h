#pragma once

#include <app/AttributeAccessInterface.h>
#include <app/util/attribute-storage.h>
#include <app/InteractionModelEngine.h>

#include "soil_sensor.h"   // your ADC helpers

class SoilAttrAccess : public chip::app::AttributeAccessInterface
{
public:
    SoilAttrAccess();

    CHIP_ERROR Read(const chip::app::ConcreteReadAttributePath & path,
                    chip::app::AttributeValueEncoder & encoder) override;

    // Optional: push unsolicited reports
    void ReportIfChanged(uint16_t newPercent);

private:
    uint16_t mLastPercent = 0;
};

extern SoilAttrAccess gSoilAttrAccess;
