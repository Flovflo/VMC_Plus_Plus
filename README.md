# Programmation du contrôleur VMC

Le code est un programme en langage C qui contrôle une ventilation mécanique contrôlée (VMC) via un Raspberry Pi. Il mesure la température et l'humidité à l'aide d'un capteur DHT11, contrôle un relais GPIO pour allumer ou éteindre la VMC en fonction des conditions et communique avec un client WebSocket pour permettre le contrôle à distance et l'affichage des mesures.

## Dépendances

Avant de compiler et exécuter le programme, assurez-vous d'avoir installé les dépendances suivantes :

- `wiringPi`
- `libsqlite3`
- `libwebsockets`

## Compilation

Pour compiler le programme, exécutez la commande suivante dans le répertoire du projet : make

