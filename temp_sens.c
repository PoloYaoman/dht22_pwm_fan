
/*
    Serial output :
    sudo minicom -b 115200 -o -D /dev/ttyACM0
*/


#include <dht.h>
#include <pico/stdlib.h>
#include <stdio.h>
#include <hardware/pwm.h>
#include <hardware/gpio.h>


// change this to match your setupuint8_t tach_pin
static const dht_model_t DHT_MODEL = DHT22;
static const uint DATA_PIN = 15;

static const uint PWM_PIN = 16;
static const uint TACH_PIN = 17;


// for tach data extraction
volatile uint64_t last_time = 0;
volatile float rpm = 0;


// params
static const float TEMP_THRESHOLD = 25;


void gpio_callback(uint gpio, uint32_t events) {
    uint64_t current_time = time_us_64();
    uint64_t pulse_width = current_time - last_time;

    if (pulse_width > 10000) {                      // 10ms in microseconds (filter out noise)
        float freq = 1.0f / (pulse_width / 1e6f);   // Convert to Hz
        rpm = (freq / 2.0f) * 60.0f;                // 2 pulses per revolution
        last_time = current_time;
    }
}


uint32_t pwm_set_freq_duty(uint slice_num, uint chan, uint32_t f, int d) {
    printf("Setting PWM to %d duty cycle\n", d);

    uint32_t clock = 125000000;

    uint32_t divider16 = clock / f / 4096 + (clock % (f * 4096) != 0);
    if (divider16 / 16 == 0)    divider16 = 16;

    uint32_t wrap = clock * 16 / divider16 / f - 1;

    pwm_set_clkdiv_int_frac(slice_num, divider16/16, divider16 & 0xF);
    pwm_set_wrap(slice_num, wrap);
    pwm_set_chan_level(slice_num, chan, wrap * d / 100);
    return wrap;
}


void pwm_gen(uint slice_num, uint chan, int set) {
    if (set)    pwm_set_freq_duty(slice_num, chan, 25000, 100);  // on
    else        pwm_set_freq_duty(slice_num, chan, 25000, 0);  // or off
    pwm_set_enabled(slice_num, true);
}


void fan_pwm_init(uint8_t pwm_pin, uint* slice_num, uint* chan, uint8_t tach_pin) {
    // pwm setup
    gpio_set_function(pwm_pin, GPIO_FUNC_PWM);
    *slice_num = pwm_gpio_to_slice_num(pwm_pin);
    *chan = pwm_gpio_to_channel(pwm_pin);
    pwm_gen(*slice_num, *chan, 0);
    
    // tach gpio setup
    gpio_init(tach_pin);
    gpio_set_dir(tach_pin, GPIO_IN);
    gpio_pull_up(tach_pin);

    // irq interrupt to gather tach
    gpio_set_irq_enabled_with_callback(
        tach_pin,
        GPIO_IRQ_EDGE_FALL,
        true,
        &gpio_callback
    );
}


int main() {
    uint slice_num, chan;
    fan_pwm_init(PWM_PIN, &slice_num, &chan, TACH_PIN);     // fan control init

    int temp_mem = 0;
    int prev_temp_mem = 0;

    stdio_init_all();   // serial output
    puts("\nDHT test");

    dht_t dht;
    dht_init(&dht, DHT_MODEL, pio0, DATA_PIN, true /* pull_up */);

    do {
        if (time_us_64() - last_time > 1000000)     rpm = 0;
        printf("tach gpio output : %.1f\n", rpm);

        dht_start_measurement(&dht);
        
        float humidity;
        float temperature_c;
        dht_result_t result = dht_finish_measurement_blocking(&dht, &humidity, &temperature_c);

        if (result == DHT_RESULT_OK) {
            printf("%.1f C, %.1f%% humidity\n", temperature_c, humidity);
            if (temperature_c > TEMP_THRESHOLD)     temp_mem = 1;
            else                        temp_mem = 0;
        } else if (result == DHT_RESULT_TIMEOUT) {
            puts("DHT sensor not responding. Please check your wiring.");
        } else {
            assert(result == DHT_RESULT_BAD_CHECKSUM);
            puts("Bad checksum");
        }

        if (temp_mem != prev_temp_mem) {
            pwm_gen(slice_num, chan, temp_mem);
            prev_temp_mem = temp_mem;
        }

        sleep_ms(2000);
        tight_loop_contents();
    } while (true);
}
