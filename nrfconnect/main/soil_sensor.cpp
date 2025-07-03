#include "include/soil_sensor.h"

#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

// ADC configuration (channel 1 → AIN1)
#define ADC_RESOLUTION         12
#define ADC_GAIN               ADC_GAIN_1_6
#define ADC_REFERENCE          ADC_REF_INTERNAL
#define ADC_ACQUISITION_TIME   ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 10)
#define ADC_CHANNEL_ID         1

#define ADC_NODE               DT_NODELABEL(adc)
#define BUFFER_SIZE            1

static const struct adc_channel_cfg channel_cfg = {
    .gain             = ADC_GAIN,
    .reference        = ADC_REFERENCE,
    .acquisition_time = ADC_ACQUISITION_TIME,
    .channel_id       = ADC_CHANNEL_ID,
    .differential     = 0,
};

static int16_t sample_buffer[BUFFER_SIZE];

uint16_t ReadSoilMoistureSensor()
{
    const struct device *adc_dev = DEVICE_DT_GET(ADC_NODE);

    if (!device_is_ready(adc_dev)) {
        LOG_ERR("ADC device not ready");
        return 0;
    }

    static bool initialized = false;
    if (!initialized) {
        adc_channel_setup(adc_dev, &channel_cfg);
        initialized = true;
    }

    struct adc_sequence sequence = {
        .channels    = BIT(ADC_CHANNEL_ID),
        .buffer      = sample_buffer,
        .buffer_size = sizeof(sample_buffer),
        .resolution  = ADC_RESOLUTION,
    };

    if (adc_read(adc_dev, &sequence) != 0) {
        LOG_ERR("ADC read failed");
        return 0;
    }

    int16_t raw = sample_buffer[0];
    if (raw < 0) raw = 0;
    if (raw > 4095) raw = 4095;

    // Scale raw ADC (0–4095) to 0–100% "moisture" value
    return (raw * 100) / 4095;
}

int16_t ReadTemperatureSensor()
{
    // Simulated constant value: 22.5 °C → 2250 (0.01 °C units for Matter)
    return 2250;
}