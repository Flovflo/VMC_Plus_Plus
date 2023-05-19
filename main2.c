#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <pthread.h>
#include <wiringPi.h>
#include <unistd.h>

#define RelayPin 0  // Correspond à GPIO17 si vous utilisez WiringPi

struct MemoryStruct {
  char *memory;
  size_t size;
};

pthread_mutex_t data_mutex;
double temperature, humidity;
int etatVMC;

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
  size_t realsize = size * nmemb;
  struct MemoryStruct *mem = (struct MemoryStruct *)userp;

  char *ptr = realloc(mem->memory, mem->size + realsize + 1);
  if(ptr == NULL) {
    /* out of memory! */ 
    printf("Not enough memory (realloc returned NULL)\n");
    return 0;
  }

  mem->memory = ptr;
  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;

  return realsize;
}

void *fetchTemperatureHumidity(void *arg) {
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

void *control_relay(void *arg) {
  // load system parameters from MySQL database
  // Note: You need to replace this with your own code to connect to
  //       your MySQL database and read the system parameters.

  while (1) {
    int turn_on;

    pthread_mutex_lock(&data_mutex);
    // if the system is in manual mode, the relay state is determined by etatVMC
    // if the system is not in manual mode, the relay is turned on if any of the sensor readings are outside the set limits
    // Note: You need to replace this with your own logic for controlling the relay based on the sensor data and system parameters.
    pthread_mutex_unlock(&data_mutex);

    if (turn_on) {
      digitalWrite(RelayPin, LOW);
      printf("Relay ON\n");
    } else {
      digitalWrite(RelayPin, HIGH);
      printf("Relay OFF\n");
    }

    sleep(1);  // wait before checking sensor data again
  }

  return NULL;
}

int main() {
  if(wiringPiSetup() == -1){ 
      printf("setup wiringPi failed !\n");
      return -1; 
  }
  
  pinMode(RelayPin, OUTPUT);

  pthread_mutex_init(&data_mutex, NULL);

  pthread_t thread1, thread2;
  pthread_create(&thread1, NULL, fetchTemperatureHumidity, NULL);
  pthread_create(&thread2, NULL, control_relay, NULL);

  pthread_join(thread1, NULL);
  pthread_join(thread2, NULL);

  pthread_mutex_destroy(&data_mutex);

  return 0;
}
