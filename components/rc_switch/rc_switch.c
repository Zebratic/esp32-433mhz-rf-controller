#include "rc_switch.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/ets_sys.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "RC_SWITCH";

// Protocol definitions (matching Arduino rc-switch library)
static const rc_protocol_t protocols[] = {
    { 350, { 1, 31 }, { 1, 3 }, { 3, 1 }, false },    // Protocol 1
    { 650, { 1, 10 }, { 1, 2 }, { 2, 1 }, false },    // Protocol 2
    { 100, { 30, 71 }, { 4, 11 }, { 9, 6 }, false },  // Protocol 3
    { 380, { 1, 6 }, { 1, 3 }, { 3, 1 }, false },     // Protocol 4
    { 500, { 6, 14 }, { 1, 2 }, { 2, 1 }, false },    // Protocol 5
    { 450, { 23, 1 }, { 1, 2 }, { 2, 1 }, true },     // Protocol 6
    { 150, { 2, 62 }, { 1, 6 }, { 6, 1 }, false },    // Protocol 7
};

#define NUM_PROTOCOLS (sizeof(protocols) / sizeof(protocols[0]))

// Global receiver pointer for ISR
static rc_receiver_t* g_receiver = NULL;
static volatile uint32_t isr_trigger_count = 0;

// Decode the timing buffer to extract code, protocol, etc.
static bool decode_signal(rc_receiver_t* receiver) {
    // Try each protocol
    for (uint8_t proto = 0; proto < NUM_PROTOCOLS; proto++) {
        const rc_protocol_t* protocol = &protocols[proto];
        uint32_t code = 0;
        uint16_t delay = 0;
        uint8_t bit_count = 0;
        
        // Look for sync pattern at start
        if (receiver->buffer_pos < 8) continue;
        
        // Estimate pulse length from first few transitions
        uint32_t avg_short = 0;
        uint8_t short_count = 0;
        
        for (uint8_t i = 2; i < receiver->buffer_pos && i < 20; i++) {
            if (receiver->duration_buffer[i] < 2000) {
                avg_short += receiver->duration_buffer[i];
                short_count++;
            }
        }
        
        if (short_count == 0) continue;
        delay = avg_short / short_count;
        
        // Try to decode bits
        for (uint8_t i = 1; i < receiver->buffer_pos - 1 && bit_count < 32; i += 2) {
            uint32_t dur_high = receiver->duration_buffer[i];
            uint32_t dur_low = receiver->duration_buffer[i + 1];
            
            // Calculate expected timings
            uint32_t one_high = delay * protocol->one.high;
            uint32_t one_low = delay * protocol->one.low;
            uint32_t zero_high = delay * protocol->zero.high;
            uint32_t zero_low = delay * protocol->zero.low;
            
            // Check if it matches a '1' bit
            if (abs((int)dur_high - (int)one_high) < delay && 
                abs((int)dur_low - (int)one_low) < delay) {
                code = (code << 1) | 1;
                bit_count++;
            }
            // Check if it matches a '0' bit
            else if (abs((int)dur_high - (int)zero_high) < delay && 
                     abs((int)dur_low - (int)zero_low) < delay) {
                code = (code << 1) | 0;
                bit_count++;
            }
            else {
                // Invalid bit timing
                break;
            }
        }
        
        // Valid if we decoded at least 8 bits
        if (bit_count >= 8 && code != 0) {
            receiver->received_value = code;
            receiver->received_bitlength = bit_count;
            receiver->received_protocol = proto + 1;  // 1-indexed
            receiver->received_delay = delay;
            return true;
        }
    }
    
    return false;
}

// ISR handler
static void IRAM_ATTR rc_receiver_isr_handler(void* arg) {
    rc_receiver_t* receiver = (rc_receiver_t*)arg;
    int64_t time = esp_timer_get_time();
    int64_t duration = time - receiver->last_time;
    
    isr_trigger_count++; // Track ISR triggers for debugging

    if (duration > 5000) {  // Sync period detected (>5ms gap)
        if (receiver->buffer_pos > 7) {  // Minimum valid signal length
            // Try to decode the signal
            if (decode_signal(receiver)) {
                receiver->available = true;
            }
        }
        receiver->buffer_pos = 0;
    }

    if (receiver->buffer_pos < 256) {
        receiver->duration_buffer[receiver->buffer_pos++] = duration;
    }

    receiver->last_time = time;
}

void rc_receiver_init(rc_receiver_t* receiver, gpio_num_t pin) {
    memset(receiver, 0, sizeof(rc_receiver_t));
    receiver->pin = pin;
    g_receiver = receiver;

    // Configure GPIO
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_ANYEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << pin),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE
    };
    gpio_config(&io_conf);

    // Install ISR service
    gpio_install_isr_service(0);
    gpio_isr_handler_add(pin, rc_receiver_isr_handler, (void*)receiver);

    receiver->last_time = esp_timer_get_time();
    ESP_LOGI(TAG, "Receiver initialized on GPIO %d", pin);
}

bool rc_receiver_available(rc_receiver_t* receiver) {
    return receiver->available;
}

uint32_t rc_receiver_get_value(rc_receiver_t* receiver) {
    return receiver->received_value;
}

uint8_t rc_receiver_get_bitlength(rc_receiver_t* receiver) {
    return receiver->received_bitlength;
}

uint8_t rc_receiver_get_protocol(rc_receiver_t* receiver) {
    return receiver->received_protocol;
}

uint16_t rc_receiver_get_delay(rc_receiver_t* receiver) {
    return receiver->received_delay;
}

void rc_receiver_reset(rc_receiver_t* receiver) {
    receiver->available = false;
    receiver->buffer_pos = 0;
}

uint32_t rc_receiver_get_isr_count(void) {
    return isr_trigger_count;
}

void rc_transmitter_init(rc_transmitter_t* transmitter, gpio_num_t pin) {
    memset(transmitter, 0, sizeof(rc_transmitter_t));
    transmitter->pin = pin;
    transmitter->protocol = 0;  // Protocol 1
    transmitter->pulse_length = 350;
    transmitter->repeat_transmit = 5;

    // Configure GPIO as output
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << pin),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(pin, 0);

    ESP_LOGI(TAG, "Transmitter initialized on GPIO %d", pin);
}

void rc_transmitter_set_protocol(rc_transmitter_t* transmitter, uint8_t protocol) {
    if (protocol < NUM_PROTOCOLS) {
        transmitter->protocol = protocol;
    }
}

void rc_transmitter_set_pulse_length(rc_transmitter_t* transmitter, uint16_t pulse_length) {
    transmitter->pulse_length = pulse_length;
}

void rc_transmitter_set_repeat(rc_transmitter_t* transmitter, uint8_t repeat) {
    transmitter->repeat_transmit = repeat;
}

static void transmit_bit(rc_transmitter_t* transmitter, bool bit) {
    const rc_protocol_t* protocol = &protocols[transmitter->protocol];
    uint8_t high_cycles, low_cycles;

    if (bit) {
        high_cycles = protocol->one.high;
        low_cycles = protocol->one.low;
    } else {
        high_cycles = protocol->zero.high;
        low_cycles = protocol->zero.low;
    }

    gpio_set_level(transmitter->pin, protocol->invert_signal ? 0 : 1);
    ets_delay_us(transmitter->pulse_length * high_cycles);

    gpio_set_level(transmitter->pin, protocol->invert_signal ? 1 : 0);
    ets_delay_us(transmitter->pulse_length * low_cycles);
}

static void transmit_sync(rc_transmitter_t* transmitter) {
    const rc_protocol_t* protocol = &protocols[transmitter->protocol];

    gpio_set_level(transmitter->pin, protocol->invert_signal ? 0 : 1);
    ets_delay_us(transmitter->pulse_length * protocol->sync_factor.high);

    gpio_set_level(transmitter->pin, protocol->invert_signal ? 1 : 0);
    ets_delay_us(transmitter->pulse_length * protocol->sync_factor.low);
}

void rc_transmitter_send(rc_transmitter_t* transmitter, uint32_t code, uint8_t length) {
    if (length > 32) length = 32;

    // Transmit each repeat separately to avoid watchdog timeout
    for (uint8_t repeat = 0; repeat < transmitter->repeat_transmit; repeat++) {
        // Disable interrupts only for single transmission
        portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
        portENTER_CRITICAL(&mux);

        // Send sync
        transmit_sync(transmitter);

        // Send bits (MSB first)
        for (int8_t i = length - 1; i >= 0; i--) {
            bool bit = (code >> i) & 1;
            transmit_bit(transmitter, bit);
        }

        // Ensure pin is low
        gpio_set_level(transmitter->pin, 0);

        portEXIT_CRITICAL(&mux);

        // Small delay between repeats to let watchdog breathe
        if (repeat < transmitter->repeat_transmit - 1) {
            vTaskDelay(1);
        }
    }

    ESP_LOGI(TAG, "Transmitted: code=%lu, bits=%d, protocol=%d", code, length, transmitter->protocol + 1);
}
