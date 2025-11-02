#ifndef RC_SWITCH_H
#define RC_SWITCH_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t pulse_length;
    struct {
        uint8_t high;
        uint8_t low;
    } sync_factor;
    struct {
        uint8_t high;
        uint8_t low;
    } zero;
    struct {
        uint8_t high;
        uint8_t low;
    } one;
    bool invert_signal;
} rc_protocol_t;

typedef struct {
    gpio_num_t pin;
    bool available;
    uint32_t received_value;
    uint8_t received_bitlength;
    uint8_t received_protocol;
    uint16_t received_delay;
    volatile int64_t last_time;
    volatile uint32_t duration_buffer[256];
    volatile uint8_t buffer_pos;
} rc_receiver_t;

typedef struct {
    gpio_num_t pin;
    uint8_t protocol;
    uint16_t pulse_length;
    uint8_t repeat_transmit;
} rc_transmitter_t;

// Receiver API: for signal reception and decoding from an RF receiver module.
void rc_receiver_init(rc_receiver_t* receiver, gpio_num_t pin);
bool rc_receiver_available(rc_receiver_t* receiver);
uint32_t rc_receiver_get_value(rc_receiver_t* receiver);
uint8_t rc_receiver_get_bitlength(rc_receiver_t* receiver);
uint8_t rc_receiver_get_protocol(rc_receiver_t* receiver);
uint16_t rc_receiver_get_delay(rc_receiver_t* receiver);
void rc_receiver_reset(rc_receiver_t* receiver);
uint32_t rc_receiver_get_isr_count(void);

// Transmitter API: for configuring and sending RF codes via a transmitter module.
void rc_transmitter_init(rc_transmitter_t* transmitter, gpio_num_t pin);
void rc_transmitter_set_protocol(rc_transmitter_t* transmitter, uint8_t protocol);
void rc_transmitter_set_pulse_length(rc_transmitter_t* transmitter, uint16_t pulse_length);
void rc_transmitter_set_repeat(rc_transmitter_t* transmitter, uint8_t repeat);
void rc_transmitter_send(rc_transmitter_t* transmitter, uint32_t code, uint8_t length);

#ifdef __cplusplus
}
#endif

#endif // RC_SWITCH_H
