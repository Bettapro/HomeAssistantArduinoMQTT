/*
  DeepSleepDevice.ino
  HomeAssistantArduinoMQTT Library Example
  
  Demonstrates a battery-powered device (ESP32 or ESP8266) using Deep Sleep.
  Wakes up, connects to Wi-Fi/MQTT, advertises entities to Home Assistant,
  publishes sensor data, and immediately re-enters deep sleep to conserve battery.

  Author: Alberto Bettin
  Repository: https://github.com/Bettapro/HomeAssistantArduinoMQTT
*/

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  #define REST_SLEEP_US(us) ESP.deepSleep(us)
#elif defined(ESP32)
  #include <WiFi.h>
  #define REST_SLEEP_US(us) esp_sleep_enable_timer_wakeup(us); esp_deep_sleep_start()
#else
  #error "Board not supported! Please select ESP8266 or ESP32."
#endif

#include <HomeAssistantArduinoMQTT.h>

// ==========================================
// CONFIGURATION - Update with your settings
// ==========================================
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const char* MQTT_BROKER   = "192.168.1.100";
const uint16_t MQTT_PORT  = 1883;
const char* MQTT_USER     = "mqtt_user";
const char* MQTT_PASS     = "mqtt_password";

// Deep Sleep duration: 15 minutes (in microseconds)
const uint64_t SLEEP_TIME_US = 15ULL * 60ULL * 1000000ULL;

// Hardware ADC Pin for battery reading
#if defined(ESP32)
  const uint8_t BATT_ADC_PIN = 34;
#else
  const uint8_t BATT_ADC_PIN = A0;
#endif

// ==========================================
// GLOBALS
// ==========================================
WiFiClient netClient;

// Initialize library with 3 entity slots (Temp, Humidity, Battery)
HomeAssistantArduinoMQTT haMqtt(3);

// ==========================================
// HELPER FUNCTIONS
// ==========================================
bool connectWiFiFast() {
    Serial.print(F("Connecting to Wi-Fi... "));
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
        delay(100);
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.print(F("Connected! IP: "));
        Serial.println(WiFi.localIP());
        return true;
    } else {
        Serial.println(F("Connection Failed!"));
        return false;
    }
}

float readBatteryVoltage() {
    int raw = analogRead(BATT_ADC_PIN);
#if defined(ESP32)
    float voltage = (raw / 4095.0) * 3.3 * 2.0;
#else
    float voltage = (raw / 1023.0) * 4.2;
#endif
    return voltage;
}

float readTemperatureSensor() {
    return 20.4;
}

float readHumiditySensor() {
    return 48.2;
}

// ==========================================
// MAIN SETUP (Deep Sleep Pattern)
// ==========================================
void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println(F("\n--- HomeAssistantArduinoMQTT: Deep Sleep Battery Node ---"));

    // 1. Read sensors immediately upon wake up
    float temperature = readTemperatureSensor();
    float humidity    = readHumiditySensor();
    float batteryVolts = readBatteryVoltage();

    Serial.print(F("Sensors Read -> Temp: "));
    Serial.print(temperature, 1);
    Serial.print(F(" °C | Humidity: "));
    Serial.print(humidity, 1);
    Serial.print(F(" % | Battery: "));
    Serial.print(batteryVolts, 2);
    Serial.println(F(" V"));

    // 2. Connect to Wi-Fi network
    if (!connectWiFiFast()) {
        Serial.println(F("Going back to deep sleep due to Wi-Fi failure."));
        REST_SLEEP_US(SLEEP_TIME_US);
        return;
    }

    // 3. Configure MQTT Device settings
    haMqtt.MQTTDeviceName = "garden_climate_node";
    haMqtt.HADeviceName   = "Garden Climate Sensor";
    haMqtt.Manufacturer   = "Custom DIY";
    haMqtt.Model          = "ESP-DeepSleep-Sensors";
    haMqtt.Version        = "1.0.0";
    haMqtt.MqttUser       = MQTT_USER;
    haMqtt.MqttPassword   = MQTT_PASS;
    
    // Command subscription is disabled for deep sleep device to save network overhead
    haMqtt.commandEnabled = false;

    // Initialize MQTT client
    haMqtt.begin(netClient, MQTT_BROKER, MQTT_PORT);

    // Maintain loop until MQTT connects
    unsigned long mqttStart = millis();
    while (!haMqtt.connected() && millis() - mqttStart < 5000) {
        haMqtt.loop();
        delay(50);
    }

    if (haMqtt.connected()) {
        Serial.println(F("MQTT Connected. Registering Home Assistant Auto-Discovery entities..."));

        // Register Temperature Sensor
        HAEntityBuilder tempSensor = haMqtt.newSensorEntity("temperature", "Temperature");
        tempSensor.deviceClass("temperature");
        tempSensor.stateClass("measurement");
        tempSensor.unit("°C");
        tempSensor.suggestedDisplayPrecision(1);
        tempSensor.publish();

        // Register Humidity Sensor
        HAEntityBuilder humSensor = haMqtt.newSensorEntity("humidity", "Humidity");
        humSensor.deviceClass("humidity");
        humSensor.stateClass("measurement");
        humSensor.unit("%");
        humSensor.suggestedDisplayPrecision(1);
        humSensor.publish();

        // Register Battery Voltage Sensor
        HAEntityBuilder battSensor = haMqtt.newSensorEntity("battery_voltage", "Battery Voltage");
        battSensor.deviceClass("voltage");
        battSensor.stateClass("measurement");
        battSensor.category("diagnostic");
        battSensor.unit("V");
        battSensor.suggestedDisplayPrecision(2);
        battSensor.publish();

        // Format and set entity values
        char buf[16];

        HAAM_FORMAT_FLOAT(buf, temperature, 1);
        haMqtt.setValue("temperature", buf);

        HAAM_FORMAT_FLOAT(buf, humidity, 1);
        haMqtt.setValue("humidity", buf);

        HAAM_FORMAT_FLOAT(buf, batteryVolts, 2);
        haMqtt.setValue("battery_voltage", buf);

        // Send all values forcibly
        Serial.println(F("Publishing state values to MQTT..."));
        haMqtt.sendValues(true);

        // Process loop briefly to ensure MQTT network packets are fully flushed
        for (int i = 0; i < 10; i++) {
            haMqtt.loop();
            delay(20);
        }

        Serial.println(F("Data successfully published to Home Assistant!"));
    } else {
        Serial.println(F("Failed to connect to MQTT broker within timeout."));
    }

    // 4. Clean up connections before entering sleep
    WiFi.disconnect(true);
    Serial.println(F("Entering deep sleep... Goodnight!"));
    Serial.flush();

    // 5. Trigger Deep Sleep
    REST_SLEEP_US(SLEEP_TIME_US);
}

void loop() {
    // Loop is never reached in Deep Sleep mode as board resets upon wake up
}
