#include <mcp_can_dfs.h>
#include <mcp_can.h>

#include <SPI.h>
#include <Arduino.h>

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

unsigned long last_dispatch_time = 0;

MCP_CAN ev_can(SPI_CS_EV);

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

void handle_power_steering_pump_message(uint32_t message_id, uint8_t len, uint8_t buf[]) {
  if (message_id == 576) {
    Serial.println("Power steering torque_related");
    uint8_t first_byte = (buf[0] << 7) | (buf[1] >> 1);
    uint8_t second_byte = (buf[1] << 7) | (buf[2] >> 1);
    uint16_t torque = (first_byte << 8) | second_byte;

    Serial.print("    Torque: ");
    Serial.print(torque);
    Serial.println("");
  }
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

void loop() {
  uint32_t id; 
  uint8_t len; 
  uint8_t ret;
  uint8_t buf[24];

  // process all inputs
  if (!digitalRead(INT_EV)) {
    ret = ev_can.readMsgBuf(&id, &len, buf);
    if (ret != CAN_OK) {
      Serial.println("Error decoding CAN message");
      return;
    }

    handle_bms_message(id, len, buf);
    // handle_power_steering_pump_message(id, len, buf);

    if (id == 0x18ff50e5) {  // charger message
      Serial.print(millis() / 1000);
      Serial.println(": Got a message from the charger:");
      print_charger_message(len, buf);
    } else {
      char buffer[10];
      sprintf(buffer, "%X", id);
      Serial.print(millis() / 1000);
      Serial.print(": Got unrecognized CAN message with ID: ");
      Serial.print(buffer);
      Serial.print(" and length ");
      Serial.print(len);
      Serial.println("");
    }
  }



  // Send info to CAN pump every 10ms or so
  if (millis() - last_dispatch_time > 10) {
    Serial.println("dispatched message to PS Pump");
    convert_513_values(500, 0, buf);
    ev_can.sendMsgBuf(513, 0, 8, buf);
    last_dispatch_time = millis();
  }
}
