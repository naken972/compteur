#include <esp_now.h>
#include <WiFi.h>
#include "esp_sleep.h"

// ---------- Définition des boutons ----------
#define BTN1 2
#define BTN2 3
#define BTN3 4

// ---------- Structure du message ----------
typedef struct struct_message {
    int action; // 1 = Team1, 2 = Team2, 3 = annulation
} struct_message;

struct_message msg;

// ---------- MAC du récepteur ----------
uint8_t receiverMacAddress[] = {0xEC, 0xE3, 0x34, 0x67, 0x56, 0xBC};

// ---------- Callback ESP-NOW CORRIGÉE ----------
// L'argument pour l'adresse MAC doit être de type const wifi_tx_info_t*
void onSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
    Serial.print("Envoi terminé : ");
    if (status == ESP_NOW_SEND_SUCCESS) {
        Serial.println("OK ✅");
    } else {
        Serial.println("Échec ❌");
    }
}

// ---------- Fonction pour envoyer une action ----------
void sendAction(int action) {
    msg.action = action;
    esp_err_t result = esp_now_send(receiverMacAddress, (uint8_t *)&msg, sizeof(msg));
    Serial.print("Action envoyée : ");
    Serial.println(action);
    delay(200); // anti-rebond simple
}

// ---------- Setup ----------
void setup() {
    Serial.begin(115200);
    delay(500);

    // Nécessaire pour ESP-NOW
    WiFi.mode(WIFI_STA);

    if (esp_now_init() != ESP_OK) {
        Serial.println("Erreur ESP-NOW");
        return;
    }

    // L'appel ici est maintenant correcte car onSent a la bonne signature
    esp_now_register_send_cb(onSent);

    // Ajout du destinataire (Peer)
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, receiverMacAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Erreur ajout peer");
        return;
    }

    // Config boutons en INPUT_PULLUP
    pinMode(BTN1, INPUT_PULLUP);
    pinMode(BTN2, INPUT_PULLUP);
    pinMode(BTN3, INPUT_PULLUP);
    
    // ---------- Wake-up GPIO pour ESP32-C3 ----------
    gpio_wakeup_enable((gpio_num_t)BTN1, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable((gpio_num_t)BTN2, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable((gpio_num_t)BTN3, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup(); 
    
    Serial.println("ESP32-C3 prêt 🚀");
}

// ---------- Loop ----------
void loop() {
    int action = 0;

    // Vérifier quel bouton est appuyé (LOW = appuyé)
    if (digitalRead(BTN1) == LOW) {
        action = 1;
    } else if (digitalRead(BTN2) == LOW) {
        action = 2;
    } else if (digitalRead(BTN3) == LOW) {
        action = 3;
    }

    if (action != 0) {
        sendAction(action);
        
        Serial.println("Mise en Deep Sleep... 💤");
        delay(100); 
        
        esp_deep_sleep_start();
    }

    delay(100);
}