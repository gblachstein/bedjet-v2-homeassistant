// BedJet V2 <-> Home Assistant BLE-to-MQTT bridge (ESP32)
// Board: ESP32 with Bluetooth LE (classic ESP32-WROOM / DevKit recommended). The ESP32-S2 has no Bluetooth.
// Libraries: ESP32 Arduino core (BLEDevice, WiFi, ArduinoOTA) + PubSubClient (Nick O'Leary)
// All user settings live in config.h - see config.example.h.

#include "BLEDevice.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include "config.h"   // copy config.example.h -> config.h and fill it in

BLEUUID serviceUUID;
BLEUUID rxCharUUID;
BLEUUID txCharUUID;

String bedjetMacStr = BEDJET_MAC;

BLEClient* pClient;
BLERemoteCharacteristic* pRemoteRX;
BLERemoteCharacteristic* pRemoteTX;
bool deviceConnected = false;
bool bleEnabled = true;

WiFiClient espClient;
PubSubClient mqttClient(espClient);

unsigned long lastStatusPrint = 0;
unsigned long lastCommandTime = 0;
unsigned long lastReconnectAttempt = 0;
const int ledPin = 2;

uint8_t globalActiveMode = 0x03; 
uint8_t globalByte5 = 0xB6;      
uint8_t globalHours = 8;
uint8_t globalMinutes = 0;
uint8_t globalStep = 10;
uint8_t globalByte8 = 0;
int globalRemainingMins = 0;
bool isPoweredOn = false;

class MyClientCallback : public BLEClientCallbacks {
  void onConnect(BLEClient* pclient) {
    deviceConnected = true;
    Serial.println("BLE Connected successfully.");
  }
  void onDisconnect(BLEClient* pclient) {
    deviceConnected = false;
    Serial.println("BLE Disconnected. Will retry.");
  }
};

MyClientCallback clientCallback;

void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
  if (length >= 14 && pData[0] == 0x59) {
    uint8_t byte3 = pData[3];
    uint8_t byte4 = pData[4];
    uint8_t byte5 = pData[5];
    uint8_t byte6 = pData[6];
    uint8_t byte7 = pData[7];
    uint8_t byte8 = pData[8];
    
    globalByte8 = byte8;

    String currentMode = "UNKNOWN";
    int fanPercent = 0;
    bool currentlyOn = false;

    if (byte4 >= 97 && byte4 <= 116) {
      currentMode = "COOL";
      fanPercent = (uint8_t)(byte4 + 160) * 5;
      globalActiveMode = 0x03;
      currentlyOn = true;
    } else if (byte4 >= 65 && byte4 <= 84) {
      currentMode = "HEAT";
      fanPercent = (uint8_t)(byte4 + 192) * 5;
      globalActiveMode = 0x02;
      currentlyOn = true;
    } else if (byte4 >= 33 && byte4 <= 52) {
      currentMode = "TURBO";
      fanPercent = (uint8_t)(byte4 + 224) * 5; 
      globalActiveMode = 0x01;
      currentlyOn = true;
    } else if (byte5 == 0x00) {
      currentMode = "OFF";
      fanPercent = 0;
      currentlyOn = false;
    }
    
    isPoweredOn = currentlyOn;
    globalByte5 = (byte7 & 0x7F) | 0x80;
    
    if (currentlyOn) {
      globalStep = fanPercent / 5;
    }

    int hours = byte5 >> 4;
    int subRaw = ((byte5 & 0x0F) << 8) | byte6;
    int totalSeconds = hours * 3600 + (subRaw * 60 + 32) / 64;
    
    if (totalSeconds > 0) {
      globalHours = totalSeconds / 3600;
      globalMinutes = (totalSeconds % 3600) / 60;
      globalRemainingMins = totalSeconds / 60;
    } else {
      globalRemainingMins = 0;
    }

    String beepState = "ON";
    if (byte8 & 0x80) {
      beepState = "OFF";
    }

    if (millis() > lastStatusPrint + 3000) {
      lastStatusPrint = millis();
      float tempF = (byte3 & 0x7F) * 0.9 + 32.0;
      float targetTempF = (byte7 & 0x7F) * 0.9 + 32.0;

      if (mqttClient.connected()) {
        String payload = "{\"mode\":\"" + currentMode + "\", \"fan\":" + String(fanPercent) + ", \"temp\":" + String(tempF, 1) + ", \"target\":" + String(targetTempF, 1) + ", \"beep\":\"" + beepState + "\", \"timer\":" + String(globalRemainingMins) + "}";
        mqttClient.publish("home/bedjet/status", payload.c_str());
      }
    }
  }
}

bool connectToBedJet() {
  if (!bleEnabled) return false;

  Serial.print("Connecting to BedJet BLE...");
  
  if (pClient == nullptr) {
    pClient = BLEDevice::createClient();
    (*pClient).setClientCallbacks(&clientCallback);
  }

  if (!(*pClient).connect(BLEAddress(bedjetMacStr.c_str()))) {
    Serial.println(" Failed to connect to MAC.");
    return false;
  }

  BLERemoteService* pRemoteService = (*pClient).getService(serviceUUID);
  if (pRemoteService == nullptr) {
    Serial.println(" Failed to find BedJet V2 Service.");
    (*pClient).disconnect();
    return false;
  }

  pRemoteRX = (*pRemoteService).getCharacteristic(rxCharUUID);
  if (pRemoteRX == nullptr) {
    Serial.println(" Failed to find RX Command Characteristic.");
    (*pClient).disconnect();
    return false;
  }

  pRemoteTX = (*pRemoteService).getCharacteristic(txCharUUID);
  if (pRemoteTX != nullptr && (*pRemoteTX).canNotify()) {
    (*pRemoteTX).registerForNotify(notifyCallback);
    Serial.print(" Subscribed to BedJet TX Channel...");
  }

  Serial.println(" Connected and Ready!");
  deviceConnected = true;
  return true;
}

void disconnectBedJet() {
  if (pClient != nullptr && (*pClient).isConnected()) {
    (*pClient).disconnect();
    Serial.println("Disconnected from BedJet BLE for phone app usage.");
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  if (millis() < lastCommandTime + 300) {
    delay(300);
  }
  lastCommandTime = millis();

  String msg = "";
  for (int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  msg.trim();
  msg.toUpperCase();

  Serial.print("\nReceived MQTT command: ");
  Serial.println(msg);

  if (msg == "BLE OFF") {
    bleEnabled = false;
    disconnectBedJet();
    if (mqttClient.connected()) {
      mqttClient.publish("home/bedjet/ble", "OFF", true);
      String statusPayload = "{\"mode\":\"DISABLED\", \"fan\":0, \"temp\":0, \"target\":0, \"beep\":\"OFF\", \"timer\":0}";
      mqttClient.publish("home/bedjet/status", statusPayload.c_str());
    }
    return;
  }

  if (msg == "BLE ON") {
    bleEnabled = true;
    if (mqttClient.connected()) {
      mqttClient.publish("home/bedjet/ble", "ON", true);
    }
    if (!deviceConnected) {
      connectToBedJet();
    }
    return;
  }

  if (!bleEnabled) {
    Serial.println("BLE Bridge is currently disabled. Command ignored.");
    return;
  }

  if (msg.startsWith("TEMP ")) {
    float desiredTemp = msg.substring(5).toFloat();
    if (desiredTemp < 66.0) desiredTemp = 66.0;
    if (desiredTemp > 109.0) desiredTemp = 109.0;

    int offsetInt = 32;
    int negOffsetInt = ~offsetInt + 1;
    float negOffset = (float)negOffsetInt;

    uint8_t targetVal = round((desiredTemp + negOffset) / 0.9);
    globalByte5 = targetVal | 0x80;

    uint8_t packet[10];
    packet[0] = 0x58;
    packet[1] = 0x07;
    packet[2] = 0x0E;
    packet[3] = globalActiveMode;
    packet[4] = globalStep;
    packet[5] = globalByte5;
    
    uint8_t h = globalHours;
    uint8_t m = globalMinutes;
    if (h == 0 && m == 0) {
      h = 8; 
      m = 0;
    }
    
    packet[6] = h;
    packet[7] = m;
    packet[8] = 0x00; 

    uint32_t sum = 0;
    for(int i = 0; i < 9; i++) {
      sum += packet[i];
    }
    packet[9] = ~(sum & 0xFF);

    if (deviceConnected && pRemoteRX != nullptr) {
      (*pRemoteRX).writeValue(packet, 10, true);
      Serial.print("Sent TEMP Command: ");
      Serial.println(desiredTemp);
    }
    return;
  }

  if (msg.startsWith("FAN ")) {
    int desiredFan = msg.substring(4).toInt();
    if (desiredFan < 5) desiredFan = 5;
    if (desiredFan > 100) desiredFan = 100;
    
    desiredFan = ((desiredFan + 2) / 5) * 5;
    uint8_t step = desiredFan / 5;

    uint8_t packet[10];
    packet[0] = 0x58;
    packet[1] = 0x07;
    packet[2] = 0x0E;
    packet[3] = globalActiveMode;
    packet[4] = step;
    packet[5] = globalByte5;
    
    uint8_t h = globalHours;
    uint8_t m = globalMinutes;
    if (h == 0 && m == 0) {
      h = 8; 
      m = 0;
    }
    
    packet[6] = h;
    packet[7] = m;
    packet[8] = 0x00; 

    uint32_t sum = 0;
    for(int i = 0; i < 9; i++) {
      sum += packet[i];
    }
    packet[9] = ~(sum & 0xFF);

    if (deviceConnected && pRemoteRX != nullptr) {
      (*pRemoteRX).writeValue(packet, 10, true);
      Serial.println("Sent FAN Command!");
    }
    return;
  }

  if (msg.startsWith("TIMER ")) {
    String arg = msg.substring(6);
    int totalMins = 0;

    if (arg.startsWith("+")) {
      totalMins = globalRemainingMins + arg.substring(1).toInt();
    } else if (arg.startsWith(String(char(45)))) {
      totalMins = globalRemainingMins + (~arg.substring(1).toInt() + 1);
    } else {
      totalMins = arg.toInt();
    }

    if (totalMins < 1) totalMins = 1;
    if (totalMins > 720) totalMins = 720;

    globalHours = totalMins / 60;
    globalMinutes = totalMins % 60;

    uint8_t packet[10];
    packet[0] = 0x58;
    packet[1] = 0x07;
    packet[2] = 0x0E;
    packet[3] = globalActiveMode;
    packet[4] = globalStep;
    packet[5] = globalByte5;
    packet[6] = globalHours;
    packet[7] = globalMinutes;
    packet[8] = 0x00; 

    uint32_t sum = 0;
    for(int i = 0; i < 9; i++) {
      sum += packet[i];
    }
    packet[9] = ~(sum & 0xFF);

    if (deviceConnected && pRemoteRX != nullptr) {
      (*pRemoteRX).writeValue(packet, 10, true);
      Serial.print("Sent TIMER Command: ");
      Serial.print(totalMins);
      Serial.println(" mins");
    }
    return;
  }

  uint8_t packet[5];
  packet[0] = 0x58; 
  packet[1] = 0x02; 
  packet[2] = 0x01; 

  if (msg == "TURBO") {
    packet[3] = 0x01;
  } else if (msg == "HEAT") {
    packet[3] = 0x02;
  } else if (msg == "COOL") {
    packet[3] = 0x03;
  } else if (msg == "OFF") {
    if (!isPoweredOn) {
      Serial.println("Already OFF. Ignoring.");
      return;
    }
    packet[3] = globalActiveMode;
  } else if (msg == "BEEP ON" || msg == "BEEP OFF") {
    bool currentlyMuted = (globalByte8 & 0x80) != 0;
    bool wantBeepOn = (msg == "BEEP ON");

    if ((wantBeepOn && currentlyMuted) || (!wantBeepOn && !currentlyMuted)) {
      packet[2] = 0x01;
      packet[3] = 0x10;
    } else {
      Serial.println("Beep state matches. Ignoring.");
      return;
    }
  } else {
    Serial.println("Unrecognized command.");
    return;
  }

  uint32_t sum = packet[0] + packet[1] + packet[2] + packet[3];
  uint8_t maskedSum = sum & 0xFF;
  packet[4] = ~maskedSum;

  if (deviceConnected && pRemoteRX != nullptr) {
    (*pRemoteRX).writeValue(packet, 5, true);
    Serial.println("Sent Toggle Command!");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  String srv = "49535343" + String(char(45)) + "fe7d" + String(char(45)) + "4ae5" + String(char(45)) + "8fa9" + String(char(45)) + "9fafd205e455";
  String rx = "49535343" + String(char(45)) + "8841" + String(char(45)) + "43f4" + String(char(45)) + "a8d4" + String(char(45)) + "ecbe34729bb3";
  String tx = "49535343" + String(char(45)) + "1e4d" + String(char(45)) + "4bd9" + String(char(45)) + "ba61" + String(char(45)) + "23c647249616";
  
  serviceUUID = BLEUUID(srv.c_str());
  rxCharUUID = BLEUUID(rx.c_str());
  txCharUUID = BLEUUID(tx.c_str());

  Serial.println("\n*** ESP32 BedJet V2 BLE Bridge Booting ***");

  Serial.print("Connecting to WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected! IP: " + WiFi.localIP().toString());

  ArduinoOTA.setHostname(OTA_HOSTNAME);
#ifdef OTA_PASSWORD
  ArduinoOTA.setPassword(OTA_PASSWORD);
#endif
  ArduinoOTA.begin();

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(callback);

  BLEDevice::init("");
  lastReconnectAttempt = millis();
}

void loop() {
  ArduinoOTA.handle();

  if (!mqttClient.connected()) {
    Serial.print("Connecting to MQTT Broker...");
    if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASSWORD)) {
      Serial.println("Connected!");
      mqttClient.subscribe("home/bedjet/command");
      
      mqttClient.publish("home/bedjet/ip", WiFi.localIP().toString().c_str(), true);
      mqttClient.publish("home/bedjet/ble", bleEnabled ? "ON" : "OFF", true);
      
    } else {
      Serial.println("Failed. Retrying in 5s...");
      delay(5000);
    }
  }
  mqttClient.loop();

  if (bleEnabled && !deviceConnected) {
    if (millis() > lastReconnectAttempt + 20000) {
      lastReconnectAttempt = millis();
      Serial.println("Attempting to reconnect to BedJet...");
      connectToBedJet();
    }
  }
}
