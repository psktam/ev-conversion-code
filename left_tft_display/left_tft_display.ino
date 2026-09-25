// SPDX-FileCopyrightText: 2023 Limor Fried for Adafruit Industries
// SPDX-License-Identifier: MIT
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
    480 /* width */, 480 /* height */, rgbpanel, 3 /* rotation */, true /* auto_flush */,
    expander, GFX_NOT_DEFINED /* RST */, TL021WVC02_init_operations, sizeof(TL021WVC02_init_operations));


// you know what, fuck it. I'm going to write my own 
// SPI slave driver because this shit ain't working
// Fuck ESP32; you made me do this.
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
#define RX_BUF_SIZE 8
uint8_t rx_buf[RX_BUF_SIZE] {};
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

  delay(1000);

  gfx->fillScreen(RGB565_BLACK);
  gfx->setFont(u8g2_font_crox1hb_tf);

  expander->pinMode(PCA_TFT_BACKLIGHT, OUTPUT);
  expander->digitalWrite(PCA_TFT_BACKLIGHT, HIGH);
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
      rxBitCounter = 0;
      rxByteCounter = 0;
      for (int i=0; i < RX_BUF_SIZE; i++) {
        rx_buf[i] = 0;
      }
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
    } else if ((rxByteCounter < RX_BUF_SIZE) && currSCLKStatus && !prevSCLKStatus) {
      bool newBit = digitalRead(MOSI);
      rx_buf[rxByteCounter] += newBit << (7 - rxBitCounter);
      rxBitCounter++;
      if (rxBitCounter == 8) {
        rxBitCounter = 0;
        rxByteCounter++;
      }
    }
  }

  // for drawing, 0 degrees points straight to the right, and 
  // clockwise is the positive direction.

  // drawing battery-related stuff
  uint16_t maxPackVoltagemV = 0;
  uint16_t minPackVoltagemV = 0;
  uint16_t maxPackTempK = 0;
  uint16_t minPackTempK = 0;

  if (transmissionComplete) {

    maxPackVoltagemV = rx_buf[0] | (rx_buf[1] << 8);
    minPackVoltagemV = rx_buf[2] | (rx_buf[3] << 8);
    maxPackTempK = rx_buf[4] | (rx_buf[5] << 8);
    minPackTempK = rx_buf[6] | (rx_buf[7] << 8);
    transmissionComplete = false;

    float battScaleMax = 4.280;
    float battScaleMin = 3.200;

    float minBattAngle = scaleQuantity(
        (float)minPackVoltagemV * 0.001,
        battScaleMin,
        battScaleMax,
        60.0,
        -60.0,
        true
        );
    float maxBattAngle = scaleQuantity(
        (float)maxPackVoltagemV * 0.001,
        battScaleMin,
        battScaleMax,
        60.0, 
        -60.0,
        true
        );

    float tempScaleMin = -10;
    float tempScaleMax = 50;
    float minTempAngle = scaleQuantity(
        (float)(minPackTempK - 273),
        tempScaleMin,
        tempScaleMax,
        120.0,
        -120.0,
        false
    );
    float maxTempAngle = scaleQuantity(
        (float)(maxPackTempK - 273),
        tempScaleMin,
        tempScaleMax,
        120.0, 
        -120.0,
        false
    );

    gfx->fillScreen(RGB565_BLACK);
    
    char caption[15] = "";
    memset(caption, 0, sizeof(caption));
    formatBatteryVoltage(caption, true, minPackVoltagemV);
    drawNeedle(
        minBattAngle, caption, 
        color_for_value(minPackVoltagemV, 3300, 3400, 4100, 4200)
    );

    memset(caption, 0, sizeof(caption));
    formatBatteryVoltage(caption, false, maxPackVoltagemV);
    drawNeedle(
        maxBattAngle, caption,
        color_for_value(maxPackVoltagemV, 3300, 3400, 4100, 4200)
    );

    memset(caption, 0, sizeof(caption));
    formatBatteryTemperature(caption, true, (int16_t)minPackTempK - 273);
    drawNeedle(
        minTempAngle, caption,
        color_for_value(minPackTempK, 0 + 273, 15 + 273, 27 + 273, 45 + 273)
    );

    memset(caption, 0, sizeof(caption));
    formatBatteryTemperature(caption, false, (int16_t)maxPackTempK - 273);
    drawNeedle(
        maxTempAngle, caption,
        color_for_value(maxPackTempK, 0 + 273, 15 + 273, 27 + 273, 45 + 273)
    );
  }

  prevCSELStatus = currCSELStatus;
  prevSCLKStatus = currSCLKStatus;
}

float clamp(float value, float minValue, float maxValue) {
  return min(max(value, minValue), maxValue);
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


float scaleQuantity(
    float value, 
    float minValue, 
    float maxValue, 
    float minAngle,
    float maxAngle,
    bool invertRotation) {


  value = clamp(value, minValue, maxValue);
  float angleDelta = maxAngle - minAngle;
  if (invertRotation == (minAngle < maxAngle)) {
    // this means we cross 180 at some point
    angleDelta += 360.0;
  }
  float angle = (value - minValue) / (maxValue - minValue) * angleDelta + minAngle;
  return std::fmod(angle + 180.0, 360.0) - 180.0;
}

void formatBatteryVoltage(char* buffer, bool isMin, uint16_t voltagemV) {
  if (isMin) {
    buffer[0] = 'M';
    buffer[1] = 'i';
    buffer[2] = 'n';
    buffer[3] = ':';
    buffer[4] = ' ';
  } else {
    buffer[0] = 'M';
    buffer[1] = 'a';
    buffer[2] = 'x';
    buffer[3] = ':';
    buffer[4] = ' ';
  }

  buffer[5] = 48 + (voltagemV / 1000);
  buffer[6] = '.';
  buffer[7] = 48 + ((voltagemV % 1000) / 100);
  buffer[8] = 48 + ((voltagemV % 100) / 10);
  buffer[9] = 48 + (voltagemV % 10);
  buffer[10] = ' ';
  buffer[11] = 'V';
}


void formatBatteryTemperature(char* buffer, bool isMin, int16_t temp_c) {
  if (isMin) {
    buffer[0] = 'M';
    buffer[1] = 'i';
    buffer[2] = 'n';
    buffer[3] = ':';
    buffer[4] = ' ';
  } else {
    buffer[0] = 'M';
    buffer[1] = 'a';
    buffer[2] = 'x';
    buffer[3] = ':';
    buffer[4] = ' ';
  }

  int idx = 5;
  if (temp_c < 0) {
    temp_c = -temp_c;
    buffer[5] = '-';
    idx = 6;
  }

  if ((temp_c / 10) != 0) {
    buffer[idx++] = 48 + (temp_c / 10);
  }
  buffer[idx++] = 48 + (temp_c % 10);
  buffer[idx++] = ' ';
  buffer[idx++] = 'C';
}


void drawNeedle(float needleAngle, char* caption, uint16_t color) {

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

    for (int x_offset = -2; x_offset < 3; x_offset++) {
      for (int y_offset = -2; y_offset < 3; y_offset++) {
        gfx->drawLine(
            240 + x_offset, 240 + y_offset, 
            tip_x + x_offset, tip_y + y_offset, 
            color);
      }
    }

    gfx->setCursor(text_x, text_y);
    gfx->println(caption);
}

uint16_t color_for_value(
    uint16_t val, 
    uint16_t danger_low, uint16_t warn_low, 
    uint16_t warn_high, uint16_t danger_high
) {
  if ((val < danger_low) || (val > danger_high)) {
    return 0xf00f;
  } else if ((val < warn_low) || (val > warn_high)) {
    return 0xfe0f;
  } 
  return 0xffff;
}

