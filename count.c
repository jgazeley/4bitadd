/*
 * File: combined_counter_manual_override_with_tune.c
 * Author: Your Name
 *
 * Description:
 * This program runs on an ATmega328P (16 MHz clock) and implements two modes:
 *
 * 1. Counting Mode (if the SPDT mode switch on PC5 reads LOW):
 *    - Group A (PC0–PC3) and Group B (PB0–PB3) are configured as outputs.
 *    - A counter (from 0 to 15) runs with a delay determined by a potentiometer on ADC4 (PC4).
 *    - A pause button on PD2 (active low) permits pausing/resuming the counter.
 *    - When the counter reaches 15, both groups flash three times.
 *
 * 2. Manual Override Mode (if PC5 reads HIGH):
 *    - Groups A and B are reconfigured as inputs with internal pull-ups so that external switches
 *      (wired to 5V) can set their state.
 *
 * Regardless of mode, the program continuously monitors PD3–PD7 (a 5‑bit number, with PD7 as MSB)
 * and prints its value via USART in binary (5 digits), hex, and decimal format only when the value changes.
 *
 * Additionally, when in Manual Override mode, if the PD3–PD7 reading equals 21,
 * the program will play the "secret" tune (from tunes.h) once.
 * A latch prevents repeated retriggering until the PD value changes.
 *
 * A buzzer on PB4 is used both for mode-change beeps and tune playback.
 * USART output is on PD0 (TX) and PD1 (RX).
 */

#include <avr/io.h>
#include <util/delay.h>
#include <util/delay_basic.h>  // For _delay_loop_2()
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "pitches.h"  // Definitions for NOTE_G6, etc., and END.
#include "tunes.h"    // Defines: int secret[] = { NOTE_G6, 16, NOTE_FS6, 16, ..., END };

#ifndef F_CPU
#define F_CPU 16000000UL
#endif

// --- Define MIN_DELAY and MAX_DELAY macros ---
#define MIN_DELAY 30      // Minimum delay in ms.
#define MAX_DELAY 1000    // Maximum delay in ms.
#define DURATION 36000

// -------- Function Prototypes --------
void USART0_init(void);
void USART0_transmit(char data);
void USART0_print(const char *str);
void checkAndPrintPDMonitor(void);
void ADC_init(void);
uint16_t ADC_read(uint8_t channel);
uint16_t getDelayFromPot(void);
void set_counter_mode(void);
void set_manual_override_mode(void);
void beep(uint8_t count);
void tone(uint16_t frequency, uint16_t duration);
void playSong(const int *song);
void delay_ms_custom(uint16_t ms);
void delay_ms_var(uint16_t ms);
void my_delay_us(uint16_t us);

// -------- Global Variables --------
int tempo = 90;                  // Beats per minute.
int wholeNoteDuration;            // Computed whole note duration (ms).
volatile uint8_t paused = 0;      // For pause button operation.
volatile uint8_t secretPlayed = 0; // Latch to prevent repeated tune playback.

// -------- USART Setup Implementation --------
#define BAUD 9600
#define UBRR_VALUE ((F_CPU/16/BAUD)-1)

void USART0_init(void) {
    UBRR0H = (uint8_t)(UBRR_VALUE >> 8);
    UBRR0L = (uint8_t)(UBRR_VALUE);
    UCSR0B = (1 << TXEN0);  // Enable transmitter.
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);  // 8 data bits, no parity, 1 stop bit.
}

void USART0_transmit(char data) {
    while (!(UCSR0A & (1 << UDRE0)))
        ; // Wait for the transmit buffer to be empty.
    UDR0 = data;
}

void USART0_print(const char *str) {
    while (*str)
        USART0_transmit(*str++);
}

// -------- PD Monitor (5-bit from PD3–PD7) --------
// Reads PD3–PD7 and prints the value (binary, hex, and decimal) only when changed.
void checkAndPrintPDMonitor(void) {
    static uint8_t lastValue = 0xFF;  // An impossible initial value.
    uint8_t newValue = (PIND >> 3) & 0x1F;  // Extract bits PD3..PD7.
    if(newValue != lastValue) {
        char binStr[6];
        for (int i = 4; i >= 0; i--) {
            binStr[4 - i] = ((newValue >> i) & 0x01) ? '1' : '0';
        }
        binStr[5] = '\0';
        char buffer[40];
        sprintf(buffer, "PD: %s  %02X  %d\r\n", binStr, newValue, newValue);
        USART0_print(buffer);
        lastValue = newValue;
    }
}

// -------- ADC Setup (Potentiometer on ADC4/PC4) --------
void ADC_init(void) {
    ADMUX = (1 << REFS0);  // Use AVcc as reference.
    ADCSRA = (1 << ADEN)  // Enable ADC.
           | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);  // Prescaler = 128.
}

uint16_t ADC_read(uint8_t channel) {
    ADMUX = (ADMUX & 0xF8) | (channel & 0x07);  // Select channel.
    ADCSRA |= (1 << ADSC); // Start conversion.
    while (ADCSRA & (1 << ADSC))
        ; // Wait for conversion to complete.
    return ADC;
}

uint16_t getDelayFromPot(void) {
    uint16_t potValue = ADC_read(4);  // ADC4 on PC4.
    uint32_t delay = MIN_DELAY + ((uint32_t)potValue * (MAX_DELAY - MIN_DELAY)) / 1023;
    return (uint16_t)delay;
}

// -------- Mode Switching for Groups A (PC0–PC3) and B (PB0–PB3) --------
void set_counter_mode(void) {
    DDRC |= 0x0F;   // Set PC0-3 as outputs.
    PORTC &= 0xF0;  // Clear lower nibble.
    DDRB |= 0x0F;   // Set PB0-3 as outputs.
    PORTB &= 0xF0;  // Clear lower nibble.
}

void set_manual_override_mode(void) {
    DDRC &= ~0x0F;  // Set PC0-3 as inputs.
    PORTC |= 0x0F;  // Enable internal pull-ups on PC0-3.
    DDRB &= ~0x0F;  // Set PB0-3 as inputs.
    PORTB |= 0x0F;  // Enable internal pull-ups on PB0-3.
}

// -------- Simple Variable Millisecond Delay --------
// Loops _delay_ms(1) for the specified ms.
void delay_ms_var(uint16_t ms) {
    for (uint16_t i = 0; i < ms; i++) {
        _delay_ms(1);
    }
}

// -------- Buzzer / Tone Generation --------
// Use buzzer on PB4.
#define BUZZER_DDR   DDRB
#define BUZZER_PORT  PORTB
#define BUZZER_PIN   PB4

// Custom microsecond delay function using _delay_loop_2.
// _delay_loop_2(x) delays for 4*x cycles.
void my_delay_us(uint16_t us) {
    _delay_loop_2(us * 4);
}

void tone(uint16_t frequency, uint16_t duration) {
    if (frequency == 0) {
        delay_ms_var(duration);
        return;
    }
    uint16_t halfPeriod_us = 500000UL / frequency;  // Half period in µs.
    unsigned long cycles = ((unsigned long)duration * 1000UL) / (2 * halfPeriod_us);
    for (unsigned long i = 0; i < cycles; i++) {
        BUZZER_PORT |= (1 << BUZZER_PIN);
        my_delay_us(halfPeriod_us);
        BUZZER_PORT &= ~(1 << BUZZER_PIN);
        my_delay_us(halfPeriod_us);
    }
}

void beep(uint8_t count) {
    for (uint8_t i = 0; i < count; i++) {
        BUZZER_PORT |= (1 << BUZZER_PIN);
        delay_ms_var(50);
        BUZZER_PORT &= ~(1 << BUZZER_PIN);
        delay_ms_var(50);
    }
}

// -------- Song Playback (Tune) --------
// Walks through the song array (from tunes.h) and plays it.
void playSong(const int *song) {
    int noteIndex = 0;
    unsigned long noteDuration;
    while (song[noteIndex] != END) {
        int pitch = song[noteIndex];
        int divider = song[noteIndex + 1];
        if (divider < 0) {
            int base = -divider;
            int dot = -(2 * divider);
            noteDuration = (wholeNoteDuration / dot) + (wholeNoteDuration / base);
        } else {
            noteDuration = wholeNoteDuration / divider;
        }
        uint16_t playTime = (uint16_t)(noteDuration * 0.9);
        tone(pitch, playTime);
        delay_ms_var(noteDuration - playTime);
        noteIndex += 2;
    }
}

// -------- Pause Button and Custom Delay --------
// Uses PD2 as pause button (active low with pull-up).
void delay_ms_custom(uint16_t ms) {
    for (uint16_t i = 0; i < ms; i++) {
        _delay_ms(1);
        if ((i % 10) == 0)
            checkAndPrintPDMonitor();
        if (!(PIND & (1 << PD2))) {
            _delay_ms(50);
            if (!(PIND & (1 << PD2))) {
                paused = !paused;
                while (!(PIND & (1 << PD2)))
                    _delay_ms(1);
                _delay_ms(50);
            }
        }
        while (paused) {
            checkAndPrintPDMonitor();
            if (!(PIND & (1 << PD2))) {
                _delay_ms(50);
                if (!(PIND & (1 << PD2))) {
                    paused = 0;
                    while (!(PIND & (1 << PD2)))
                        _delay_ms(1);
                    _delay_ms(50);
                }
            }
        }
    }
}

// -------- Main Program --------
int main(void) {
    unsigned char count;
    uint8_t lastMode = 0;  // 0: Counting Mode, 1: Manual Override.
    
    // Initialize peripherals.
    USART0_init();
    ADC_init();
    
    // Initialize buzzer on PB4.
    BUZZER_DDR |= (1 << BUZZER_PIN);
    BUZZER_PORT &= ~(1 << BUZZER_PIN);
    
    // Configure pause button on PD2.
    DDRD &= ~(1 << PD2);
    PORTD |= (1 << PD2);  // Enable internal pull-up.
    
    // Configure mode select switch (SPDT) on PC5.
    DDRC &= ~(1 << PC5);
    PORTC |= (1 << PC5);  // Enable internal pull-up.
    
    // PD3–PD7 remain as inputs for the PD monitor.
    
    USART0_print("System Initialized\r\n");
    
    // Compute whole note duration based on tempo.
    wholeNoteDuration = DURATION / tempo;
    
    while (1) {
        checkAndPrintPDMonitor();
        
        // Read mode from PC5: PC5 high => Manual Override, low => Counting.
        uint8_t currentMode = (PINC & (1 << PC5)) ? 1 : 0;
        if (currentMode != lastMode) {
            if (currentMode == 1) {
                beep(1);
                USART0_print("Mode: Manual Override\r\n");
            } else {
                beep(2);
                USART0_print("Mode: Counting\r\n");
            }
            lastMode = currentMode;
        }
        
        if (currentMode == 1) {
            set_manual_override_mode();
            // In Manual Override Mode, check PD3–PD7.
            uint8_t pdValue = (PIND >> 3) & 0x1F;
            if (pdValue == 21 && !secretPlayed) {
                USART0_print("Playing secret tune...\r\n");
                playSong(secret);
                secretPlayed = 1;
            } else if (pdValue != 21) {
                secretPlayed = 0;
            }
            _delay_ms(100);
        } else {
            set_counter_mode();
            for (count = 0; count <= 15; count++) {
                uint16_t delayTime = getDelayFromPot();
                PORTC = (PORTC & 0xF0) | (count & 0x0F);
                delay_ms_custom(delayTime);
                PORTB = (PORTB & 0xF0) | (count & 0x0F);
                delay_ms_custom(delayTime);
                if (PINC & (1 << PC5))
                    break;
            }
            if (count > 15) {
                for (uint8_t i = 0; i < 3; i++) {
                    uint16_t delayTime = getDelayFromPot();
                    PORTC = (PORTC & 0xF0) | 0x0F;
                    PORTB = (PORTB & 0xF0) | 0x0F;
                    delay_ms_custom(delayTime);
                    PORTC &= 0xF0;
                    PORTB &= 0xF0;
                    delay_ms_custom(delayTime);
                }
                PORTC &= 0xF0;
                PORTB &= 0xF0;
                delay_ms_custom(getDelayFromPot());
            }
        }
    }
    
    return 0;
}
