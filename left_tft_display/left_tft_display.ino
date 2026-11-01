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
    480 /* width */, 480 /* height */, rgbpanel, 3 /* rotation */, false /* auto_flush */,
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

volatile uint8_t rxByteCounter = 0;
volatile uint8_t rxBitCounter = 0;
#define RX_BUF_SIZE 8
volatile uint8_t rx_buf[RX_BUF_SIZE] {};
volatile bool transmissionComplete = false;
bool prevCSELStatus;
bool currCSELStatus;
bool prevSCLKStatus;
bool currSCLKStatus;
volatile SPIState currentSPIState;

struct Limits {
  uint16_t alarmLow;
  uint16_t warnLow;
  uint16_t warnHigh;
  uint16_t alarmHigh;
};

struct CyclicValue {
  uint16_t oldValue = 0;
  uint16_t newValue = 0;
};

struct GaugeParameters{
  float gaugeStartAngle;
  float gaugeEndAngle;
  float gaugeMinValue;
  float gaugeMaxValue;
  bool isClockwise = false;
};

struct NeedleGraphicsParms {
  float tip_x;
  float tip_y;
  float text_x;
  float text_y;
};

GaugeParameters voltageGauge;
GaugeParameters temperatureGauge;
Limits packVoltageLimits_mV;
Limits packTempLimits_K;

CyclicValue maxPackVoltage_mV;
CyclicValue minPackVoltage_mV;
CyclicValue maxPackTemp_K;
CyclicValue minPackTemp_K;


void IRAM_ATTR onCSLow() {
  if (currentSPIState == IDLE) {
    rxBitCounter = 0;
    rxByteCounter = 0;
    for (int i = 0; i < RX_BUF_SIZE; i++) {
      rx_buf[i] = 0;
    }
    currentSPIState = LISTENING;
    transmissionComplete = false;
  }
}


void IRAM_ATTR onCSHigh() {
  // just punt the state machine back to idle
  if (currentSPIState == LISTENING) {
    currentSPIState = IDLE;
    transmissionComplete = true;
  }
}


void IRAM_ATTR onCSChange() {
  if (digitalRead(CSEL)) {
    onCSHigh();
  } else {
    onCSLow();
  }
}


void IRAM_ATTR onSCLKHigh() {
  if ((currentSPIState != LISTENING) || (rxByteCounter >= RX_BUF_SIZE)) {
    return;
  }

  bool newBit = digitalRead(MOSI);
  rx_buf[rxByteCounter] += newBit << (7 - rxBitCounter);
  rxBitCounter++;
  if (rxBitCounter == 8) {
    rxBitCounter = 0;
    rxByteCounter++;
  }
}


void drawCross() {
  gfx->startWrite();
  for (int offset = -3; offset < 4; offset++) {
    gfx->writeFastVLine(240 + offset, 20, 440, RGB565_WHITE);
    gfx->writeFastHLine(60, 240 + offset, 360, RGB565_WHITE);
  }
  gfx->endWrite();
  gfx->flush();
}

void blankDynamicTextAreas() {
  gfx->writeFillRect(270, 182, 110, 40, RGB565_BLACK);
  gfx->writeFillRect(270, 327, 110, 40, RGB565_BLACK);
  gfx->writeFillRect(100, 182, 110, 40, RGB565_BLACK);
  gfx->writeFillRect(100, 327, 110, 40, RGB565_BLACK);
}


void setup(void)
{
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

  // attach SPI-related interrupts
  attachInterrupt(CSEL, onCSChange, CHANGE);
  attachInterrupt(SCLK, onSCLKHigh, RISING);

  // initialize gauge settings
  voltageGauge.gaugeStartAngle = 60.0;
  voltageGauge.gaugeEndAngle = -60.0;
  voltageGauge.gaugeMinValue = 3200.0;
  voltageGauge.gaugeMaxValue = 4280.0;

  temperatureGauge.gaugeStartAngle = 120.0;
  temperatureGauge.gaugeEndAngle = -120.0;
  temperatureGauge.gaugeMinValue = 273.0 - 10.0;
  temperatureGauge.gaugeMaxValue = 273.0 + 50.0;
  temperatureGauge.isClockwise = true;

  packVoltageLimits_mV.alarmLow = 3300;
  packVoltageLimits_mV.warnLow = 3400;
  packVoltageLimits_mV.warnHigh = 4100;
  packVoltageLimits_mV.alarmHigh = 4200;

  packTempLimits_K.alarmLow = 273 + 0;
  packTempLimits_K.warnLow = 273 + 15;
  packTempLimits_K.warnHigh = 273 + 27;
  packTempLimits_K.alarmHigh = 273 + 45;

  // initial drawing
  gfx->fillScreen(RGB565_BLACK);
  gfx->setFont(u8g2_font_crox1hb_tf);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);

  gfx->setCursor(270, 130);
  gfx->setTextSize(2);
  gfx->print("Max Pack");
  gfx->setCursor(270, 160);
  gfx->print("Voltage (V)");

  gfx->setCursor(270, 275);
  gfx->print("Min Pack");
  gfx->setCursor(270, 305);
  gfx->print("Voltage (V)");

  gfx->setCursor(100, 130);
  gfx->print("Max Pack");
  gfx->setCursor(100, 160);
  gfx->print("Temp (C)");

  gfx->setCursor(100, 275);
  gfx->print("Min Pack");
  gfx->setCursor(100, 305);
  gfx->print("Temp (C)");
  gfx->setTextSize(4);

  drawCross();

  expander->pinMode(PCA_TFT_BACKLIGHT, OUTPUT);
  expander->digitalWrite(PCA_TFT_BACKLIGHT, HIGH);
}

void rotate(CyclicValue &vals) {
  vals.oldValue = vals.newValue;
}


float calculateAngle(GaugeParameters gauge, float value) {
  value = clamp(value, gauge.gaugeMinValue, gauge.gaugeMaxValue);
  float angleDelta = gauge.gaugeEndAngle - gauge.gaugeStartAngle;
  if (gauge.isClockwise != (gauge.gaugeStartAngle < gauge.gaugeEndAngle)) {
    angleDelta += 360.0;
  }

  float angle = (value - gauge.gaugeMinValue) / 
    (gauge.gaugeMaxValue - gauge.gaugeMinValue) * angleDelta + gauge.gaugeStartAngle;
  return std::fmod(angle + 180.0, 360.0) - 180.0;
  // return angle;
}


void calculateAngleGraphicsPositions(float angle, NeedleGraphicsParms &graphixParms) {
  float x = cos(angle / 180.0 * M_PI);
  float y = sin(angle / 180.0 * M_PI);

  graphixParms.tip_x = 240 + 240 * x;
  graphixParms.tip_y = 240 + 240 * y;

  graphixParms.text_x = 240 + 100 * x;
  graphixParms.text_y = 240 + 100 * y;

  if (((angle > 90) && (angle < 180)) || (angle > 270)) {
    graphixParms.text_y += 12;
  }
}


void renderNeedle(GaugeParameters &gauge, Limits &lims, CyclicValue &values) {
  // first black out where the needle previously was
  int thickness = 3;
  NeedleGraphicsParms needle_gfx;
  float angle = calculateAngle(gauge, values.oldValue);
  calculateAngleGraphicsPositions(angle, needle_gfx);
  drawLineImpl(240, 240, needle_gfx.tip_x, needle_gfx.tip_y, thickness, RGB565_BLACK);

  // now draw the line to match its current value
  angle = calculateAngle(gauge, values.newValue);
  calculateAngleGraphicsPositions(angle, needle_gfx);
  drawLineImpl(240, 240, needle_gfx.tip_x, needle_gfx.tip_y, thickness, color_for_value(values.newValue, lims));
}


void renderGauge(
    GaugeParameters &gauge,
    Limits &lims, 
    CyclicValue &topValues, 
    CyclicValue &botValues
) {
    float top_angle_rad = calculateAngle(gauge, topValues.newValue) * M_PI / 180.0;
    float alarm_high_rad = calculateAngle(gauge, lims.alarmHigh) * M_PI / 180.0;
    float gauge_top_rad = gauge.gaugeEndAngle * M_PI / 180.0;
    float warn_high_rad = calculateAngle(gauge, lims.warnHigh) * M_PI / 180.0;
    float warn_low_rad = calculateAngle(gauge, lims.warnLow) * M_PI / 180.0;
    float alarm_low_rad = calculateAngle(gauge, lims.alarmLow) * M_PI / 180.0;
    float low_angle_rad = calculateAngle(gauge, botValues.newValue) * M_PI / 180.0;
    float gauge_bot_rad = gauge.gaugeStartAngle * M_PI / 180.0;

    Serial.print("top angle: ");
    Serial.println(top_angle_rad * 180.0 / M_PI);
    Serial.print("alarm high angle: ");
    Serial.println(alarm_high_rad * 180.0 / M_PI);
    Serial.print("warn high angle: ");
    Serial.println(warn_high_rad * 180.0 / M_PI);
    Serial.print("warn low angle: ");
    Serial.println(warn_low_rad * 180.0 / M_PI);
    Serial.print("alarm low angle: ");
    Serial.println(alarm_low_rad * 180.0 / M_PI);
    Serial.print("low angle: ");
    Serial.println(low_angle_rad * 180.0 / M_PI);

    for (int i = 0; i < 10; i++) {
      renderArc(
          240, 240,
          200 + i,
          top_angle_rad,
          low_angle_rad,
          gauge_top_rad,
          alarm_high_rad,
          warn_high_rad,
          warn_low_rad,
          alarm_low_rad,
          gauge_bot_rad,
          gauge.isClockwise
      );
    }
}


void renderArc(
  uint16_t center_x, 
  uint16_t center_y, 
  uint16_t radius, 
  float top_val_rad,
  float bot_val_rad,
  float gauge_top_rad,
  float alarm_high_rad,
  float warn_high_rad,
  float warn_low_rad,
  float alarm_low_rad,
  float gauge_bot_rad, 
  bool arc_is_clockwise
) {
  float x_stop_f = ceil(radius / sqrt(2.0));
  for (int x = center_x; x < min(480, center_x + (int)x_stop_f); x++) {
    int y = round(sqrt(radius * radius - (x - center_x) * (x - center_x)) + center_y);
    int dx = x - center_x;
    int dy = y - center_y;

    int xs[8] = {dx, dy, -dy, -dx, -dx, -dy, dy, dx};
    int ys[8] = {dy, dx, dx, dy, -dy, -dx, -dx, -dy};

    for (int i = 0; i < 8; i++) {
      float angle = atan2(ys[i], xs[i]);
      uint16_t color;
      if (
          check_in_arc(gauge_top_rad, top_val_rad, angle, arc_is_clockwise) ||
          check_in_arc(bot_val_rad, gauge_bot_rad, angle, arc_is_clockwise)
      ) {
        color = RGB565_BLACK;
      } else if (check_in_arc(gauge_top_rad, alarm_high_rad, angle, arc_is_clockwise)) {
        color = RGB565_RED;
      } else if (check_in_arc(alarm_high_rad, warn_high_rad, angle, arc_is_clockwise)) {
        color = RGB565_YELLOW;
      } else if (check_in_arc(warn_high_rad, warn_low_rad, angle, arc_is_clockwise)) {
        color = 0b0000011111100000;
      } else if (check_in_arc(warn_low_rad, alarm_low_rad, angle, arc_is_clockwise)) {
        color = RGB565_YELLOW;
      } else if (check_in_arc(alarm_low_rad, gauge_bot_rad, angle, arc_is_clockwise)){
        color = RGB565_RED;
      } else {
        continue;
      }
      gfx->writePixel(center_x + xs[i], center_y + ys[i], color);
    }
  }
}


bool check_in_arc(float arc_start, float arc_end, float value, bool arc_is_clockwise) {
  // this case means we have a discontinuity
  if (arc_is_clockwise && (arc_start < arc_end)) {
    return (value < arc_start) || (value > arc_end);
  }
  return (value > min(arc_start, arc_end)) && (value < max(arc_end, arc_start));
}


void drawLineImpl(int start_x, int start_y, int end_x, int end_y, int thickness, uint16_t color) {
  for (int x_offset = 1 - thickness; x_offset < thickness; x_offset++) {
    for (int y_offset = 1 - thickness; y_offset < thickness; y_offset++) {
      gfx->writeSlashLine(
          start_x + x_offset, 
          start_y + y_offset, 
          end_x + x_offset, 
          end_y + y_offset, color);
    }
  }
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

  // for drawing, 0 degrees points straight to the right, and 
  // clockwise is the positive direction.
  if (transmissionComplete) {

    rotate(maxPackVoltage_mV);
    rotate(minPackVoltage_mV);
    rotate(minPackTemp_K);
    rotate(maxPackTemp_K);

    maxPackVoltage_mV.newValue = rx_buf[0] | (rx_buf[1] << 8);
    minPackVoltage_mV.newValue = rx_buf[2] | (rx_buf[3] << 8);
    maxPackTemp_K.newValue = (rx_buf[4] | (rx_buf[5] << 8));
    minPackTemp_K.newValue = (rx_buf[6] | (rx_buf[7] << 8));

    gfx->startWrite();
    renderGauge(voltageGauge, packVoltageLimits_mV, maxPackVoltage_mV, minPackVoltage_mV);
    renderGauge(temperatureGauge, packTempLimits_K, maxPackTemp_K, minPackTemp_K);
    blankDynamicTextAreas();
    gfx->endWrite();
    gfx->flush();

    // update text readouts
    gfx->setTextSize(4);
    gfx->setCursor(270, 222);
    gfx->print(maxPackVoltage_mV.newValue / 1000.0);

    gfx->setCursor(270, 367);
    gfx->print(minPackVoltage_mV.newValue / 1000.0);

    gfx->setCursor(100, 222);
    gfx->print(maxPackTemp_K.newValue - 273);

    gfx->setCursor(100, 367);
    gfx->print(minPackTemp_K.newValue - 273);
    gfx->flush();

    transmissionComplete = false;
  }

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
        gfx->writeSlashLine(
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
    Limits &lims
) {
  if ((val < lims.alarmLow) || (val > lims.alarmHigh)) {
    return 0xf00f;
  } else if ((val < lims.warnLow) || (val > lims.warnHigh)) {
    return 0xfe0f;
  } 
  return 0xffff;
}

