# Internet-of-things_IOT
🔒 Secure IoT Communication System (ESP32 + AES-128-GCM + MQTT)
📌 Project Overview

This project presents a lightweight, secure, and reliable end-to-end communication system tailored for Resource-Constrained Internet of Things (IoT) environments. Built using dual ESP32 microcontrollers, the system ensures data confidentiality, authenticity, and message integrity using symmetric AES-128-GCM encryption over the MQTT messaging protocol.

Developed as part of the IoT coursework at Al-Razi University (First Semester 2026) under the supervision of Dr. Majd Alsurihi.
✨ Key Features

    Authenticated Encryption: Implements AES-128-GCM via mbedtls to provide confidentiality and anti-tampering (Authentication Tag).

    Wireless Protocol: Uses lightweight MQTT protocol for real-time pub/sub message delivery.

    Replay Attack Defense: Dynamic Nonce generation for every transmitted packet.

    Reliable Feedback: Automated Acknowledgment (ACK) feedback system with non-blocking timeouts (ACK_TIMEOUT).

    Local User Interface:

        4x4 Matrix Keypad for message input.

        16x2 I2C LCD for real-time visual status updates.

🛠️ Hardware & Software Architecture
Hardware Components

    MCU: ESP32 Microcontrollers (Transmitter & Receiver Nodes)

    Input Device: 4x4 Matrix Keypad

    Display: 16x2 Character LCD with I2C Backpack Module

    Connectivity: Wi-Fi & MQTT Broker

Software & Libraries

    Framework: Arduino IDE / C++

    Crypto Library: mbedtls (AES-128-GCM)

    IoT Protocols: PubSubClient (MQTT)

    Interface Libraries: LiquidCrystal_I2C, Keypad

🚀 How It Works

    Input & Encryption: The user inputs a text message using the 4x4 Keypad. The sender node encrypts the payload using AES-128-GCM with a dynamic Nonce.

    Transmission: The encrypted payload, along with the auth tag and nonce, is published to the MQTT broker.

    Decryption & Validation: The receiver node subscribes to the topic, decrypts the payload, and verifies message integrity via the auth tag.

    Display & ACK: If valid, the clear text appears on the receiver’s I2C LCD, and an automated ACK response is transmitted back to the sender.

⚙️ Installation & Setup

    Clone this repository:
    Bash

    git clone https://github.com/YourUsername/Secure-IoT-Communication-System.git

    Open the project in Arduino IDE.

    Install required libraries:

        PubSubClient

        LiquidCrystal_I2C

        Keypad

    Configure your Wi-Fi credentials and MQTT broker details in config.h (or top of the sketch).

    Flash the code to your ESP32 board.

📝 Future Improvements

    Implementation of dynamic key exchange algorithms (e.g., ECDH).

    Integration of ultra-lightweight ciphers like PRESENT-80 for extremely constrained 8-bit sensor nodes.

    Expansion to multi-node mesh topologies.

📜 License & Citation

Designed for academic and educational purposes at Al-Razi University. Feel free to fork and build upon this repository!
