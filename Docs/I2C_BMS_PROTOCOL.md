# DJI Spark BMS I2C/SMBus Protocol Reverse Engineering

This document outlines the decoded I2C/SMBus communication protocol between the DJI Spark Flight Controller (FC) and its proprietary Smart Battery. The data was sniffed and decoded using a Saleae Logic Analyzer.

By understanding this protocol, we can emulate the original DJI battery using a custom microcontroller, allowing the use of a standard 3S LiPo battery pack without triggering FC lockouts or emergency landings.

## 1. Physical Layer & Bus Fundamentals

DJI utilizes the industry-standard **Smart Battery Data Specification (SBS)** over SMBus, layered with their own proprietary authentication commands to lock down the ecosystem.

* **Bus Protocol:** SMBus @ 100kHz
* **Battery I2C Address:** `0x0B` (Standard SBS Address)
* **Logic Level:** 3.3V
* **Byte Order:** Little-Endian (Least Significant Byte first)
* **Integrity Check:** PEC (Packet Error Checking) using **CRC-8** (Polynomial `0x07`). 
  * *Critical:* Every read transaction must conclude with a valid PEC byte. If the PEC is missing or incorrectly calculated, the FC will immediately reject the data and trigger an emergency drop.

---

## 2. Standard SMBus Commands (Dynamic Telemetry)

The FC continuously polls the battery for real-time telemetry. The responses are typically 2 bytes of payload data followed by 1 byte of PEC.

* **`0x08` - Temperature:** * Example Payload: `0xB6 0x0B` -> `0x0BB6` = `2998`
  * Math: Value is in 0.1 Kelvin. (299.8 K = **26.65 °C**)
* **`0x09` - Pack Voltage:** * Example Payload: `0x8B 0x31` -> `0x318B` = `12683`
  * Math: Value is in mV. Result = **12.68 V**
* **`0x0A` - Current:** * Example Payload: `0xD3 0xFD` -> `0xFDD3` = `64979`
  * Math: Represented in Two's Complement for discharge. Result = **-557 mA**
* **`0x0D` - Relative State of Charge (SOC):** * Example Payload: `0x60 0x00` -> `0x0060` = `96`
  * Math: Result = **96%**
* **`0x0F` - Remaining Capacity:** * Example Payload: `0xA7 0x04` -> `0x04A7` = `1191`
  * Math: Value is in mAh. Result = **1191 mAh**
* **`0x17` - Cycle Count:** * Example Payload: `0x63 0x00` -> `0x0063` = `99`
  * Math: Result = **99 cycles**

---

## 3. DJI Proprietary Commands (The "Unknown" Hexes)

DJI extends the SBS standard to pull individual cell voltages and verify the hardware authenticity of the battery. If the custom board fails to provide the exact expected responses to these specific registers, the drone will display an "Invalid Battery" error and refuse to take off.

### A. Cell Voltages
* **`0x3F` - Cell 1 Voltage:** `0x81 0x10` -> `0x1081` = **4225 mV**
* **`0x3E` - Cell 2 Voltage:** `0x86 0x10` -> `0x1086` = **4230 mV**
* **`0x3D` - Cell 3 Voltage:** `0x84 0x10` -> `0x1084` = **4228 mV**

### B. Identification / Handshake Responses
* **`0xD6` - The Magic Handshake:** * Expected Response: `0x55 0xAA 0x66`
  * Note: `55 AA` is a well-known hardware boot signature. The FC uses this to confirm the BMS chip is running authorized firmware.
* **`0xD8` - Battery Serial / Barcode:** * Expected Response: `0x0E 0x30 0x43 0x30 0x41 0x45 0x39 0x56 0x42 0x33 0x33 0x30 0x31 0x53 0x51 0x60`
  * Decoded: The first byte (`0x0E`) is the length (14 characters). The rest is the ASCII representation of the physical barcode printed on the battery: **`0C0AE9VB3301SQ`**.
* **`0xD9` - Encrypted Firmware Version:** * Expected Response: `0x04 0xE2 0x17 0x83 0xD6 0xE3` (Static 4-byte versioning).
* **`0x20` - Manufacturer Name:** ASCII string returning `ATL NVT77777...`
* **`0x21` - Device Name:** ASCII string returning `DJI016ffff...`

---

## 4. The Multiplexer Mechanism (Command `0x00`)

The FC occasionally reads sub-pages of manufacturer data using `0x00` as a page selector/multiplexer.
1. **Write:** Master writes to `0x0B`: `0x00 0x51 0x00` (Instructs BMS to prep page `0x51`).
2. **Read:** Master reads from `0x0B` -> BMS returns block data: `0x04 0x00 0x00 0x00 0x00 0x54`.

The FC repeats this polling for sub-pages `0x51`, `0x53`, `0x54`, and `0x55`. The custom firmware must cache the last requested sub-page address and serve the corresponding static byte array upon the subsequent read request.

---

## 5. Emulation Strategy

To reproduce the expected smart-battery communication behavior:
1. **Dynamic Data:** Read physical analog values via ADC (Overall Voltage, Current via INA199) and map them dynamically to commands `0x09`, `0x0A`, `0x0D`, `0x0F`, `0x3D`, `0x3E`, `0x3F`.
2. **Static Spoofing:** Return hardcoded byte arrays for the authentication commands (`0xD6`, `0xD8`, `0xD9`, `0x20`, `0x21`). The FC does not care if the serial number is always the same, as long as it exists and is formatted correctly.
3. **Flawless CRC:** Pass the `[Address + Read/Write Bit]`, `[Command]`, and `[Payload]` through the CRC-8 algorithm. Append the resulting PEC byte to the end of every transmission. 

---

## 6. Hardware Constraints: The Clock Stretching Trap

When selecting a microcontroller to emulate the DJI Smart Battery, processing speed is less important than low-level hardware protocol compliance. 

The DJI Flight Controller interrogates the battery at high speeds. When the FC requests dynamic data (like cell voltages) or requires a PEC (CRC-8) calculation on the fly, the emulation MCU needs a few microseconds to compute the response before transmitting. 

To prevent the FC from reading incomplete data during this computation window, the emulation MCU **must** physically pull the SCL (Clock) line LOW. This forces the Master (FC) to pause. This mechanism is known as **I2C Clock Stretching**.

**Why AVR / ATmega328P?**
Many modern, high-clock-speed, 32-bit microcontrollers (like the ESP32) have notoriously flawed or strictly software-emulated I2C Slave hardware that fails to execute Clock Stretching reliably. If the clock is not stretched properly, the FC reads garbage data, assumes a catastrophic battery failure, and initiates an immediate drop/shutdown.

The ATmega328P was explicitly chosen for this architecture because its native hardware TWI (Two-Wire Interface) module handles I2C Slave operations and Clock Stretching flawlessly at the silicon level, ensuring the drone's FC never experiences a bus timeout or corrupted packet.