#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <pthread.h>
#include <wiringPi.h>
#include <unistd.h>
#include <mysql.h>

// Compilation : gcc -o main3 main3.c -lcurl -lpthread -lwiringPi -lcjson -lmysqlclient -I/usr/include/mysql

#define RelayPin 0  // Correspond à GPIO17 si vous utilisez WiringPi

struct MemoryStruct {
  char *memory;
  size_t size;
};

pthread_mutex_t data_mutex; // Mutex pour protéger les variables de température, d'humidité et d'état VMC
double temperature, humidity; // Variables pour stocker les mesures de température et d'humidité
int etatVMC; // Variable pour stocker l'état de la VMC (Ventilation Mécanique Contrôlée)

MYSQL *con; // Connection à la base de données MySQL

// Gérer les erreurs avec MySQL
void finish_with_error(MYSQL *con)
{
  fprintf(stderr, "%s\n", mysql_error(con));
  mysql_close(con);
  exit(1);        
}

// Initialiser la connexion à la base de données
void init_db() {
  con = mysql_init(NULL);
  
  if (con == NULL) {
      fprintf(stderr, "mysql_init() failed\n");
      exit(1);
  }
  
  if (mysql_real_connect(con, "host", "user", "password", "database", 0, NULL, 0) == NULL) {
      finish_with_error(con);
  }    
}

// Ecrire les mesures de température et d'humidité dans la base de données
void GetLog(double temperature, double humidity) {
  char query[100];

  sprintf(query, "INSERT INTO weather_data(temperature, humidity) VALUES(%f, %f)", temperature, humidity);

  if (mysql_query(con, query)) {
      finish_with_error(con);
  }
}

// Lire les paramètres de la base de données
double lireparam(char* param_name) {
  char query[100];
  double value;

  sprintf(query, "SELECT value FROM parameters WHERE name='%s'", param_name);

  if (mysql_query(con, query)) {
      finish_with_error(con);
  }

  MYSQL_RES *result = mysql_store_result(con);

  if (result == NULL) {
      finish_with_error(con);
  }

  MYSQL_ROW row = mysql_fetch_row(result);
  value = atof(row[0]);
  mysql_free_result(result);

  return value;
}

// Modifier les paramètres dans la base de données
void modifparam(char* param_name, double new_value) {
  char query[100];

  sprintf(query, "UPDATE parameters SET value=%f WHERE name='%s'", new_value, param_name);

  if (mysql_query(con, query)) {
      finish_with_error(con);
  }
}

// Rappel de fonction pour écrire dans une structure mémoire
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
   size_t realsize = size * nmemb;
  struct MemoryStruct *mem = (struct MemoryStruct *)userp;

  char *ptr = realloc(mem->memory, mem->size + realsize + 1);
  if(ptr == NULL) {
    /* pas assez de mémoire ! */ 
    printf("Pas assez de mémoire (realloc a renvoyé NULL)\n");
    return 0;
  }

  mem->memory = ptr;
  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;

  return realsize;
}

// Récupérer et traiter les données de température et d'humidité
void *MessureAction(void *arg) {
    while (1) {
    CURL *curl_handle;
    CURLcode res;

    struct MemoryStruct chunk;

    chunk.memory = malloc(1);  /* will be grown as needed by the realloc above */ 
    chunk.size = 0;    /* no data at this point */ 

    curl_global_init(CURL_GLOBAL_ALL);

    curl_handle = curl_easy_init();

    // Use your own URL, headers, and other CURL options here

  curl_easy_setopt(curl_handle, CURLOPT_URL, "https://openapi.tuyaeu.com/v1.0/devices/bf4aa81bfcb66ccb96rybs/status");
  struct curl_slist *headers = NULL;
  headers = curl_slist_append(headers, "sign_method: HMAC-SHA256");
  headers = curl_slist_append(headers, "client_id: f5ujcnr98swqnhca5vcy");
  headers = curl_slist_append(headers, "t: 1684414976667");
  headers = curl_slist_append(headers, "mode: cors");
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, "sign: 52619FFAAADC4A46902DAB977C50A06475C018209BECFCC42560C17574ACCECD");
  headers = curl_slist_append(headers, "access_token: a0e6c3d9398e64276522ecf95656ae16");
  curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
  curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);


    res = curl_easy_perform(curl_handle);

    if(res != CURLE_OK) {
      fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
    } else {
      printf("%lu bytes received\n", (long)chunk.size);
      cJSON *json = cJSON_Parse(chunk.memory);
      if (json == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            fprintf(stderr, "Error before: %s\n", error_ptr);
        }
      } else {
        cJSON *result = cJSON_GetObjectItemCaseSensitive(json, "result");
        cJSON *temperature_item;
        cJSON *humidity_item;
        cJSON_ArrayForEach(temperature_item, result) {
          cJSON *code = cJSON_GetObjectItemCaseSensitive(temperature_item, "code");
          if (cJSON_IsString(code) && strcmp(code->valuestring, "va_temperature") == 0) {
            cJSON *value = cJSON_GetObjectItemCaseSensitive(temperature_item, "value");
            if (cJSON_IsNumber(value)) {
              pthread_mutex_lock(&data_mutex);
              temperature = value->valuedouble/10;
              pthread_mutex_unlock(&data_mutex);
            }
          } else if (cJSON_IsString(code) && strcmp(code->valuestring, "va_humidity") == 0) {
            cJSON *value = cJSON_GetObjectItemCaseSensitive(temperature_item, "value");
            if (cJSON_IsNumber(value)) {
              pthread_mutex_lock(&data_mutex);
              humidity = value->valueint;
              pthread_mutex_unlock(&data_mutex);
            }
          }
        }
        cJSON_Delete(json);
      }
    }

    curl_easy_cleanup(curl_handle);

    free(chunk.memory);

    curl_global_cleanup();

    sleep(1);  // wait before reading sensor data again
  }

  return NULL;
}

// Contrôler l'état du relais
void *control_relay(void *arg) {
    // Charger les paramètres système de la base de données MySQL

    while (1) {
        int turn_on;  // Variable pour déterminer si le relais doit être activé ou non

        pthread_mutex_lock(&data_mutex); // Verrouiller le mutex pour protéger les variables partagées
        // Si le système est en mode manuel, l'état du relais est déterminé par etatVMC
        // Si le système n'est pas en mode manuel, le relais est activé si une des mesures des capteurs est hors des limites définies
        // Note : Vous devez remplacer ceci par votre propre logique pour contrôler le relais en fonction des données des capteurs et des paramètres du système.
        pthread_mutex_unlock(&data_mutex); // Déverrouiller le mutex après modification des variables partagées

        // Active ou désactive le relais en fonction de l'état de la variable turn_on
        if (turn_on) {
            digitalWrite(RelayPin, LOW); // Met le GPIO à LOW pour activer le relais
            printf("Relais activé\n");
        } else {
            digitalWrite(RelayPin, HIGH); // Met le GPIO à HIGH pour désactiver le relais
            printf("Relais désactivé\n");
        }

        sleep(1);  // Attendre avant de vérifier à nouveau les données du capteur
    }

    return NULL;
}

// Point d'entrée du programme
int main() {
  if(wiringPiSetup() == -1){ 
      printf("setup wiringPi failed !\n");
      return -1; 
  }
  
  init_db(); // Initialisation de la base de données

  pthread_mutex_init(&data_mutex, NULL); // Initialisation du mutex

  pthread_t thread1, thread2; // Threads pour exécuter les fonctions de mesure et de contrôle
  pthread_create(&thread1, NULL, MessureAction, NULL);
  pthread_create(&thread2, NULL, control_relay, NULL);

  pthread_join(thread1, NULL); // Attendre que les threads se terminent
  pthread_join(thread2, NULL);

  pthread_mutex_destroy(&data_mutex); // Destruction du mutex

  mysql_close(con); // Fermer la connexion à la base de données

  return 0;
}
