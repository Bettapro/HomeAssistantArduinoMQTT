/*
  AlwaysOnDevice.ino
  HomeAssistantArduinoMQTT Library Example
  
  Demonstrates an always-on device (ESP32, ESP8266, AVR, SAMD) connected continuously.
  Registers a Temperature Sensor, Humidity Sensor, Relay Switch, Action Button
  and LED Mode Select with Home Assistant Auto-Discovery.

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

// Initialize HomeAssistantArduinoMQTT instance with capacity for 5 entities
HomeAssistantArduinoMQTT haMqtt(5);

// Options for the LED Mode select entity
const char* LED_MODES[] = {"Off", "On", "Blink"};
uint8_t ledMode = 0; // index into LED_MODES

// Entity handles, returned by publish(): setValue()/sendValue() with a handle
// reach the entity directly, without searching it by name
HAAMEntity temperatureEntity;
HAAMEntity humidityEntity;
HAAMEntity relayEntity;
HAAMEntity ledModeEntity;

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

                // Reflect updated state back to Home Assistant
                haMqtt.setValue(relayEntity, newState ? "true" : "false");
                haMqtt.sendValue(relayEntity);
                
                Serial.print(F("Relay state updated to: "));
                Serial.println(newState ? "ON" : "OFF");
            }
            // Handle Trigger Button command
            else if (strcmp(item, "restart_btn") == 0) {
                Serial.println(F("Restart Button pressed from Home Assistant! Rebooting..."));
                delay(500);
                rebootDevice();
            }
            // Handle LED Mode select command
            else if (strcmp(item, "led_mode") == 0) {
                for (uint8_t i = 0; i < sizeof(LED_MODES) / sizeof(LED_MODES[0]); i++) {
                    if (strcmp(value, LED_MODES[i]) == 0) {
                        ledMode = i;
                        haMqtt.setValue(ledModeEntity, LED_MODES[i]);
                        haMqtt.sendValue(ledModeEntity);

                        Serial.print(F("LED mode updated to: "));
                        Serial.println(LED_MODES[i]);
                        break;
                    }
                }
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
    temperatureEntity = tempSensor.publish();

    // 2. Humidity Sensor
    HAEntityBuilder humSensor = haMqtt.newSensorEntity("humidity", "Room Humidity");
    humSensor.deviceClass("humidity");
    humSensor.stateClass("measurement");
    humSensor.unit("%");
    humSensor.icon("mdi:water-percent");
    humSensor.suggestedDisplayPrecision(1);
    humidityEntity = humSensor.publish();

    // 3. Relay Switch
    HAEntityBuilder mainRelay = haMqtt.newSwitchEntity("main_relay", "Main Power Relay");
    mainRelay.icon("mdi:power-socket-eu");
    mainRelay.startup("false");
    relayEntity = mainRelay.publish();

    // 4. Restart Button
    HAEntityBuilder restartBtn = haMqtt.newButtonEntity("restart_btn", "Restart Device");
    restartBtn.icon("mdi:restart");
    restartBtn.category("diagnostic");
    restartBtn.publish();

    // 5. LED Mode Select
    HAEntityBuilder ledModeSelect = haMqtt.newSelectEntity("led_mode", "Status LED Mode");
    ledModeSelect.options(LED_MODES);
    ledModeSelect.icon("mdi:led-on");
    ledModeSelect.startup(LED_MODES[ledMode]);
    ledModeEntity = ledModeSelect.publish();
}

void updateStatusLed() {
    if (ledMode == 2) {
        // Blink: toggle every 500 ms
        digitalWrite(STATUS_LED, (millis() / 500) % 2 ? HIGH : LOW);
    } else {
        digitalWrite(STATUS_LED, ledMode == 1 ? HIGH : LOW);
    }
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
    haMqtt.mqttDeviceName = "climate_node_01";
    haMqtt.haDeviceName   = "Climate & Control Node";
    haMqtt.manufacturer   = "Custom DIY";
    haMqtt.model          = "MultiArch-AlwaysOn-Node";
    haMqtt.version        = "1.0.0";
    haMqtt.mqttUser       = MQTT_USER;
    haMqtt.mqttPassword   = MQTT_PASS;

    // Set callback listener for incoming Home Assistant commands
    haMqtt.setCallback(&mqttCallbackHandler);

    // Initialize MQTT client (the connection is opened by haMqtt.loop())
    haMqtt.begin(netClient, MQTT_BROKER, MQTT_PORT);
}

void loop() {
    // Reconnect Network if connection drops
    if (!isNetworkConnected()) {
        connectNetwork();
    }

    // Process MQTT loop (handles reconnection and incoming command callbacks)
    haMqtt.loop();

    // Publish the Home Assistant Discovery entities on every new connection:
    // publish() sends nothing while offline, and the broker may have lost
    // the retained configs while we were disconnected
    static bool wasConnected = false;
    bool isConnected = haMqtt.connected();
    if (isConnected && !wasConnected) setupEntities();
    wasConnected = isConnected;

    // Drive the status LED according to the selected mode
    updateStatusLed();

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
            haMqtt.setValue(temperatureEntity, strBuf);

            // Format and update Humidity
            HAAM_FORMAT_FLOAT(strBuf, hum, 1);
            haMqtt.setValue(humidityEntity, strBuf);

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