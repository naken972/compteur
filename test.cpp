// Broche de test pour le bouton
const int BTN_PIN = 18; // D18 sur la Wemos

// Variable pour mémoriser l'état précédent
int lastBtnState = HIGH; 

void setup() {
  Serial.begin(115200);
  delay(1000); // Laisse le temps au moniteur série de se connecter
  Serial.println("--- Début du Test Bouton Simple ---");

  // Configure la broche 18 avec la résistance PULLUP interne
  // L'état par défaut sera HIGH (3.3V)
  pinMode(BTN_PIN, INPUT_PULLUP);

  Serial.println("Broche 18 (D18) configurée en INPUT_PULLUP.");
  Serial.print("État actuel (avant appui) : ");
  Serial.println(digitalRead(BTN_PIN) == HIGH ? "HIGH (Relâché)" : "LOW (Erreur?)");
  Serial.println("\nAppuyez sur le bouton connecté entre D18 et GND...");
}

void loop() {
  // Lit l'état actuel de la broche
  int currentBtnState = digitalRead(BTN_PIN);

  // Vérifie si l'état a changé
  if (currentBtnState != lastBtnState) {
    
    if (currentBtnState == LOW) {
      Serial.println("-> Bouton PRESSÉ (état LOW)");
    } else {
      Serial.println("-> Bouton RELÂCHÉ (état HIGH)");
    }
    
    // Mémorise le nouvel état
    lastBtnState = currentBtnState;
  }

  // Petite pause pour éviter de surcharger le moniteur
  delay(50); 
}