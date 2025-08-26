#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <SoftwareSerial.h>
#include <DHT11.h>
#include "CQRobotTDS.h"

// Wi-Fi credentials - Replace these with your Wi-Fi network name and password
const char* ssid = "asdf";         // Replace with your Wi-Fi SSID
const char* password = "01234567"; // Replace with your Wi-Fi password

// Firebase configuration - Replace these with your Firebase Database URL and API key
const char* firebaseHost = "defense-8eba8-default-rtdb.firebaseio.com"; // Ensure no 'https://' or trailing '/'
const char* firebaseAuth = "AIzaSyC9myW3cKeRrGhAOK8YE8Bp4IwyqSE45YA";   // Replace with your Firebase API key

SoftwareSerial mySerial(D3, D2);  // RX, TX
DHT11 dht11(D0);
const int ldrPin = D7;
const int DE = D4;
const int RE = D5;

WiFiClientSecure client;
CQRobotTDS tds(A0);
unsigned long timeout = 0;

// Relay pin
#define RELAY_PIN D1  // Use D6 for relay to avoid conflict with LDR on D1

// Relay timing
unsigned long relayInterval = 5000; // 5 seconds ON/OFF
unsigned long previousRelayMillis = 0;
bool relayState = false;

void setup() {
  Serial.begin(9600);
  mySerial.begin(4800);

  pinMode(ldrPin, INPUT);
  pinMode(DE, OUTPUT); 
  pinMode(RE, OUTPUT);
  digitalWrite(DE, LOW);
  digitalWrite(RE, LOW);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // Relay OFF (active LOW)

  Serial.println("Relay Module Test Starting...");

  // Connect to Wi-Fi
  Serial.println("Connecting to Wi-Fi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting...");
  }
  Serial.println("Connected to Wi-Fi.");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  client.setInsecure(); // Disable SSL certificate verification
  client.setTimeout(10000); // Add a 10-second timeout
}

void sendToFirebase(String path, String jsonData) {
  Serial.print("Connecting to Firebase: ");
  Serial.println(firebaseHost);

  if (client.connect(firebaseHost, 443)) {
    Serial.println("Connection successful.");
    String url = String("/") + path + ".json?auth=" + firebaseAuth;
    Serial.print("Sending data to URL: ");
    Serial.println(url);

    client.println("PUT " + url + " HTTP/1.1");
    client.println("Host: " + String(firebaseHost));
    client.println("Content-Type: application/json");
    client.print("Content-Length: ");
    client.println(jsonData.length());
    client.println();
    client.println(jsonData);

    while (client.available()) {
      String line = client.readStringUntil('\r');
      Serial.print(line);
    }
    client.stop();
  } else {
    Serial.println("Connection to Firebase failed.");
  }
}

void loop() {
  // Relay control logic (5s ON / 5s OFF)
  unsigned long currentMillis = millis();
  if (currentMillis - previousRelayMillis >= relayInterval) {
    previousRelayMillis = currentMillis;
    relayState = !relayState;
    digitalWrite(RELAY_PIN, relayState ? LOW : HIGH); // Active LOW
    Serial.println(relayState ? "Relay ON" : "Relay OFF");
  }

  // --- Sensor + Firebase ---
  // Read DHT11 sensor
  int temperature = 0, humidity = 0;
  int result = dht11.readTemperatureHumidity(temperature, humidity);

  // Read LDR state
  int ldrState = digitalRead(ldrPin);
  String lightCondition = (ldrState == HIGH) ? "Bright" : "Low Light";

  // Read TDS sensor
    float temp = 20.0; // Placeholder temperature value
    float tdsValue = tds.update(temp);

  if (timeout < millis()) {

    Serial.print("TDS value: ");
    Serial.print(tdsValue, 0);
    Serial.println(" ppm");
    timeout = millis() + 1000;
  }

  // Collect data
  String jsonData = "{";
  jsonData += "\"TDS_Value\":" + String(tdsValue, 0) + ",";
  if (result == 0) {
    jsonData += "\"Temperature\":" + String(temperature) + ",";
    jsonData += "\"Humidity\":" + String(humidity) + ",";
  }
  jsonData += "\"LightCondition\":\"" + lightCondition + "\"";

  // Add soil sensor data
  byte queryData[]{ 0x01, 0x03, 0x00, 0x00, 0x00, 0x07, 0x04, 0x08 };
  byte receivedData[19];

  digitalWrite(DE, HIGH);
  digitalWrite(RE, HIGH);
  mySerial.write(queryData, sizeof(queryData));
  digitalWrite(DE, LOW);
  digitalWrite(RE, LOW);
  delay(1000);

  if (mySerial.available() >= sizeof(receivedData)) {
    mySerial.readBytes(receivedData, sizeof(receivedData));

    unsigned int soilHumidity = (receivedData[3] << 8) | receivedData[4];
    unsigned int soilTemperature = (receivedData[5] << 8) | receivedData[6];
    unsigned int soilPH = (receivedData[9] << 8) | receivedData[10];
    unsigned int nitrogen = (receivedData[11] << 8) | receivedData[12];
    unsigned int phosphorus = (receivedData[13] << 8) | receivedData[14];
    unsigned int potassium = (receivedData[15] << 8) | receivedData[16];

    jsonData += ",";
    jsonData += "\"SoilHumidity\":" + String((float)soilHumidity / 10.0) + ",";
    jsonData += "\"SoilTemperature\":" + String((float)soilTemperature / 10.0) + ",";
    jsonData += "\"SoilPH\":" + String((float)soilPH / 10.0) + ",";
    jsonData += "\"Nitrogen\":" + String(nitrogen) + ",";
    jsonData += "\"Phosphorus\":" + String(phosphorus) + ",";
    jsonData += "\"Potassium\":" + String(potassium);
  }
  jsonData += "}";

  // Send data to Firebase
  Serial.println("Uploading data to Firebase...");
  sendToFirebase("SensorData", jsonData);

  delay(5000);  // Delay before the next sensor read + upload
}
