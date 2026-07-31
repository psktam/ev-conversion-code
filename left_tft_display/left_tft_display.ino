// SPDX-FileCopyrightText: 2023 Limor Fried for Adafruit Industries
//
// SPDX-License-Identifier: MIT
#include <ESP32SPISlave.h>
#include <driver/spi_slave.h>
#include <U8g2lib.h>
#include <Arduino_GFX_Library.h>

Arduino_XCA9554SWSPI *expander = new Arduino_XCA9554SWSPI(
    PCA_TFT_RESET, PCA_TFT_CS, PCA_TFT_SCK, PCA_TFT_MOSI,
    &Wire, 0x3F);

Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    TFT_DE, TFT_VSYNC, TFT_HSYNC, TFT_PCLK,
    TFT_R1, TFT_R2, TFT_R3, TFT_R4, TFT_R5,
    TFT_G0, TFT_G1, TFT_G2, TFT_G3, TFT_G4, TFT_G5,
    TFT_B1, TFT_B2, TFT_B3, TFT_B4, TFT_B5,
    1 /* hsync_polarity */, 50 /* hsync_front_porch */, 2 /* hsync_pulse_width */, 44 /* hsync_back_porch */,
    1 /* vsync_polarity */, 16 /* vsync_front_porch */, 2 /* vsync_pulse_width */, 18 /* vsync_back_porch */
    //    ,1, 30000000
    );

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
// 2.1" 480x480 round display
    480 /* width */, 480 /* height */, rgbpanel, 0 /* rotation */, true /* auto_flush */,
    expander, GFX_NOT_DEFINED /* RST */, TL021WVC02_init_operations, sizeof(TL021WVC02_init_operations));


unsigned long runtime;
uint16_t needle_angle = 0;

void post_setup_cb(spi_slave_transaction_t *t) {

}

void post_transaction_cb(spi_slave_transaction_t *t) {

}

// you know what, fuck it. I'm going to write my own 
// SPI slave driver because this shit ain't working
// Fuck ESP32; you made me do this.

esp_err_t err;

// SPI-related stuff
#define MOSI 7
#define MISO 6
#define SCLK 5
#define CSEL 15

enum SPIState {IDLE, LISTENING};

bool pin15_status;
bool pin5_status;
bool pin6_status;
bool pin7_status;

uint8_t rxByteCounter = 0;
uint8_t rxBitCounter = 0;
uint8_t rx_buf[2] {123, 123};
uint8_t tx_buf[2] {222, 111};
bool transmissionComplete = false;
bool prevCSELStatus;
bool currCSELStatus;
bool prevSCLKStatus;
bool currSCLKStatus;
SPIState currentSPIState;

void setup(void)
{
#ifdef GFX_EXTRA_PRE_INIT
  GFX_EXTRA_PRE_INIT();
#endif
  // Init Display

  Wire.setClock(1000000); // speed up I2C
  if (!gfx->begin()) {
    Serial.println("gfx->begin() failed!");
  }

  delay(1000);
  // initialize SPI settings
  currentSPIState = IDLE;
  pinMode(MOSI, INPUT);
  pinMode(MISO, OUTPUT);
  pinMode(CSEL, INPUT);
  pinMode(SCLK, INPUT);
  digitalWrite(MISO, LOW);
  prevCSELStatus = digitalRead(CSEL);
  currCSELStatus = prevCSELStatus;
  prevSCLKStatus = digitalRead(SCLK);
  currSCLKStatus = prevSCLKStatus;
  rxBitCounter = 0;
  rxByteCounter = 0;
  rx_buf[0] = 0;
  rx_buf[1] = 0;

  delay(1000);

  gfx->fillScreen(RGB565_BLACK);

  expander->pinMode(PCA_TFT_BACKLIGHT, OUTPUT);
  expander->digitalWrite(PCA_TFT_BACKLIGHT, HIGH);

  runtime = millis();
  drawNeedle(0.0);
}


void loop()
{
  unsigned long looptime = millis();
  currCSELStatus = digitalRead(CSEL);
  currSCLKStatus = digitalRead(SCLK);
  // use the buttons to turn off
  if (! expander->digitalRead(PCA_BUTTON_DOWN)) {
    expander->digitalWrite(PCA_TFT_BACKLIGHT, LOW);
  }
  // and on the backlight
  if (! expander->digitalRead(PCA_BUTTON_UP)) {
    expander->digitalWrite(PCA_TFT_BACKLIGHT, HIGH);
  }

  // SPI state-machine handling
  if (currentSPIState == IDLE) {
    if (!currCSELStatus) {
      rx_buf[0] = 0;
      rx_buf[1] = 0;
      rxBitCounter = 0;
      rxByteCounter = 0;
      currentSPIState = LISTENING;
      transmissionComplete = false;
    }
  } else if (currentSPIState == LISTENING) {
    // first check to see if CSEL has 
    // transitioned high, in which case we 
    // return to the idle state
    if (currCSELStatus) {
      currentSPIState = IDLE;
      transmissionComplete = true;
    } else if ((rxByteCounter < 2) && currSCLKStatus && !prevSCLKStatus) {
      bool newBit = digitalRead(MOSI);
      rx_buf[rxByteCounter] += newBit << (7 - rxBitCounter);
      rxBitCounter++;
      if (rxBitCounter == 8) {
        rxBitCounter = 0;
        rxByteCounter++;
      }
    }
  }

  if (transmissionComplete) {
    needle_angle = (rx_buf[0] << 8) + rx_buf[1];
    transmissionComplete = false;

    gfx->fillScreen(RGB565_BLACK);
    drawNeedle(needle_angle);
  }

  prevCSELStatus = currCSELStatus;
  prevSCLKStatus = currSCLKStatus;
}

void printBits(uint8_t n) {
  // print from MSB to LSB
  uint8_t mask = 0b10000000;
  Serial.print("   ");
  for (int i = 0; i < 8; i++) {
    if ((mask & n) > 0) {
      Serial.print(" 1");
    } else {
      Serial.print(" 0");
    }
    mask = mask >> 1;
  }
}

void drawNeedle(float needleAngle) {

    float angle_x = cos(needleAngle/ 180.0 * M_PI);
    float angle_y = sin(needleAngle/ 180.0 * M_PI);

    int16_t tip_x = 240 + 240 * angle_x;
    int16_t tip_y = 240 + 240 * angle_y;

    int16_t text_x = 240 + 100 * angle_x;
    int16_t text_y = 240 + 100 * angle_y;

    if (((needleAngle > 90) && (needleAngle < 180)) || (needleAngle > 270)) {
      text_y += 12;
    }

    int16_t angle_as_int = needleAngle;

    gfx->setCursor(text_x, text_y);
    gfx->setFont(u8g2_font_crox1hb_tf);
    gfx->print("max voltage: ");
    gfx->println(angle_as_int);

    gfx->drawLine(240, 240, tip_x, tip_y, 0xffff);
}

