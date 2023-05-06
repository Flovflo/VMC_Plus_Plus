#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <wiringPi.h>
#include <pthread.h>
#include <mysql/mysql.h>
#include "PIM357.h"
#include "PIM480.h"

#define RELAY_PIN 0 // GPIO Pin for the relay

// Variables globales
pthread_mutex_t data_mutex;
int temp, humidity, eCO2, tVOC;
int manual_mode = 0;
int relay_state = 0;

// Connexion à la base de données
MYSQL *conn;

// Déclarations de fonctions
void *measure_data(void *arg);
void *control_relay(void *arg);
void update_database(int temp, int humidity, int eCO2, int tVOC, int relay_state);
void init_database();

int main() {
    // Initialisation de WiringPi
    wiringPiSetup();
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);

    // Initialisation du mutex
    pthread_mutex_init(&data_mutex, NULL);

   // Initialisation de la base de données
    init_database();

    // Créer des threads
    pthread_t measure_thread, relay_thread;
    pthread_create(&measure_thread, NULL, measure_data, NULL);
    pthread_create(&relay_thread, NULL, control_relay, NULL);

   // Attendre que les threads se terminent
    pthread_join(measure_thread, NULL);
    pthread_join(relay_thread, NULL);

    // Clean
    mysql_close(conn);
    pthread_mutex_destroy(&data_mutex);
    digitalWrite(RELAY_PIN, LOW);

    return 0;
}

void *measure_data(void *arg) {
    PIM357 sensor357;
    PIM480 sensor480;

    while (1) {
        // Lire les données des capteurs
        temp = sensor357.readTemperature();
        humidity = sensor357.readHumidity();
        eCO2 = sensor480.readCO2();
        tVOC = sensor480.readTVOC();

        // Verrouiller le mutex et mettre à jour les variables globales
        pthread_mutex_lock(&data_mutex);
        update_database(temp, humidity, eCO2, tVOC, relay_state);
        pthread_mutex_unlock(&data_mutex);

        
        sleep(60);
    }
}

void *control_relay(void *arg) {
    while (1) {
        int turn_on = 0;

        // Vérifier si le relais doit être activé
        if (manual_mode || (humidity > 60 && eCO2 > 1000)) {
            turn_on = 1;
        }

        // Verrouiller le mutex et mettre à jour l'état du relais
        pthread_mutex_lock(&data_mutex);
        if (turn_on && !relay_state) {
            digitalWrite(RELAY_PIN, HIGH);
            relay_state = 1;
        } else if (!turn_on && relay_state) {
            digitalWrite(RELAY_PIN, LOW);
            relay_state = 0;
        }
        pthread_mutex_unlock(&data_mutex);

        // Veille pendant un certain temps avant de procéder à une nouvelle vérification
        sleep(10);
    }
}

void update_database(int temp, int humidity, int eCO2, int tVOC, int relay_state) {
    // Préparer et exécuter une requête SQL pour mettre à jour la base de données
    char query[256];
    sprintf(query, "INSERT INTO Logs (T, H, eCO2, Tvoc, etatVMC) VALUES (%d, %d, %d, %d, %d)", temp, humidity, eCO2, tVOC, relay_state);
    if (mysql_query(conn, query)) {
        fprintf(stderr, "%s\n", mysql_error(conn));
    }
}

void init_database() {
    const char *server = "localhost";
    const char *user = "Flo";
    const char *password = "testbDD";
    const char *database = "vmc";
    conn = mysql_init(NULL);

    if (!mysql_real_connect(conn, server, user, password, database, 0, NULL, 0)) {
        fprintf(stderr, "%s\n", mysql_error(conn));
        exit(1);
    }
}