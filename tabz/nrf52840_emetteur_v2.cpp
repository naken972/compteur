#include <bluefruit.h>
#include "nrf_gpio.h"

#define BTN1 D0  // Team 1
#define BTN2 D1  // Team 2
#define BTN3 D2  // Undo

#define LONG_PRESS_MS 3000
#define ACTIVE_WINDOW 3000
#define ADV_MIN_PACKETS 25    // Nombre min de paquets ADV à envoyer
#define ADV_INTERVAL_MS 20    // ~20ms entre paquets (interval 32 slots)
#define DEBOUNCE_MS 50        // Anti-rebond matériel

// Company ID factice obligatoire pour format Manufacturer Data BLE valide
const uint8_t COMPANY_ID[2] = {0xFF, 0xFF};

// Compteur global : chaque nouvel appui incrémente, le récepteur détecte le changement
uint8_t actionCounter = 1;  // Commence à 1 (0 = release/idle)

void majBLE(uint8_t code) {
  // Payload : [Company_Lo][Company_Hi][code][counter]
  uint8_t payload[4] = {COMPANY_ID[0], COMPANY_ID[1], code, actionCounter};
  Bluefruit.Advertising.stop();
  Bluefruit.Advertising.clearData();
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addName();
  Bluefruit.Advertising.addData(BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, payload, 4);
  Bluefruit.Advertising.setInterval(32, 32);  // ~20ms
  Bluefruit.Advertising.start(0);
}

// Attente non-bloquante : envoie le nombre minimum de paquets ADV
// tout en restant réactif aux autres boutons
void waitMinPackets(int count) {
  delay(count * ADV_INTERVAL_MS);
}

void setup() {
  Serial.begin(115200);

  // Lecture LATCH P0 ET P1 avant tout (BTN2/D1 est sur Port 1)
  uint32_t latch0 = NRF_P0->LATCH;
  uint32_t latch1 = NRF_P1->LATCH;
  NRF_P0->LATCH = latch0;
  NRF_P1->LATCH = latch1;

  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  pinMode(BTN3, INPUT_PULLUP);

  Bluefruit.begin();
  Bluefruit.setTxPower(8);    // +8 dBm = puissance max nRF52840
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
  uint32_t lastButtonTime = 0;  // Anti-rebond global

  while (millis() - dernierClic < ACTIVE_WINDOW) {

    uint8_t activeBtn = 0;

    if      (!digitalRead(BTN1) || latchPressed(BTN1)) activeBtn = 1;
    else if (!digitalRead(BTN2) || latchPressed(BTN2)) activeBtn = 2;
    else if (!digitalRead(BTN3) || latchPressed(BTN3)) activeBtn = 3;

    // Efface le latch après le premier tour pour ne pas rejouer l'événement
    latch0 = latch1 = 0;

    if (activeBtn != 0 && (millis() - lastButtonTime >= DEBOUNCE_MS)) {
      lastButtonTime = millis();

      // Affichage MAC pour debug
      ble_gap_addr_t mac = Bluefruit.getAddr();
      Serial.print("=> ADRESSE MAC : ");
      for (int i = 5; i >= 0; i--) {
        if (mac.addr[i] < 0x10) Serial.print("0");
        Serial.print(mac.addr[i], HEX);
        if (i > 0) Serial.print(":");
      }
      Serial.println();

      Serial.println("\n--- NOUVELLE ACTION ---");
      Serial.print("Bouton detecte : ");
      Serial.println(activeBtn);

      uint32_t currentPin = (activeBtn == 1) ? BTN1 : (activeBtn == 2 ? BTN2 : BTN3);
      bool longPressTriggered = false;
      uint32_t t0 = millis();

      // Détection appui long
      while (!digitalRead(currentPin)) {
        if (millis() - t0 >= LONG_PRESS_MS) {
          uint8_t longCode = (activeBtn == 1) ? 5 : (activeBtn == 2 ? 6 : 4);
          Serial.print("Long press -> code ");
          Serial.print(longCode);
          Serial.print(" counter=");
          Serial.println(actionCounter);

          majBLE(longCode);
          actionCounter++;
          if (actionCounter == 0) actionCounter = 1; // Skip 0

          waitMinPackets(ADV_MIN_PACKETS);
          longPressTriggered = true;

          // Attendre relâchement sans bloquer inutilement
          while (!digitalRead(currentPin)) { delay(10); }
          break;
        }
        delay(10);
      }

      if (!longPressTriggered) {
        Serial.print("Short press -> code ");
        Serial.print(activeBtn);
        Serial.print(" counter=");
        Serial.println(actionCounter);

        majBLE(activeBtn);
        actionCounter++;
        if (actionCounter == 0) actionCounter = 1; // Skip 0

        // Envoyer seulement le minimum nécessaire (~500ms au lieu de 1200ms)
        waitMinPackets(ADV_MIN_PACKETS);
      }

      // PAS de release (code 0) : le compteur suffit pour différencier les appuis
      // Ça économise ~400ms par action !

      dernierClic = millis();
      lastButtonTime = millis();
    }

    delay(5);  // Polling plus rapide
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
