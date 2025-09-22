
/*
    Serial output :
    sudo minicom -b 115200 -o -D /dev/ttyACM0
*/

#include <string.h>
#include <stdlib.h>
#include "pico/cyw43_arch.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#define TCP_PORT 4242
#define DEBUG_printf printf
#define BUF_SIZE 1460
#define CMD_SIZE 20
#define TEST_ITERATIONS 10
#define POLL_TIME_S 5


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
volatile bool fan_auto = true;  // automatic fan control based on temperature


// params
static const float TEMP_THRESHOLD = 25;
static const uint MAX_FAN_SPEED = 100;  // max fan speed in percent


typedef struct TCP_SERVER_T_ {
    struct tcp_pcb *server_pcb;
    struct tcp_pcb *client_pcb;
    bool complete;
    uint8_t buffer_sent[BUF_SIZE];
    uint8_t buffer_recv[BUF_SIZE];
    int sent_len;
    int recv_len;
    int run_count;
} TCP_SERVER_T;


typedef struct SYSTEM_STATE_ {
    float temperature;
    float humidity;
    float rpm;
} SYSTEM_STATE_;


volatile SYSTEM_STATE_ sys_state;


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
    if (set)    pwm_set_freq_duty(slice_num, chan, 25000, MAX_FAN_SPEED);  // on
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


void get_system_state(dht_t* dht, uint slice_num, uint chan, int* temp_mem, int* prev_temp_mem) {
    if (time_us_64() - last_time > 1000000)     rpm = 0;

    dht_start_measurement(dht);
    
    float humidity;
    float temperature_c;
    dht_result_t result = dht_finish_measurement_blocking(dht, &humidity, &temperature_c);

    if (result == DHT_RESULT_OK) {
        if (temperature_c > TEMP_THRESHOLD && fan_auto)     *temp_mem = 1;
        else                                    *temp_mem = 0;
    } else if (result == DHT_RESULT_TIMEOUT) {
        puts("DHT sensor not responding. Please check your wiring.");
    } else {
        assert(result == DHT_RESULT_BAD_CHECKSUM);
        puts("Bad checksum");
    }

    if (*temp_mem != *prev_temp_mem) {
        pwm_gen(slice_num, chan, *temp_mem);
        *prev_temp_mem = *temp_mem;
    }

    sys_state.temperature = temperature_c;
    sys_state.humidity = humidity;
    sys_state.rpm = rpm;
}


static TCP_SERVER_T* tcp_server_init(void) {
    TCP_SERVER_T *state = calloc(1, sizeof(TCP_SERVER_T));
    if (!state) {
        DEBUG_printf("failed to allocate state\n");
        return NULL;
    }
    return state;
}


static err_t tcp_server_sent(void *arg, struct tcp_pcb *tpcb, u16_t len) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    DEBUG_printf("tcp_server_sent %u\n", len);
    state->sent_len += len;

    if (state->sent_len >= BUF_SIZE) {
        // We should get the data back from the client
        state->recv_len = 0;
        DEBUG_printf("Waiting for buffer from client\n");
    }

    return ERR_OK;
}


static err_t tcp_server_close(void *arg) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    err_t err = ERR_OK;
    if (state->client_pcb != NULL) {
        tcp_arg(state->client_pcb, NULL);
        tcp_poll(state->client_pcb, NULL, 0);
        tcp_sent(state->client_pcb, NULL);
        tcp_recv(state->client_pcb, NULL);
        tcp_err(state->client_pcb, NULL);
        err = tcp_close(state->client_pcb);
        if (err != ERR_OK) {
            DEBUG_printf("close failed %d, calling abort\n", err);
            tcp_abort(state->client_pcb);
            err = ERR_ABRT;
        }
        state->client_pcb = NULL;
    }
    if (state->server_pcb) {
        tcp_arg(state->server_pcb, NULL);
        tcp_close(state->server_pcb);
        state->server_pcb = NULL;
    }
    return err;
}


static err_t tcp_server_result(void *arg, int status) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    if (status == 0) {
        DEBUG_printf("test success\n");
    }
    return ERR_OK;
}


err_t tcp_server_send_data(void *arg, struct tcp_pcb *tpcb, char* sent_msg) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;

    memset(state->buffer_sent, 0, BUF_SIZE);    // Clear buffer
    if (sent_msg) {
        strncpy((char *)state->buffer_sent, sent_msg, BUF_SIZE - 1);    // Copy string, leave space for null terminator
        printf("Prepared message to send: %s\n", state->buffer_sent);
    } else {
        state->buffer_sent[0] = '\0';   // Empty string if sent_msg is NULL
    }

    state->sent_len = 0;
    DEBUG_printf("Writing %ld bytes to client\n", BUF_SIZE);
    
    // this method is callback from lwIP, so cyw43_arch_lwip_begin is not required, however you
    // can use this method to cause an assertion in debug mode, if this method is called when
    // cyw43_arch_lwip_begin IS needed
    cyw43_arch_lwip_check();
    err_t err = tcp_write(tpcb, state->buffer_sent, strlen((char*)state->buffer_sent), TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        DEBUG_printf("Failed to write data %d\n", err);
        return tcp_server_result(arg, -1);
    }
    return ERR_OK;
}


err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;

    if (!p) {
        return tcp_server_result(arg, -1);
    }

    // this method is callback from lwIP, so cyw43_arch_lwip_begin is not required, however you
    // can use this method to cause an assertion in debug mode, if this method is called when
    // cyw43_arch_lwip_begin IS needed
    cyw43_arch_lwip_check();
    if (p->tot_len > 0) {
        DEBUG_printf("tcp_server_recv %d/%d err %d\n", p->tot_len, state->recv_len, err);
        printf("cmd received from client: %s\n", state->buffer_recv);

        // Receive the buffer
        const uint16_t buffer_left = BUF_SIZE - state->recv_len;
        state->recv_len += pbuf_copy_partial(p, state->buffer_recv + state->recv_len,
                                             p->tot_len > buffer_left ? buffer_left : p->tot_len, 0);
        tcp_recved(tpcb, p->tot_len);
    }

    pbuf_free(p);

    // Have we have received the whole buffer
    if (state->recv_len == BUF_SIZE) {

        // check it matches
        if (memcmp(state->buffer_sent, state->buffer_recv, BUF_SIZE) != 0) {
            DEBUG_printf("buffer mismatch\n");
        }
        DEBUG_printf("tcp_server_recv buffer ok\n");

        // Test complete?
        state->run_count++;
        if (state->run_count >= TEST_ITERATIONS) {
            tcp_server_result(arg, 0);
            return ERR_OK;
        }

        char sent_msg[BUF_SIZE];

        // printf("cmd received from client: %s\n", state->buffer_recv);
        if (strncmp(state->buffer_recv, "status", 6) == 0) {
            printf("Sending current system status to client\n");
            snprintf(sent_msg, sizeof(sent_msg), 
                "\n\nCurrent system status:\nTemperature: %.1f C\nHumidity: %.1f %%\nFan Speed: %.1f RPM\n\n\0", sys_state.temperature, sys_state.humidity, sys_state.rpm);
        } else if (strncmp(state->buffer_recv, "setpwm", 6) == 0) {
            int pwm_value = atoi(state->buffer_recv + 7); // Extract the value after "setpwm "
            if ((pwm_value < 0 || pwm_value > 100) && pwm_value != -1) {
                printf("Invalid PWM value received: %d. Must be between 0 and 100 or -1 to default.\n", pwm_value);
                snprintf(sent_msg, sizeof(sent_msg), "Error: Invalid PWM value. Must be between 0 and 100.\n\n\0");
            } else if (pwm_value == -1) {
                printf("Resetting to automatic fan control based on temperature.\n");
                // Reset to automatic control
                fan_auto = true;
                snprintf(sent_msg, sizeof(sent_msg), "Fan control set to auto\n\n\0");
            } else {
                printf("Setting fan PWM to %d%%\n", pwm_value);
                // Set manual PWM and disable automatic control
                fan_auto = false;
                pwm_set_freq_duty(pwm_gpio_to_slice_num(PWM_PIN), pwm_gpio_to_channel(PWM_PIN), 25000, pwm_value);
                snprintf(sent_msg, sizeof(sent_msg), "Fan PWM set to %d\n\n\0", pwm_value);
            }
        } else {
            printf("Unknown command from client\n");
            snprintf(sent_msg, sizeof(sent_msg), "Error: Unknown command\n\n\0");
        }

        memset(state->buffer_recv, 0, BUF_SIZE);    // Clear receive buffer for next command
        state->recv_len = 0;  
        
        // Send another buffer
        return tcp_server_send_data(arg, state->client_pcb, sent_msg);
    }
    return ERR_OK;
}


static err_t tcp_server_poll(void *arg, struct tcp_pcb *tpcb) {
    return tcp_server_result(arg, -1); // no response is an error?
}


static void tcp_server_err(void *arg, err_t err) {
    if (err != ERR_ABRT) {
        DEBUG_printf("tcp_client_err_fn %d\n", err);
        tcp_server_result(arg, err);
    }
}


static err_t tcp_server_accept(void *arg, struct tcp_pcb *client_pcb, err_t err) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    if (err != ERR_OK || client_pcb == NULL) {
        DEBUG_printf("Failure in accept\n");
        tcp_server_result(arg, err);
        return ERR_VAL;
    }
    DEBUG_printf("Client connected\n");

    state->client_pcb = client_pcb;
    tcp_arg(client_pcb, state);
    tcp_sent(client_pcb, tcp_server_sent);
    tcp_recv(client_pcb, tcp_server_recv);
    tcp_poll(client_pcb, tcp_server_poll, POLL_TIME_S * 2);
    tcp_err(client_pcb, tcp_server_err);

    return tcp_server_send_data(arg, state->client_pcb, NULL);
}


static bool tcp_server_open(void *arg) {
    TCP_SERVER_T *state = (TCP_SERVER_T*)arg;
    DEBUG_printf("Starting server at %s on port %u\n", ip4addr_ntoa(netif_ip4_addr(netif_list)), TCP_PORT);

    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) {
        DEBUG_printf("failed to create pcb\n");
        return false;
    }

    err_t err = tcp_bind(pcb, NULL, TCP_PORT);
    if (err) {
        DEBUG_printf("failed to bind to port %u\n", TCP_PORT);
        return false;
    }

    state->server_pcb = tcp_listen_with_backlog(pcb, 1);
    if (!state->server_pcb) {
        DEBUG_printf("failed to listen\n");
        if (pcb) {
            tcp_close(pcb);
        }
        return false;
    }

    tcp_arg(state->server_pcb, state);
    tcp_accept(state->server_pcb, tcp_server_accept);

    return true;
}


void run_tcp_server_test(void) {
    TCP_SERVER_T *state = tcp_server_init();
    if (!state) {
        return;
    }
    if (!tcp_server_open(state)) {
        tcp_server_result(state, -1);
        return;
    }

    uint slice_num, chan;
    fan_pwm_init(PWM_PIN, &slice_num, &chan, TACH_PIN);     // fan control init

    int temp_mem = 0;
    int prev_temp_mem = 0;

    dht_t dht;
    dht_init(&dht, DHT_MODEL, pio0, DATA_PIN, true /* pull_up */);

    while(!state->complete) {
        // the following #ifdef is only here so this same example can be used in multiple modes;
        // you do not need it in your code
#if PICO_CYW43_ARCH_POLL
        // if you are using pico_cyw43_arch_poll, then you must poll periodically from your
        // main loop (not from a timer) to check for Wi-Fi driver or lwIP work that needs to be done.
        cyw43_arch_poll();
        // you can poll as often as you like, however if you have nothing else to do you can
        // choose to sleep until either a specified time, or cyw43_arch_poll() has work to do:
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(1000));
#else
        // if you are not using pico_cyw43_arch_poll, then WiFI driver and lwIP work
        // is done via interrupt in the background. This sleep is just an example of some (blocking)
        // work you might be doing.

        get_system_state(&dht, slice_num, chan, &temp_mem, &prev_temp_mem);

        sleep_ms(2000);
#endif
    }

    tcp_server_close(state);
    free(state);
}


int main() {
    stdio_init_all();   // serial output
    puts("\nDHT test");

    if (cyw43_arch_init()) {
        printf("failed to initialise\n");
        return 1;
    }

    cyw43_arch_enable_sta_mode();
    ip_addr_t* ip_address = malloc(sizeof(ip_addr_t));

    connect_wifi:
    printf("Connecting to Wi-Fi...\n");
    if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("failed to connect.\n");
        goto connect_wifi;
    } else {
        printf("Connected.\n");
    }

    run_tcp_server_test();
    cyw43_arch_deinit();

    return 0;
}
