#include <bluefruit.h>
#include "nrf_gpio.h"

#define BTN1 D0
#define BTN2 D1
#define BTN3 D2
#define LONG_PRESS_MS 3000
#define ACTIVE_WINDOW 3000

// Company ID factice obligatoire pour format Manufacturer Data valide
const uint8_t COMPANY_ID[2] = {0xFF, 0xFF};

void majBLE(uint8_t code) {
  uint8_t payload[3] = {COMPANY_ID[0], COMPANY_ID[1], code};
  Bluefruit.Advertising.stop();
  Bluefruit.Advertising.clearData();
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addName();
  Bluefruit.Advertising.addData(BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, payload, 3);
  Bluefruit.Advertising.setInterval(32, 32);
  Bluefruit.Advertising.start(0);
}

void setup() {
  Serial.begin(115200);

  // Lecture LATCH P0 ET P1 avant tout (correction BTN2/D1 sur Port 1)
  uint32_t latch0 = NRF_P0->LATCH;
  uint32_t latch1 = NRF_P1->LATCH;
  NRF_P0->LATCH = latch0;
  NRF_P1->LATCH = latch1;

  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  pinMode(BTN3, INPUT_PULLUP);

  Bluefruit.begin();
  Bluefruit.setTxPower(4);
  Bluefruit.setName("NRF_BTN");
  Bluefruit.autoConnLed(false);

  // Lambda : teste le bon registre LATCH selon le port hardware du pin
  auto latchPressed = [&](uint8_t pin) -> bool {
    uint32_t hwPin = g_ADigitalPinMap[pin];
    if (hwPin < 32)
      return (latch0 >> hwPin) & 1;
    else
      return (latch1 >> (hwPin - 32)) & 1;
  };

  uint32_t dernierClic = millis();

  while (millis() - dernierClic < ACTIVE_WINDOW) {

    uint8_t activeBtn = 0;
    if      (!digitalRead(BTN1) || latchPressed(BTN1)) activeBtn = 1;
    else if (!digitalRead(BTN2) || latchPressed(BTN2)) activeBtn = 2;
    else if (!digitalRead(BTN3) || latchPressed(BTN3)) activeBtn = 3;

    // Efface le latch après le premier tour pour ne pas rejouer l'événement
    latch0 = latch1 = 0;

    if (activeBtn != 0) {
      Serial.println("\n--- NOUVELLE ACTION ---");
      Serial.print("Bouton detecte : ");
      Serial.println(activeBtn);

      ble_gap_addr_t mac = Bluefruit.getAddr();
      Serial.print("=> ADRESSE MAC : ");
      for (int i = 5; i >= 0; i--) {
        if (mac.addr[i] < 0x10) Serial.print("0");
        Serial.print(mac.addr[i], HEX);
        if (i > 0) Serial.print(":");
      }
      Serial.println();

      uint32_t currentPin = (activeBtn == 1) ? BTN1 : (activeBtn == 2 ? BTN2 : BTN3);
      bool longPressTriggered = false;
      uint32_t t0 = millis();

      while (!digitalRead(currentPin)) {
        if (millis() - t0 >= LONG_PRESS_MS) {
          uint8_t longCode = (activeBtn == 1) ? 5 : (activeBtn == 2 ? 6 : 4);
          majBLE(longCode);
          longPressTriggered = true;
          delay(200);
          while (!digitalRead(currentPin)) { delay(10); }
          break;
        }
        delay(10);
      }

      if (!longPressTriggered) {
        majBLE(activeBtn);
        delay(200);
      }

      majBLE(0);
      delay(100);
      dernierClic = millis();
    }

    delay(10);
  }

  // Deep sleep
  Serial.println("\nZzz... Deep Sleep.");
  delay(100);
  Bluefruit.Advertising.stop();

  pinMode(BTN1, INPUT);
  pinMode(BTN2, INPUT);
  pinMode(BTN3, INPUT);

  uint32_t hwPins[3] = {
    g_ADigitalPinMap[BTN1],
    g_ADigitalPinMap[BTN2],
    g_ADigitalPinMap[BTN3]
  };
  for (int i = 0; i < 3; i++) {
    nrf_gpio_cfg_sense_input(hwPins[i], NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
  }

  sd_softdevice_disable();
  NRF_POWER->SYSTEMOFF = 1;
}

void loop() {}