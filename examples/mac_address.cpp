#include "WiFi.h"

void setup() {
  Serial.begin(115200);
  delay(300);
    WiFi.mode(WIFI_STA);
    Serial.print("Current MAC Address: ");
    Serial.println(WiFi.macAddress());
}


void loop() {
    
}   


