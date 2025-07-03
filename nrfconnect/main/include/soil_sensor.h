#pragma once

#include <stdint.h>

// Read raw ADC moisture level and return scaled value (0–100%)
uint16_t ReadSoilMoistureSensor();

// Simulated temperature readout for demonstration
int16_t ReadTemperatureSensor(); // returns in 0.01 °C units (Matter spec)