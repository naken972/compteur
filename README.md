deepsleep_d0_d1_d2.txt : test de deepsleep sur les pins d0,d1,d2 de esp32c3, partiellemet fonctionnel mais encore buggué
emeteur_v0.cpp : première version fontionnelle de l'emteur avec esp-now, mais sans deep sleep
emeteur_v1.cpp : version de l'emeteur pour intégrer deep sleep mais ne marche pas, pb avec deep sleep et les pins de l'esp32, comme si ce n'était pas supposrté sur toutes les pins
recepteur_v0.cpp et recepteur_v1.cpp: deux versions fonctionnelles dont je ne sais plus quelles sont les diffs mais a revoir

test.cpp: test simple des boutons sur la carte 
C'est une excellente idée de documenter cela proprement. Pour GitHub, le format Markdown permet d'avoir un rendu pro, lisible et facilement modifiable. J'ai structuré le rapport avec des sections claires, une gestion des coûts détaillée et une analyse technique pour sécuriser ton montage.

Voici le code Markdown que tu peux copier-coller dans un fichier `README.md` :

---

# 🚀 Projet : Système d'Affichage LED Nomade (ESP32)

Ce dépôt contient la liste des composants, le budget et les spécifications techniques pour un système de transmission sans fil contrôlant un panneau LED matriciel.

## 📊 Récapitulatif Budgétaire

| Section | Composant | Prix | Note |
| --- | --- | --- | --- |
| **Récepteur** | Shield ESP32-S3 HUB75 | 11,67 € | Interface simplifiée pour LED Matrix |
|  | Panneau LED RGB Matrix | 10,19 € | Affichage principal |
|  | ESP32-S3 DevKit | 4,29 € | Microcontrôleur récepteur |
|  | Support Batterie 18650 | 0,89 € | Logement pile |
|  | Batterie 18650 (x3) | 8.52 € | (Basé sur pack de 10 à 28,39€) |
|  | Module de charge TP4056 | 0,69 € | Gestion charge USB |
|  | Convertisseur Boost MT3608 | 0,59 € | Élévateur de tension (3.7V vers 5V) |
| **Émetteur** | ESP32-C3 SuperMini | 5,69 € | Ultra-compact |
|  | Batterie LiPo 60mAh | 4,99 € | Alimentation émetteur |
| **Logistique** | Câblage (Silicone/AWG) | 5,76 € | Connexions |
|  | Consommables (PLA, Étain, Boutons) frais de port | 10,00 € | Estimation châssis et soudure |
| **TOTAL** |  | **63,28 €** |  |

---

## 🛠️ Architecture Technique

### Partie Récepteur (Affichage)

Le récepteur est conçu pour être autonome. Le Shield Seengreat permet d'éviter un "nid de câbles" entre l'ESP32 et le panneau LED.

* **Gestion de l'énergie :** L'utilisation du **Boost de tension** est **indispensable**. La batterie 18650 fournit une tension nominale de , alors que le panneau LED et le Shield nécessitent  pour fonctionner sans instabilité chromatique.
* **Autonomie :** Avec une batterie 18650 standard (env. 2500mAh), l'autonomie dépendra de la luminosité des LED.

### Partie Émetteur (Contrôle)

L'émetteur utilise ~un ESP32-C3~  une carte nrf52840 (copie Aliexpress d'une Xiao seeed studio) pour sa taille réduite, idéal pour une intégration dans une petite manette ou un boîtier compact.

On utilise le protocol de communication BLE entre l'émetteur et le recepteur, ainsi que du deep sleep pour que l'emetteur ne soit allumé que pour l'envoie de la donnée, et éteint sinon.

