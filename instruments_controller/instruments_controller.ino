#include <SPI.h>

void setup() {
  Serial.begin(115200);
  pinMode(SS, OUTPUT);
  pinMode(SCK, OUTPUT);
  pinMode(MOSI, OUTPUT);
  // SPI.begin();

  digitalWrite(SS, HIGH);
  digitalWrite(SCK, LOW);
  digitalWrite(MOSI, LOW);

  Serial.print("MOSI Pin: ");
  Serial.println(MOSI);
  Serial.print("MISO Pin: ");
  Serial.println(MISO);
  Serial.print("SCK Pin:  ");
  Serial.println(SCK);
  Serial.print("CS Pin:   ");
  Serial.println(SS);
}

// we transfer in mode 0, with COPA 0 and CPHA 0
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

void printBits(uint8_t b) {
  uint8_t mask = 0b10000000;
  for (int i = 0; i < 8; i++) {
    if (mask & b) {
      Serial.print(" 1");
    } else {
      Serial.print(" 0");
    }
    mask = mask >> 1;
  }
}

// on my nano, there appears to be an issue with SPI 
// implementation. I might just roll my own bit-banged 
// implementation of a SPI controller node.

void loop() {
  // get a needle position from the serial console
  Serial.print("please provide an angle to set the needle to: ");
  while (Serial.available() == 0) { }
  Serial.println();

  uint16_t angle = Serial.parseInt();
  Serial.print("got an angle: ");
  Serial.println(angle);

  uint8_t rx_buffer[8] {0, 1, 2, 3, 4, 5, 6, 7};

  // send to the peripheral
  digitalWrite(SS, LOW);
  delay(10);
  // rx_buffer[0] = SPITransfer(angle, 1);
  // SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  SPITransfer(angle >> 8, 5);
  SPITransfer(angle & 0x00ff, 5);
  // SPI.endTransaction();
  delay(10);
  digitalWrite(SS, HIGH);

  printBits(angle >> 8);
  Serial.print("    ");
  printBits(angle & 0x00ff);
  Serial.println();
}
