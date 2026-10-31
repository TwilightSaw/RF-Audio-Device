#include <zephyr/kernel.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/usb/usb_device.h>

#include <hal/nrf_saadc.h>
#include <esb.h>
#include <string.h>







/* ---------------- AUDIO CONFIG ---------------- */

#define SAMPLES_PER_PACKET 16
#define AUDIO_PACKET_SIZE  (SAMPLES_PER_PACKET * 2)
#define SAMPLE_DELAY_US    40  /* 8 kHz */

/* ---------------- ADC ---------------- */

#define ADC_NODE DT_NODELABEL(adc)

static const struct device *adc_dev = DEVICE_DT_GET(ADC_NODE);

static int16_t sample_buffer;
static int16_t audio_samples[SAMPLES_PER_PACKET];
static uint8_t tx_payload_data[AUDIO_PACKET_SIZE];

static struct adc_channel_cfg channel_cfg = {
    .gain = ADC_GAIN_1_4,
    .reference = ADC_REF_INTERNAL,
    .acquisition_time = ADC_ACQ_TIME_DEFAULT,
    .channel_id = 1,

#if defined(CONFIG_ADC_NRFX_SAADC)
    .input_positive = NRF_SAADC_INPUT_AIN1,
#endif
};

static struct adc_sequence sequence = {
    .channels = BIT(1),
    .buffer = &sample_buffer,
    .buffer_size = sizeof(sample_buffer),
    .resolution = 12,
};


/* ---------------- LED ---------------- */

#define LED0_NODE DT_ALIAS(led0)

#if DT_NODE_HAS_STATUS(LED0_NODE, okay)
static const struct gpio_dt_spec led_tx =
    GPIO_DT_SPEC_GET(LED0_NODE, gpios);
#endif

/* ---------------- ESB ---------------- */

static void ptx_event_handler(struct esb_evt const *event)
{
    switch (event->evt_id) {

    case ESB_EVENT_TX_SUCCESS:
        break;

    case ESB_EVENT_TX_FAILED:
        esb_flush_tx();
        break;

    case ESB_EVENT_RX_RECEIVED:
        break;

    default:
        break;
    }
}

static int esb_initialize(void)
{
    int err;

    struct esb_config config = ESB_DEFAULT_CONFIG;

    config.protocol = ESB_PROTOCOL_ESB_DPL;
    config.mode = ESB_MODE_PTX;
    config.bitrate = ESB_BITRATE_2MBPS;
    config.event_handler = ptx_event_handler;
    config.selective_auto_ack = true;

    err = esb_init(&config);
    if (err) {
        printk("esb_init failed: %d\n", err);
        return err;
    }

    uint8_t base_addr_0[4] = {0xE7, 0xE7, 0xE7, 0xE7};
    uint8_t base_addr_1[4] = {0xC2, 0xC2, 0xC2, 0xC2};
    uint8_t addr_prefix[8] = {
        0xE7, 0xC2, 0xC3, 0xC4,
        0xC5, 0xC6, 0xC7, 0xC8
    };

    esb_set_base_address_0(base_addr_0);
    esb_set_base_address_1(base_addr_1);
    esb_set_prefixes(addr_prefix, ARRAY_SIZE(addr_prefix));

    printk("ESB initialized\n");
    return 0;
}





int main(void)
{
    int err;
    uint32_t packet_counter = 0;

    usb_enable(NULL);
    k_sleep(K_SECONDS(2));

    printk("\nStarting XIAO PTX microphone stream\n");

#if DT_NODE_HAS_STATUS(LED0_NODE, okay)
    if (gpio_is_ready_dt(&led_tx)) {
        gpio_pin_configure_dt(&led_tx, GPIO_OUTPUT_INACTIVE);
        
    } else {
        
    }
#else
    printk("No led0 alias\n");
#endif

    if (!device_is_ready(adc_dev)) {
        
        return 0;
    }

    

    err = adc_channel_setup(adc_dev, &channel_cfg);
    if (err) {
        printk("ADC setup failed: %d\n", err);
        return 0;
    }

    printk("ADC channel setup OK\n");

    err = esb_initialize();
    if (err) {
        printk("ESB init failed\n");
        return 0;
    }

    while (1) {

        for (int i = 0; i < SAMPLES_PER_PACKET; i++) {

            err = adc_read(adc_dev, &sequence);

            if (err == 0) {
                audio_samples[i] = sample_buffer;
            } else {
                audio_samples[i] = 2048;
            }

            k_busy_wait(SAMPLE_DELAY_US);
        }

        for (int i = 0; i < SAMPLES_PER_PACKET; i++) {

            uint16_t value = audio_samples[i];

            if (value > 4095) {
                value = 4095;
            }

            tx_payload_data[i * 2]     = value & 0xFF;
            tx_payload_data[i * 2 + 1] = (value >> 8) & 0xFF;
        }

        struct esb_payload payload = {
            .length = AUDIO_PACKET_SIZE,
            .pipe = 0,
            .noack = true,
        };

        memcpy(payload.data, tx_payload_data, AUDIO_PACKET_SIZE);

        err = esb_write_payload(&payload);

#if DT_NODE_HAS_STATUS(LED0_NODE, okay)
        gpio_pin_toggle_dt(&led_tx);
#endif

        if (err) {
            printk("Payload send failed: %d\n", err);
        }

        packet_counter++;

        if ((packet_counter % 200) == 0) {
    uint16_t min_v = 4095;
    uint16_t max_v = 0;
    uint32_t sum = 0;

    for (int i = 0; i < SAMPLES_PER_PACKET; i++) {
        uint16_t v = audio_samples[i];

        if (v < min_v) min_v = v;
        if (v > max_v) max_v = v;

        sum += v;
    }

    uint16_t avg = sum / SAMPLES_PER_PACKET;

    
}

        
    }
}