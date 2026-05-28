# FanCam

FanCam is an ESP32-CAM project that combines live MJPEG video streaming with a fan controller, tachometer feedback, LED dimming, OTA updates, and a polished browser dashboard.

## Features

- OV2640 camera streaming over a web dashboard
- 4-pin PWM fan control with tachometer monitoring
- Stall detection and safety handling
- Flash LED dimming and pulse modes
- Persistent settings stored in NVS
- OTA firmware updates
- Responsive single-page UI
- WiFi setup from the dashboard

## Hardware

- AI-Thinker ESP32-CAM
- OV2640 camera module
- Fan controlled from GPIO13
- Tachometer input on GPIO2
- Flash LED on GPIO4

## Notes

- The sanitized copy uses placeholder WiFi defaults.
- Configure your network from the web UI after flashing.
- Designed for the AI-Thinker ESP32-CAM board profile.

