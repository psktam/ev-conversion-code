// SPDX-FileCopyrightText: 2023 Limor Fried for Adafruit Industries
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
    480 /* width */, 480 /* height */, rgbpanel, 0 /* rotation */, true /* auto_flush */,
    expander, GFX_NOT_DEFINED /* RST */, TL021WVC02_init_operations, sizeof(TL021WVC02_init_operations));


uint16_t *screenBuffer;
unsigned long runtime;

// The Capacitive touchscreen overlays uses hardware I2C (SCL/SDA)

// Most touchscreens use FocalTouch with I2C Address often but not always 0x48!
#define I2C_TOUCH_ADDR 0x48

// 2.1" 480x480 round display use CST826 touchscreen with I2C Address at 0x15
//#define I2C_TOUCH_ADDR 0x15  // often but not always 0x48!

bool isFocalTouch = false;

float needle_angle = 0.0;

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

  Serial.println("Initialized!");

  gfx->fillScreen(RGB565_BLACK);

  expander->pinMode(PCA_TFT_BACKLIGHT, OUTPUT);
  expander->digitalWrite(PCA_TFT_BACKLIGHT, HIGH);

  screenBuffer = (uint16_t *) ps_malloc(gfx->width() * gfx->height() * sizeof(uint16_t));
  // if (screenBuffer) {
    // generateColorWheel(screenBuffer);
    // gfx->draw16bitRGBBitmap(0, 0, screenBuffer, gfx->width(), gfx->height());
  //}

  runtime = millis();
}

void loop()
{
  unsigned long looptime = millis();
  // use the buttons to turn off
  if (! expander->digitalRead(PCA_BUTTON_DOWN)) {
    expander->digitalWrite(PCA_TFT_BACKLIGHT, LOW);
  }
  // and on the backlight
  if (! expander->digitalRead(PCA_BUTTON_UP)) {
    expander->digitalWrite(PCA_TFT_BACKLIGHT, HIGH);
  }

  if ((looptime - runtime) > 1000) {
    needle_angle += 6.0;
    if (needle_angle > 360.0) {
      needle_angle -= 360.0;
    }

    gfx->fillScreen(RGB565_BLACK);
  
    float angle_x = cos(needle_angle / 180.0 * M_PI);
    float angle_y = sin(needle_angle / 180.0 * M_PI);

    int16_t tip_x = 240 + 240 * angle_x;
    int16_t tip_y = 240 + 240 * angle_y;

    int16_t text_x = 240 + 100 * angle_x;
    int16_t text_y = 240 + 100 * angle_y;

    if (((needle_angle > 90) && (needle_angle < 180)) || (needle_angle > 270)) {
      text_y += 12;
    }

    int16_t angle_as_int = needle_angle;

    gfx->setCursor(text_x, text_y);
    gfx->setFont(u8g2_font_crox1hb_tf);
    gfx->print("max voltage: ");
    gfx->println(angle_as_int);

    gfx->drawLine(240, 240, tip_x, tip_y, 0xffff);

    runtime = millis();
  }
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

