# NextRound (WIP)
*A smart party breathalyzer*

NextRound is a self-built, portable breathalyzer designed as a technical showcase in embedded systems, hardware design, and user interface development. This project is inspired by [ESP32_Brethalyzer](https://github.com/Edvinkajen/ESP32-Breathalyzer), which represents the first iteration of this project created during a university electronic projects course. NextRound builds upon that foundation with a significantly more advanced hardware platform, refined measurement logic, and a more polished and robust system design.

The device measures alcohol concentration using a semiconductor-based ethanol sensor and presents the result directly on an onboard OLED display. Users can create profiles, view measurement history, and interact with various party-oriented game modes that run entirely on the device. The goal is to turn breathalyzer measurements into a social experience rather than a purely functional one.

## Hardware Overview

The hardware is built around an ESP32-S3 microcontroller, providing both Wi-Fi and Bluetooth connectivity. The system is battery-powered and designed for low power consumption, with integrated charging and power-path management. Additional sensors are used to enhance both usability and gameplay.

Key hardware components include:
- ESP32-S3 microcontroller with Wi-Fi and BLE 
- Semiconductor ethanol sensor for alcohol detection (MP-3B)
- Electret microphone as airflow sensor
- OLED display and push button for User Interface
- Buzzer, Vibration motor and neopixel for user feedback
- Accelerometer for motion-based interactions and games
- AHT20 temperature/Humidity sensor for enviromental calibration
- 800mAh 14500 lithium Ion cell
- Battery management, charging, and basic fuel gauging circuitry  

All electronics are designed on a custom PCB with a compact form factor, optimized for portability.

## Software & Features

The firmware is structured to be modular and extensible. A lightweight UI framework handles menus, icons, animations, and dynamic text on the small display. Game logic, sensor handling, and communication are clearly separated to allow easy expansion.

Main features include:
- Sensor measurement logic
- On-device user profiles and measurement history 
- Local storage
- Bluetooth/Wi-Fi communication for webui or external apps
- Power-aware behavior with sleep modes and battery monitoring
- Party and game modes using motion and timing
- OTA firmware updates, allowing the end user to easily update firmware over WiFi

## Project Status

> [!NOTE]
> NextRound is an active hobby and learning project. Both hardware and firmware are under continuous development, with planned improvements in sensor calibration, UI polish, and mobile app support.
> ***If you come up with a good solution or a feature you'd like me to add, feel free to open a pull request and i'll take a look!***

## ToDo
- Finish measurement logic and WiFi/BLE communication.
- Replace 3.3V linear regulator with a Buck/Boost converter.
- Review choice of charging ic. Look over cheaper syncrounous alternatives with powerpath managment.
- Implement sepaerate fuel gauge ic or implement circuitry for [BatterySense](https://github.com/rlogiacco/BatterySense) by rlogiacco.
- Implement party modes.
- Finish mobile app.
- Review alternatives to electret, thats not as sensitive to moisture. Hydrophobic membrane and higher sensitivity?

> [!WARNING]  
> This project is intended for educational and entertainment purposes only. It is not a certified medical device and must not be used for legal or safety-critical decisions such as driving.
