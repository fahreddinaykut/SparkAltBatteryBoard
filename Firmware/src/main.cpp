#include <Arduino.h>
#include <Wire.h>

#define Unknown0xD5Request 0xD5
#define DesignCapacityRequest 0x18
#define CycleCountRequest 0x17
#define DesignVoltageRequest 0x19
#define ManufactureDateRequest 0x1B
#define SerialNumberRequest 0x1C
#define ManufacturerNameRequest 0x20
#define DeviceNameRequest 0x21
#define ManufacturerDataRequest 0x00

#define PackVoltageRequest 0x09
#define CurrentRequest 0x0A
#define FullChargeCapacityRequest 0x10
#define RemainingCapacityRequest 0x0F
#define TemperatureRequest 0x08
#define RelativeStateOfChargeRequest 0x0D
#define Unknown0x51Request 0x51
#define Unknown0xD2Request 0xD2
#define CellVoltage1Request 0x3F
#define CellVoltage2Request 0x3E
#define CellVoltage3Request 0x3D
#define Unknown0xD6Request 0xD6
#define Unknown0xD9Request 0xD9
#define Unknown0xD8Request 0xD8
#define Unknown0xC2Request 0xC2

#define ManufacturerReadRequest 0x23
#define Unkown0x66Request 0x66
#define Unknown0x2FRequest 0x2F

#define BATTERY_PIN_1 A7 // BATCELL1 net: cell1 tap
#define BATTERY_PIN_2 A0 // BATCELL2 net: cell1+cell2 tap (PC0)
#define BATTERY_PIN_3 A1 // BATCELL3 net: full pack tap (PC1)
#define THERMISTOR_PIN A3
#define CURRENT_PIN A6 // INA199 shunt amplifier output
#define LED_PIN 8
#define ADCReferanceVoltage 3.3f
// Each ADC tap has its own top resistor (Rtop) against a shared 10k bottom resistor (Rbottom),
// ratio = Rbottom / (Rtop + Rbottom). Bigger tap voltage needs a bigger Rtop to stay in ADC range.
constexpr float CELL1_DIVIDER_RATIO = 10.0f / (4.7f + 10.0f); // BATTERY_PIN_1 (BATCELL1): cell1 tap, 4.7k/10k
constexpr float CELL2_DIVIDER_RATIO = 10.0f / (20.0f + 10.0f); // BATTERY_PIN_2 (BATCELL2): cell1+cell2 tap, 20k/10k
constexpr float PACK_DIVIDER_RATIO = 10.0f / (33.0f + 10.0f); // BATTERY_PIN_3 (BATCELL3): full pack tap, 33k/10k
uint8_t I2C_ADDRESS = 0x0B;

// Sensor values refreshed every 100ms in loop(); requestEvent()/sendAnswer() (I2C ISR)
// only reads these, it never calls analogRead() itself so it can't stall the I2C bus.
volatile int currentCell1Mv = 0;
volatile int currentCell2Mv = 0;
volatile int currentCell3Mv = 0;
volatile int currentPackMv = 0;
volatile int currentTemperature = 0;
volatile int currentDrawMa = 0; // from INA199
volatile int currentPercent = 0;

void receiveEvent(int bytes);
void requestEvent();
void sendAnswer(byte command);
void cycleCountResponse(int cycleCount);
void designVoltageResponse(int voltage);
void packVoltageResponse(int voltage);
void currentResponse(int current);
void fullChargeCapacityResponse(int capacity);
void remainingCapacityResponse(int capacity);
void temperatureResponse(int temperature);
void relativeStateOfChargeResponse(int stateOfCharge);
void cellVoltage1Response(int voltage);
void cellVoltage2Response(int voltage);
void cellVoltage3Response(int voltage);
int readVoltage(int pin, float dividerRatio);
int getThermistorReading();
int voltageToPercentage(float voltage);
#define CycleCount 0x63
#define DesignVoltage 0x88
#define PackVoltage 0xB8
#define Current 0xCE
#define FullChargeCapacity 0xDA
#define RemainingCapacity 0x50
#define Temperature 0xB0
#define RelativeStateOfCharge 0x59
#define CellVoltage1 0x3C
#define CellVoltage2 0x3F
#define CellVoltage3 0x3C

int cycleCount = 1;
int designCapacity = 1480;
int designVoltage = 11400;
int packVoltage = 12472;
int cur = 64910;
int remainingCapacity = 1104;
int fullChargeCapacity = 1480;
int temp = 2936;
int relativeStateOfCharge = 89;

uint8_t Unknown0xD5Response[] = {0x00, 0x00, 0x4E};
uint8_t DesignCapacityResponse[] = {0xC8, 0x05, 0x44};
uint8_t CycleCountResponse[] = {0x63, 0x00, 0x02}; // for crc 0x16 0x17 0x17 0x63 0x00 = 0x2
uint8_t DesignVoltageResponse[] = {0x88, 0x2C, 0xD6};
uint8_t ManufactureDateResponse[] = {0x3E, 0x4B, 0xF9};
uint8_t SerialNumberResponse[] = {0x98, 0x03, 0x02};
uint8_t ManufacturerNameResponse[] = {0x07, 0x41, 0x54, 0x4C, 0x20, 0x4E, 0x56, 0x54, 0x37, 0x37, 0x37, 0x37, 0x37, 0x37, 0x37, 0x37, 0x37, 0x37, 0x37, 0x37, 0x37, 0x37};
uint8_t DeviceNameResponse[] = {0x06, 0x44, 0x4A, 0x49, 0x30, 0x31, 0x36, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66};
uint8_t PackVoltageResponse[] = {0xB8, 0x30, 0x1C};
uint8_t RelativeStateOfChargeResponse[] = {0x59, 0x00, 0x82};
uint8_t Unknown0x51Response[] = {0x04, 0x00, 0x00, 0x00, 0x00, 0xDA};
uint8_t Unknown0xD2Response[] = {0x00, 0x00, 0x2C};
uint8_t CellVoltage1Response[] = {0x3C, 0x10, 0xC3};
uint8_t CellVoltage2Response[] = {0x3F, 0x10, 0xEA};
uint8_t CellVoltage3Response[] = {0x3C, 0x10, 0xEF};
uint8_t Unknown0xD6Response[] = {0x55, 0xAA, 0x66};
uint8_t Unknown0xD9Response[] = {0x04, 0xE2, 0x17, 0x83, 0xD6, 0xE3};
uint8_t Unknown0xD8Response[] = {0x0E, 0x30, 0x43, 0x30, 0x41, 0x45, 0x39, 0x56, 0x42, 0x33, 0x33, 0x30, 0x31, 0x53, 0x51, 0x60};
uint8_t Unknown0xC2Response[] = {0x08, 0x01, 0x00, 0x00, 0x00, 0x55, 0x00, 0x00, 0x01, 0xAE};
uint8_t ManufacturerData51[] = {0x04, 0x00, 0x00, 0x00, 0x00, 0x54};
uint8_t ManufacturerData53[] = {0x04, 0x00, 0x00, 0x00, 0x00, 0x54};
uint8_t ManufacturerData54[] = {0x04, 0x86, 0x03, 0x06, 0x00, 0xD2};
uint8_t ManufacturerData55[] = {0x02, 0x10, 0x08, 0x16, 0x16, 0x16};

uint8_t Unkown0x2FResponse[] = {0x14, 0x93, 0x8D, 0X0C, 0X00, 0X86, 0X8, 0X90, 0XE6, 0XCD, 0XC8, 0XF8, 0XE2, 0X98, 0XA0, 0X8B, 0XF2, 0X57, 0X4B, 0X10, 0XEE, 0X55};
uint8_t Unkown0x66Response[] = {0x20, 0x94, 0x48, 0xFA, 0x63, 0x01, 0x60, 0xE4, 0x04, 0xA8, 0x10, 0xB1, 0x10, 0x0, 0x0, 0x0, 0x0, 0xEC, 0x04, 0x0, 0x0, 0x0A, 0x32, 0x1A, 0x17, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0xC8};
uint8_t Unkown0x23Response[] = {0x4, 0x00, 0x00, 0x00, 0x00, 0x54};

uint8_t lastCommandIn = 0;
uint8_t lastManufacturerDataAddressIn = 0;

#define SMBUS_CRC8_POLY 0x07
uint8_t pec(uint8_t *data, uint8_t len)
{
  uint8_t crc = 0;
  for (int i = 0; i < len; i++)
  {
    crc ^= data[i];
    for (int j = 0; j < 8; j++)
    {
      if (crc & 0x80)
      {
        crc = (crc << 1) ^ SMBUS_CRC8_POLY;
      }
      else
      {
        crc <<= 1;
      }
    }
  }
  return crc;
}
void setup()
{
  // pinMode(MOTOR_PIN, OUTPUT);
  Wire.begin(I2C_ADDRESS);

  Wire.onRequest(requestEvent);
  Wire.onReceive(receiveEvent);
  pinMode(LED_PIN, OUTPUT);
  Serial.begin(115200);
  Serial.println("I2C Slave started");
}

void loop()
{
  static unsigned long lastSensorUpdateMs = 0;

  if (millis() - lastSensorUpdateMs < 100)
  {
    return;
  }
  lastSensorUpdateMs = millis();

  int cell1Mv = readVoltage(BATTERY_PIN_1, CELL1_DIVIDER_RATIO);     // BATCELL1 tap: cell1 only
  int upToCell2Mv = readVoltage(BATTERY_PIN_2, CELL2_DIVIDER_RATIO); // BATCELL2 tap: cell1+cell2
  int packMv = readVoltage(BATTERY_PIN_3, PACK_DIVIDER_RATIO);       // BATCELL3 tap: full pack (cell1+cell2+cell3)

  currentCell1Mv = max(cell1Mv, 1000);
  currentCell2Mv = max(upToCell2Mv - cell1Mv, 1000);
  currentCell3Mv = max(packMv - upToCell2Mv, 1000);
  currentPackMv = max(packMv, 3000);

  currentPercent = voltageToPercentage(currentPackMv / 1000.0f);
  currentTemperature = getThermistorReading();

  // INA199: Vout(mV) = I(A) * Rshunt(0.002R) * Gain(50) = I(A) * 100mV/A -> I(mA) = Vout(mV) * 10
  int currentAdc = analogRead(CURRENT_PIN);
  float currentMv = currentAdc * (ADCReferanceVoltage * 1000.0f / 1024.0f);
  currentDrawMa = (int)(currentMv * 10.0f);
}

void receiveEvent(int bytes)
{
  digitalWrite(LED_PIN, HIGH);
  lastCommandIn = Wire.read();
  // Serial.print("  : 0x");
  // Serial.println(lastCommandIn, HEX);
  switch (lastCommandIn)
  {
  case ManufacturerDataRequest:
    lastManufacturerDataAddressIn = Wire.read();
    Wire.read();
    Wire.read();
    break;
  case 0xD3:
    Wire.read();
    Wire.read();
    Wire.read();
    break;
  default:
    break;
  }
  digitalWrite(LED_PIN, LOW);
}

void requestEvent()
{
  sendAnswer(lastCommandIn);
}
void sendAnswer(byte command)
{
  // Serial.print("snd: 0x");
  // Serial.println(command, HEX);
  switch (command)
  {
  case Unknown0xD5Request:
    Wire.write(Unknown0xD5Response, sizeof(Unknown0xD5Response));
    break;
  case DesignCapacityRequest:
    Wire.write(DesignCapacityResponse, sizeof(DesignCapacityResponse));
    break;
  case CycleCountRequest:
    cycleCountResponse(cycleCount);
    break;
  case DesignVoltageRequest:
    Wire.write(DesignVoltageResponse, sizeof(DesignVoltageResponse));
    break;
  case ManufactureDateRequest:
    Wire.write(ManufactureDateResponse, sizeof(ManufactureDateResponse));
    break;
  case SerialNumberRequest:
    Wire.write(SerialNumberResponse, sizeof(SerialNumberResponse));
    break;
  case ManufacturerNameRequest:
    Wire.write(ManufacturerNameResponse, sizeof(ManufacturerNameResponse));
    break;
  case DeviceNameRequest:
    Wire.write(DeviceNameResponse, sizeof(DeviceNameResponse));
    break;
  case ManufacturerDataRequest:
    switch (lastManufacturerDataAddressIn)
    {
    case 0x51:
      Wire.write(ManufacturerData51, sizeof(ManufacturerData51));
      break;
    case 0x53:
      Wire.write(ManufacturerData53, sizeof(ManufacturerData53));
      break;
    case 0x54:
      Wire.write(ManufacturerData54, sizeof(ManufacturerData54));
      break;
    case 0x55:
      Wire.write(ManufacturerData55, sizeof(ManufacturerData55));
      break;
    }
    // Wire.write(ManufacturerDataResponse, sizeof(ManufacturerDataResponse));
    break;
  case PackVoltageRequest:
    packVoltageResponse(currentPackMv);
    break;
  case CurrentRequest:
    currentResponse(currentDrawMa);
    break;
  case FullChargeCapacityRequest:
    fullChargeCapacityResponse(fullChargeCapacity); // kept equal to designCapacity (1480mAh)
    break;
  case RemainingCapacityRequest:
    remainingCapacityResponse((designCapacity * currentPercent) / 100);
    break;
  case TemperatureRequest:
    temperatureResponse(currentTemperature);
    break;
  case RelativeStateOfChargeRequest:
    relativeStateOfChargeResponse(currentPercent);
    break;
  case Unknown0x51Request:
    Wire.write(Unknown0x51Response, sizeof(Unknown0x51Response));
    break;
  case Unknown0xD2Request:
    Wire.write(Unknown0xD2Response, sizeof(Unknown0xD2Response));
    break;
  case CellVoltage1Request:
    cellVoltage1Response(currentCell1Mv);
    break;
  case CellVoltage2Request:
    cellVoltage2Response(currentCell2Mv);
    break;
  case CellVoltage3Request:
    cellVoltage3Response(currentCell3Mv);
    break;
  case Unknown0xD6Request:
    Wire.write(Unknown0xD6Response, sizeof(Unknown0xD6Response));
    break;
  case Unknown0xD9Request:
    Wire.write(Unknown0xD9Response, sizeof(Unknown0xD9Response));
    break;
  case Unknown0xD8Request:
    Wire.write(Unknown0xD8Response, sizeof(Unknown0xD8Response));
    break;
  case Unknown0xC2Request:
    Wire.write(Unknown0xC2Response, sizeof(Unknown0xC2Response));
    break;
  case Unknown0x2FRequest:
    Wire.write(Unkown0x2FResponse, sizeof(Unkown0x2FResponse));
    break;
  case Unkown0x66Request:
    Wire.write(Unkown0x66Response, sizeof(Unkown0x66Response));
    break;
  case ManufacturerReadRequest:
    Wire.write(Unkown0x23Response, sizeof(Unkown0x23Response));
    break;

  default:
    // Unknown command: no response sent. Do not Serial.print here, this runs in the I2C ISR.
    break;
  }
}
// void cycleCountResponse()
// {
//   uint8_t rawdata[4];

//   rawdata[0] = 0x0B;
//   rawdata[1] = cycleCount & 0xFF;
//   rawdata[2] = (cycleCount >> 8) & 0xFF;

//   uint8_t temp[5] = {0x16, 0x17, 0x17, rawdata[1], rawdata[2]};
//   rawdata[3] = pec(temp, 5);
//   Wire.write(rawdata, 4);

// }
void cycleCountResponse(int cyclecount)
{
  uint8_t rawdata[4];

  rawdata[0] = cyclecount & 0xFF;
  rawdata[1] = (cyclecount >> 8) & 0xFF;

  uint8_t temp[5] = {0x16, CycleCountRequest, 0x17, rawdata[0], rawdata[1]};
  rawdata[2] = pec(temp, 5);

  Wire.write(rawdata, 3);
  // Serial.print("cycle count raw data: ");
  // for (int i = 0; i < 4; i++)
  // {
  //   Serial.print(rawdata[i], HEX);
  //   Serial.print(" ");
  // }
  // Serial.println();
}
void designVoltageResponse(int voltage)
{
  uint8_t rawdata[3];

  rawdata[0] = voltage & 0xFF;
  rawdata[1] = (voltage >> 8) & 0xFF;
  rawdata[2] = 0x00; // this is pec value that will change after calculating the pec

  uint8_t temp[5] = {0x16, DesignVoltageRequest, 0x17, rawdata[0], rawdata[1]};
  uint8_t calculatedPEC = pec(temp, 5);

  rawdata[2] = calculatedPEC;

  Wire.write(rawdata, 3);

  // Serial.print("Raw data: ");
  // for (int i = 0; i < 3; i++)
  // {
  //   Serial.print(rawdata[i], HEX);
  //   Serial.print(" ");
  // }
  // Serial.println();
}

void packVoltageResponse(int voltage)
{
  uint8_t rawdata[3];

  rawdata[0] = voltage & 0xFF;
  rawdata[1] = (voltage >> 8) & 0xFF;
  rawdata[2] = 0x00; // this is pec value that will change after calculating the pec

  uint8_t temp[5] = {0x16, PackVoltageRequest, 0x17, rawdata[0], rawdata[1]};
  uint8_t calculatedPEC = pec(temp, 5);

  rawdata[2] = calculatedPEC;

  Wire.write(rawdata, 3);

  // Serial.print("Raw data: ");
  // for (int i = 0; i < 3; i++)
  // {
  //   Serial.print(rawdata[i], HEX);
  //   Serial.print(" ");
  // }
  // Serial.println();
}

void currentResponse(int current)
{
  uint8_t rawdata[3];

  rawdata[0] = current & 0xFF;
  rawdata[1] = (current >> 8) & 0xFF;
  rawdata[2] = 0x00; // this is pec value that will change after calculating the pec

  uint8_t temp[5] = {0x16, CurrentRequest, 0x17, rawdata[0], rawdata[1]};
  uint8_t calculatedPEC = pec(temp, 5);

  rawdata[2] = calculatedPEC;

  Wire.write(rawdata, 3);
}

void fullChargeCapacityResponse(int capacity)
{
  uint8_t rawdata[3];

  rawdata[0] = capacity & 0xFF;
  rawdata[1] = (capacity >> 8) & 0xFF;
  rawdata[2] = 0x00; // this is pec value that will change after calculating the pec

  uint8_t temp[5] = {0x16, FullChargeCapacityRequest, 0x17, rawdata[0], rawdata[1]};
  uint8_t calculatedPEC = pec(temp, 5);

  rawdata[2] = calculatedPEC;

  Wire.write(rawdata, 3);
}
void remainingCapacityResponse(int remainingcapacity)
{
  uint8_t rawdata[3];

  rawdata[0] = remainingcapacity & 0xFF;
  rawdata[1] = (remainingcapacity >> 8) & 0xFF;
  rawdata[2] = 0x00; // this is pec value that will change after calculating the pec

  uint8_t temp[5] = {0x16, RemainingCapacityRequest, 0x17, rawdata[0], rawdata[1]};
  uint8_t calculatedPEC = pec(temp, 5);

  rawdata[2] = calculatedPEC;

  Wire.write(rawdata, 3);
}
void relativeStateOfChargeResponse(int stateofcharge)
{
  uint8_t rawdata[3];

  rawdata[0] = stateofcharge & 0xFF;
  rawdata[1] = (stateofcharge >> 8) & 0xFF;
  rawdata[2] = 0x00; // this is pec value that will change after calculating the pec

  uint8_t temp[5] = {0x16, RelativeStateOfChargeRequest, 0x17, rawdata[0], rawdata[1]};
  uint8_t calculatedPEC = pec(temp, 5);

  rawdata[2] = calculatedPEC;

  Wire.write(rawdata, 3);
}
void temperatureResponse(int temperature)
{
  uint8_t rawdata[3];

  rawdata[0] = temperature & 0xFF;
  rawdata[1] = (temperature >> 8) & 0xFF;
  rawdata[2] = 0x00; // this is pec value that will change after calculating the pec

  uint8_t temp[5] = {0x16, TemperatureRequest, 0x17, rawdata[0], rawdata[1]};
  uint8_t calculatedPEC = pec(temp, 5);

  rawdata[2] = calculatedPEC;

  Wire.write(rawdata, 3);
}
void cellVoltage1Response(int voltage)
{
  uint8_t rawdata[3] = {0x3C, 0x10, 0x00};

  rawdata[0] = voltage & 0xFF;
  rawdata[1] = (voltage >> 8) & 0xFF;
  rawdata[2] = 0x00; // this is pec value that will change after calculating the pec

  uint8_t temp[5] = {0x16, CellVoltage1Request, 0x17, rawdata[0], rawdata[1]};

  rawdata[2] = pec(temp, 5);

  Wire.write(rawdata, 3);

  // Serial.print("Raw data: ");
  // for (int i = 0; i < 3; i++)
  // {
  //   Serial.print(rawdata[i], HEX);
  //   Serial.print(" ");
  // }
  // Serial.println();
}

void cellVoltage2Response(int voltage)
{
  uint8_t rawdata[3];

  rawdata[0] = voltage & 0xFF;
  rawdata[1] = (voltage >> 8) & 0xFF;
  rawdata[2] = 0x00; // this is pec value that will change after calculating the pec

  uint8_t temp[5] = {0x16, CellVoltage2Request, 0x17, rawdata[0], rawdata[1]};

  rawdata[2] = pec(temp, 5);

  Wire.write(rawdata, 3);

  // Serial.print("Raw data: ");
  // for (int i = 0; i < 3; i++)
  // {
  //   Serial.print(rawdata[i], HEX);
  //   Serial.print(" ");
  // }
  // Serial.println();
}

void cellVoltage3Response(int voltage)
{
  uint8_t rawdata[3];

  rawdata[0] = voltage & 0xFF;
  rawdata[1] = (voltage >> 8) & 0xFF;
  rawdata[2] = 0x00; // this is pec value that will change after calculating the pec

  uint8_t temp[5] = {0x16, CellVoltage3Request, 0x17, rawdata[0], rawdata[1]};

  rawdata[2] = pec(temp, 5);

  Wire.write(rawdata, 3);

  // Serial.print("Raw data: ");
  // for (int i = 0; i < 3; i++)
  // {
  //   Serial.print(rawdata[i], HEX);
  //   Serial.print(" ");
  // }
  // Serial.println();
}
// int readVoltage(int pin) {
//   int sensorValue = analogRead(pin);
//   float voltage = sensorValue * (3.3 / 1023.0);
//   return (int)(voltage*1000);
// }
int readVoltage(int pin, float dividerRatio)
{
  int result = (int)((analogRead(pin) * ADCReferanceVoltage) / 1024.0 / dividerRatio * 1000.0f);
  return result;
}
int getThermistorReading()
{
  // No NTC wired up. SMBus TemperatureRequest expects 0.1K units:
  // (25.0 + 273.15) * 10 = 2982, i.e. a fixed 25C reading.
  return 2982;
}
int voltageToPercentage(float voltage)
{
  // 3S LiPo pack: 4.2V/cell peak
  const float maxVoltage = 12.6;
  const float minVoltage = 9.0;

  // Clamp the input voltage between min and max values
  voltage = constrain(voltage, minVoltage, maxVoltage);

  // Calculate the percentage
  float percentage = (voltage - minVoltage) / (maxVoltage - minVoltage) * 100;

  return (int)percentage;
}