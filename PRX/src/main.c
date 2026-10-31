#include <zephyr/kernel.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/usb/usb_device.h>

#include <esb.h>
#include <string.h>

#define I2S_DEV_NODE DT_NODELABEL(i2s0)

#define SAMPLE_RATE 8000
#define SAMPLES_PER_PACKET 16
#define AUDIO_PACKET_SIZE 32

#define ADC_CENTER 2120
#define AUDIO_GAIN 2

#define CHANNELS 2
#define I2S_BLOCK_SIZE (SAMPLES_PER_PACKET * CHANNELS * sizeof(int16_t))

static const struct device *i2s_dev = DEVICE_DT_GET(I2S_DEV_NODE);

K_MEM_SLAB_DEFINE_STATIC(i2s_mem_slab, I2S_BLOCK_SIZE, 8, 4);
K_MSGQ_DEFINE(audio_msgq, AUDIO_PACKET_SIZE, 64, 4);

static struct esb_payload rx_payload;

#define LED0_NODE DT_ALIAS(led0)

#if DT_NODE_HAS_STATUS(LED0_NODE, okay)
static const struct gpio_dt_spec led_rx =
    GPIO_DT_SPEC_GET(LED0_NODE, gpios);
#endif

static void prx_event_handler(struct esb_evt const *event)
{
    if (event->evt_id == ESB_EVENT_RX_RECEIVED) {

        while (esb_read_rx_payload(&rx_payload) == 0) {

            if (rx_payload.length == AUDIO_PACKET_SIZE) {
                k_msgq_put(&audio_msgq, rx_payload.data, K_NO_WAIT);

#if DT_NODE_HAS_STATUS(LED0_NODE, okay)
                gpio_pin_toggle_dt(&led_rx);
#endif
            }
        }
    }
}

static int esb_initialize(void)
{
    int err;

    struct esb_config config = ESB_DEFAULT_CONFIG;

    config.protocol = ESB_PROTOCOL_ESB_DPL;
    config.mode = ESB_MODE_PRX;
    config.bitrate = ESB_BITRATE_2MBPS;
    config.event_handler = prx_event_handler;
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

    err = esb_start_rx();
    if (err) {
        printk("esb_start_rx failed: %d\n", err);
        return err;
    }

    printk("ESB PRX started\n");
    return 0;
}

static int i2s_initialize(void)
{
    int err;

    if (!device_is_ready(i2s_dev)) {
        printk("I2S not ready\n");
        return -1;
    }

    struct i2s_config cfg = {
        .word_size = 16,
        .channels = CHANNELS,
        .format = I2S_FMT_DATA_FORMAT_I2S,
        .options = I2S_OPT_BIT_CLK_MASTER | I2S_OPT_FRAME_CLK_MASTER,
        .frame_clk_freq = SAMPLE_RATE,
        .block_size = I2S_BLOCK_SIZE,
        .mem_slab = &i2s_mem_slab,
        .timeout = 1000,
    };

    err = i2s_configure(i2s_dev, I2S_DIR_TX, &cfg);
    if (err) {
        printk("i2s_configure failed: %d\n", err);
        return err;
    }

    printk("I2S configured\n");
    return 0;
}




static int32_t prev_x = 0;
static int32_t prev_y = 0;

static int16_t highpass_filter(int16_t x)
{
    int32_t y = x - prev_x + ((prev_y * 995) / 1000);

    prev_x = x;
    prev_y = y;

    if (y > 32767) y = 32767;
    if (y < -32768) y = -32768;

    return (int16_t)y;
}




int main(void)
{
    int err;
    bool i2s_started = false;
    uint32_t played_packets = 0;

    usb_enable(NULL);
    k_sleep(K_SECONDS(2));

    printk("\nPRX ESB AUDIO TO MAX98357A\n");

#if DT_NODE_HAS_STATUS(LED0_NODE, okay)
    if (gpio_is_ready_dt(&led_rx)) {
        gpio_pin_configure_dt(&led_rx, GPIO_OUTPUT_INACTIVE);
    }
#endif

    err = i2s_initialize();
    if (err) {
        return 0;
    }

    err = esb_initialize();
    if (err) {
        return 0;
    }

 uint8_t packet[AUDIO_PACKET_SIZE];
uint8_t last_packet[AUDIO_PACKET_SIZE] = {0};

while (1) {

  bool have_packet = false;

if (k_msgq_get(&audio_msgq, packet, K_NO_WAIT) == 0) {
    have_packet = true;
}

    void *mem_block;

    err = k_mem_slab_alloc(&i2s_mem_slab, &mem_block, K_MSEC(100));
    if (err) {
        printk("slab alloc failed: %d\n", err);
        k_sleep(K_MSEC(10));
        continue;
    }

    int16_t *pcm = (int16_t *)mem_block;

   for (int i = 0; i < SAMPLES_PER_PACKET; i++) {

    uint16_t adc;

    if (have_packet) {
        adc = packet[i * 2] |
             (packet[i * 2 + 1] << 8);
    } else {
        adc = ADC_CENTER; // тиша, якщо нового пакета нема
    }

    if (adc > 4095) {
        adc = 4095;
    }

    int32_t centered = (int32_t)adc - ADC_CENTER;
    int32_t amplified = centered * AUDIO_GAIN;

    if (amplified > 32767) amplified = 32767;
    if (amplified < -32768) amplified = -32768;

    int16_t sample = (int16_t)amplified;

    pcm[i * 2]     = sample;
    pcm[i * 2 + 1] = sample;
}

    err = i2s_write(i2s_dev, mem_block, I2S_BLOCK_SIZE);

    if (err) {
        printk("i2s_write failed: %d\n", err);
        k_mem_slab_free(&i2s_mem_slab, mem_block);

        i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
        i2s_configure(i2s_dev, I2S_DIR_TX, NULL);
        i2s_initialize();
        i2s_started = false;

        k_sleep(K_MSEC(100));
        continue;
    }

    if (!i2s_started) {
        err = i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_START);
        if (err) {
            printk("i2s start failed: %d\n", err);
            return 0;
        }

        printk("I2S started\n");
        i2s_started = true;
    }

    played_packets++;

    if ((played_packets % 500) == 0) {
        printk("played blocks: %u\n", played_packets);
    }
}
}