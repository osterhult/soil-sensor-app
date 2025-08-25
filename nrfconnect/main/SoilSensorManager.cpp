#include "include/SoilSensorManager.h"
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/devicetree.h> 
#include <zephyr/drivers/adc.h> 

LOG_MODULE_REGISTER(soil_sensor, CONFIG_LOG_DEFAULT_LEVEL);

/* One-time ADC channel descriptions pulled from your overlay */
static const struct adc_dt_spec kAdcChannels[] = {
    ADC_DT_SPEC_GET_BY_IDX(DT_NODELABEL(zephyr_user), 0),
    ADC_DT_SPEC_GET_BY_IDX(DT_NODELABEL(zephyr_user), 1),
};

void SoilSensorManager::Init(uint8_t ch)
{
    const struct adc_dt_spec * chan = &kAdcChannels[ch];
    adc_is_ready_dt(chan);
    adc_channel_setup_dt(chan);
    mSeq = {};
    mSeq.buffer      = &mBuf;
    mSeq.buffer_size = sizeof(mBuf);
    adc_sequence_init_dt(chan, &mSeq);
}

uint8_t SoilSensorManager::Sample(uint8_t ch)
{
    const struct adc_dt_spec * chan = &kAdcChannels[ch];
    if (adc_read(chan->dev, &mSeq) != 0) {
        return 0;
    }
    int32_t mv = mBuf;
    adc_raw_to_millivolts_dt(chan, &mv);

    // Calibrate these two bounds for your probe:
    // e.g., 3000 mV ≈ dry → 0%, 1800 mV ≈ wet → 100%
    mv = CLAMP(mv, 1800, 3000);
    return static_cast<uint8_t>(((3000 - mv) * 100) / 1200);
}