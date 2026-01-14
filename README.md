# SlEirig™
*A smart party breathalyzer*

SlEirig™ is a self-built, portable breathalyzer designed as a technical showcase in embedded systems, hardware design, and user interface development. The project blends sensor-based measurement with fun, social features, making it equally suitable for experimentation, learning, and demonstration purposes.

The device measures alcohol concentration using a semiconductor-based ethanol sensor and presents the result directly on an onboard OLED display. Users can create profiles, view measurement history, and interact with various party-oriented game modes that run entirely on the device. The goal is to turn breathalyzer measurements into a social experience rather than a purely functional one.

## Hardware Overview

The hardware is built around an ESP32-S3 microcontroller, providing both Wi-Fi and Bluetooth connectivity. The system is battery-powered and designed for low power consumption, with integrated charging and power-path management. Additional sensors are used to enhance both usability and gameplay.

Key hardware components include:
- ESP32-S3 microcontroller with Wi-Fi and BLE  
- Semiconductor ethanol sensor for alcohol detection
- Electret microphone as airflow sensor 
- OLED display for local UI
- Buzzer, Vibration motor and neopixel for user feedback
- Accelerometer for motion-based interactions and games
- AHT20 temperature/Humidity sensor for enviromental calibration  
- Battery management, charging, and fuel gauging circuitry  

All electronics are designed on a custom PCB with a compact form factor, optimized for portability and iterative prototyping.

## Software & Features

The firmware is structured to be modular and extensible. A lightweight UI framework handles menus, icons, animations, and dynamic text on the small display. Game logic, sensor handling, and communication are clearly separated to allow easy expansion.

Main features include:
- On-device user profiles and measurement history  
- Local storage for measurement history and user profiles
- Sensor measurement logic
- Party and game modes using motion and timing  
- Bluetooth/Wi-Fi communication for external apps 
- Power-aware behavior with sleep modes and battery monitoring  

## Project Status

Alkoblås is an active hobby and learning project. Both hardware and firmware are under continuous development, with planned improvements in sensor calibration, UI polish, and companion app support.

> ⚠️ **Disclaimer**  
> This project is intended for educational and entertainment purposes only. It is not a certified medical device and must not be used for legal or safety-critical decisions such as driving.
