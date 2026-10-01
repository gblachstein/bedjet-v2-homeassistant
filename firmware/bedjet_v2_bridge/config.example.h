// Copy this file to config.h (same folder) and fill in your values.
// config.h is git-ignored so your credentials never get committed.
#pragma once

// BedJet V2 Bluetooth MAC address (find it with a BLE scanner app such as nRF Connect)
#define BEDJET_MAC      "XX:XX:XX:XX:XX:XX"

// Wi-Fi (ESP32 only supports 2.4 GHz)
#define WIFI_SSID       "YOUR_WIFI_SSID"
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"

// MQTT broker (with the Mosquitto add-on this is your Home Assistant IP)
#define MQTT_HOST       "192.168.1.x"
#define MQTT_PORT       1883
#define MQTT_USER       "MQTT_USERNAME"
#define MQTT_PASSWORD   "MQTT_PASSWORD"
#define MQTT_CLIENT_ID  "ESP32_BedJet"   // must be unique per bridge

// Over-the-air updates
#define OTA_HOSTNAME    "bedjet_esp32"
// #define OTA_PASSWORD "choose-a-password"   // uncomment to require a password for OTA
