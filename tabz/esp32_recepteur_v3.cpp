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
String targetMac = "e3:b7:5e:9d:d8:bf";
uint8_t dernierCounter = 0;    // Dernier compteur reçu (détection par changement)
BLEScan* pBLEScan;

// -------- ÉTATS DU SYSTÈME --------
enum SystemState {
  STATE_BOOT,
  STATE_GAME,
  STATE_MENU_GAMES,
  STATE_MENU_ADVANTAGE,
  STATE_MENU_MODE,        // <-- NOUVEAU : mode de match (sets / tie-break / super tie)
  STATE_MENU_BRIGHTNESS
};
SystemState currentState = STATE_BOOT;

// -------- CONFIGURATION JEU & AFFICHAGE --------
int configGamesPerSet = 6;
int configAdvantageMode = 0; // 0: Ad (STD), 1: Punto de Oro, 2: Star Point
const char* advModeNames[] = {"STD", "Punto Oro", "STAR PT"};  // "3AD" -> "STAR PT"

// Mode de match : 0 = sets normaux, 1 = tie-break (en 7), 2 = super tie (en 10)
int configMatchMode = 0;
const char* matchModeNames[] = {"SETS", "TIE 7", "SUPER10"};

// Luminosité : tranches de 10%
int brightnessPercent = 60;

// -------- SCORE DATA --------
int pointsTeam1_raw = 0, pointsTeam2_raw = 0;
int gamesTeam1 = 0, gamesTeam2 = 0;
int setScores1[3] = {0}, setScores2[3] = {0};
int currentSetIdx = 0;
bool advTeam1 = false, advTeam2 = false;
bool inTieBreak = false;
bool tieBreakTriggered = false;
bool matchOver = false;
bool isDecisivePoint = false;
bool pureTieBreakMatch = false;   // true en mode TIE 7 / SUPER10
int gamesPlayedTotal = 0;         // jeux complets joués dans TOUT le match (TB compris)

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

int matchWinner = 0;

// -------- FILE D'ACTIONS BLE (pour éviter les pertes) --------
#define ACTION_QUEUE_SIZE 16
volatile uint8_t actionQueue[ACTION_QUEUE_SIZE];
volatile int actionQueueHead = 0;
volatile int actionQueueTail = 0;

void enqueueAction(uint8_t code) {
  int nextHead = (actionQueueHead + 1) % ACTION_QUEUE_SIZE;
  if (nextHead != actionQueueTail) {  // Queue pas pleine
    actionQueue[actionQueueHead] = code;
    actionQueueHead = nextHead;
  }
}

bool dequeueAction(uint8_t &code) {
  if (actionQueueTail == actionQueueHead) return false;
  code = actionQueue[actionQueueTail];
  actionQueueTail = (actionQueueTail + 1) % ACTION_QUEUE_SIZE;
  return true;
}

// -------- PROTOTYPES --------
void drawScore();
void drawMenu();
void recalcPoints();
void applyBrightness();
void handleAction(int act);

// ================= UTILITAIRE LUMINOSITÉ =================
void applyBrightness() {
  int val = map(brightnessPercent, 0, 100, 0, 255);
  dma_display->setBrightness8(val);
}

// ================= LOGIQUE ACTION =================
void handleAction(int act) {
  if (act == 0) return;

  // Si match terminé : un appui sur A (1) ou B (3) reset pour une nouvelle partie
  if (matchOver && currentState == STATE_GAME) {
    if (act == 1 || act == 3 || act == 6) {
      Serial.println("[MATCH] Reset -> nouvelle partie");
      matchOver = false;
      matchWinner = 0;
      currentGameActions.clear();
      tieBreakTriggered = false;
      recalcPoints();
      return;
    }
    else if (act == 4 || act == 5) {
      // Laisser tomber dans le code ci-dessous
    } else {
      return;
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
    if (act == 1) {
      if (currentState == STATE_MENU_GAMES)
        configGamesPerSet = (configGamesPerSet >= 9) ? 1 : configGamesPerSet + 1;
      else if (currentState == STATE_MENU_ADVANTAGE)
        configAdvantageMode = (configAdvantageMode + 1) % 3;
      else if (currentState == STATE_MENU_MODE) {
        configMatchMode = (configMatchMode + 1) % 3;
        // Changer le format de match => on repart d'un score vierge
        currentGameActions.clear();
        matchOver = false; matchWinner = 0; tieBreakTriggered = false;
      }
      else if (currentState == STATE_MENU_BRIGHTNESS) {
        brightnessPercent = (brightnessPercent >= 100) ? 100 : brightnessPercent + 10;
        applyBrightness();
      }
    }
    else if (act == 3) {
      if (currentState == STATE_MENU_GAMES)
        configGamesPerSet = (configGamesPerSet <= 1) ? 9 : configGamesPerSet - 1;
      else if (currentState == STATE_MENU_ADVANTAGE)
        configAdvantageMode = (configAdvantageMode + 2) % 3;
      else if (currentState == STATE_MENU_MODE) {
        configMatchMode = (configMatchMode + 2) % 3;
        currentGameActions.clear();
        matchOver = false; matchWinner = 0; tieBreakTriggered = false;
      }
      else if (currentState == STATE_MENU_BRIGHTNESS) {
        brightnessPercent = (brightnessPercent <= 10) ? 10 : brightnessPercent - 10;
        applyBrightness();
      }
    }
    else if (act == 2) {
      // Enchaînement des menus : JEUX -> REGLE AD -> MODE -> retour jeu
      if (currentState == STATE_MENU_GAMES)
        currentState = STATE_MENU_ADVANTAGE;
      else if (currentState == STATE_MENU_ADVANTAGE)
        currentState = STATE_MENU_MODE;
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

        uint8_t boutonCode = 0;
        uint8_t counter = 0;

        // Nouveau format : [Company_Lo][Company_Hi][code][counter] = 4 bytes
        if (mfgData.length() >= 4) {
          boutonCode = (uint8_t)mfgData[2];
          counter = (uint8_t)mfgData[3];
        }
        // Ancien format 3 bytes (rétrocompat sans compteur)
        else if (mfgData.length() >= 3) {
          boutonCode = (uint8_t)mfgData[2];
          // Pas de compteur : fallback ancien comportement
          static uint8_t oldState = 255;
          if (boutonCode != 255 && boutonCode != oldState) {
            oldState = boutonCode;
            if (boutonCode != 0) {
              enqueueAction(boutonCode);
              Serial.print("[BLE] Code recu (legacy): ");
              Serial.println(boutonCode);
            }
          }
          return;
        }
        // Très ancien format 1 byte
        else if (mfgData.length() == 1) {
          boutonCode = (uint8_t)mfgData[0];
          static uint8_t veryOldState = 255;
          if (boutonCode != 255 && boutonCode != veryOldState) {
            veryOldState = boutonCode;
            if (boutonCode != 0) {
              enqueueAction(boutonCode);
            }
          }
          return;
        }
        else {
          return;
        }

        // Nouveau format avec compteur : chaque changement de compteur = nouvelle action
        if (counter != dernierCounter && counter != 0) {
          dernierCounter = counter;
          if (boutonCode != 0) {
            enqueueAction(boutonCode);
            Serial.print("[BLE] Code=");
            Serial.print(boutonCode);
            Serial.print(" cnt=");
            Serial.println(counter);
          }
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
  int totalGames = 0;   // jeux complets joués (TB compris) -> sert au calcul du serveur

  // =========================================================
  // MODE TIE-BREAK UNIQUE (TIE 7 / SUPER10) : tout le match = 1 seul tie-break
  // =========================================================
  if (configMatchMode != 0) {
    int target = (configMatchMode == 1) ? 7 : 10;
    tieBreak = true;                       // on est toujours en "tie-break"
    for (int act : currentGameActions) {
      if (mOver) break;
      if (act == 1) p1++; else p2++;
      if (p1 >= target && p1 - p2 >= 2)      { mOver = true; mWinner = 1; }
      else if (p2 >= target && p2 - p1 >= 2) { mOver = true; mWinner = 2; }
    }
    // g1/g2/sets restent à 0, totalGames = 0 -> 1er service = équipe A
  }
  // =========================================================
  // MODE SETS NORMAUX
  // =========================================================
  else {
    for (int act : currentGameActions) {
      if (mOver) break;

      if (act == 1) p1++; else p2++;

      if (tieBreak) {
        if (p1 >= 7 && p1 - p2 >= 2) {
          g1++;
          s1[setIdx] = g1; s2[setIdx] = g2;
          setsWon1++;
          totalGames++;                       // le tie-break compte comme 1 jeu
          g1 = g2 = p1 = p2 = 0;
          setIdx++; tieBreak = false;
          if (setsWon1 >= 2) { mOver = true; mWinner = 1; }
        } else if (p2 >= 7 && p2 - p1 >= 2) {
          g2++;
          s1[setIdx] = g1; s2[setIdx] = g2;
          setsWon2++;
          totalGames++;
          g1 = g2 = p1 = p2 = 0;
          setIdx++; tieBreak = false;
          if (setsWon2 >= 2) { mOver = true; mWinner = 2; }
        }
      } else {
        if (p1 >= 3 && p2 >= 3 && p1 == p2) deuceCount++;

        bool gameWon = false;
        bool gold = (configAdvantageMode == 1 && p1 >= 3 && p2 >= 3) ||
                    (configAdvantageMode == 2 && deuceCount >= 3);   // Star Point

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
          totalGames++;                       // 1 jeu joué de plus

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
  gamesPlayedTotal = totalGames;
  pureTieBreakMatch = (configMatchMode != 0);
  currentSetIdx = (setIdx > 2) ? 2 : setIdx;
  for (int i = 0; i < 3; i++) {
    setScores1[i] = s1[i];
    setScores2[i] = s2[i];
  }

  // Animation tie-break (seulement en mode sets normaux, à 6-6)
  if (tieBreak && !tieBreakTriggered && currentState == STATE_GAME && configMatchMode == 0) {
    tieBreakTriggered = true;
    tieBreakAnimationActive = true;
    tieBreakAnimationStart = millis();
  } else if (!tieBreak) {
    tieBreakTriggered = false;
  }

  inTieBreak = tieBreak;
  isDecisivePoint = decisive;

  advTeam1 = (!tieBreak && !decisive && p1 >= 3 && p2 >= 3 && p1 == p2 + 1);
  advTeam2 = (!tieBreak && !decisive && p2 >= 3 && p1 >= 3 && p2 == p1 + 1);

  if (mOver && !matchOver) {
    matchOver = true;
    matchWinner = mWinner;
    Serial.print("[MATCH] Termine ! Vainqueur : Equipe ");
    Serial.println(matchWinner);
  }

  if (currentState == STATE_GAME) drawScore();
}

// ================= AFFICHAGE =================
void drawBall(int x, int y, uint16_t color) {
  dma_display->fillRect(x, y, 3, 3, color);
}

// Calcul du serveur basé sur le TOTAL de jeux du match (TB = 1 jeu).
// => le service reste correct au début d'un nouveau set.
// On suppose que l'équipe A sert le 1er jeu du match.
bool computeServer(bool tieBreak, int p1, int p2, int totalGames) {
  bool firstServerA = (totalGames % 2 == 0);   // qui sert le jeu en cours
  if (!tieBreak) return firstServerA;

  // Tie-break : 1er point par "firstServerA", puis alternance toutes les 2 balles
  int k = p1 + p2 + 1;                          // numéro du point en cours (1-indexé)
  bool otherTurn = ((k / 2) % 2) == 1;
  return otherTurn ? !firstServerA : firstServerA;
}

void drawScore() {
  // Animation tie-break
  if (tieBreakAnimationActive) {
    dma_display->clearScreen();
    dma_display->setTextSize(1);
    if (tieBreakVisible) {
      dma_display->setTextColor(dma_display->color565(255, 255, 0));
      dma_display->setCursor(5, 12);
      dma_display->print("TIE BREAK");
    }
    return;
  }

  // --- ÉCRAN VICTOIRE (cyan, comme le logo DIGICOURT) ---
  if (matchOver) {
    dma_display->clearScreen();
    dma_display->setTextSize(1);
    uint16_t cyan = dma_display->color565(0, 255, 255);
    dma_display->setTextColor(cyan);
    dma_display->setCursor(8, 8);          // "VICTOIRE" centré (8 car. x 6px)
    dma_display->print("VICTOIRE");
    dma_display->setCursor(8, 18);         // "EQUIPE A" / "EQUIPE B"
    dma_display->print(matchWinner == 1 ? "EQUIPE A" : "EQUIPE B");
    return;
  }

  dma_display->clearScreen();
  uint16_t c1 = dma_display->color565(255, 0, 0);
  uint16_t c2 = dma_display->color565(0, 255, 0);
  bool srvA = computeServer(inTieBreak, pointsTeam1_raw, pointsTeam2_raw, gamesPlayedTotal);

  String sP1, sP2;

  if (inTieBreak) {
    sP1 = String(pointsTeam1_raw);
    sP2 = String(pointsTeam2_raw);
  }
  else if (isDecisivePoint) {
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
  if (!pureTieBreakMatch) {                  // pas de colonnes jeux/sets en mode tie-break pur
    for (int i = 0; i <= currentSetIdx; i++) {
      dma_display->setCursor(15 + (i * 9), 4);
      dma_display->print(i == currentSetIdx ? gamesTeam1 : setScores1[i]);
    }
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
  if (!pureTieBreakMatch) {
    for (int i = 0; i <= currentSetIdx; i++) {
      dma_display->setCursor(15 + (i * 9), 20);
      dma_display->print(i == currentSetIdx ? gamesTeam2 : setScores2[i]);
    }
  }
  dma_display->setTextSize(2);
  dma_display->setCursor(40 + (sP2.length() == 1 ? 6 : 0), 17);
  if (!blinkActive || blinkTeam != 2 || blinkVisible) dma_display->print(sP2);
  if (!srvA) drawBall(8, 22, c2);
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
  else if (currentState == STATE_MENU_MODE) {
    dma_display->setCursor(2, 12);
    dma_display->print("MODE");
    dma_display->setCursor(0, 22);
    dma_display->print(">");
    dma_display->print(matchModeNames[configMatchMode]);
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
  pBLEScan->setActiveScan(false);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(100);
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

  // Traiter les actions en file d'attente (depuis le callback BLE)
  uint8_t action;
  while (dequeueAction(action)) {
    handleAction(action);
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

  // Clignotement des points (3 secondes)
  if (blinkActive && currentState == STATE_GAME) {
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
