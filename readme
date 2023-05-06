Assurez-vous d'installer la bibliothèque wiringPi et les headers pour MySQL avant de compiler ce code. 
Vous pouvez les installer avec les commandes suivantes:
```
sudo apt-get install wiringpi
sudo apt-get install libmysqlclient-dev
sudo apt-get install libmicrohttpd-dev

```

Voici ce que fait le programme main.c :

Il initialise WiringPi et définit la broche du relais.
Il initialise un mutex pour protéger l'accès aux variables partagées.
Il se connecte à une base de données MySQL et récupère les paramètres de ventilation (température, humidité, CO2 et TVOC max/min).
Il crée deux threads : un pour lire les données des capteurs et un autre pour contrôler le relais en fonction des données et des paramètres.
Il démarre un serveur HTTP pour exposer les données des capteurs et permettre la mise à jour des paramètres via des requêtes HTTP.
Il attend qu'un utilisateur appuie sur Entrée pour arrêter le serveur HTTP.
À la fin, il nettoie les ressources et éteint le relais.

Les fonctions principales du programme sont les suivantes :

measure_data : lit les données des capteurs (température, humidité, CO2 et TVOC) et les stocke dans la base de données.
control_relay : contrôle l'état du relais en fonction des données des capteurs et des paramètres de ventilation.
update_database : met à jour la base de données avec les nouvelles mesures et l'état du relais.
init_database : initialise la connexion à la base de données.
fetch_parameters : récupère les paramètres de ventilation de la base de données.
handle_http_request : gère les requêtes HTTP pour l'API REST (récupération des données des capteurs et mise à jour des paramètres de ventilation).
update_parameters : met à jour les paramètres de ventilation et les enregistre dans la base de données.