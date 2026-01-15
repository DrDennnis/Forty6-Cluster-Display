#include <Arduino.h>
#include "driver/twai.h"
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH1106.h>

#define CAN_RX_PIN GPIO_NUM_6
#define CAN_TX_PIN GPIO_NUM_7

#define OLED_SDA SDA
#define OLED_SCL SCL

// Screen padding
#define PADDING_LEFT 10
#define PADDING_TOP 4
#define PADDING_RIGHT 10

// Temperature conversion constants
#define TEMP_OFFSET 48
#define COOLANT_TEMP_MULTIPLIER 0.75

// Display refresh rate
#define DISPLAY_REFRESH_MS 125

// CAN message IDs
#define CAN_BASE_ID 0x5F0
#define CAN_ID_GEAR (CAN_BASE_ID + 2)
#define CAN_ID_MODE (CAN_BASE_ID + 5)
#define CAN_ID_OIL_TEMP 0x545
#define CAN_ID_COOLANT_TEMP 0x329

Adafruit_SH1106 display(OLED_SDA, OLED_SCL);

const uint32_t canTimeout = 1000;
int8_t canGear = 0;
uint8_t canMode = 0;
int8_t canTcuOilTemp = 0;
int8_t canOilTemp = 0;
int8_t canCoolantTemp = 0;
String gear = "";
String mode = "";

uint32_t lastCanMessageTime = 0;
uint32_t lastDisplayUpdate = 0;
bool canActive = false;

#ifdef DEBUG_LAYOUT
// FPS counter variables
uint32_t frameCount = 0;
uint32_t lastFpsUpdate = 0;
float fps = 0.0;

// Spinner variables
const char spinnerChars[] = {'|', '/', '-', '\\'};
uint8_t canSpinnerIndex = 0;
uint8_t displaySpinnerIndex = 0;
uint8_t canMessageCount = 0;
#endif

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
  uint32_t currentMillis = millis();

  // Check for CAN messages
  uint32_t alerts_triggered;
  twai_read_alerts(&alerts_triggered, pdMS_TO_TICKS(1));
  if (alerts_triggered & TWAI_ALERT_RX_DATA) {
    twai_message_t message;
    while (twai_receive(&message, 0) == ESP_OK) {
      lastCanMessageTime = currentMillis;
      canActive = true;

#ifdef DEBUG_LAYOUT
      // Advance CAN spinner every 2 messages
      canMessageCount++;
      if (canMessageCount >= 2) {
        canMessageCount = 0;
        canSpinnerIndex = (canSpinnerIndex + 1) % 4;
      }
#endif

      if (message.identifier == CAN_ID_GEAR) {
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
      if (message.identifier == CAN_ID_MODE) {
        canTcuOilTemp = (int8_t)message.data[2];
        canMode = message.data[3];
        switch (canMode) {
          case 0: mode = "D"; break;
          case 1: mode = "S"; break;
          case 2: mode = "M"; break;
          default: mode = "";
        }
      }

      if (message.identifier == CAN_ID_OIL_TEMP) {
        canOilTemp = message.data[4] - TEMP_OFFSET;
      }

      if (message.identifier == CAN_ID_COOLANT_TEMP) {
        canCoolantTemp = (message.data[1] * COOLANT_TEMP_MULTIPLIER) - TEMP_OFFSET;
      }
    }
  }

  // Check for CAN timeout
  if (canActive && (currentMillis - lastCanMessageTime > canTimeout)) {
    canActive = false;
    gear = "";
    mode = "";
  }

  // Update display at fixed refresh rate
  if (currentMillis - lastDisplayUpdate >= DISPLAY_REFRESH_MS) {
    lastDisplayUpdate = currentMillis;

#ifdef DEBUG_LAYOUT
    frameCount++;

    // Calculate FPS every second
    if (currentMillis - lastFpsUpdate >= 1000) {
      fps = frameCount * 1000.0 / (currentMillis - lastFpsUpdate);
      frameCount = 0;
      lastFpsUpdate = currentMillis;
    }

    // Advance display spinner every frame
    displaySpinnerIndex = (displaySpinnerIndex + 1) % 4;
#endif

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

#ifdef DEBUG_LAYOUT
    // FPS counter and spinners in bottom left
    display.setTextSize(1);
    display.setCursor(PADDING_LEFT, display.height() - 8);
    display.print(spinnerChars[displaySpinnerIndex]);
    display.print(" ");
    if (canActive) {
      display.print(spinnerChars[canSpinnerIndex]);
    } else {
      display.print("X");
    }
    display.print(" ");
    display.print(String((int)fps) + "fps");
#endif

    display.display();
  }
}
