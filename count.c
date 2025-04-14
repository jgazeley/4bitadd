/*
 * File: combined_counter_manual_override_PDmonitor_changeOnly.c
 * Author: Your Name
 *
 * Description:
 * This program runs on an ATmega328P (16 MHz clock) and implements two modes:
 *
 *  1. Counting Mode (if the SPDT switch on PC5 reads LOW):
 *     - Group A (PC0–PC3) and Group B (PB0–PB3) are configured as outputs.
 *     - A counter (from 0 to 15) runs with a delay determined by a potentiometer on ADC4 (PC4).
 *     - A pause button on PD2 (active low) permits pausing/resuming the counter.
 *     - When the counter reaches 15, both groups flash three times.
 *
 *  2. Manual Override Mode (if PC5 reads HIGH):
 *     - Groups A and B are reconfigured as inputs with internal pull-ups so that external switches
 *       (wired to 5V) can set their state.
 *
 * Regardless of mode, the program continuously monitors PD3–PD7 (a 5‑bit value with PD7 as MSB)
 * and prints its value in binary (5 digits), hexadecimal, and decimal format via USART
 * only when the value changes.
 *
 * Additionally, a buzzer on PB4 beeps on mode changes:
 *   - 1 beep when entering Manual Override.
 *   - 2 beeps when entering Counting Mode.
 *
 * USART output is on PD0 (TX) and PD1 (RX).
 */

#include <avr/io.h>
#include <util/delay.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef F_CPU
#define F_CPU 16000000UL
#endif

// ---------------- USART Setup ----------------
#define BAUD 9600
#define UBRR_VALUE ((F_CPU/16/BAUD)-1)

void USART0_init(void) {
    UBRR0H = (uint8_t)(UBRR_VALUE >> 8);
    UBRR0L = (uint8_t)(UBRR_VALUE);
    UCSR0B = (1 << TXEN0);  // Enable transmitter
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);  // 8 data bits, no parity, 1 stop bit.
}

void USART0_transmit(char data) {
    while (!(UCSR0A & (1 << UDRE0)))
        ; // Wait for transmit buffer empty.
    UDR0 = data;
}

void USART0_print(const char *str) {
    while (*str)
        USART0_transmit(*str++);
}

// ---------------- PD Monitor (Change Only) ----------------
// This function reads PD3–PD7 (5 bits, PD7 as MSB) and prints the
// value in binary, hex, and decimal only if it has changed.
void checkAndPrintPDMonitor(void) {
    static uint8_t lastValue = 0xFF;  // Invalid initial value.
    uint8_t newValue = (PIND >> 3) & 0x1F;  // Extract PD3..PD7.
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

// ---------------- ADC Setup (Potentiometer on ADC4/PC4) ----------------
void ADC_init(void) {
    ADMUX = (1 << REFS0);  // AVcc as reference.
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

#define MIN_DELAY 30      // Minimum delay in ms.
#define MAX_DELAY 1000    // Maximum delay in ms.

uint16_t getDelayFromPot(void) {
    uint16_t potValue = ADC_read(4);  // ADC channel 4 (PC4)
    uint32_t delay = MIN_DELAY + ((uint32_t)potValue * (MAX_DELAY - MIN_DELAY)) / 1023;
    return (uint16_t)delay;
}

// ---------------- Mode Switching for Groups A and B ----------------
// Group A: PC0–PC3; Group B: PB0–PB3.
// In Counting Mode these pins are outputs; in Manual Override they are inputs with pull-ups.
void set_counter_mode(void) {
    DDRC |= 0x0F;   // Set PC0-3 as outputs.
    PORTC &= 0xF0;  // Clear lower nibble.
    DDRB |= 0x0F;   // Set PB0-3 as outputs.
    PORTB &= 0xF0;  // Clear lower nibble.
}

void set_manual_override_mode(void) {
    DDRC &= ~0x0F;  // Set PC0-3 as inputs.
    PORTC |= 0x0F;  // Enable pull-ups on PC0-3.
    DDRB &= ~0x0F;  // Set PB0-3 as inputs.
    PORTB |= 0x0F;  // Enable pull-ups on PB0-3.
}

// ---------------- Buzzer (on PB4) ----------------
#define BUZZER_PIN PB4

void beep(uint8_t count) {
    for (uint8_t i = 0; i < count; i++) {
        PORTB |= (1 << BUZZER_PIN);   // Turn buzzer ON.
        _delay_ms(50);
        PORTB &= ~(1 << BUZZER_PIN);  // Turn buzzer OFF.
        _delay_ms(50);
    }
}

// ---------------- Pause Button and Custom Delay Routine ----------------
// Pause button on PD2 (active low with internal pull-up).
volatile uint8_t paused = 0;

void delay_ms_custom(uint16_t ms) {
    for (uint16_t i = 0; i < ms; i++) {
        _delay_ms(1);
        // Check and update PD monitor every 10 ms.
        if ((i % 10) == 0)
            checkAndPrintPDMonitor();
        // Check pause button on PD2.
        if (!(PIND & (1 << PD2))) {
            _delay_ms(50);  // Debounce.
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

// ---------------- Main Program ----------------
int main(void) {
    unsigned char count;
    uint8_t lastMode = 0;  // 0: Counting Mode, 1: Manual Override.
    
    // Initialize peripherals.
    USART0_init();
    ADC_init();
    
    // Initialize buzzer on PB4.
    DDRB |= (1 << BUZZER_PIN);
    PORTB &= ~(1 << BUZZER_PIN);
    
    // Configure pause button on PD2.
    DDRD &= ~(1 << PD2);
    PORTD |= (1 << PD2);  // Enable internal pull-up.
    
    // Configure mode select switch (SPDT) on PC5.
    DDRC &= ~(1 << PC5);
    PORTC |= (1 << PC5);  // Enable internal pull-up.
    
    // Ensure PD3–PD7 are inputs (for the PD monitor).
    // PD2, already set, plus PD3-PD7 in their default state.
    
    USART0_print("System Initialized\r\n");
    
    while (1) {
        // Always check and print PD monitor (only if changed).
        checkAndPrintPDMonitor();
        
        // Read mode selection from PC5.
        // PC5 high => Manual Override Mode; PC5 low => Counting Mode.
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
            // Manual Override Mode:
            set_manual_override_mode();
            // Just wait (external switches drive A and B).
            _delay_ms(500);
        } else {
            // Counting Mode:
            set_counter_mode();
            // Run counter from 0 to 15.
            for (count = 0; count <= 15; count++) {
                uint16_t delayTime = getDelayFromPot();
                // Update Group A.
                PORTC = (PORTC & 0xF0) | (count & 0x0F);
                delay_ms_custom(delayTime);
                // Update Group B.
                PORTB = (PORTB & 0xF0) | (count & 0x0F);
                delay_ms_custom(delayTime);
                // Break out if mode changes.
                if (PINC & (1 << PC5))
                    break;
            }
            // If counter finished (count > 15), flash both groups 3 times.
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
