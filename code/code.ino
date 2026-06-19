#include <mcp_can_dfs.h>
#include <mcp_can.h>

#include <SPI.h>
#include <Arduino.h>


const int INT_PUMP = 6;
const int SPI_CS_PUMP = 8;
unsigned long last_dispatch_time = 0;

MCP_CAN can_pump(SPI_CS_PUMP);

// Provide vehicle speed in km/hr, and engine speed in RPM
void convert_201_values(
    uint16_t engine_speed, 
    uint64_t vehicle_speed,
    uint8_t buf[]
) {
  // apparently the Mazda 3 pump expects RPM as raw bytes
  // and vehicle speed in km/hr * 100
  vehicle_speed *= 100;

  // found an awesome resource here: https://docs.google.com/spreadsheets/d/1SKfXAyo6fbAfMUENw1KR3w4Fvx_Ihj6sTPSVXBdOXKk/edit?gid=0#gid=0
  // that shows the breakdown of the 0x201 code.
  // This message ID is broadcast every 10ms. Breakdown 
  // of bytes is as follows:
  //   B1-2: RPM
  //   B3-4: could be engine torque; unclear
  //   B5-6: vehicle speed
  //   B7: accelerator pedal position
  //   B8: some accelerometer value
  buf[0] = engine_speed / 256;
  buf[1] = engine_speed % 256;
  buf[4] = vehicle_speed / 256;
  buf[5] = vehicle_speed % 256;
}

void setup() {
  Serial.begin(115200);
  pinMode(INT_PUMP, INPUT);

  uint8_t ret = can_pump.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ);
  if (ret == CAN_OK) {
    can_pump.setMode(MCP_NORMAL);
  } else {
    Serial.println("Error initializing pump CAN");
    while (1);
  }
  last_dispatch_time = millis();
}

void print_message_contents(uint32_t id, uint8_t len, uint8_t buf[]) {
  Serial.print("0x");
  Serial.print(id, HEX);
  Serial.print(": ");
  for (uint8_t i=0; i < len; i++) {
    Serial.print("0x");
    Serial.print(buf[i], HEX);
    Serial.print(",");
  }
  Serial.println();
}

// All this code will do is tell the pump that the engine 
// is at 1000 RPM and that we're traveling at around 50km/h
void loop() {
  uint32_t id;
  uint8_t len;
  uint8_t ret;
  uint8_t buf[24];

  if (!digitalRead(INT_PUMP)) {
    ret = can_pump.readMsgBuf(&id, &len, buf);

    // pump RPM/current information
    if (id == 0x240) {
      Serial.print("Received from pump CAN: ");
      print_message_contents(id, len, buf);
    }
  }

  // send the info to the pump CAN about every 10ms or so
  if ((millis() - last_dispatch_time) >= 10) {
    convert_201_values(1000, 50, buf);
    can_pump.sendMsgBuf(0x201, 0, 8, buf);
    last_dispatch_time = millis();
  }
}
