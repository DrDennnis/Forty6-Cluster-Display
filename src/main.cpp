#include <Arduino.h>
#include "driver/twai.h"
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH1106.h>

#define CAN_RX_PIN MOSI
#define CAN_TX_PIN SS

#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

#define OLED_SDA SDA
#define OLED_SCL SCL
Adafruit_SH1106 display(OLED_SDA, OLED_SCL);

// Screen padding
#define PADDING_LEFT 10
#define PADDING_TOP 4
#define PADDING_RIGHT 10

int8_t canGear;
uint8_t canMode;
int8_t canTcuOilTemp;
int8_t canOilTemp;
int8_t canCoolantTemp;
String gear;
String mode;

uint16_t baseID = 0x5F0;

void setup() {
  Serial.begin(115200);

  // Initialize configuration structures using macro initializers
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT((gpio_num_t)CAN_TX_PIN, (gpio_num_t)CAN_RX_PIN, TWAI_MODE_LISTEN_ONLY);
  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();  //Look in the api-reference for other speed sets.
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  // Install TWAI driver
  if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK) {
    Serial.println("TWAI Driver installed");
  } else {
    Serial.println("Failed to install TWAI driver");
    return;
  }

  // Start TWAI driver
  if (twai_start() == ESP_OK) {
    Serial.println("TWAI Driver started");
  } else {
    Serial.println("Failed to start TWAI driver");
    return;
  }

  // Reconfigure alerts to detect frame receive, Bus-Off error and RX queue full states
  uint32_t alerts_to_enable = TWAI_ALERT_RX_DATA | TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_ERROR | TWAI_ALERT_RX_QUEUE_FULL;
  if (twai_reconfigure_alerts(alerts_to_enable, NULL) == ESP_OK) {
    Serial.println("TWAI Alerts reconfigured");
  } else {
    Serial.println("Failed to reconfigure TWAI alerts");
    return;
  }

  display.begin(SH1106_SWITCHCAPVCC, 0x3C);
  display.setRotation(2);
  display.setTextColor(WHITE);

  Serial.println("Setup complete...");
}

void loop() {
  unsigned long currentMillis = millis();

  uint32_t alerts_triggered;
  twai_read_alerts(&alerts_triggered, pdMS_TO_TICKS(1));
  if (alerts_triggered & TWAI_ALERT_RX_DATA) {

    twai_message_t message;
    while (twai_receive(&message, 0) == ESP_OK) {
      if (message.identifier == (baseID + 2)) {
        // Gear -3 is N, -2 is R, -1 is P, 0 is invalid, 1-8 are gears
        canGear = (int8_t)message.data[2];
        if (canGear < 1) {
          if (canGear == -3) gear = "N";
          else if (canGear == -2) gear = "R";
          else if (canGear == -1) gear = "P";
          else gear = "";
        } else {
          gear = String(canGear);
        }
      }

      // TCU Drive Mode 0=Drive, 1=Sport, 2=Manual
      if (message.identifier == (baseID + 5)) {
        canTcuOilTemp = (int8_t)message.data[2];
        canMode = message.data[3];
        switch (canMode) {
          case 0: mode = "D"; break;
          case 1: mode = "S"; break;
          case 2: mode = "M"; break;
          default: mode = "";
        }
      }

      if (message.identifier == 0x545) {
        canOilTemp = message.data[4] - 48;
      }

      if (message.identifier == 0x329) {
        canCoolantTemp = (message.data[1] * 0.75) - 48;
      }
    }

    display.clearDisplay();

    // Left side - Large gear display (size 4)
    display.setTextSize(4);
    int16_t gx1, gy1;
    uint16_t gw, gh;
    String gearText;

    if (gear == "P" || gear == "N" || gear == "R") {
      gearText = gear;
    } else if (mode == "D" || mode == "S" || mode == "M") {
      gearText = mode + gear;
    }

    display.getTextBounds(gearText, 0, 0, &gx1, &gy1, &gw, &gh);
    display.setCursor((display.width() / 2 - gw) / 2 + 5, (display.height() - gh) / 2);
    display.print(gearText);

    // Right side - Temperature values stacked vertically (size 2)
    int16_t x1, y1;
    uint16_t w, h;
    uint8_t lineHeight = 16;
    uint8_t startY = PADDING_TOP + 4;

    // Oil temp
    display.setTextSize(2);
    String oilValue = String(canOilTemp);
    display.getTextBounds(oilValue, 0, 0, &x1, &y1, &w, &h);
    display.setCursor(display.width() - w - 8 - PADDING_RIGHT, startY);
    display.print(oilValue);
    display.setTextSize(1);
    display.setCursor(display.width() - 6 - PADDING_RIGHT, startY);
    display.print("O");

    // Coolant temp
    display.setTextSize(2);
    String coolantValue = String(canCoolantTemp);
    display.getTextBounds(coolantValue, 0, 0, &x1, &y1, &w, &h);
    display.setCursor(display.width() - w - 8 - PADDING_RIGHT, startY + lineHeight);
    display.print(coolantValue);
    display.setTextSize(1);
    display.setCursor(display.width() - 6 - PADDING_RIGHT, startY + lineHeight);
    display.print("C");

    // TCU temp
    display.setTextSize(2);
    String tcuValue = String(canTcuOilTemp);
    display.getTextBounds(tcuValue, 0, 0, &x1, &y1, &w, &h);
    display.setCursor(display.width() - w - 8 - PADDING_RIGHT, startY + lineHeight * 2);
    display.print(tcuValue);
    display.setTextSize(1);
    display.setCursor(display.width() - 6 - PADDING_RIGHT, startY + lineHeight * 2);
    display.print("T");

    display.display();
  }

#ifdef DEBUG_LAYOUT
  display.clearDisplay();

  // Left side - Large gear display (size 4)
  display.setTextSize(4);
  int16_t gx1, gy1;
  uint16_t gw, gh;
  // Alternate between R and S8 every second
  String gearText = (millis() / 1000) % 2 == 0 ? "R" : "S8";

  display.getTextBounds(gearText, 0, 0, &gx1, &gy1, &gw, &gh);
  display.setCursor((display.width() / 2 - gw) / 2 + 5, (display.height() - gh) / 2);
  display.print(gearText);

  // Right side - All values stacked vertically (size 2)
  int16_t x1, y1;
  uint16_t w, h;
  uint8_t lineHeight = 16;
  uint8_t startY = PADDING_TOP + 4;

  // Oil temp
  display.setTextSize(2);
  String oilValue = "125";
  display.getTextBounds(oilValue, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(display.width() - w - 8 - PADDING_RIGHT, startY);
  display.print(oilValue);
  display.setTextSize(1);
  display.setCursor(display.width() - 6 - PADDING_RIGHT, startY);
  display.print("O");

  // Coolant temp
  display.setTextSize(2);
  String coolantValue = "110";
  display.getTextBounds(coolantValue, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(display.width() - w - 8 - PADDING_RIGHT, startY + lineHeight);
  display.print(coolantValue);
  display.setTextSize(1);
  display.setCursor(display.width() - 6 - PADDING_RIGHT, startY + lineHeight);
  display.print("C");

  // TCU temp
  display.setTextSize(2);
  String tcuValue = "88";
  display.getTextBounds(tcuValue, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(display.width() - w - 8 - PADDING_RIGHT, startY + lineHeight * 2);
  display.print(tcuValue);
  display.setTextSize(1);
  display.setCursor(display.width() - 6 - PADDING_RIGHT, startY + lineHeight * 2);
  display.print("T");

  display.display();
#endif
}
