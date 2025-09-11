// Minimal ADC helper for prospective sensor integration.

#include <zephyr/drivers/adc.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>
#include <errno.h>

static const struct adc_dt_spec adc_channels[] = {
    ADC_DT_SPEC_GET_BY_IDX(DT_NODELABEL(zephyr_user), 0),
    ADC_DT_SPEC_GET_BY_IDX(DT_NODELABEL(zephyr_user), 1),
};

int adc_init_all(void)
{
    for (size_t i = 0; i < ARRAY_SIZE(adc_channels); ++i) {
        if (!device_is_ready(adc_channels[i].dev)) {
            return -ENODEV;
        }
        int err = adc_channel_setup_dt(&adc_channels[i]);
        if (err) {
            return err;
        }
    }
    return 0;
}

int SoilSensorManager_ReadRaw(uint16_t *raw0, uint16_t *raw1)
{
    int16_t sample0, sample1;

    struct adc_sequence seq0;
    adc_sequence_init_dt(&adc_channels[0], &seq0);
    seq0.buffer = &sample0;
    seq0.buffer_size = sizeof(sample0);

    struct adc_sequence seq1;
    adc_sequence_init_dt(&adc_channels[1], &seq1);
    seq1.buffer = &sample1;
    seq1.buffer_size = sizeof(sample1);

    int err = adc_read_dt(&adc_channels[0], &seq0);
    if (err) return err;

    err = adc_read_dt(&adc_channels[1], &seq1);
    if (err) return err;

    *raw0 = (uint16_t)sample0;
    *raw1 = (uint16_t)sample1;
    return 0;
}
