#include <Arduino.h>
#include "driver/twai.h"
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define CAN_RX_PIN MOSI
#define CAN_TX_PIN SS

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

int8_t canGear;
uint8_t canMode;
int8_t canTcuOilTemp;
int8_t canOilTemp;
int8_t canCoolantTemp;
String gear;
String mode;

uint16_t baseID = 0x5F0;

unsigned long lastCanMessageTime = 0;
const unsigned long canTimeout = 1000;

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

  display.begin();
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
      lastCanMessageTime = millis();

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
    display.setCursor(50, 10);
    display.setTextSize(3);
    if (gear == "P" || gear == "N" || gear == "R") {
      display.setCursor(55, 10);
      display.println(gear);
    } else if (mode == "D" || mode == "S" || mode == "M") {
      display.print(mode);
      display.println(gear);
    }

    display.setTextSize(2);
    display.setCursor(0, 0);
    display.print(canOilTemp);
    display.println("C");

    display.setCursor(0, 16);
    display.print(canCoolantTemp);
    display.println("C");

    String tcuText = String(canTcuOilTemp) + "C";
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(tcuText, 0, 0, &x1, &y1, &w, &h);
    display.setCursor(display.width() - w - 2, 0);
    display.print(tcuText);
    display.display();
  }

  if (millis() - lastCanMessageTime > canTimeout) {
    display.clearDisplay();
    display.display();
  }
}
