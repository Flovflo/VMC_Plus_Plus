#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <pthread.h>
#include <wiringPi.h>
#include <unistd.h>
#include <libwebsockets.h>
#include <sqlite3.h>

//compilation : gcc main6.c -o exe -lwiringPi -lsqlite3 -lpthread -lwebsockets -lcjson

#define MAX_TIME 85
#define DHT11PIN 2

#define Hmax 90
#define Tmax 39

#define RelayPin 0  // Correspond à GPIO17 si vous utilisez WiringPi

void dht11_read_val();
int insertLog(int t, int h, int eco, int tvoc, int etatVMC);
void *MessureAction(void *arg);
void *control_relay(void *arg);
int callback_ws(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len);

int current_relay_state = -1;
int dht11_val[5]={0,0,0,0,0};
pthread_mutex_t data_mutex;
struct lws_context *context;
struct lws_protocols protocols[] = {
	{
		"vmc-protocol",
		callback_ws,
		0,
		0,
	},
	{ NULL, NULL, 0, 0 }
};
int automatique = 1;

int temp = 0;
int hum = 0;

int main() {
  
  struct lws_context_creation_info info;
  memset(&info, 0, sizeof(info));
  
  if(wiringPiSetup() == -1){ 
      printf("setup wiringPi failed !\n");
      return -1; 
  }
  
  pinMode(RelayPin, OUTPUT);
    info.port = 8000;
    info.iface = NULL;
    info.protocols = protocols;
    info.extensions = NULL;
    info.gid = -1;
    info.uid = -1;
    info.options |= LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    
    context = lws_create_context(&info);
    if (context == NULL)
    {
        fprintf(stderr, "Erreur lors de la création du contexte du serveur WebSocket\n");
        return -1;
    }

	pthread_mutex_init(&data_mutex, NULL); 
  pthread_t thread1, thread2;
  pthread_create(&thread1, NULL, MessureAction, NULL);
  pthread_create(&thread2, NULL, control_relay, NULL);
  
  pthread_join(thread1, NULL);
  pthread_join(thread2, NULL);

  return 0;
}

void *MessureAction(void *arg) {
  while(1)
  {
     pthread_mutex_lock(&data_mutex);
     dht11_read_val();
     pthread_mutex_unlock(&data_mutex);
     delay(1000);
  }
  return 0;
}

/*
 *Function :  dht11_read_val
 *Description : mesure la température et l'humidité et la stock dans dht11_val
 */
void dht11_read_val()
{
  uint8_t lststate=HIGH;
  uint8_t counter=0;
  uint8_t j=0,i;
  float farenheit;
  for(i=0;i<5;i++)
     dht11_val[i]=0;
     
  pinMode(DHT11PIN,OUTPUT);
  digitalWrite(DHT11PIN,LOW);
  delay(18);
  digitalWrite(DHT11PIN,HIGH);
  delayMicroseconds(40);
  pinMode(DHT11PIN,INPUT);
  for(i=0;i<MAX_TIME;i++)
  {
    counter=0;
    while(digitalRead(DHT11PIN)==lststate){
      counter++;
      delayMicroseconds(1);
      if(counter==255)
        break;
    }
    lststate=digitalRead(DHT11PIN);
    if(counter==255)
       break;
    // top 3 transitions are ignored
    if((i>=4)&&(i%2==0)){
      dht11_val[j/8]<<=1;
      if(counter>16)
        dht11_val[j/8]|=1;
      j++;
    }
  }
  // verify checksum and print the verified data
  if((j>=40)&&(dht11_val[4]==((dht11_val[0]+dht11_val[1]+dht11_val[2]+dht11_val[3])& 0xFF)))
  {
    farenheit=dht11_val[2]*9./5.+32;
    printf("Humidity = %d.%d %% Temperature = %d.%d *C (%.1f *F)\n",dht11_val[0],dht11_val[1],dht11_val[2],dht11_val[3],farenheit);

    // Mise à jour des variables globales avec les valeurs tronquées de la température et de l'humidité
    temp = dht11_val[2];
    hum = dht11_val[0];

    insertLog(temp, hum, dht11_val[1], dht11_val[3], !digitalRead(RelayPin));
  }
  //else
    //rintf("Invalid Data!!\n");
}

/*
 *Function :  insertLog
 *Description : insère les mesures du capteurs dans la table, avec la date actuelle.
 */
int insertLog(int t, int h, int eco, int tvoc, int etatVMC){
	sqlite3 *db;
    char *err_msg = 0;
    
    //open database
    int rc = sqlite3_open("vmc.db", &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }
    
	//Allocation dynamique
	char *sql = malloc(100* sizeof(char));
	if(sql == NULL){
		fprintf(stderr, "Erreur d'allocation mémoire");
		return 1;
	}
	
	//obtention de la date actuelle
	time_t now = time(NULL);
	struct tm *tm_info = localtime(&now);
	char timestamp[20];
	strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
	
	//Requete
	sprintf(sql, "INSERT INTO logs (date, t, h, eco, tvoc, etatVMC) VALUES ('%s', %d, %d, %d, %d, %d);",timestamp, t,h,eco,tvoc,etatVMC);
    rc = sqlite3_exec(db, sql, 0, 0, &err_msg);
    
    //erreurs
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        free(sql);
        return 1;
    }
    //fin
    free(sql);
    sqlite3_close(db);
	return 0;
}

/*
 *Function :  callback_ws
 *Description : Callback appelée lorsqu'un message est reçu du client WebSocket
 */
int callback_ws(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len)
{
    switch (reason)
    {
        case LWS_CALLBACK_ESTABLISHED:
            printf("Connexion WebSocket établie\n");

            // Crée un objet JSON avec les valeurs de temp et hum
            cJSON *root = cJSON_CreateObject();
            cJSON_AddNumberToObject(root, "temperature", temp);
            cJSON_AddNumberToObject(root, "humidity", hum);
            cJSON_AddNumberToObject(root, "etatVMC", current_relay_state);

            // Convertit l'objet JSON en une chaîne
            char *json_string = cJSON_Print(root);
            printf("Envoi des valeurs initiales : %s\n", json_string);

            // Envoie la chaîne au client
            lws_write(wsi, (unsigned char*)json_string, strlen(json_string), LWS_WRITE_TEXT);

            break;

        case LWS_CALLBACK_RECEIVE:
            // Message reçu du client WebSocket
            printf("Message reçu du client WebSocket : %.*s\n", (int)len, (char *)in);
            char *message = (char *)in;
            message[len] = '\0';
            //printf("%s !!",message);

            // Vous pouvez traiter les messages ici et envoyer une réponse si nécessaire
            if(strcmp(message, "0")==0){
                automatique=0;
                // Eteint VMC
                printf("éteint\n");
                digitalWrite(RelayPin, HIGH); // Met le GPIO à HIGH pour désactiver le relais
                //printf("Relais désactivé\n");
                //lws_write(wsi, (unsigned char*)"VMC éteinte", 12, LWS_WRITE_TEXT);
                //lws_write(wsi, (unsigned char*)"VMC off", sizeof("VMC off"), LWS_WRITE_TEXT);
                //Eteint VMC
                
                
            }
            else if(strcmp(message, "1")==0){
                automatique=0;
                // allume VMC
                printf("allume\n");
                digitalWrite(RelayPin, LOW); // Met le GPIO à LOW pour activer le relais
                //lws_write(wsi, (unsigned char*)"VMC allumé", sizeof("VMC allumé"), LWS_WRITE_TEXT);
                //printf("Relais activé\n");
                //lws_write(wsi, (unsigned char*)"VMC allumée", sizeof("VMC allumée"), LWS_WRITE_TEXT);
                //allume VMC
                
            }
            else if (strcmp(message, "2")==0){
				automatique=1;
            }
            else{
					//printf("Valeur inattendue\n");
					//lws_write(wsi, (unsigned char*)"Valeur inattendue", sizeof("Valeur inattendue"), LWS_WRITE_TEXT);
				}
            break;

        default:
            break;
    }



    return 0;
}


/*
 *Function :  control_relay
 *Description : contrôle régulièrement l'état du relais.
 */
void *control_relay(void *arg) {
    // Boucle principale du serveur WebSocket
    while (1)
    {
        lws_service(context, 0);
        usleep(1000);

        // Contrôle automatique
        if(automatique==1){
            
			pthread_mutex_lock(&data_mutex);
			if(hum>Hmax || temp>Tmax){ //si temperature > Tmax et humidité > Hmax
				if(current_relay_state != 1) { // vérifie si le relais est déjà allumé
					printf("allume\n");
					digitalWrite(RelayPin, LOW); // Met le GPIO à LOW pour activer le relais
					current_relay_state = 1;
				}
			}else{
				if(current_relay_state != 0) { // vérifie si le relais est déjà éteint
					printf("éteint\n");
					digitalWrite(RelayPin, HIGH); // Met le GPIO à HIGH pour désactiver le relais
					current_relay_state = 0;
				}
			}
			pthread_mutex_unlock(&data_mutex);
    }
    }
    return NULL;
}
