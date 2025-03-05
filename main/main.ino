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

  // global variables from config.h
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");

  ip = WiFi.localIP();
  Serial.print("Local IP: ");
  Serial.println(ip);

  // use SPI for RFID scanner
  SPI.begin(RFID_SCK_PIN, RFID_MISO_PIN, RFID_MOSI_PIN, RFID_SS_PIN);
  mfrc522.PCD_Init();   // start RC522 (RFID scanner)
  Serial.println("RFID-Scanner started.");

  // start LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  
  lcd.setCursor(0, 0);
  lcd.print("Scan RFID");
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
  lcd.print("UID:");
  lcd.setCursor(0, 1);
  lcd.print(uid);
  
  tone(PIEZO_PIN, 2800, 150); // (pin, frequency, duration in ms)

  httpPostRFID(uid);

  delay(2000);

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

  int httpCode = http.POST(payload);  // user_id: (optional)
                                      // hex_uid: (optional) though one of two needed
                                      // date_time: (optional)
  // ESP error / WiFi connection
  if (httpCode <= 0) {
    Serial.printf("[HTTP] GET... failed, error: %s (%d) (WiFi-Status: %d)\n", http.errorToString(httpCode).c_str(), httpCode, WiFi.status());
    return;
  }

  // 4xx: server error, 2xx okay
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[HTTP] GET... code: %d\n", httpCode);
    return;
  }

  http.end();
}
