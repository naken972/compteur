#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <vector>
#include "esp_bt.h"

// -------- CONFIGURATION MATRICE --------
#define PANEL_RES_X 64
#define PANEL_RES_Y 32
#define PANEL_CHAIN 1

HUB75_I2S_CFG mxconfig(PANEL_RES_X, PANEL_RES_Y, PANEL_CHAIN);
MatrixPanel_I2S_DMA *dma_display;

// -------- CONFIGURATION BLE --------
String targetMac = "f3:d3:3d:71:e8:9c";
uint8_t dernierEtat = 255;
BLEScan* pBLEScan;

// -------- ÉTATS DU SYSTÈME --------
enum SystemState {
  STATE_BOOT,
  STATE_GAME,
  STATE_MENU_GAMES,
  STATE_MENU_ADVANTAGE,
  STATE_MENU_BRIGHTNESS
};
SystemState currentState = STATE_BOOT;

// -------- CONFIGURATION JEU & AFFICHAGE --------
int configGamesPerSet = 6;
int configAdvantageMode = 0; // 0: Ad (STD), 1: Punto de Oro, 2: 3Ad
const char* advModeNames[] = {"STD", "Punto Oro", "3AD"};

// Luminosité : tranches de 10% propres (0, 10, 20, ..., 100)
// On stocke le pourcentage directement, conversion en 0-255 à l'application
int brightnessPercent = 60; // 60% par défaut

// -------- SCORE DATA --------
int pointsTeam1_raw = 0, pointsTeam2_raw = 0;
int gamesTeam1 = 0, gamesTeam2 = 0;
int setScores1[3] = {0}, setScores2[3] = {0};
int currentSetIdx = 0;
bool advTeam1 = false, advTeam2 = false;
bool inTieBreak = false;
bool tieBreakTriggered = false;
bool matchOver = false;         // Best of 3 : match terminé
bool isDecisivePoint = false;   // Punto de Oro ou 3Ad : point décisif

std::vector<int> currentGameActions;
const char* pointStr[] = {"0", "15", "30", "40"};

// -------- ANIMATIONS / TIMERS --------
bool blinkActive = false;
unsigned long blinkStartTime = 0, lastBlinkToggle = 0;
bool blinkVisible = true;
int blinkTeam = 0, lastP1 = 0, lastP2 = 0;

bool tieBreakAnimationActive = false;
unsigned long tieBreakAnimationStart = 0, lastTieBreakToggle = 0;
bool tieBreakVisible = true;

// Animation fin de match
bool matchOverAnimationActive = false;
unsigned long matchOverAnimationStart = 0, lastMatchOverToggle = 0;
bool matchOverVisible = true;
int matchWinner = 0; // 1 ou 2

// -------- PROTOTYPES --------
void drawScore();
void drawMenu();
void recalcPoints();
void applyBrightness();

// ================= UTILITAIRE LUMINOSITÉ =================
void applyBrightness() {
  int val = map(brightnessPercent, 0, 100, 0, 255);
  dma_display->setBrightness8(val);
}

// ================= LOGIQUE ACTION =================
void handleAction(int act) {
  if (act == 0) return;

  // Si match terminé : un appui sur A (1) ou B (3) reset pour une nouvelle partie
  // Le long press reset (6) fonctionne aussi. Les menus restent accessibles.
  if (matchOver && currentState == STATE_GAME) {
    if (act == 1 || act == 3 || act == 6) {
      Serial.println("[MATCH] Reset -> nouvelle partie");
      matchOver = false;
      matchOverAnimationActive = false;
      matchWinner = 0;
      currentGameActions.clear();
      tieBreakTriggered = false;
      recalcPoints();
      return;
    }
    // Les menus restent accessibles
    else if (act == 4 || act == 5) {
      // Laisser tomber dans le code ci-dessous
    } else {
      return; // Ignore les autres actions pendant match over
    }
  }

  // --- Raccourci : Menu Principal (long press BTN3 = code 4) ---
  if (act == 4) {
    if (currentState == STATE_GAME) {
      currentState = STATE_MENU_GAMES;
      drawMenu();
    } else {
      currentState = STATE_GAME;
      recalcPoints();
    }
    return;
  }

  // --- Raccourci : Menu Luminosité (long press BTN1 = code 5) ---
  if (act == 5) {
    if (currentState == STATE_GAME) {
      currentState = STATE_MENU_BRIGHTNESS;
      drawMenu();
    } else {
      currentState = STATE_GAME;
      recalcPoints();
    }
    return;
  }

  // --- Gestion de la navigation dans les menus ---
  if (currentState != STATE_GAME) {
    if (act == 1) { // Bouton (+)
      if (currentState == STATE_MENU_GAMES)
        configGamesPerSet = (configGamesPerSet >= 9) ? 1 : configGamesPerSet + 1;
      else if (currentState == STATE_MENU_ADVANTAGE)
        configAdvantageMode = (configAdvantageMode + 1) % 3;
      else if (currentState == STATE_MENU_BRIGHTNESS) {
        brightnessPercent = (brightnessPercent >= 100) ? 100 : brightnessPercent + 10;
        applyBrightness();
      }
    }
    else if (act == 3) { // Bouton (-)
      if (currentState == STATE_MENU_GAMES)
        configGamesPerSet = (configGamesPerSet <= 1) ? 9 : configGamesPerSet - 1;
      else if (currentState == STATE_MENU_ADVANTAGE)
        configAdvantageMode = (configAdvantageMode + 2) % 3;
      else if (currentState == STATE_MENU_BRIGHTNESS) {
        brightnessPercent = (brightnessPercent <= 10) ? 10 : brightnessPercent - 10;
        applyBrightness();
      }
    }
    else if (act == 2) { // Bouton (Valider / Suivant)
      if (currentState == STATE_MENU_GAMES)
        currentState = STATE_MENU_ADVANTAGE;
      else {
        currentState = STATE_GAME;
        recalcPoints();
        return;
      }
    }
    drawMenu();
  }
  // --- Gestion du mode Jeu ---
  else {
    switch (act) {
      case 1: currentGameActions.push_back(1); recalcPoints(); break;
      case 3: currentGameActions.push_back(2); recalcPoints(); break;
      case 2:
        if (!currentGameActions.empty()) {
          currentGameActions.pop_back();
          recalcPoints();
        }
        break;
      case 6:
        currentGameActions.clear();
        tieBreakTriggered = false;
        matchOver = false;
        matchOverAnimationActive = false;
        matchWinner = 0;
        recalcPoints();
        break;
    }
  }
}

// ================= CALLBACKS BLE =================
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (advertisedDevice.getAddress().toString() == targetMac.c_str()) {
      if (advertisedDevice.haveManufacturerData()) {
        String mfgData = advertisedDevice.getManufacturerData();

        uint8_t boutonCode = 255;

        // Format correct : [Company_ID_Lo][Company_ID_Hi][code] = 3 bytes
        if (mfgData.length() >= 3) {
          boutonCode = (uint8_t)mfgData[2];
        }
        // Rétrocompatibilité ancien firmware (1 byte sans Company ID)
        else if (mfgData.length() == 1) {
          boutonCode = (uint8_t)mfgData[0];
        }

        if (boutonCode != 255 && boutonCode != dernierEtat) {
          dernierEtat = boutonCode;
          Serial.print("[BLE] Code recu: ");
          Serial.println(boutonCode);
          handleAction(boutonCode);
        }
      }
    }
  }
};

void scanEndedCB(BLEScanResults results) {
  pBLEScan->start(0, scanEndedCB, false);
}

// ================= LOGIQUE TENNIS =================
void recalcPoints() {
  int p1 = 0, p2 = 0, g1 = 0, g2 = 0;
  int s1[3] = {0}, s2[3] = {0}, setIdx = 0;
  bool tieBreak = false;
  int deuceCount = 0;
  bool decisive = false;
  int setsWon1 = 0, setsWon2 = 0;
  bool mOver = false;
  int mWinner = 0;

  for (int act : currentGameActions) {
    // Si match déjà fini, ignorer les actions restantes
    if (mOver) break;

    if (act == 1) p1++; else p2++;

    if (tieBreak) {
      if (p1 >= 7 && p1 - p2 >= 2) {
        g1++;
        s1[setIdx] = g1; s2[setIdx] = g2;
        setsWon1++;
        g1 = g2 = p1 = p2 = 0;
        setIdx++; tieBreak = false;
        // Best of 3 : check victoire
        if (setsWon1 >= 2) { mOver = true; mWinner = 1; }
      } else if (p2 >= 7 && p2 - p1 >= 2) {
        g2++;
        s1[setIdx] = g1; s2[setIdx] = g2;
        setsWon2++;
        g1 = g2 = p1 = p2 = 0;
        setIdx++; tieBreak = false;
        if (setsWon2 >= 2) { mOver = true; mWinner = 2; }
      }
    } else {
      if (p1 >= 3 && p2 >= 3 && p1 == p2) deuceCount++;

      bool gameWon = false;
      // Point décisif : Punto de Oro à 40-40, ou 3Ad au 3ème deuce
      bool gold = (configAdvantageMode == 1 && p1 >= 3 && p2 >= 3) ||
                  (configAdvantageMode == 2 && deuceCount >= 3);

      if (gold) {
        decisive = (p1 >= 3 && p2 >= 3 && p1 == p2);
        if (p1 != p2) gameWon = true;
      } else {
        decisive = false;
        if ((p1 > 3 && p1 >= p2 + 2) || (p2 > 3 && p2 >= p1 + 2)) gameWon = true;
      }

      if (gameWon) {
        if (p1 > p2) g1++; else g2++;
        p1 = p2 = 0; deuceCount = 0; decisive = false;

        if (g1 == configGamesPerSet && g2 == configGamesPerSet) {
          tieBreak = true;
        }
        else if ((g1 >= configGamesPerSet && g1 - g2 >= 2) || g1 == configGamesPerSet + 1) {
          s1[setIdx] = g1; s2[setIdx] = g2;
          setsWon1++;
          g1 = g2 = 0; setIdx++;
          if (setsWon1 >= 2) { mOver = true; mWinner = 1; }
        }
        else if ((g2 >= configGamesPerSet && g2 - g1 >= 2) || g2 == configGamesPerSet + 1) {
          s1[setIdx] = g1; s2[setIdx] = g2;
          setsWon2++;
          g1 = g2 = 0; setIdx++;
          if (setsWon2 >= 2) { mOver = true; mWinner = 2; }
        }
      }
    }
  }

  // Mise à jour des animations de clignotement
  if (currentState == STATE_GAME) {
    if (p1 != lastP1) blinkTeam = 1;
    else if (p2 != lastP2) blinkTeam = 2;
    if (p1 != lastP1 || p2 != lastP2) {
      blinkActive = true;
      blinkStartTime = millis();
      lastBlinkToggle = millis();
      blinkVisible = true;
    }
  }

  lastP1 = p1; lastP2 = p2;
  pointsTeam1_raw = p1; pointsTeam2_raw = p2;
  gamesTeam1 = g1; gamesTeam2 = g2;
  currentSetIdx = (setIdx > 2) ? 2 : setIdx;
  for (int i = 0; i < 3; i++) {
    setScores1[i] = s1[i];
    setScores2[i] = s2[i];
  }

  // Animation tie-break
  if (tieBreak && !tieBreakTriggered && currentState == STATE_GAME) {
    tieBreakTriggered = true;
    tieBreakAnimationActive = true;
    tieBreakAnimationStart = millis();
  } else if (!tieBreak) {
    tieBreakTriggered = false;
  }

  inTieBreak = tieBreak;
  isDecisivePoint = decisive;

  // Avantage standard (mode STD uniquement)
  advTeam1 = (!tieBreak && !decisive && p1 >= 3 && p2 >= 3 && p1 == p2 + 1);
  advTeam2 = (!tieBreak && !decisive && p2 >= 3 && p1 >= 3 && p2 == p1 + 1);

  // Best of 3 : match terminé
  if (mOver && !matchOver) {
    matchOver = true;
    matchWinner = mWinner;
    matchOverAnimationActive = true;
    matchOverAnimationStart = millis();
    lastMatchOverToggle = millis();
    matchOverVisible = true;
    Serial.print("[MATCH] Termine ! Vainqueur : Equipe ");
    Serial.println(matchWinner);
  }

  if (currentState == STATE_GAME) drawScore();
}

// ================= AFFICHAGE =================
void drawBall(int x, int y, uint16_t color) {
  dma_display->fillRect(x, y, 3, 3, color);
}

bool computeServer(bool tieBreak, int p1, int p2, int g1, int g2) {
  int totalGames = g1 + g2;
  bool serverA = (totalGames % 2 == 0);
  if (!tieBreak) return serverA;
  return (((p1 + p2) - 1) / 2) % 2 == 0 ? !serverA : serverA;
}

void drawScore() {
  // Animation fin de match
  if (matchOverAnimationActive) {
    dma_display->clearScreen();
    dma_display->setTextSize(1);
    if (matchOverVisible) {
      uint16_t winColor = (matchWinner == 1) ?
        dma_display->color565(255, 0, 0) : dma_display->color565(0, 255, 0);
      dma_display->setTextColor(winColor);
      dma_display->setCursor(7, 4);
      dma_display->print("VICTOIRE");
      dma_display->setCursor(10, 16);
      dma_display->print("EQUIPE ");
      dma_display->print(matchWinner == 1 ? "A" : "B");
    }
    return;
  }

  // Animation tie-break
  if (tieBreakAnimationActive) {
    dma_display->clearScreen();
    dma_display->setTextSize(1);
    if (tieBreakVisible) {
      dma_display->setTextColor(dma_display->color565(0, 255, 255));
      dma_display->setCursor(5, 12);
      dma_display->print("TIE BREAK");
    }
    return;
  }

  dma_display->clearScreen();
  uint16_t c1 = dma_display->color565(255, 0, 0);
  uint16_t c2 = dma_display->color565(0, 255, 0);
  bool srvA = computeServer(inTieBreak, pointsTeam1_raw, pointsTeam2_raw, gamesTeam1, gamesTeam2);

  // Construction des chaînes de points
  String sP1, sP2;

  if (inTieBreak) {
    sP1 = String(pointsTeam1_raw);
    sP2 = String(pointsTeam2_raw);
  }
  else if (isDecisivePoint) {
    // Point décisif (Punto de Oro ou 3Ad) : afficher Ad-Ad
    sP1 = "Ad";
    sP2 = "Ad";
  }
  else if (advTeam1) {
    sP1 = "Ad";
    sP2 = "40";
  }
  else if (advTeam2) {
    sP1 = "40";
    sP2 = "Ad";
  }
  else if (pointsTeam1_raw >= 3 && pointsTeam2_raw >= 3) {
    // Deuce (40-40 en mode STD)
    sP1 = "40";
    sP2 = "40";
  }
  else {
    sP1 = pointStr[min(pointsTeam1_raw, 3)];
    sP2 = pointStr[min(pointsTeam2_raw, 3)];
  }

  // --- Ligne équipe A ---
  dma_display->setTextSize(1);
  dma_display->setTextColor(c1);
  dma_display->setCursor(1, 4);
  dma_display->print("A");
  for (int i = 0; i <= currentSetIdx; i++) {
    dma_display->setCursor(15 + (i * 9), 4);
    dma_display->print(i == currentSetIdx ? gamesTeam1 : setScores1[i]);
  }
  dma_display->setTextSize(2);
  dma_display->setCursor(40 + (sP1.length() == 1 ? 6 : 0), 1);
  if (!blinkActive || blinkTeam != 1 || blinkVisible) dma_display->print(sP1);
  if (srvA) drawBall(8, 6, c1);

  // --- Ligne équipe B ---
  dma_display->setTextSize(1);
  dma_display->setTextColor(c2);
  dma_display->setCursor(1, 20);
  dma_display->print("B");
  for (int i = 0; i <= currentSetIdx; i++) {
    dma_display->setCursor(15 + (i * 9), 20);
    dma_display->print(i == currentSetIdx ? gamesTeam2 : setScores2[i]);
  }
  dma_display->setTextSize(2);
  dma_display->setCursor(40 + (sP2.length() == 1 ? 6 : 0), 17);
  if (!blinkActive || blinkTeam != 2 || blinkVisible) dma_display->print(sP2);
  if (!srvA) drawBall(8, 22, c2);

  // Si match terminé (après animation), afficher le score final figé
  if (matchOver && !matchOverAnimationActive) {
    // Le score est déjà affiché normalement, c'est OK
  }
}

void drawMenu() {
  dma_display->clearScreen();
  dma_display->setTextSize(1);
  dma_display->setTextColor(dma_display->color565(0, 255, 255));

  dma_display->setCursor(2, 2);
  dma_display->print("MENU");

  if (currentState == STATE_MENU_GAMES) {
    dma_display->setCursor(2, 12);
    dma_display->print("JEUX/SET");
    dma_display->setCursor(0, 22);
    dma_display->print(">");
    dma_display->print(configGamesPerSet);
  }
  else if (currentState == STATE_MENU_ADVANTAGE) {
    dma_display->setCursor(2, 12);
    dma_display->print("REGLE AD");
    dma_display->setCursor(0, 22);
    dma_display->print(">");
    dma_display->print(advModeNames[configAdvantageMode]);
  }
  else if (currentState == STATE_MENU_BRIGHTNESS) {
    dma_display->setCursor(2, 12);
    dma_display->print("LUMINOSITE");
    dma_display->setCursor(0, 22);
    dma_display->print(">");
    dma_display->print(brightnessPercent);
    dma_display->print("%");
  }
}

// ================= SETUP & LOOP =================
void setup() {
  // Config Pins Matrice
  mxconfig.gpio.r1 = 18; mxconfig.gpio.g1 = 25; mxconfig.gpio.b1 = 5;
  mxconfig.gpio.r2 = 22; mxconfig.gpio.g2 = 33; mxconfig.gpio.b2 = 16;
  mxconfig.gpio.a  = 4;  mxconfig.gpio.b  = 3;  mxconfig.gpio.c  = 0;
  mxconfig.gpio.d  = 21; mxconfig.gpio.e  = 32;
  mxconfig.gpio.lat = 19; mxconfig.gpio.oe = 15; mxconfig.gpio.clk = 2;

  Serial.begin(115200);
  Serial.println("Demarrage DIGICOURT v2...");

  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  dma_display->begin();
  applyBrightness();

  // Splash screen
  dma_display->clearScreen();
  dma_display->setTextColor(dma_display->color565(0, 255, 255));
  dma_display->setCursor(5, 12);
  dma_display->print("DIGICOURT");

  // BLE init
  BLEDevice::init("DIGICOURT_RCV");

  // Puissance radio maximale
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_DEFAULT, ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV,     ESP_PWR_LVL_P9);
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_SCAN,    ESP_PWR_LVL_P9);

  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks(), true);
  pBLEScan->setActiveScan(false);  // Passive scan = écoute continue sans SCAN_REQ
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(100);        // 100% duty cycle
  pBLEScan->start(0, scanEndedCB, false);

  Serial.println("BLE scan demarre (passive, 100% duty)");
}

void loop() {
  unsigned long now = millis();

  // Boot delay
  if (currentState == STATE_BOOT && now > 4000) {
    currentState = STATE_GAME;
    recalcPoints();
  }

  // Animation tie-break (3 secondes clignotant)
  if (tieBreakAnimationActive) {
    if (now - tieBreakAnimationStart > 3000) {
      tieBreakAnimationActive = false;
      drawScore();
    } else if (now - lastTieBreakToggle > 300) {
      tieBreakVisible = !tieBreakVisible;
      lastTieBreakToggle = now;
      drawScore();
    }
  }

  // Animation fin de match (5 secondes clignotant, puis score figé)
  if (matchOverAnimationActive) {
    if (now - matchOverAnimationStart > 5000) {
      matchOverAnimationActive = false;
      drawScore(); // Affiche le score final figé
    } else if (now - lastMatchOverToggle > 400) {
      matchOverVisible = !matchOverVisible;
      lastMatchOverToggle = now;
      drawScore();
    }
  }

  // Clignotement des points (3 secondes)
  if (blinkActive && currentState == STATE_GAME && !matchOverAnimationActive) {
    if (now - blinkStartTime > 3000) {
      blinkActive = false;
      blinkVisible = true;
      drawScore();
    } else if (now - lastBlinkToggle > 300) {
      blinkVisible = !blinkVisible;
      lastBlinkToggle = now;
      drawScore();
    }
  }

  delay(10);
}
