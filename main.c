#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <wiringPi.h>
#include <pthread.h>
#include <mysql/mysql.h>
//#include "PIM357.h"
//#include "PIM480.h"
#include <microhttpd.h>
#include <string.h>
#include <json-c/json.h>

#define RELAY_PIN 0 // Broche GPIO pour le relais

// Variables globales
pthread_mutex_t data_mutex;
int temp, humidity, eCO2, tVOC;
int manual_mode = 0;
int relay_state = 0;

// Connexion à la base de données
MYSQL *conn;

// Paramètres du tableau Param
int Tmax, Tmin, Hmax, Hmin, eCO2Max, eCO2Min, TvocMax, TvocMin, dVentileMax, dVentileMin, etatVMC;

// Déclarations de fonctions
void *measure_data(void *arg);
void *control_relay(void *arg);
void update_database(int temp, int humidity, int eCO2, int tVOC, int relay_state);
void init_database();
void fetch_parameters();
int handle_http_request(void *cls, struct MHD_Connection *connection, const char *url, const char *method, const char *version, const char *upload_data, size_t *upload_data_size, void **ptr);

int main() {
    // Initialisation de WiringPi
    wiringPiSetup();
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);

    // Initialisation du mutex
    pthread_mutex_init(&data_mutex, NULL);

    // Initialisation de la base de données
    init_database();

    // Récupération des paramètres dans le tableau Param
    fetch_parameters();

    // Créer des fils de discussion
    pthread_t measure_thread, relay_thread;
    pthread_create(&measure_thread, NULL, measure_data, NULL);
    pthread_create(&relay_thread, NULL, control_relay, NULL);

    // Initialisation et démarrage du serveur HTTP
    struct MHD_Daemon *daemon;
    daemon = MHD_start_daemon(MHD_USE_SELECT_INTERNALLY, 8888, NULL, NULL, &handle_http_request, NULL, MHD_OPTION_END);

    if (daemon == NULL) {
        fprintf(stderr, "Failed to start HTTP server\n");
        return 1;
    }

    // Attendre l'intervention de l'utilisateur pour arrêter le serveur
    getchar();

    // Arrêt du serveur HTTP et nettoyage
    MHD_stop_daemon(daemon);

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

       // Veille pendant un certain temps avant de prendre la mesure suivante
        sleep(60);
    }
}

void *control_relay(void *arg) {
    while (1) {
        int turn_on = 0;
                // Vérifiez si le relais doit être activé en fonction des paramètres
        pthread_mutex_lock(&data_mutex);
        if (manual_mode) {
            turn_on = etatVMC;
        } else {
            if (temp > Tmax || temp < Tmin || humidity > Hmax || humidity < Hmin || eCO2 > eCO2Max || eCO2 < eCO2Min || tVOC > TvocMax || tVOC < TvocMin) {
                turn_on = 1;
            } else {
                turn_on = 0;
            }
        }
        pthread_mutex_unlock(&data_mutex);

        // Contrôlez le relais en fonction de la variable turn_on
        if (turn_on && !relay_state) {
            digitalWrite(RELAY_PIN, HIGH);
            relay_state = 1;
        } else if (!turn_on && relay_state) {
            digitalWrite(RELAY_PIN, LOW);
            relay_state = 0;
        }

        // Attendez un moment avant de vérifier à nouveau les conditions
        sleep(10);
    }
}

// Mettre à jour la base de données avec les nouvelles mesures et l'état du relais
void update_database(int temp, int humidity, int eCO2, int tVOC, int relay_state) {
    char query[256];
    sprintf(query, "INSERT INTO Logs (T, H, eCO2, Tvoc, etatVMC) VALUES (%d, %d, %d, %d, %d)", temp, humidity, eCO2, tVOC, relay_state);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "%s\n", mysql_error(conn));
    }
}

// Initialiser la connexion à la base de données
void init_database() {
    const char *server = "localhost";
    const char *user = "your_username";
    const char *password = "your_password";
    const char *database = "ventilation_db";

    conn = mysql_init(NULL);

    if (!mysql_real_connect(conn, server, user, password, database, 0, NULL, 0)) {
        fprintf(stderr, "%s\n", mysql_error(conn));
        exit(1);
    }
}

// Récupérer les paramètres de la table Param
void fetch_parameters() {
    MYSQL_RES *result;
    MYSQL_ROW row;

    if (mysql_query(conn, "SELECT * FROM Param ORDER BY id DESC LIMIT 1")) {
        fprintf(stderr, "%s\n", mysql_error(conn));
        exit(1);
    }

    result = mysql_store_result(conn);

    if ((row = mysql_fetch_row(result)) != NULL) {
        Tmax = atoi(row[1]);
        Tmin = atoi(row[2]);
        Hmax = atoi(row[3]);
        Hmin = atoi(row[4]);
        eCO2Max = atoi(row[5]);
        eCO2Min = atoi(row[6]);
        TvocMax = atoi(row[7]);
        TvocMin = atoi(row[8]);
        dVentileMax = atoi(row[9]);
        dVentileMin = atoi(row[10]);
        etatVMC = atoi(row[11]);
    }

    mysql_free_result(result);
}

// Gérer les requêtes HTTP pour l'API REST
int handle_http_request(void *cls, struct MHD_Connection *connection, const char *url, const char *method, const char *version, const char *upload_data, size_t *upload_data_size, void **ptr) {
    static int dummy;
    if (&dummy != *ptr) {
        *ptr = &dummy;
        return MHD_YES;
    }

    int ret;
    struct MHD_Response *response;

    if (strcmp(url, "/api/sensor_data") == 0) {
        // Gérer la requête GET pour les données des capteurs
        if (strcmp(method, "GET") == 0) {
            pthread_mutex_lock(&data_mutex);

            json_object *jobj = json_object_new_object();
            json_object_object_add(jobj, "temperature", json_object_new_int(temp));
            json_object_object_add(jobj, "humidity", json_object_new_int(humidity));
            json_object_object_add(jobj, "eCO2", json_object_new_int(eCO2));
            json_object_object_add(jobj, "tVOC", json_object_new_int(tVOC));
            json_object_object_add(jobj, "relay_state", json_object_new_int(relay_state));

            const char *response_data = json_object_to_json_string(jobj);
            response = MHD_create_response_from_buffer(strlen(response_data), (void *)response_data, MHD_RESPMEM_MUST_COPY);
            MHD_add_response_header(response, "Content-Type", "application/json");
            ret = MHD_queue_response(connection, MHD_HTTP_OK, response);

            pthread_mutex_unlock(&data_mutex);
            json_object_put(jobj);
            MHD_destroy_response(response);
        } else {
            // Unsupported method
            ret = MHD_queue_response(connection, MHD_HTTP_METHOD_NOT_ALLOWED, NULL);
        }
    } else if (strcmp(url, "/api/update_parameters") == 0) {
        // Gérer la requête POST pour mettre à jour les paramètres
        if (strcmp(method, "POST") == 0) {
            if (*upload_data_size != 0) {
                json_object *jobj = json_object_new_object();
                json_object *parsed_json = json_tokener_parse(upload_data);

                json_object_object_foreach(parsed_json, key, val) {
                    if (json_object_is_type(val, json_type_int)) {
                        json_object_object_add(jobj, key, json_object_new_int(json_object_get_int(val)));
                    }
                }

                const char *response_data = json_object_to_json_string(jobj);
                response = MHD_create_response_from_buffer(strlen(response_data), (void *)response_data, MHD_RESPMEM_MUST_COPY);
                MHD_add_response_header(response, "Content-Type", "application/json");
                ret = MHD_queue_response(connection, MHD_HTTP_OK, response);

                pthread_mutex_lock(&data_mutex);
                update_parameters(jobj);
                pthread_mutex_unlock(&data_mutex);

                *upload_data_size = 0;

                json_object_put(parsed_json);
                json_object_put(jobj);
                MHD_destroy_response(response);
            } else {
                ret = MHD_YES;
            }
        } else {
            // Unsupported method
            ret = MHD_queue_response(connection, MHD_HTTP_METHOD_NOT_ALLOWED, NULL);
        }
    } else {
        // URL non prise en charge
        ret = MHD_queue_response(connection, MHD_HTTP_NOT_FOUND, NULL);
    }

    return ret;
}

void update_parameters(json_object *jobj) {
    json_object *jvalue;
    if (    json_object_object_get_ex(jobj, "Tmax", &jvalue)) {
        Tmax = json_object_get_int(jvalue);
    }
    if (json_object_object_get_ex(jobj, "Tmin", &jvalue)) {
        Tmin = json_object_get_int(jvalue);
    }
    if (json_object_object_get_ex(jobj, "Hmax", &jvalue)) {
        Hmax = json_object_get_int(jvalue);
    }
    if (json_object_object_get_ex(jobj, "Hmin", &jvalue)) {
        Hmin = json_object_get_int(jvalue);
    }
    if (json_object_object_get_ex(jobj, "eCO2Max", &jvalue)) {
        eCO2Max = json_object_get_int(jvalue);
    }
    if (json_object_object_get_ex(jobj, "eCO2Min", &jvalue)) {
        eCO2Min = json_object_get_int(jvalue);
    }
    if (json_object_object_get_ex(jobj, "TvocMax", &jvalue)) {
        TvocMax = json_object_get_int(jvalue);
    }
    if (json_object_object_get_ex(jobj, "TvocMin", &jvalue)) {
        TvocMin = json_object_get_int(jvalue);
    }
    if (json_object_object_get_ex(jobj, "dVentileMax", &jvalue)) {
        dVentileMax = json_object_get_int(jvalue);
    }
    if (json_object_object_get_ex(jobj, "dVentileMin", &jvalue)) {
        dVentileMin = json_object_get_int(jvalue);
    }
    if (json_object_object_get_ex(jobj, "etatVMC", &jvalue)) {
        etatVMC = json_object_get_int(jvalue);
    }

    // Mettre à jour la table Param dans la base de données
    char query[512];
    sprintf(query, "INSERT INTO Param (Tmax, Tmin, Hmax, Hmin, eCO2Max, eCO2Min, TvocMax, TvocMin, dVentileMax, dVentileMin, etatVMC) VALUES (%d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d)", Tmax, Tmin, Hmax, Hmin, eCO2Max, eCO2Min, TvocMax, TvocMin, dVentileMax, dVentileMin, etatVMC);

    if (mysql_query(conn, query)) {
        fprintf(stderr, "%s\n", mysql_error(conn));
    }
}


