# SparkAltBatteryBoard Firmware

Firmware that reproduces the I2C/SMBus communication interface of the DJI Spark's original Smart Battery. The goal is to respond correctly to the SBS + DJI proprietary commands expected by the flight controller (FC), so that a third-party/aftermarket 3S LiPo battery pack can be used without triggering an "Invalid Battery" error or an emergency landing.

## Hardware

- **Target MCU:** ATmega328P (`nanoatmega328` in PlatformIO, Arduino Uno/Nano pinout).
- **Why ATmega328P:** The DJI FC polls the battery at high speed, and the slave needs a few microseconds to compute the PEC (CRC-8) before responding. This requires hardware **clock stretching** on the I2C slave side (physically holding SCL low). Most modern 32-bit MCUs (e.g. ESP32) don't reliably support clock stretching in their I2C slave peripheral; the ATmega328P's native TWI module handles it correctly.
- **I2C slave address:** `0x0B` (standard SBS battery address).

### Pin map (`src/main.cpp`)

| Pin | Define | Purpose |
|---|---|---|
| A7 | `BATTERY_PIN_1` | BATCELL1 net — cell1 divider tap (4.7k/10k) |
| A0 (PC0) | `BATTERY_PIN_2` | BATCELL2 net — cell1+cell2 divider tap (20k/10k) |
| A1 (PC1) | `BATTERY_PIN_3` | BATCELL3 net — full pack divider tap (33k/10k) |
| A6 | `CURRENT_PIN` | INA199 shunt amplifier output (current sense) |
| A3 | `THERMISTOR_PIN` | NTC input — unused on the current hardware revision (see Known Limitations) |
| 8 | `LED_PIN` | I2C activity LED |
| A4 / A5 | SDA / SCL | I2C bus (Arduino `Wire` library defaults) |

Each divider tap shares a common 10kΩ bottom resistor against a different top resistor (4.7k / 20k / 33k), stepping down each cumulative cell voltage into the ADC's 0–3.3V range. `readVoltage()` takes the matching ratio (`CELL1_DIVIDER_RATIO`, `CELL2_DIVIDER_RATIO`, `PACK_DIVIDER_RATIO`) as a parameter for each pin.

## Protocol

- **Bus:** SMBus, little-endian byte order.
- **PEC (Packet Error Checking):** Every response ends with a CRC-8 byte (poly `0x07`, computed in `pec()`). If the FC can't validate this byte it rejects the data and can trigger an emergency landing, so every dynamic response function computes the PEC on the fly over `[address+W, command, address+R, data_lo, data_hi]`.
- Commands fall into two groups:
  - **Dynamic commands:** computed from live ADC measurements and sent.
  - **Static commands:** fixed bytes sniffed from the original DJI battery, sent back verbatim (auth/handshake, serial number, manufacturer name, etc.) — the FC only cares that these are always present and consistent, not that they're live.

### Supported commands

| Command | Meaning | Type |
|---|---|---|
| `0x08` | Temperature | Dynamic (currently fixed at 25°C — no NTC) |
| `0x09` | Pack Voltage | Dynamic |
| `0x0A` | Current | Dynamic (INA199) |
| `0x0D` | Relative State of Charge | Emulated / placeholder |
| `0x0F` | Remaining Capacity | Emulated / derived/static placeholder |
| `0x10` | Full Charge Capacity | Static |
| `0x17` | Cycle Count | Fixed (1) |
| `0x18` | Design Capacity | Static |
| `0x19` | Design Voltage | Static |
| `0x1B` | Manufacture Date | Static |
| `0x1C` | Serial Number | Static |
| `0x20` | Manufacturer Name | Static |
| `0x21` | Device Name | Static |
| `0x3D` / `0x3E` / `0x3F` | Cell3 / Cell2 / Cell1 Voltage | Dynamic |
| `0x00` | Manufacturer Data (mux, sub-page `0x51`/`0x53`/`0x54`/`0x55`) | Static |
| `0xD6` | Auth handshake (`55 AA 66`) | Static |
| `0xD8` | Barcode / serial | Static |
| `0xD9` | Encrypted firmware version | Static |
| `0xD2`, `0xD5`, `0xC2`, `0x23`, `0x66`, `0x2F` | Other DJI proprietary commands | Static |

Note: This firmware is not a calibrated fuel-gauge implementation. SOC and remaining-capacity related values are emulated for protocol compatibility and should not be used as accurate battery state estimation.

## Build & flash

With PlatformIO:

```
pio run -e uno
pio run -e uno -t upload
```

## File layout

- `src/main.cpp` — I2C slave logic, ADC reads, PEC calculation, all command responses.
- `platformio.ini` — build target: ATmega328P, 16MHz, Arduino framework.
- `DJISPoofer.xlsx` — raw I2C command/response data collected during reverse engineering (reference).

## Known limitations

- Temperature: the physical NTC was removed on the current hardware revision, so `getThermistorReading()` returns a fixed 25°C (`2982`, in 0.1K units).
- Values like Design Voltage, Design Capacity, and Cycle Count are static/fixed and independent of the actual hardware (should be set by hand to match the real battery spec).
