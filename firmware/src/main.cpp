#include <Arduino.h>
#include <Wire.h>

// I2C Pins
#define I2C_SDA 10
#define I2C_SCL 11

// TCA9555 GPIO Expander Address
#define TCA9555_ADDR 0x20

/**
 * Enable the speaker amplifier via the TCA9555 expander.
 * On this board, Pin 8 (P08) of the expander controls the Speaker EN.
 */
void enableSpeaker() {
    Wire.beginTransmission(TCA9555_ADDR);
    Wire.write(0x06); // Configuration register for Port 1 (Pins 8-15)
    Wire.write(0x00); // Set all pins in Port 1 as output (0x00 = output)
    Wire.endTransmission();

    Wire.beginTransmission(TCA9555_ADDR);
    Wire.write(0x02); // Output Port register for Port 1
    Wire.write(0x01); // Set Pin 8 (P08) to HIGH (binary 00000001)
    Wire.endTransmission();
    
    Serial.println("Speaker amplifier enabled.");
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("Initializing Waveshare ESP32-S3-AUDIO-Board...");

    // Initialize I2C for the expander and codecs
    Wire.begin(I2C_SDA, I2C_SCL);
    
    // Enable the speaker
    enableSpeaker();

    Serial.println("System Ready.");
}

void loop() {
    // Add audio logic here
    delay(1000);
}
