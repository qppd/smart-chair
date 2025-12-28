# Smart Chair

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

An intelligent seating solution powered by Arduino, designed to enhance user comfort and interaction through automated adjustments and sensor-based feedback.

## Table of Contents

- [Description](#description)
- [Features](#features)
- [Hardware Requirements](#hardware-requirements)
- [Software Requirements](#software-requirements)
- [Installation](#installation)
- [Usage](#usage)
- [Project Structure](#project-structure)
- [Circuit Diagram](#circuit-diagram)
- [Contributing](#contributing)
- [License](#license)
- [Contact](#contact)

## Description

The Smart Chair project leverages Arduino microcontroller technology to transform traditional seating into an interactive, adaptive experience. By integrating various sensors and actuators, the system can detect user presence, monitor environmental conditions, and automatically adjust chair settings for optimal comfort and ergonomics.

This project serves as a foundation for IoT-enabled furniture, demonstrating the potential of embedded systems in everyday objects. The current implementation provides a basic framework that can be extended with additional features such as climate control, posture monitoring, and integration with smart home ecosystems.

## Features

- **Occupancy Detection**: Automatically detects when a user sits on the chair
- **Pressure Sensing**: Utilizes 5 thin film pressure sensors to monitor weight distribution and posture
- **Vibration Detection**: 4 vibration sensors near pressure sensors for motion and activity monitoring
- **Posture Monitoring**: 2 flex sensors on back support to track spinal curvature and posture
- **Sensor Integration**: Supports multiple sensor types for environmental monitoring
- **Automated Adjustments**: Programmable responses to user presence and conditions
- **Extensible Design**: Modular architecture for easy addition of new features
- **Low-Power Operation**: Optimized for energy-efficient microcontroller usage

## Hardware Requirements

### Core Components
- Arduino Uno (or compatible board with ATmega328P)
- Power supply (5V DC recommended)
- Jumper wires and breadboard for prototyping

### Sensors
- **5 Thin Film Pressure Sensors** (100kg limit each) for weight and pressure distribution detection
  - Reference: [Thin Film Pressure Sensor on Shopee](https://shopee.ph/product/126253254/2021211981?gads_t_sig=VTJGc2RHVmtYMTlxTFVSVVRrdENkU1psNndicnpENjFrR2ZiZlcxU0ZES0RsbU5keVZKcnAvQzVHVVZocmxzT2ZxU0F3TUNrRERvMy9HY2l2NlF2L3A5dzRybE9BbjhmV0VmVERkM0EzWitsRStKb2krYU80WFVpZUxhK21LYVY&gad_source=1&gad_campaignid=23303611172&gbraid=0AAAAADPpU9BJW_Fea7rnhtzZW-foMf3J_&gclid=Cj0KCQiApL7KBhC7ARIsAD2Xq3DXCacC1htv5ALgjK617ci9V6qacZEQ-gKhLiwxe2URSwPZU7T4T5waAjr1EALw_wcB)
- **4 Vibration Sensors** positioned near the thin film pressure sensors for motion and vibration detection
- **2 Flex Sensors** on the back support for posture and back curvature monitoring
- PIR motion sensor for occupancy detection (optional)
- Temperature/humidity sensor (e.g., DHT11/DHT22) for environmental monitoring (optional)
- Ultrasonic sensors for proximity measurement (optional)

### Actuators (optional, for advanced features)
- LED indicators for status display
- Buzzer for audio feedback

## Software Requirements

- Arduino IDE (version 1.8.0 or later)
- Arduino AVR Boards package (included with IDE)
- Fritzing (for viewing/editing wiring diagrams)

## Installation

### Hardware Setup
1. Assemble the circuit according to the wiring diagram (see [Circuit Diagram](#circuit-diagram))
2. Connect sensors and actuators to the appropriate Arduino pins
3. Power the Arduino board via USB or external power supply

### Software Setup
1. Download and install the Arduino IDE from [arduino.cc](https://www.arduino.cc/en/software)
2. Clone this repository:
   ```bash
   git clone https://github.com/qppd/smart-chair.git
   ```
3. Open `source/SmartChair/SmartChair.ino` in Arduino IDE
4. Select your Arduino board from Tools > Board
5. Select the correct port from Tools > Port
6. Click Upload to flash the code to your Arduino

## Usage

1. Power on the Arduino board
2. The system will initialize and begin monitoring sensors
3. Sit on the chair to trigger occupancy detection
4. Observe LED indicators or serial output for system status
5. Customize behavior by modifying the Arduino sketch

For debugging, open the Serial Monitor in Arduino IDE (Tools > Serial Monitor) to view sensor readings and system messages.

## Project Structure

```
smart-chair/
├── source/
│   └── SmartChair/
│       └── SmartChair.ino          # Main Arduino sketch
├── wiring/
│   ├── Wiring.fzz                  # Fritzing circuit diagram
│   └── Wiring.png                  # Wiring diagram image
├── diagram/                        # Additional project diagrams
├── LICENSE                         # MIT License
└── README.md                       # This file
```

## Circuit Diagram

The hardware connections are documented in the wiring directory:

- **[Wiring.fzz](wiring/Wiring.fzz)**: Editable Fritzing diagram for circuit design
- **[Wiring.png](wiring/Wiring.png)**: Static image of the wiring schematic

Use Fritzing software to view and modify the circuit diagram. Ensure all connections match the pin assignments in the Arduino code.

## Contributing

We welcome contributions to the Smart Chair project! Here's how you can help:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

### Development Guidelines
- Follow Arduino coding best practices
- Document any new hardware additions
- Update wiring diagrams for circuit changes
- Test thoroughly on target hardware

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Contact

**Sajed Lopez Mendoza**

- Project Repository: [https://github.com/qppd/smart-chair](https://github.com/qppd/smart-chair)
- Email: [your-email@example.com] (replace with actual contact)

For questions, issues, or suggestions, please open an issue on GitHub or contact the maintainer directly.