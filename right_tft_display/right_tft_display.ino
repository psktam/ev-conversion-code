// SPDX-FileCopyrightText: 2023 Limor Fried for Adafruit Industries
//
//
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
    480 /* width */, 480 /* height */, rgbpanel, 1 /* rotation */, true /* auto_flush */,
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

bool status_batt_12v;
bool status_abs_warning;
bool status_seat_belts;
bool status_hi_beam;
bool status_fl_door;
bool status_fr_door;
bool status_rl_door;
bool status_rr_door;
bool status_tailgate;

void drawChassis() {
  for (int offset = -2; offset <= 2; offset++) {
    gfx->drawRoundRect(164 + offset, 109 + offset, 151 - 2 * offset, 262 - 2 * offset, 20 - offset, 0xfe0f);
  }

  for (int offset = -1; offset <= 1; offset++) {
    gfx->drawRoundRect(180 + offset, 149 + offset, 119 - 2 * offset, 40 - 2 * offset, max(5 - offset, 0), 0xfe0f);
    gfx->drawRoundRect(180 + offset, 240 + offset, 119 - 2 * offset, 120 - 2 * offset, max(5 - offset, 0), 0xfe0f);
  }

  gfx->setCursor(202, 420);
  gfx->println(rx_buf[0]);
  gfx->setCursor(202, 440);
  gfx->println(rx_buf[1]);
  gfx->setCursor(240, 420);
  gfx->println(rxByteCounter);
  gfx->setCursor(240, 440);
  gfx->println(rxBitCounter);
}

void drawFlags() {
  if (status_tailgate) {
    for (int i = 0; i < 10; i++) {
      gfx->drawFastHLine(202, 383 + i, 74, 0xf00f);
    }
  }

  if (status_fl_door) {
    for (int i = 0; i < 10; i++) {
      gfx->drawLine(88 + i, 240 + i, 141 + i, 187 + i, 0xf00f);
    }
  }

  if (status_rl_door) {
    for (int i = 0; i < 10; i++) {
      gfx->drawLine(88 + i, 293 + i, 141 + i, 240 + i, 0xf00f);
    }
  }

  if (status_fr_door) {
    for (int i = 0; i < 10; i++) {
      gfx->drawLine(337 - i, 187+ i, 390 - i, 240 + i, 0xf00f);
    }
  }

  if (status_rr_door) {
    for (int i = 0; i < 10; i++) {
      gfx->drawLine(337 - i, 240 + i, 390 - i, 293 + i, 0xf00f);
    }
  }

  if (status_hi_beam) {
    gfx->fillTriangle(
      172, 40,
      222, 40,
      197, 97,
      0x09ff
    );
    gfx->fillTriangle(
        258, 40,
        308, 40,
        283,  97,
        0x09ff
    );
  }
}


void setup(void)
{
  Serial.begin(115200);
  //while (!Serial) delay(100);

#ifdef GFX_EXTRA_PRE_INIT
  GFX_EXTRA_PRE_INIT();
#endif

  Serial.println("Beginning");
  // Init Display

  Wire.setClock(1000000); // speed up I2C
  if (!gfx->begin()) {
    Serial.println("gfx->begin() failed!");
  }

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
  drawChassis();

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
      for (int i = 0; i < RX_BUF_SIZE; i++) {
        rx_buf[i] = 0;
      }
      currentSPIState = LISTENING;
      transmissionComplete = false;
    }
  } else if (currentSPIState == LISTENING) {
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

  if (transmissionComplete) {
    status_batt_12v =    rx_buf[0] & 1;
    status_abs_warning = rx_buf[0] & (1 << 1);
    status_seat_belts =  rx_buf[0] & (1 << 2);
    status_hi_beam =     rx_buf[0] & (1 << 3);
    status_fl_door =     rx_buf[0] & (1 << 4);
    status_fr_door =     rx_buf[0] & (1 << 5);
    status_rl_door =     rx_buf[0] & (1 << 6);
    status_rr_door =     rx_buf[0] & (1 << 7);
    status_tailgate =    rx_buf[1] & 1;
    transmissionComplete = false;

    gfx->fillScreen(0x0000);
    drawChassis();
    drawFlags();
  }

  prevSCLKStatus = currSCLKStatus;
}

// https://chat.openai.com/share/8edee522-7875-444f-9fea-ae93a8dfa4ec
void generateColorWheel(uint16_t *colorWheel) {
  int width = gfx->width();
  int height = gfx->height();
  int half_width = width / 2;
  int half_height = height / 2;
  float angle;
  uint8_t r, g, b;
  int index, scaled_index;

  for(int y = 0; y < half_height; y++) {
    for(int x = 0; x < half_width; x++) {
      index = y * half_width + x;
      angle = atan2(y - half_height / 2, x - half_width / 2);
      r = uint8_t(127.5 * (cos(angle) + 1));
      g = uint8_t(127.5 * (sin(angle) + 1));
      b = uint8_t(255 - (r + g) / 2);
      uint16_t color = RGB565(r, g, b);

      // Scale this pixel into 4 pixels in the full buffer
      for(int dy = 0; dy < 2; dy++) {
        for(int dx = 0; dx < 2; dx++) {
          scaled_index = (y * 2 + dy) * width + (x * 2 + dx);
          colorWheel[scaled_index] = color;
        }
      }
    }
  }
}

