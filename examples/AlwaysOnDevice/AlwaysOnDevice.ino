/*
  AlwaysOnDevice.ino
  HomeAssistantArduinoMQTT Library Example
  
  Demonstrates an always-on device (ESP32, ESP8266, AVR, SAMD) connected continuously.
  Registers a Temperature Sensor, Humidity Sensor, Relay Switch, and Action Button 
  with Home Assistant Auto-Discovery.

  Author: Alberto Bettin
  Repository: https://github.com/Bettapro/HomeAssistantArduinoMQTT
*/

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  WiFiClient netClient;
#elif defined(ESP32)
  #include <WiFi.h>
  WiFiClient netClient;
#elif defined(ARDUINO_ARCH_AVR) || defined(ARDUINO_ARCH_SAMD)
  #include <Ethernet.h>
  EthernetClient netClient;
  byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
#else
  #error "Board non supportata!"
#endif

#include <HomeAssistantArduinoMQTT.h>

// ==========================================
// CONFIGURATION - Update with your settings
// ==========================================
#if defined(ESP8266) || defined(ESP32)
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
#endif

const char* MQTT_BROKER   = "192.168.1.100";
const uint16_t MQTT_PORT  = 1883;
const char* MQTT_USER     = "mqtt_user";
const char* MQTT_PASS     = "mqtt_password";

// Hardware Pins
#if defined(LED_BUILTIN)
const uint8_t STATUS_LED  = LED_BUILTIN;
#else
const uint8_t STATUS_LED  = 13;
#endif
const uint8_t RELAY_PIN   = 5;   // GPIO connected to Relay

// Initialize HomeAssistantArduinoMQTT instance with capacity for 4 entities
HomeAssistantArduinoMQTT haMqtt(4);

// ==========================================
// MULTI-PLATFORM HELPERS
// ==========================================
void rebootDevice() {
#if defined(ESP8266) || defined(ESP32)
    ESP.restart();
#elif defined(ARDUINO_ARCH_SAMD)
    NVIC_SystemReset();
#elif defined(ARDUINO_ARCH_AVR)
    asm volatile ("jmp 0"); // Soft-reset per AVR
#endif
}

bool isNetworkConnected() {
#if defined(ESP8266) || defined(ESP32)
    return WiFi.status() == WL_CONNECTED;
#elif defined(ARDUINO_ARCH_AVR) || defined(ARDUINO_ARCH_SAMD)
    return Ethernet.linkStatus() != LinkOFF;
#else
    return true;
#endif
}

void connectNetwork() {
#if defined(ESP8266) || defined(ESP32)
    if (WiFi.status() == WL_CONNECTED) return;

    Serial.print(F("Connecting to Wi-Fi SSID: "));
    Serial.println(WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(F("."));
    }

    Serial.println();
    Serial.print(F("Wi-Fi Connected! IP Address: "));
    Serial.println(WiFi.localIP());
#elif defined(ARDUINO_ARCH_AVR) || defined(ARDUINO_ARCH_SAMD)
    Serial.println(F("Initializing Ethernet via DHCP..."));
    if (Ethernet.begin(mac) == 0) {
        Serial.println(F("DHCP Failed! Check cable connection."));
    } else {
        Serial.print(F("Ethernet Connected! IP Address: "));
        Serial.println(Ethernet.localIP());
    }
#endif
}

// Callback Handler Class for incoming Home Assistant commands
class CustomMQTTCallback : public HAMQTTCallback {
public:
    void onMQTTMessage(const char* item, const char* value, bool isState) override {
        Serial.print(F("[MQTT Callback] Item: "));
        Serial.print(item);
        Serial.print(F(" | Value: "));
        Serial.print(value);
        Serial.print(F(" | IsState: "));
        Serial.println(isState ? "true" : "false");

        // We handle commands sent from Home Assistant (isState == false)
        if (!isState) {
            // Handle Relay Switch command
            if (strcmp(item, "main_relay") == 0) {
                bool newState = (strcmp(value, "true") == 0);
                digitalWrite(RELAY_PIN, newState ? HIGH : LOW);
                digitalWrite(STATUS_LED, newState ? HIGH : LOW);

                // Reflect updated state back to Home Assistant
                haMqtt.setValue("main_relay", newState ? "true" : "false");
                haMqtt.sendValue("main_relay");
                
                Serial.print(F("Relay state updated to: "));
                Serial.println(newState ? "ON" : "OFF");
            }
            // Handle Trigger Button command
            else if (strcmp(item, "restart_btn") == 0) {
                Serial.println(F("Restart Button pressed from Home Assistant! Rebooting..."));
                delay(500);
                rebootDevice();
            }
        }
    }
};

CustomMQTTCallback mqttCallbackHandler;

// Timing variables for non-blocking loop updates
unsigned long lastSensorReadTime = 0;
const unsigned long SENSOR_READ_INTERVAL = 10000; // Read sensors every 10 seconds

void setupEntities() {
    Serial.println(F("Publishing Home Assistant Auto-Discovery entities..."));

    // 1. Temperature Sensor
    HAEntityBuilder tempSensor = haMqtt.newSensorEntity("temperature", "Room Temperature");
    tempSensor.deviceClass("temperature");
    tempSensor.stateClass("measurement");
    tempSensor.unit("°C");
    tempSensor.icon("mdi:thermometer");
    tempSensor.suggestedDisplayPrecision(1);
    tempSensor.publish();

    // 2. Humidity Sensor
    HAEntityBuilder humSensor = haMqtt.newSensorEntity("humidity", "Room Humidity");
    humSensor.deviceClass("humidity");
    humSensor.stateClass("measurement");
    humSensor.unit("%");
    humSensor.icon("mdi:water-percent");
    humSensor.suggestedDisplayPrecision(1);
    humSensor.publish();

    // 3. Relay Switch
    HAEntityBuilder mainRelay = haMqtt.newSwitchEntity("main_relay", "Main Power Relay");
    mainRelay.icon("mdi:power-socket-eu");
    mainRelay.startup("false");
    mainRelay.publish();

    // 4. Restart Button
    HAEntityBuilder restartBtn = haMqtt.newButtonEntity("restart_btn", "Restart Device");
    restartBtn.icon("mdi:restart");
    restartBtn.category("diagnostic");
    restartBtn.publish();
}

// Simulated sensor reading functions
float readTemperature() {
    return 21.5 + (random(-10, 10) / 10.0);
}

float readHumidity() {
    return 55.0 + (random(-20, 20) / 10.0);
}

// ==========================================
// SETUP & LOOP
// ==========================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println(F("\n--- HomeAssistantArduinoMQTT: Always-On Device ---"));

    pinMode(RELAY_PIN, OUTPUT);
    pinMode(STATUS_LED, OUTPUT);
    digitalWrite(RELAY_PIN, LOW);
    digitalWrite(STATUS_LED, LOW);

    connectNetwork();

    // Device metadata
    haMqtt.MQTTDeviceName = "climate_node_01";
    haMqtt.HADeviceName   = "Climate & Control Node";
    haMqtt.Manufacturer   = "Custom DIY";
    haMqtt.Model          = "MultiArch-AlwaysOn-Node";
    haMqtt.Version        = "1.0.0";
    haMqtt.MqttUser       = MQTT_USER;
    haMqtt.MqttPassword   = MQTT_PASS;

    // Set callback listener for incoming Home Assistant commands
    haMqtt.setCallback(&mqttCallbackHandler);

    // Initialize MQTT client connection
    haMqtt.begin(netClient, MQTT_BROKER, MQTT_PORT);

    // Register entities with Home Assistant Discovery
    setupEntities();
}

void loop() {
    // Reconnect Network if connection drops
    if (!isNetworkConnected()) {
        connectNetwork();
    }

    // Process MQTT loop (handles reconnection and incoming command callbacks)
    haMqtt.loop();

    // Non-blocking timer for reading and sending sensor values
    unsigned long currentMillis = millis();
    if (currentMillis - lastSensorReadTime >= SENSOR_READ_INTERVAL) {
        lastSensorReadTime = currentMillis;

        if (haMqtt.connected()) {
            float temp = readTemperature();
            float hum = readHumidity();

            char strBuf[16];

            // Format and update Temperature
            HAAM_FORMAT_FLOAT(strBuf, temp, 1);
            haMqtt.setValue("temperature", strBuf);

            // Format and update Humidity
            HAAM_FORMAT_FLOAT(strBuf, hum, 1);
            haMqtt.setValue("humidity", strBuf);

            // Transmit changed sensor values to MQTT
            haMqtt.sendValues();

            Serial.print(F("Published -> Temp: "));
            Serial.print(temp, 1);
            Serial.print(F(" °C | Humidity: "));
            Serial.print(hum, 1);
            Serial.println(F(" %"));
        }
    }
}