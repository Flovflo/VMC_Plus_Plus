#include <wiringPi.h>
#include <stdio.h>

#define RelayPin 0  // Correspond à GPIO17 si vous utilisez WiringPi

int main(void){
    if(wiringPiSetup() == -1){ 
        printf("setup wiringPi failed !\n");
        return -1; 
    }
    
    pinMode(RelayPin, OUTPUT);

    while(1){
        digitalWrite(RelayPin, LOW);
        printf("Relay ON\n");
        delay(10000);
        digitalWrite(RelayPin, HIGH);
        printf("Relay OFF\n");
        delay(10000);
    }

    return 0;
}
