#include <esp_now.h>
#include <WiFi.h>

// ------------------- CONFIGURATION -------------------

// Adresse MAC du récepteur
uint8_t receiverMacAddress[] = {0xEC, 0xE3, 0x34, 0x67, 0x56, 0xBC}; 

// Broches des boutons (Xiao ESP32-C3)
const int BTN_TEAM1_PIN = 2; // D0
const int BTN_TEAM2_PIN = 3; // D1
const int BTN_UNDO_PIN  = 4; // D2

// ------------------- STRUCTURE -------------------
typedef struct struct_message {
    int action; // 1 = Team1, 2 = Team2, 3 = Annulation (court), 4 = Reset (long)
} struct_message;

struct_message myData;

// ------------------- GESTION DEBOUNCE & APPUI LONG -------------------
unsigned long debounceDelay = 50;

unsigned long lastDebounceTimeBtn1 = 0;
unsigned long lastDebounceTimeBtn2 = 0;
unsigned long lastDebounceTimeBtn3 = 0;

int lastBtn1State = HIGH;
int lastBtn2State = HIGH;
int lastBtn3State = HIGH;

int lastBtn1Reading = HIGH;
int lastBtn2Reading = HIGH;
int lastBtn3Reading = HIGH;

// Appui long
const unsigned long REQUIRED_LONG_PRESS_DURATION = 3000; // 3 secondes
unsigned long undoButtonPressTime = 0; 
bool undoButtonSentLongPress = false; 

// ------------------- CALLBACK ESP-NOW -------------------
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
    Serial.print("Statut envoi: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Livraison OK ✅" : "Livraison échouée ❌");
}

// ------------------- FONCTION D'ENVOI -------------------
void sendAction(int action) {
    myData.action = action;
    esp_err_t result = esp_now_send(receiverMacAddress, (uint8_t*)&myData, sizeof(myData));
    Serial.printf("Action %d envoyée %s.\n", action, result == ESP_OK ? "avec succès" : "ERREUR");
}

// ------------------- SETUP -------------------
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("Émetteur Tennis/Padel prêt ✅");

    pinMode(BTN_TEAM1_PIN, INPUT_PULLUP);
    pinMode(BTN_TEAM2_PIN, INPUT_PULLUP);
    pinMode(BTN_UNDO_PIN, INPUT_PULLUP);

    WiFi.mode(WIFI_STA);
    delay(100);

    if (esp_now_init() != ESP_OK) {
        Serial.println("Erreur d'initialisation ESP-NOW ❌");
        return;
    }

    esp_now_register_send_cb(OnDataSent);

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, receiverMacAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Erreur ajout du peer ❌");
        return;
    }

    Serial.print("Adresse MAC de cet émetteur : ");
    Serial.println(WiFi.macAddress());
    Serial.println("Prêt à envoyer des actions...");
}

// ------------------- LOOP -------------------
void loop() {
    unsigned long currentTime = millis();

    // --- Bouton 1 (Team 1) ---
    int reading1 = digitalRead(BTN_TEAM1_PIN);
    if (reading1 != lastBtn1Reading) lastDebounceTimeBtn1 = currentTime;
    if ((currentTime - lastDebounceTimeBtn1) > debounceDelay && reading1 != lastBtn1State) {
        lastBtn1State = reading1;
        if (lastBtn1State == LOW) sendAction(1);
    }
    lastBtn1Reading = reading1;

    // --- Bouton 2 (Team 2) ---
    int reading2 = digitalRead(BTN_TEAM2_PIN);
    if (reading2 != lastBtn2Reading) lastDebounceTimeBtn2 = currentTime;
    if ((currentTime - lastDebounceTimeBtn2) > debounceDelay && reading2 != lastBtn2State) {
        lastBtn2State = reading2;
        if (lastBtn2State == LOW) sendAction(2);
    }
    lastBtn2Reading = reading2;

    // --- Bouton 3 (Annuler/Reset) ---
    int reading3 = digitalRead(BTN_UNDO_PIN);
    if (reading3 != lastBtn3Reading) lastDebounceTimeBtn3 = currentTime;

    if ((currentTime - lastDebounceTimeBtn3) > debounceDelay) {

        // Front descendant
        if (reading3 == LOW && lastBtn3State == HIGH) {
            undoButtonPressTime = currentTime;
            undoButtonSentLongPress = false;
        }

        // Vérification appui long
        if (reading3 == LOW && !undoButtonSentLongPress && (currentTime - undoButtonPressTime >= REQUIRED_LONG_PRESS_DURATION)) {
            sendAction(4); // RESET
            undoButtonSentLongPress = true;
            Serial.println("Bouton 3 maintenu 3s : RESET envoyé !");
        }

        // Front montant
        if (reading3 == HIGH && lastBtn3State == LOW) {
            if (!undoButtonSentLongPress) {
                sendAction(3); // ANNULATION
                Serial.println("Bouton 3 relâché : Annulation envoyée !");
            }
        }

        lastBtn3State = reading3;
    }
    lastBtn3Reading = reading3;

    delay(10);
}
