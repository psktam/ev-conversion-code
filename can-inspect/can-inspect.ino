#include <mcp_can_dfs.h>
#include <mcp_can.h>

#include <Adafruit_LEDBackpack.h>
#include <SPI.h>
#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal.h>

const int INT_EV = 6;
const int SPI_CS_EV = 8;
// pullups
const int DI_BRAKE_WARNING = 1;
const int DI_12V_BATT_WARNING = 2;
const int DI_ABS_WARNING = 3;
const int DI_SEAT_BELTS = 4;
// pulldowns
const int DI_HI_BEAM = 5;
const int DI_LEFT_TURN = 6;
const int DI_RIGHT_TURN = 7;
// Pullups
const int DI_FL_DOOR_WARNING = 8; 
const int DI_FR_DOOR_WARNING = 9;
const int DI_RL_DOOR_WARNING = 10;
const int DI_RR_DOOR_WARNING = 11;
const int DI_TAILGATE_WARNING = 12;

// some SPI configuration
const int DO_LEFT_LCD_CS = 13;
const int DO_RIGHT_LCD_CS = 14;

// some I2C configuration
const int I2C_ADDR_SPEED_DISPLAY = 0x23;
const int I2C_ADDR_TACH_DISPLAY = 0x24;

// LCD pins
const int PWM_LCD_R = 15;
const int PWM_LCD_G = 16;
const int PWM_LCD_B = 17;
const int DO_LCD_RS = 18;
const int DO_LCD_EN = 19;
const int DO_LCD_DB4 = 20;
const int DO_LCD_DB5 = 21;
const int DO_LCD_DB6 = 22;
const int DO_LCD_DB7 = 23;

const int AI_VSS = 13;

unsigned long last_dispatch_time = 0;

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
      digitalWrite(MOSI, HIGH);
    } else {
      digitalWrite(MOSI, LOW);
    }
    delayMicroseconds(delayus / 4);

    // then cycle SCK
    digitalWrite(SCK, HIGH);
    delayMicroseconds(delayus / 4);
    returnValue |= (digitalRead(MISO) << i);
    mask = mask >> 1;
    digitalWrite(SCK, LOW);
    delayMicroseconds(delayus / 4);
  }
  digitalWrite(MOSI, LOW);

  return returnValue;
}

void setup() {
  Serial.begin(115200);
  Serial.println("Initialized");
  pinMode(INT_EV, INPUT);

  pinMode(DI_BRAKE_WARNING, INPUT);
  pinMode(DI_12V_BATT_WARNING, INPUT);
  pinMode(DI_ABS_WARNING, INPUT);
  pinMode(DI_SEAT_BELTS, INPUT);
  pinMode(DI_HI_BEAM, INPUT);
  pinMode(DI_LEFT_TURN, INPUT);
  pinMode(DI_RIGHT_TURN, INPUT);
  pinMode(DI_FL_DOOR_WARNING, INPUT);
  pinMode(DI_FR_DOOR_WARNING, INPUT);
  pinMode(DI_RL_DOOR_WARNING, INPUT);
  pinMode(DI_RR_DOOR_WARNING, INPUT);
  pinMode(DI_TAILGATE_WARNING, INPUT);
  pinMode(AI_VSS, INPUT);

  Wire.begin();

  lcd.createChar(CHAR_FULL_BAR, full_bar);
  lcd.createChar(CHAR_EMPTY_BAR, empty_bar);
  lcd.begin(20, 4, LCD_5x8DOTS);

  speedometer_display_7seg.begin(I2C_ADDR_SPEED_DISPLAY);
  tachometer_display_7seg.begin(I2C_ADDR_TACH_DISPLAY);

  uint8_t ret = ev_can.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ);
  if (ret == CAN_OK) {
    ev_can.setMode(MCP_NORMAL);
    Serial.println("EV CAN initialization successful");
  } else {
    Serial.println("Error initializing EV CAN");
  }
  last_dispatch_time = millis();
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

void handle_bms_message(uint32_t message_id, uint8_t len, uint8_t buf[]) {
  if (message_id == 849) {
    print_bms_setpoints(buf);
  } else if (message_id == 853) {
    print_bms_soc(buf);
  } else if (message_id == 854) {
    print_bms_status(buf);
  }
}

void handle_x1_CAN_message(uint32_t message_id, uint8_t len, uint8_t buf[]) {

}

void print_charger_message(uint8_t len, uint8_t buf[]) {
  uint16_t voltage = (buf[0] << 8) | buf[1];
  uint16_t current = (buf[2] << 8) | buf[3];
  uint8_t status = buf[4];

  bool hardware_failure = (status & 1) == 1;
  bool overtemp_protection_active = (status & 2) == 1;
  bool input_voltage_invalid = (status & 4) == 1;
  bool reverse_polarity_protection_active = (status & 8) == 1;
  bool comm_timeout = (status & 16) == 1;


  Serial.println("Charger sent a message indicating:");
  Serial.print("    Output voltage: ");
  Serial.print(voltage);
  Serial.println("");

  Serial.print("    Output current: ");
  Serial.print(current);
  Serial.println("");

  if (status > 0) {
    Serial.println("    Diagnostic status detected");
  }
  if (hardware_failure) {
    Serial.println("        Hardware failure detected");
  }
  if (overtemp_protection_active) {
    Serial.println("        Overtemp protection active");
  }
  if (input_voltage_invalid) {
    Serial.println("        Invalid input voltage");
  }
  if (reverse_polarity_protection_active) {
    Serial.println("        Reverse polarity protection activated");
  }
  if (comm_timeout) {
    Serial.println("        Comms timeout");
  }

}

// message ID 849
void print_bms_setpoints(uint8_t buf[]) {
  uint16_t charge_voltage_u16 = buf[0] | (buf[1] << 8);
  float charge_voltage = charge_voltage_u16 * 0.1;

  uint16_t charge_current_u16 = buf[2] | (buf[3] << 8);
  float charge_current = charge_current_u16 * 0.1;

  uint16_t discharge_current_u16 = buf[4] | (buf[5] << 8);
  float discharge_current = discharge_current_u16 * 0.1;

  uint16_t discharge_voltage_u16 = buf[6] | (buf[7] << 8);
  float discharge_voltage = discharge_voltage_u16 * 0.1;

  // arduino's implementation of sprintf can't handle floating points 
  // natively.
  Serial.println("Vero BMS setpoints");
  Serial.print("    Charge voltage: "); Serial.print(charge_voltage, 1); Serial.println(" V");
  Serial.print("    Charge current: "); Serial.print(charge_current, 1); Serial.println(" A");
  Serial.print("    Discharge voltage: "); Serial.print(discharge_voltage, 1); Serial.println(" V");
  Serial.print("    Discharge current: "); Serial.print(discharge_current, 1); Serial.println(" A");
}

void print_bms_soc(uint8_t buf[]) {
  uint16_t soc = buf[0] | (buf[1] << 8);
  uint16_t soh = buf[2] | (buf[3] << 8);
  uint16_t soc_dec_u16 = buf[4] | (buf[5] << 8);
  float soc_dec = soc_dec_u16 * 0.1;

  Serial.println("Vero BMS charge/health readings");
  char printbuf[40];
  sprintf(printbuf, "    State of charge: %u %%", soc);
  Serial.println(printbuf);
  sprintf(printbuf, "    State of health: %u %%", soh);
  Serial.println(printbuf);
  Serial.print("    State of charge (decimal): "); Serial.print(soc_dec, 1); Serial.println(" %");
}

void print_bms_status(uint8_t buf[]) {
  uint16_t voltage_u16 = buf[0] | (buf[1] << 8);
  float voltage = voltage_u16 * 0.01;

  int16_t current_i16 = buf[2] | (buf[3] << 8);
  float current = current_i16 * 0.1;

  int16_t temperature_i16 = buf[4] | (buf[5] << 8);
  float temp = temperature_i16 * 0.1;

  Serial.println("Vero BMS status");
  Serial.print("    Voltage: "); Serial.print(voltage, 2); Serial.println(" V");
  Serial.print("    Current: "); Serial.print(current, 1); Serial.println(" A");
  Serial.print("    Temperature: "); Serial.print(temp, 1); Serial.println(" C");
}

// vehicle properties to track from loop to loop
// these are mostly going to be from CAN
uint8_t max_pack_voltage = 0;
uint8_t min_pack_voltage = 0;
uint8_t max_pack_temperature = 0;
uint8_t min_pack_temperature = 0;
uint16_t pack_charge_level = 0;

uint16_t motor_tach_raw = 0;

void writeLCD() {
  lcd.clear();
  
  // display tach values
  float max_tach = 1024.0;
  float tach_ratio_f = (float)motor_tach_raw / max_tach * 15;
  int tach_ratio_i = roundf(tach_ratio_f);

  lcd.print("Tach:");
  for (int i=0; i < tach_ratio_i; i++) {
    if (i < 10) {
      lcd.write(CHAR_EMPTY_BAR);
    } else {
      lcd.write(CHAR_FULL_BAR);
    }
  }

  // display charge values
  float max_charge = 1024.0;
  float charge_ratio_f = (float)pack_charge_level / max_charge * 13;
  int charge_ratio_i = roundf(charge_ratio_f);
  lcd.setCursor(0, 1);
  lcd.print("Charge:");

  for (int i=0; i < charge_ratio_i; i++){
    if (i > 0) {
      lcd.write(CHAR_FULL_BAR);
    } else {
      lcd.write(CHAR_EMPTY_BAR);
    }
  }

}

void writeBatteryTFT() {
  digitalWrite(DO_LEFT_LCD_CS, HIGH);
  delay(1);
  SPITransfer(max_pack_voltage, 5);
  SPITransfer(min_pack_voltage, 5);
  SPITransfer(max_pack_temperature, 5);
  SPITransfer(min_pack_temperature, 5);
  delay(1);
  digitalWrite(DO_LEFT_LCD_CS, LOW);
}

void writeVehicleStatusTFT(bool fl_door, bool fr_door, bool rl_door, bool rr_door, bool tailgate) {
  byte status = tailgate + 2 * rr_door + 4 * rl_door + 8 * fr_door + 16 * fl_door;
  digitalWrite(DO_RIGHT_LCD_CS, HIGH);
  delay(1);
  SPITransfer(status, 5);
  delay(1);
  digitalWrite(DO_RIGHT_LCD_CS, LOW);
}

void loop() {
  uint32_t id; 
  uint8_t len; 
  uint8_t ret;
  uint8_t buf[24];

  // process all inputs
  // first, process whatever might have come over from the CAN bus.
  if (!digitalRead(INT_EV)) {
    ret = ev_can.readMsgBuf(&id, &len, buf);
    if (ret != CAN_OK) {
      Serial.println("Error decoding CAN message");
      return;
    }

    handle_bms_message(id, len, buf);
    handle_x1_CAN_message(id, len, buf);

  }

  // vehicle speed
  int vehicle_speed_raw = analogRead(AI_VSS);
  // a bunch of discrete inputs (wonder if these could be 
  // replaced with actual hardware and some bulbs)
  bool status_brake_warning = digitalRead(DI_BRAKE_WARNING);
  bool status_12v_batt_warning = digitalRead(DI_12V_BATT_WARNING);
  bool status_abs_warning = digitalRead(DI_ABS_WARNING);
  bool status_seat_belts = digitalRead(DI_SEAT_BELTS);
  bool status_hi_beam = digitalRead(DI_HI_BEAM);
  bool status_left_turn = digitalRead(DI_LEFT_TURN);
  bool status_right_turn = digitalRead(DI_RIGHT_TURN);
  bool status_fl_door_warning = digitalRead(DI_FL_DOOR_WARNING);
  bool status_fr_door_warning = digitalRead(DI_FR_DOOR_WARNING);
  bool status_rl_door_warning = digitalRead(DI_RL_DOOR_WARNING);
  bool status_rr_door_warning = digitalRead(DI_RR_DOOR_WARNING);
  bool status_tailgate_warning = digitalRead(DI_TAILGATE_WARNING);

  // Now we send outputs
  writeLCD();
  writeBatteryTFT();
  writeVehicleStatusTFT(
      status_fl_door_warning, 
      status_fr_door_warning,
      status_rl_door_warning,
      status_rr_door_warning,
      status_tailgate_warning
  );
  speedometer_display_7seg.print(vehicle_speed_raw, DEC);
  speedometer_display_7seg.writeDisplay();
  tachometer_display_7seg.print(motor_tach_raw, DEC);
  tachometer_display_7seg.writeDisplay();

  // Send info to CAN pump every 10ms or so
  if (millis() - last_dispatch_time > 10) {
    Serial.println("dispatched message to PS Pump");
    convert_513_values(500, 0, buf);
    ev_can.sendMsgBuf(513, 0, 8, buf);
    last_dispatch_time = millis();
  }
}
