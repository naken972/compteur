#include <esp_now.h>
#include <WiFi.h>
#include <vector>

typedef struct struct_message {
  int action; // 1 = point Team1, 2 = point Team2, 3 = annulation dernière action
} struct_message;

struct_message incomingData;

// ------------------- Score de tennis -------------------
int pointsTeam1_raw = 0;
int pointsTeam2_raw = 0;

int gamesTeam1 = 0;
int gamesTeam2 = 0;

int setsTeam1 = 0;
int setsTeam2 = 0;

// Gestion Avantage
bool advTeam1 = false;
bool advTeam2 = false;

// NOUVELLE VARIABLE D'ÉTAT
bool inTieBreak = false;

// Pile du jeu courant pour annuler actions
std::vector<int> currentGameActions;

// Tableau pour conversion points numériques → affichage
const char* pointStr[] = {"0", "15", "30", "40"};

// ------------------- Fonctions -------------------
void clearSerial() {
  for (int i = 0; i < 50; i++) Serial.println();
}

// Ajout d’un point pour une équipe
void scorePoint(int team) {
  currentGameActions.push_back(team);
  recalcPoints();
}

// Annulation de la dernière action
void undoLast() {
  if (!currentGameActions.empty()) {
    currentGameActions.pop_back();
    recalcPoints();
    Serial.println("↩️ Dernière action annulée !");
  } else {
    Serial.println("🚫 Pas d'action à annuler");
  }
}

// Recalcul des points et gestion des jeux/sets (FONCTION CORRIGÉE)
void recalcPoints() {
  // 1. Recalculer les points bruts (TOUJOURS)
  pointsTeam1_raw = 0;
  pointsTeam2_raw = 0;
  advTeam1 = false; // Réinitialiser l'état d'avantage
  advTeam2 = false;

  for (int act : currentGameActions) {
    if (act == 1) pointsTeam1_raw++;
    else if (act == 2) pointsTeam2_raw++;
  }

  // 2. Logique principale : Tie-Break OU Jeu Normal
  if (inTieBreak) {
    // --- LOGIQUE TIE-BREAK ---
    // Les points (pointsTeam1_raw, pointsTeam2_raw) SONT le score du tie-break

    // Vérification de victoire
    if (pointsTeam1_raw >= 7 && pointsTeam1_raw >= pointsTeam2_raw + 2) {
      // Team 1 gagne le tie-break et le set
      setsTeam1++;
      gamesTeam1 = 0; // Reset pour le prochain set
      gamesTeam2 = 0;
      currentGameActions.clear();
      inTieBreak = false; // On sort du mode tie-break
      Serial.println("Team1 gagne le tie-break et le set !");

    } else if (pointsTeam2_raw >= 7 && pointsTeam2_raw >= pointsTeam1_raw + 2) {
      // Team 2 gagne le tie-break et le set
      setsTeam2++;
      gamesTeam1 = 0; // Reset pour le prochain set
      gamesTeam2 = 0;
      currentGameActions.clear();
      inTieBreak = false; // On sort du mode tie-break
      Serial.println("Team2 gagne le tie-break et le set !");
    }
    // Si personne n'a gagné le tie-break, on ne fait rien, l'affichage suivra

  } else {
    // --- LOGIQUE JEU NORMAL (votre code précédent) ---
    bool gameWon = false; // Flag pour savoir si on doit vérifier les sets

    // Cas 1: Deuce/Advantage
    if (pointsTeam1_raw >= 3 && pointsTeam2_raw >= 3) {
      if (pointsTeam1_raw == pointsTeam2_raw) {
        advTeam1 = false; advTeam2 = false;
      } else if (pointsTeam1_raw == pointsTeam2_raw + 1) {
        advTeam1 = true; advTeam2 = false;
      } else if (pointsTeam2_raw == pointsTeam1_raw + 1) {
        advTeam1 = false; advTeam2 = true;
      } else if (pointsTeam1_raw >= pointsTeam2_raw + 2) {
        gamesTeam1++; gameWon = true;
      } else if (pointsTeam2_raw >= pointsTeam1_raw + 2) {
        gamesTeam2++; gameWon = true;
      }
    }
    // Cas 2: Victoire "normale"
    else if (pointsTeam1_raw > 3 || pointsTeam2_raw > 3) {
      if (pointsTeam1_raw > 3) {
        gamesTeam1++; gameWon = true;
      } else {
        gamesTeam2++; gameWon = true;
      }
    }
    // Cas 3: Score normal (rien à faire)

    // Si un jeu a été gagné, on vérifie l'état des sets
    if (gameWon) {
      currentGameActions.clear();

      // --- VÉRIFICATION DES SETS (Déplacé ici) ---
      
      // Cas 1: On arrive à 6-6 -> Activer le Tie-Break
      if (gamesTeam1 == 6 && gamesTeam2 == 6) {
        Serial.println("Tie-break !");
        inTieBreak = true;
        
        // --- CORRECTION ---
        // On force les points bruts à 0 pour le premier affichage du Tie-Break
        pointsTeam1_raw = 0;
        pointsTeam2_raw = 0;
        // --- FIN CORRECTION ---

      }
      // Cas 2: Victoire de set normale (ex: 6-4 ou 7-5)
      else if ((gamesTeam1 >= 6 && gamesTeam1 - gamesTeam2 >= 2) || gamesTeam1 == 7) {
        setsTeam1++;
        gamesTeam1 = 0; gamesTeam2 = 0;
        Serial.println("Team1 gagne le set !");
      }
      // Cas 3: Victoire de set normale (ex: 6-4 ou 7-5)
      else if ((gamesTeam2 >= 6 && gamesTeam2 - gamesTeam1 >= 2) || gamesTeam2 == 7) {
        setsTeam2++;
        gamesTeam1 = 0; gamesTeam2 = 0;
        Serial.println("Team2 gagne le set !");
      }
    }
  } // Fin du else (LOGIQUE JEU NORMAL)

  
  // 3. Logique d'affichage (adaptée aux deux modes)

  // Si un jeu/set/tie-break vient d'être gagné, les actions sont vides.
  // On réinitialise les points bruts pour l'affichage (sauf si on entre en tie-break)
  // (Cette logique est maintenant correcte grâce au fix ci-dessus)
  if (currentGameActions.empty() && !inTieBreak) {
    pointsTeam1_raw = 0;
    pointsTeam2_raw = 0;
  }

  if (inTieBreak) {
    // Affichage Tie-Break (on utilise les points bruts)
    Serial.printf("Sets: %d-%d | Games: 6-6 | Tie-Break: %d-%d\n",
                  setsTeam1, setsTeam2, pointsTeam1_raw, pointsTeam2_raw);
  } else {
    // Affichage Normal (Jeu)
    const char* pt1_str;
    const char* pt2_str;

    if (advTeam1) {
      pt1_str = "Ad";
      pt2_str = "40";
    } else if (advTeam2) {
      pt1_str = "40";
      pt2_str = "Ad";
    } else if (pointsTeam1_raw >= 3 && pointsTeam2_raw >= 3) {
      pt1_str = "40";
      pt2_str = "40";
    } else {
      pt1_str = pointStr[pointsTeam1_raw];
      pt2_str = pointStr[pointsTeam2_raw];
    }

    Serial.printf("Sets: %d-%d | Games: %d-%d | Points: %s-%s\n",
                  setsTeam1, setsTeam2, gamesTeam1, gamesTeam2, pt1_str, pt2_str);
  }
}


// ------------------- Callback ESP-NOW -------------------
void onReceive(const esp_now_recv_info *info, const uint8_t *incomingDataBytes, int len) {
  memcpy(&incomingData, incomingDataBytes, sizeof(incomingData));

  if (incomingData.action == 1) scorePoint(1);
  else if (incomingData.action == 2) scorePoint(2);
  else if (incomingData.action == 3) undoLast();
}

// Simulation via Serial
void simulateMessage(int action) {
  struct_message msg;
  msg.action = action;
  onReceive(nullptr, (uint8_t*)&msg, sizeof(msg));
}

// ------------------- Setup et Loop -------------------
void setup() {
  Serial.begin(115200);
  delay(500);
  clearSerial();

  WiFi.mode(WIFI_STA);
  delay(100);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Erreur d'initialisation ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(onReceive);

  Serial.println("Récepteur Tennis/Padel prêt ✅");
  Serial.print("Adresse MAC : ");
  Serial.println(WiFi.macAddress());
  recalcPoints(); // Affichage initial
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == '1') simulateMessage(1);
    else if (c == '2') simulateMessage(2);
    else if (c == '3') simulateMessage(3);
  }
  delay(100);
}