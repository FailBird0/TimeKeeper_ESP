#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"   // local file for WiFi and server credentials

IPAddress ip;

HTTPClient http;

#define RFID_SS_PIN 5    // SDA (SS) → GPIO 5
#define RFID_RST_PIN 13  // ^ etc
#define RFID_SCK_PIN 18
#define RFID_MISO_PIN 19
#define RFID_MOSI_PIN 23
#define PIEZO_PIN 16
#define LCD_SDA_PIN 21
#define LCD_SCL_PIN 22

// set LCD address, number of columns and rows
LiquidCrystal_I2C lcd(0x27, 16, 2);

// declaring RFID scanner
MFRC522 mfrc522(RFID_SS_PIN, RFID_RST_PIN);

void setup() {
  // set I2C connection for LCD data & clock pins
  Wire.begin(LCD_SDA_PIN, LCD_SCL_PIN);

  Serial.begin(115200);
  
  // start LCD
  lcd.init();
  lcd.backlight();
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting...");

  // global variables from config.h
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
  
  ip = WiFi.localIP();
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi Connected.");
  lcd.setCursor(0, 1);
  lcd.print(ip);

  Serial.print("Local IP: ");
  Serial.println(ip);

  // use SPI for RFID scanner
  SPI.begin(RFID_SCK_PIN, RFID_MISO_PIN, RFID_MOSI_PIN, RFID_SS_PIN);
  mfrc522.PCD_Init();   // start RC522 (RFID scanner)
  Serial.println("RFID-Scanner started.");
  
  lcd.setCursor(0, 0);
  lcd.print("Scan RFID       ");
}

void loop() {
  if (!mfrc522.PICC_IsNewCardPresent()) return;  // cheking rfid...
  if (!mfrc522.PICC_ReadCardSerial()) return;   // trying to scan...

  String uid = "";

  // Loop through the scanned UID bytes
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) {
      uid += "0"; // Adds a zero for single hex characters
    }
    uid += String(mfrc522.uid.uidByte[i], HEX);  // Convert byte to hex and append to string
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(uid);

  httpPostRFID(uid);

  delay(500);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Scan RFID");

  mfrc522.PICC_HaltA();
}

void httpPostRFID(String uid) {
  // link to API interface
  String url = String(SERVER_IP) + String(SERVER_PORT) + "/check";

  JsonDocument doc;
  doc["hex_uid"] = uid;
  String payload;
  serializeJson(doc, payload);  // converts doc into JSON, puts into payload as string

  http.begin(url);
  http.setTimeout(10000);
  
  // !!! important
  http.addHeader("Content-Type", "application/json");

  int httpCode;

  int retries = 0;
  int maxRetries = 5;

  bool success = false;

  while (retries != maxRetries) {
    Serial.printf("Attempting HTTP request (%d)\n", retries);

    httpCode = http.POST(payload);  // user_id: (optional)
                                        // hex_uid: (optional) though one of two needed
                                        // date_time: (optional)
    lcd.setCursor(0, 1);

    if (httpCode <= 0) {
      // -11 - 0: ESP error / WiFi connection / server offline
      Serial.printf("[HTTP] GET... failed, error: %s (%d) (WiFi-Status: %d)\n", http.errorToString(httpCode).c_str(), httpCode, WiFi.status());
      retries++;
      continue; // retry with new while iteration
    } else if (httpCode >= 200 && httpCode <= 299) {
      // 200 - 299: success
      Serial.println("[HTTP] GET... success!");
      lcd.print("Success");
      success = true;
      break; // do not retry, continue function
    } else if (httpCode >= 400 && httpCode <= 499) {
      // 400 - 499: client error
      Serial.printf("[HTTP] GET... failed, error: %s (%d) (WiFi-Status: %d)\n", http.errorToString(httpCode).c_str(), httpCode, WiFi.status());
      if (httpCode == 400) {  // server USUALLY responds with a Bad Request 400
        lcd.print("RFID not in DB");
        delay(3000);
      } else {
        lcd.print("Client Error");
      }
      break; // do not retry, continue function
    } else if (httpCode >= 500 && httpCode <= 599) {
      // 500 - 599: server error
      Serial.printf("[HTTP] GET... failed, error: %s (%d) (WiFi-Status: %d)\n", http.errorToString(httpCode).c_str(), httpCode, WiFi.status());
      retries++;
      continue; // retry with new while iteration
    } else {
      // idk what to do with 300-399 & 100-199
      break;
    }
  }

  if (success) {
    String response = http.getString();
    JsonDocument resultDoc;
    deserializeJson(resultDoc, response);

    const char* userName = resultDoc["data"]["user"]["name"];
    Serial.println(userName);

    lcd.setCursor(0, 0);
    lcd.print(String(userName) + "                ");
    
    tone(PIEZO_PIN, 2800, 150); // (pin, frequency, duration in ms)
  } else {
    tone(PIEZO_PIN, 2000, 300); // evil tone
  }

  delay(2000);
  http.end();
}
