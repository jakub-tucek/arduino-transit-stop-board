#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include "../config.h"

#define DHTPIN 4
#define DHTTYPE DHT22

DHT dht(DHTPIN, DHTTYPE);

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    dht.begin();
    
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Connected");
}

void loop() {
    delay(2000);
    
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();
    
    if (isnan(temperature) || isnan(humidity)) {
        Serial.println("Failed to read from DHT sensor!");
        return;
    }
    
    Serial.printf("Temperature: %.2f°C, Humidity: %.2f%%\n", temperature, humidity);
    
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(API_ENDPOINT);
        http.addHeader("Content-Type", "application/json");
        
        StaticJsonDocument<256> doc;
        doc["device_name"] = DEVICE_NAME;
        doc["temperature"] = temperature;
        doc["humidity"] = humidity;
        doc["timestamp"] = millis();
        
        String payload;
        serializeJson(doc, payload);
        
        int httpResponseCode = http.POST(payload);
        
        if (httpResponseCode > 0) {
            Serial.printf("HTTP Response code: %d\n", httpResponseCode);
        } else {
            Serial.printf("Error code: %d\n", httpResponseCode);
        }
        
        http.end();
    }
}
