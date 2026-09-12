# Home Gas Leak Alarm System with ESP32

A system for detecting gas leaks in residential units using MQ135 sensors and ESP32 devices. When gas is detected, it automatically triggers a local alarm and notifies the unit owner and the building guard via SMS or voice call (Kavenegar service).

## Features

- Gas leak detection with an MQ135 sensor in each unit
- Local siren/buzzer activation in the unit and a central buzzer on the main device
- Automatic alert notifications (SMS or voice call) to the unit owner and guard via the Kavenegar web service
- Local web panel showing real-time unit status (no page refresh needed)
- Password-protected settings panel for configuring phone numbers and notification method for each unit and the guard
- Settings stored in onboard memory (Preferences) so they persist after power loss
- Communication between the unit device and the central device over Bluetooth

## Project structure

- **main.cpp** — Central device code: hosts the local web server, settings panel, and sends SMS/call alerts
- **main(1).cpp** — Unit device code: reads the gas sensor, activates the local siren/buzzer, and sends alerts to the central device

## Hardware required

- 2x ESP32 boards (one for the central device, one per unit)
- MQ135 gas sensor for each unit
- Siren/buzzer for each unit, and a buzzer for the central device

## Setup

1. Set your Kavenegar API credentials and default phone numbers in `central.cpp`.
2. Calibrate and set the gas detection threshold (`GAS_THRESHOLD`) in `units.cpp`.
3. Change the settings panel password (`SETTINGS_PASSWORD`).
4. Upload each device's code to its corresponding ESP32 via the Arduino IDE.

## Security note

The Kavenegar API key and the default settings-panel password are hardcoded in the source. Before publishing or deploying, be sure to change these values and avoid committing real credentials to a public GitHub repository.
