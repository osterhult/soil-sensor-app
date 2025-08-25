#pragma once
#include <zephyr/drivers/adc.h>

/** Lightweight SAADC helper translating raw millivolts to soil‑moisture %. */
class SoilSensorManager {
public:
    void Init(uint8_t channel = 0);
    uint8_t Sample(uint8_t channel = 0);   /* 0‑100 % */
private:
    uint16_t mBuf;
    struct adc_sequence mSeq;
};
