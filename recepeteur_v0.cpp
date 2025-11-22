#include <esp_now.h>
#include <WiFi.h>
#include <vector>

// ------------------- STRUCTURES -------------------

typedef struct struct_message {
  int action; // 1 = point T1, 2 = point T2, 3 = annulation, 4 = reset global
} struct_message;

struct_message incomingData;

// Historique des jeux et des sets
struct ScoreSnapshot {
    int gamesTeam1;
    int gamesTeam2;
    int setsTeam1;
    int setsTeam2;
};


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

// NOUVEAU : Stocke l'état du score avant le DERNIER jeu ou set gagné.
ScoreSnapshot lastCompletedScore = {0, 0, 0, 0}; 

// Tableau pour conversion points numériques → affichage
const char* pointStr[] = {"0", "15", "30", "40"};


// ------------------- Fonctions -------------------
void clearSerial() {
  for (int i = 0; i < 50; i++) Serial.println();
}

/**
 * @brief Réinitialise tout le score à 0-0 set, jeu, points (Action 4).
 */
void resetAllScores() {
  pointsTeam1_raw = 0;
  pointsTeam2_raw = 0;
  gamesTeam1 = 0;
  gamesTeam2 = 0;
  setsTeam1 = 0;
  setsTeam2 = 0;
  advTeam1 = false;
  advTeam2 = false;
  inTieBreak = false;
  currentGameActions.clear();
  lastCompletedScore = {0, 0, 0, 0};
  
  Serial.println("\n\n🎾 SCORE GLOBAL RÉINITIALISÉ À 0-0 (Action 4) ! 🎾\n\n");
  recalcPoints();
}


// Ajout d’un point pour une équipe
void scorePoint(int team) {
  currentGameActions.push_back(team);
  recalcPoints();
}


// Annulation de la dernière action (Action 3 - Annulation conditionnelle)
void undoLast() {
  
  // Cas 1 : Il y a des points dans le jeu courant (Annulation normale)
  if (!currentGameActions.empty()) {
    currentGameActions.pop_back();
    recalcPoints();
    Serial.println("↩️ Dernière action annulée (point) !");
    return;
  }
  
  // Cas 2 : Score 0-0 dans le jeu courant, on essaie d'annuler la victoire du dernier jeu/set
  else if (gamesTeam1 != lastCompletedScore.gamesTeam1 || gamesTeam2 != lastCompletedScore.gamesTeam2 || 
           setsTeam1 != lastCompletedScore.setsTeam1 || setsTeam2 != lastCompletedScore.setsTeam2) 
  {
      
      // On vérifie si un set a été annulé, ce qui implique de réinitialiser les jeux
      if (setsTeam1 != lastCompletedScore.setsTeam1 || setsTeam2 != lastCompletedScore.setsTeam2) {
          setsTeam1 = lastCompletedScore.setsTeam1;
          setsTeam2 = lastCompletedScore.setsTeam2;
          
          gamesTeam1 = lastCompletedScore.gamesTeam1;
          gamesTeam2 = lastCompletedScore.gamesTeam2;
          
          Serial.println("↩️ Annulation : Retour au score du set précédent.");

      }
      // Annulation d'un jeu
      else {
          // Si on était en Tie-Break (juste après le 6-6)
          if (inTieBreak) {
             inTieBreak = false; // Sortir du mode Tie-Break
             Serial.println("↩️ Annulation : Sortie du mode Tie-Break (score 6-6).");
          }
          
          gamesTeam1 = lastCompletedScore.gamesTeam1;
          gamesTeam2 = lastCompletedScore.gamesTeam2;

          Serial.println("↩️ Annulation : Retour au score du jeu précédent.");
      }

      // Pour l'interface utilisateur, il est plus simple de ramener le jeu annulé à 40-40 (Deuce).
      pointsTeam1_raw = 3;
      pointsTeam2_raw = 3;
      advTeam1 = false;
      advTeam2 = false;
      
      // On remplit le vecteur currentGameActions pour simuler le 40-40 (3 actions T1 + 3 actions T2)
      currentGameActions.clear();
      for (int i=0; i<3; i++) currentGameActions.push_back(1);
      for (int i=0; i<3; i++) currentGameActions.push_back(2);
      
      recalcPoints();
      return;
  }

  // Cas 3 : Aucune action à annuler
  else {
    Serial.println("🚫 Pas d'action à annuler (Score 0-0 partout).");
  }
}


// Recalcul des points et gestion des jeux/sets
void recalcPoints() {
  // 1. Recalculer les points bruts (TOUJOURS)
  pointsTeam1_raw = 0;
  pointsTeam2_raw = 0;
  advTeam1 = false; 
  advTeam2 = false;

  for (int act : currentGameActions) {
    if (act == 1) pointsTeam1_raw++;
    else if (act == 2) pointsTeam2_raw++;
  }

  // 2. Logique principale : Tie-Break OU Jeu Normal
  if (inTieBreak) {
    // --- LOGIQUE TIE-BREAK ---
    if (pointsTeam1_raw >= 7 && pointsTeam1_raw >= pointsTeam2_raw + 2) {
      setsTeam1++;
      gamesTeam1 = 0; 
      gamesTeam2 = 0;
      currentGameActions.clear();
      inTieBreak = false; 
      Serial.println("Team1 gagne le tie-break et le set !");
      lastCompletedScore = {gamesTeam1, gamesTeam2, setsTeam1, setsTeam2}; // Mise à jour après set gagné

    } else if (pointsTeam2_raw >= 7 && pointsTeam2_raw >= pointsTeam1_raw + 2) {
      setsTeam2++;
      gamesTeam1 = 0; 
      gamesTeam2 = 0;
      currentGameActions.clear();
      inTieBreak = false; 
      Serial.println("Team2 gagne le tie-break et le set !");
      lastCompletedScore = {gamesTeam1, gamesTeam2, setsTeam1, setsTeam2}; // Mise à jour après set gagné
    }

  } else {
    // --- LOGIQUE JEU NORMAL ---
    bool gameWon = false; 

    // Cas 1 & 2: Détermination de la victoire du jeu
    if ((pointsTeam1_raw >= 4 && pointsTeam1_raw >= pointsTeam2_raw + 2)) {
      gamesTeam1++; gameWon = true;
    } else if ((pointsTeam2_raw >= 4 && pointsTeam2_raw >= pointsTeam1_raw + 2)) {
      gamesTeam2++; gameWon = true;
    }
    // Cas Deuce/Advantage
    else if (pointsTeam1_raw >= 3 && pointsTeam2_raw >= 3) {
      if (pointsTeam1_raw == pointsTeam2_raw) {
        advTeam1 = false; advTeam2 = false;
      } else if (pointsTeam1_raw == pointsTeam2_raw + 1) {
        advTeam1 = true; advTeam2 = false;
      } else if (pointsTeam2_raw == pointsTeam1_raw + 1) {
        advTeam1 = false; advTeam2 = true;
      }
    }


    // Si un jeu a été gagné, on vérifie l'état des sets
    if (gameWon) {
        
      // >>> NOUVEAUTÉ : ENREGISTRER LE SCORE AVANT LA VICTOIRE DU JEU <<<
      // Le dernier score complet est celui avant l'incrémentation du jeu
      // On sauvegarde l'état avant de vider currentGameActions
      lastCompletedScore = {gamesTeam1 - (gamesTeam1 > gamesTeam2 ? 1 : 0), 
                            gamesTeam2 - (gamesTeam2 > gamesTeam1 ? 1 : 0), 
                            setsTeam1, setsTeam2};
      // Note: Le calcul ci-dessus est complexe car il faut l'état AVANT le jeu gagné.
      // Une approche plus simple est de mettre la sauvegarde avant l'incrémentation.
      
      currentGameActions.clear();

      // --- VÉRIFICATION DES SETS ---
      
      // Cas 1: On arrive à 6-6 -> Activer le Tie-Break
      if (gamesTeam1 == 6 && gamesTeam2 == 6) {
        Serial.println("Tie-break !");
        inTieBreak = true;
        pointsTeam1_raw = 0;
        pointsTeam2_raw = 0;
        lastCompletedScore = {6, 6, setsTeam1, setsTeam2}; // Score avant le TB
      }
      // Cas 2 & 3: Victoire de set normale
      else if ((gamesTeam1 >= 6 && gamesTeam1 - gamesTeam2 >= 2) || gamesTeam1 == 7) {
        setsTeam1++;
        gamesTeam1 = 0; gamesTeam2 = 0;
        Serial.println("Team1 gagne le set !");
        lastCompletedScore = {0, 0, setsTeam1, setsTeam2}; // Mise à jour après set gagné
      }
      else if ((gamesTeam2 >= 6 && gamesTeam2 - gamesTeam1 >= 2) || gamesTeam2 == 7) {
        setsTeam2++;
        gamesTeam1 = 0; gamesTeam2 = 0;
        Serial.println("Team2 gagne le set !");
        lastCompletedScore = {0, 0, setsTeam1, setsTeam2}; // Mise à jour après set gagné
      }
    }
  } 

  
  // 3. Logique d'affichage 
  if (currentGameActions.empty() && !inTieBreak) {
    pointsTeam1_raw = 0;
    pointsTeam2_raw = 0;
  }

  if (inTieBreak) {
    // Affichage Tie-Break
    Serial.printf("Sets: %d-%d | Games: 6-6 | Tie-Break: %d-%d\n",
                  setsTeam1, setsTeam2, pointsTeam1_raw, pointsTeam2_raw);
  } else {
    // Affichage Normal (Jeu)
    const char* pt1_str;
    const char* pt2_str;

    // Logique d'affichage des points (Deuce/Advantage/Normal)
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

  if (incomingData.action == 1) {
    scorePoint(1);
  }
  else if (incomingData.action == 2) {
    scorePoint(2);
  }
  else if (incomingData.action == 3) {
    // Action 3: Annulation simple/conditionnelle
    undoLast();
  }
  else if (incomingData.action == 4) {
    // Action 4: Reset Global (Envoyée par l'émetteur après 3s d'appui long)
    resetAllScores();
  }
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
  // Pas de logique de temporisation ici, c'est l'émetteur qui envoie l'action 4
  delay(100);
}