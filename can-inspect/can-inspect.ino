#include <mcp_can_dfs.h>
#include <mcp_can.h>

#include <Adafruit_LEDBackpack.h>
#include <SPI.h>
#include <Arduino.h>
#include <EEPROM.h>
#include <Wire.h>
#include <LiquidCrystal.h>
#include "manual_spi.hpp"

#define CAN_ID_BATT_SOC 853
#define CAN_ID_BATT_LOWS_AND_HIS 883
#define CAN_ID_BATT_STATUS 854
#define CAN_ID_INVERTER_DATA 856
#define CAN_ID_INVERTER_DIAG 857

// converts pulse rate in Hz to mph
const float SPEED_CONVERSION_FACTOR = 0.6785;

const int INT_EV = 49;
const int SPI_CS_EV = 53;
const int TFT_SCK = 24;
const int TFT_MOSI = 25;

const int DI_KEY_SWITCH = 45;

// pullups
const int DI_ABS_WARNING = 36;
const int DI_SEAT_BELTS = 22;
const int DI_HI_BEAM = 38;
const int DI_FL_DOOR_WARNING = 32; 
const int DI_FR_DOOR_WARNING = 4;
const int DI_RL_DOOR_WARNING = 30;
const int DI_RR_DOOR_WARNING = 7;
const int DI_TAILGATE_WARNING = 6;

// some SPI configuration
const int DO_LEFT_LCD_CS = 27;
const int DO_RIGHT_LCD_CS = 26;

// some I2C configuration
const int I2C_ADDR_SPEED_DISPLAY = 0x70;
const int I2C_ADDR_TACH_DISPLAY = 0x71;

// LCD pins
const int PWM_LCD_R = 15;
const int PWM_LCD_G = 16;
const int PWM_LCD_B = 17;
const int DO_LCD_RS = 39;
const int DO_LCD_EN = 37;
const int DO_LCD_DB4 = 43;
const int DO_LCD_DB5 = 41;
const int DO_LCD_DB6 = 35;
const int DO_LCD_DB7 = 33;

const int AI_VSS = A0;

unsigned long last_dispatch_time = 0;
long current_cycle_time;
long previous_cycle_time;

float mileage;
float vehicle_speed;

const int DI_ODOMETER_PIN = 2;
volatile uint16_t odometer_pulses = 0;
long last_odometer_read_time_ms;


MCP_CAN ev_can(SPI_CS_EV);

LiquidCrystal lcd(DO_LCD_RS, DO_LCD_EN, DO_LCD_DB4, DO_LCD_DB5, DO_LCD_DB6, DO_LCD_DB7);
byte CHAR_FULL_BAR = 0;
byte full_bar[8] = {
  0xff,
  0xff,
  0xff,
  0xff,
  0xff,
  0xff,
  0xff,
  0xff
};
byte CHAR_EMPTY_BAR = 1;
byte empty_bar[8] = {
 0b11111,
 0b10001,
 0b10001,
 0b10001,
 0b10001,
 0b10001,
 0b10001,
 0b11111
};

// meh, I don't like these Adafruit libraries, but that's because 
// I'm a crank
Adafruit_7segment speedometer_display_7seg = Adafruit_7segment();
Adafruit_7segment tachometer_display_7seg = Adafruit_7segment();

// we transfer in mode 0, with COPA 0 and CPHA 0
// this is a hand-rolled SPI function meant to get 
// around the fact that my wires are absolute garbage.
// Need to waaay slow down SPI transactions with the ESP32s.
uint8_t SPITransfer(uint8_t data, uint8_t cyc_ms) {
  uint16_t delayus = cyc_ms * 1000;
  uint8_t mask = 0b10000000;
  uint8_t returnValue = 0;

  delayMicroseconds(delayus / 4);
  for (int i = 0; i < 8; i ++) {
    // set MOSI pin first
    if ((mask & data) > 0) {
      digitalWrite(TFT_MOSI, HIGH);
    } else {
      digitalWrite(TFT_MOSI, LOW);
    }
    delayMicroseconds(delayus / 4);

    // then cycle SCK
    digitalWrite(TFT_SCK, HIGH);
    delayMicroseconds(delayus / 4);
    returnValue |= (digitalRead(TFT_MOSI) << i);
    mask = mask >> 1;
    digitalWrite(TFT_SCK, LOW);
    delayMicroseconds(delayus / 4);
  }
  digitalWrite(TFT_MOSI, LOW);

  return returnValue;
}

uint16_t SPITransfer16(uint16_t data, uint8_t cyc_ms) {
  return SPITransfer(data & 0xff, cyc_ms) | (SPITransfer(data >> 8, cyc_ms) << 8);
}

ManualSPI mspi(TFT_SCK, TFT_MOSI, 5);

void setup() {
  current_cycle_time = millis();
  previous_cycle_time = current_cycle_time;

  Serial.begin(115200);
  Serial.println("Initialized");
  pinMode(INT_EV, INPUT);
  uint8_t ret = ev_can.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ);
  if (ret == CAN_OK) {
    ev_can.setMode(MCP_NORMAL);
  } else {
    Serial.println("error initializing CAN");
  }

  pinMode(DI_KEY_SWITCH, INPUT);

  pinMode(DI_ABS_WARNING, INPUT_PULLUP);
  pinMode(DI_SEAT_BELTS, INPUT_PULLUP);
  pinMode(DI_HI_BEAM, INPUT_PULLUP);
  pinMode(DI_FL_DOOR_WARNING, INPUT_PULLUP);
  pinMode(DI_FR_DOOR_WARNING, INPUT_PULLUP);
  pinMode(DI_RL_DOOR_WARNING, INPUT_PULLUP);
  pinMode(DI_RR_DOOR_WARNING, INPUT_PULLUP);
  pinMode(DI_TAILGATE_WARNING, INPUT_PULLUP);
  pinMode(AI_VSS, INPUT);

  pinMode(DO_LCD_RS, OUTPUT);
  pinMode(DO_LCD_EN, OUTPUT);
  pinMode(DO_LCD_DB4, OUTPUT);
  pinMode(DO_LCD_DB5, OUTPUT);
  pinMode(DO_LCD_DB6, OUTPUT);
  pinMode(DO_LCD_DB7, OUTPUT);

  pinMode(DO_LEFT_LCD_CS, OUTPUT);
  pinMode(DO_RIGHT_LCD_CS, OUTPUT);
  pinMode(SPI_CS_EV, OUTPUT);
  pinMode(TFT_SCK, OUTPUT);
  pinMode(TFT_MOSI, OUTPUT);
  pinMode(DI_ODOMETER_PIN, INPUT_PULLUP);

  digitalWrite(DO_LEFT_LCD_CS, HIGH);
  digitalWrite(DO_RIGHT_LCD_CS, HIGH);

  Wire.begin();
  mspi.init();

  lcd.createChar(CHAR_FULL_BAR, full_bar);
  lcd.createChar(CHAR_EMPTY_BAR, empty_bar);
  lcd.begin(20, 4, LCD_5x8DOTS);
  lcd.print("initializing...");

  speedometer_display_7seg.begin(I2C_ADDR_SPEED_DISPLAY);
  tachometer_display_7seg.begin(I2C_ADDR_TACH_DISPLAY);
  tachometer_display_7seg.setDisplayState(true);
  // speedometer_display_7seg.setBrightness(15);

  last_dispatch_time = millis();
  EEPROM.get(0x00, mileage);
  last_odometer_read_time_ms = millis();
  attachInterrupt(digitalPinToInterrupt(DI_ODOMETER_PIN), countOdometer, CHANGE);
}


void countOdometer() {
  odometer_pulses += 1;
}

uint16_t readAndClearOdometerPulses(long current_cycle_time_ms, long *time_since_last_read_ms) {
  *time_since_last_read_ms = current_cycle_time_ms - last_odometer_read_time_ms;

  last_odometer_read_time_ms = current_cycle_time_ms;
  uint16_t to_return = odometer_pulses;
  odometer_pulses = 0;

  return to_return;
}

// Provide vehicle speed in km/hr, and engine speed in RPM
void convert_513_values(
    uint16_t engine_speed, 
    uint64_t vehicle_speed,
    uint8_t buf[]
) {
  // apparently the Mazda 3 pump expects RPM as raw bytes
  // and vehicle speed in km/hr * 100
  vehicle_speed *= 100;

  // found an awesome resource here: https://docs.google.com/spreadsheets/d/1SKfXAyo6fbAfMUENw1KR3w4Fvx_Ihj6sTPSVXBdOXKk/edit?gid=0#gid=0
  // that shows the breakdown of the 513 code.
  // This message ID is broadcast every 10ms. Breakdown 
  // of bytes is as follows:
  //   B1-2: RPM
  //   B3-4: could be engine torque; unclear
  //   B5-6: vehicle speed
  //   B7: accelerator pedal position
  //   B8: some accelerometer value
  buf[0] = (engine_speed * 4) >> 8;
  buf[1] = (engine_speed * 4) & 0x00ff;
  buf[4] = vehicle_speed / 256;
  buf[5] = vehicle_speed % 256;
}

// battery pack singleton data
uint16_t battery_state_of_charge_decipct = 1000;  // "decipercent"
uint16_t battery_summary_voltage_v;
uint16_t battery_summary_current_a;
uint16_t battery_summary_temperature_c;
uint16_t battery_max_pack_voltage_v = 3450;
uint16_t battery_min_pack_voltage_v = 4120;
uint16_t battery_max_pack_temp_c = 273 + 30;
uint16_t battery_min_pack_temp_c = 273 - 5;


bool unpackBMSCAN(uint32_t message_id, uint8_t len, uint8_t buf[]) {
  if (message_id == CAN_ID_BATT_SOC) { 
    battery_state_of_charge_decipct = buf[4] | (buf[5] << 8);
    return true;

  } else if (message_id == CAN_ID_BATT_LOWS_AND_HIS) { 
    battery_max_pack_voltage_v = buf[0] | (buf[1] << 8);
    battery_min_pack_voltage_v = buf[2] | (buf[3] << 8);
    battery_max_pack_temp_c = buf[4] | (buf[5] << 8);
    battery_min_pack_temp_c = buf[6] | (buf[7] << 8);
    return true;

  } else if (message_id == CAN_ID_BATT_STATUS) {
    battery_summary_voltage_v = buf[0] | (buf[1] << 8);
    battery_summary_current_a = buf[2] | (buf[3] << 8);
    battery_summary_temperature_c = buf[4] | (buf[5] << 8);
    return true;
  }
  return false;
}

int16_t inverter_motor_speed_rpm; // [-32768, 32767] <-> [-32768, 32767]
uint8_t inverter_motor_temp_c; // [0, 255] <-> [-40, 215]
uint8_t inverter_temp_1_c; // [0, 255] <-> [-40, 215]
int16_t inverter_motor_current_arms; // [-32768, 32767] <-> [-3276.8, 3276.7]
int16_t inverter_motor_torque_pct; // [-32768, 32767] <-> [-100, 100]
uint16_t inverter_sys_flags = 0;
uint8_t inverter_worst_fault_code = 0;
uint8_t inverter_worst_fault_level = 0;
uint32_t inverter_odometer = 0;

bool prev_keyswitch_state = false;
bool status_abs_warning = true;
bool status_seat_belts = true;
bool status_hi_beam = true;
bool status_fl_door_warning = true;
bool status_fr_door_warning = true;
bool status_rl_door_warning = true;
bool status_rr_door_warning = true;
bool status_tailgate_warning = true;

bool unpackInverterCAN(uint32_t message_id, uint8_t len, uint8_t buf[]) {
  if (message_id == CAN_ID_INVERTER_DATA) {
    inverter_motor_speed_rpm = buf[0] | (buf[1] << 8);
    inverter_motor_temp_c = buf[2];
    inverter_temp_1_c = buf[3];
    inverter_motor_current_arms = buf[4] | (buf[5] << 8);
    inverter_motor_torque_pct = buf[6] | (buf[7] << 8);
    return true;

  } else if (message_id == CAN_ID_INVERTER_DIAG) {
    inverter_sys_flags = buf[0] | (buf[1] << 8);
    inverter_worst_fault_code = buf[2];
    inverter_worst_fault_level = buf[3];
    inverter_odometer = buf[4] | (buf[5] << 8) | (buf[6] << 16) | (buf[7] << 24);

    return true;
  }
  return false;
}


void writeLCD() {
  lcd.clear();

  // display tach values
  float max_rpm = 7500.0;
  float redline = 6500.0;
  float tach_ratio_f = (float)inverter_motor_speed_rpm / max_rpm * 15;
  int tach_ratio_i = max(roundf(tach_ratio_f), 1);

  lcd.setCursor(0, 0);
  lcd.print("Tach:");
  for (int i=0; i < tach_ratio_i; i++) {
    if (i < 13) {
      lcd.write(CHAR_EMPTY_BAR);
    } else {
      lcd.write(CHAR_FULL_BAR);
    }
  }

  // display charge values
  float battery_charge_pct = (float)battery_state_of_charge_decipct * 0.1;
  float charge_ratio_f = battery_charge_pct / 100.0 * 15;
  int charge_ratio_i = max(roundf(charge_ratio_f), 1);
  lcd.setCursor(0, 1);
  lcd.print("Batt:");

  for (int i=0; i < charge_ratio_i; i++){
    if (i > 0) {
      lcd.write(CHAR_FULL_BAR);
    } else {
      lcd.write(CHAR_EMPTY_BAR);
    }
  }

  // display odometer 
  lcd.setCursor(0, 2);
  lcd.print("Odo : ");
  lcd.print(mileage);

  lcd.setCursor(0, 3);
  lcd.print("speed: ");
  lcd.print(vehicle_speed);
}

void writeBatteryTFT() {
  uint8_t message[8] = {
    battery_max_pack_voltage_v & 0xff,
    (battery_max_pack_voltage_v >> 8) & 0xff,
    battery_min_pack_voltage_v & 0xff,
    (battery_min_pack_voltage_v >> 8) & 0xff,
    battery_max_pack_temp_c & 0xff,
    (battery_max_pack_temp_c >> 8) & 0xff,
    battery_min_pack_temp_c & 0xff,
    (battery_min_pack_temp_c >> 8) & 0xff
  };
  mspi.queue_message(message, 8, DO_LEFT_LCD_CS);
}

void writeVehicleStatusTFT() {
  uint8_t status = status_abs_warning |
    (status_seat_belts << 1) | 
    (status_hi_beam << 2) |
    (status_fl_door_warning << 3) |
    (status_fr_door_warning << 4) | 
    (status_rl_door_warning << 5) | 
    (status_rr_door_warning << 6) | 
    (status_tailgate_warning << 7);

  digitalWrite(DO_RIGHT_LCD_CS, LOW);
  delay(1);
  SPITransfer(status, 5);
  delay(1);
  digitalWrite(DO_RIGHT_LCD_CS, HIGH);
}

void loop() {
  current_cycle_time = millis();

  // process all inputs
  // first, process whatever might have come over from the CAN bus.
  //
  uint32_t id; 
  uint8_t len; 
  uint8_t ret;
  uint8_t buf[24];
  if (!digitalRead(INT_EV)) {
    ret = ev_can.readMsgBuf(&id, &len, buf);
    if (ret != CAN_OK) {
      Serial.println("Error decoding CAN message");
    } else {
      unpackBMSCAN(id, len, buf);
      unpackInverterCAN(id, len, buf);
    }
  }

  // a bunch of discrete inputs (wonder if these could be 
  // replaced with actual hardware and some bulbs)
  status_abs_warning = !digitalRead(DI_ABS_WARNING);
  status_seat_belts = !digitalRead(DI_SEAT_BELTS);
  status_hi_beam = !digitalRead(DI_HI_BEAM);
  status_fl_door_warning = !digitalRead(DI_FL_DOOR_WARNING);
  status_fr_door_warning = !digitalRead(DI_FR_DOOR_WARNING);
  status_rl_door_warning = !digitalRead(DI_RL_DOOR_WARNING);
  status_rr_door_warning = !digitalRead(DI_RR_DOOR_WARNING);
  status_tailgate_warning = !digitalRead(DI_TAILGATE_WARNING);

  // Now we send and set the outputs
  writeLCD();

  if (mspi.ready_for_send()) {
    writeBatteryTFT();
  }
  writeVehicleStatusTFT();

  long time_since_last_odometer_read_ms;
  uint16_t pulses = readAndClearOdometerPulses(current_cycle_time, &time_since_last_odometer_read_ms);
  vehicle_speed = ((float)pulses / (float)time_since_last_odometer_read_ms * 1000.0) * SPEED_CONVERSION_FACTOR;
  mileage += (SPEED_CONVERSION_FACTOR * pulses) / 3600.0;
  speedometer_display_7seg.print((int)vehicle_speed, DEC);
  speedometer_display_7seg.writeDisplay();
  tachometer_display_7seg.print(inverter_motor_speed_rpm, DEC);
  tachometer_display_7seg.writeDisplay();

  // Send info to CAN pump every 10ms or so
  if (millis() - last_dispatch_time > 10) {
    convert_513_values(500, 0, buf);
    ev_can.sendMsgBuf(513, 0, 8, buf);
    last_dispatch_time = millis();
  }

  mspi.run();
  delay(50);
  bool current_keyswitch_state = digitalRead(DI_KEY_SWITCH);

  if (!current_keyswitch_state && prev_keyswitch_state) {
    // this is our signal to write to EEPROM
    EEPROM.put(0x00, mileage);
  }

  previous_cycle_time = current_cycle_time;
  prev_keyswitch_state = current_keyswitch_state;
}
