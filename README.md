# HomeAssistantArduinoMQTT

[![Arduino Library](https://img.shields.io/badge/Arduino-Library-00979D.svg)](https://github.com/Bettapro/HomeAssistantArduinoMQTT)
[![PlatformIO Compatible](https://img.shields.io/badge/PlatformIO-Compatible-orange.svg)](https://github.com/Bettapro/HomeAssistantArduinoMQTT)
[![Architectures](https://img.shields.io/badge/Architectures-ESP8266%20%7C%20ESP32-blue.svg)](https://github.com/Bettapro/HomeAssistantArduinoMQTT)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A flexible, lightweight C++ Arduino library designed to seamlessly interface **ESP8266** and **ESP32** devices with **Home Assistant** over **MQTT** using automatic MQTT Discovery.

---

## 🌟 Features

* **Home Assistant MQTT Discovery**: Automatically advertises device entities to Home Assistant. No manual YAML configuration required in `configuration.yaml`.
* **Entity Builder**: Clean C++ `HAEntityBuilder` helper for configuring sensors, binary sensors, switches, buttons, numbers, and selects.
* **Smart State & Availability**: Built-in support for shared device availability (via LWT - Last Will & Testament) as well as independent per-entity availability.
* **Optimized for Low Power & Deep Sleep**: Fast MQTT state publication allows battery-operated devices to send updates and re-enter deep sleep rapidly.
* **Custom Property Support**: Extend discovery payloads with custom JSON properties using flexible `.set()` key-value pairs.
* **Built on Modern Libraries**: Uses  `PubSubClient` for reliable MQTT communication.

---

## 📦 Dependencies

Only core MQTT client functionality is required:

| Library | Version | Description |
| :--- | :--- | :--- |
| [**PubSubClient**](https://github.com/knolleary/pubsubclient) | `>= 2.8` | Core MQTT client transport |

---

## ⚙️ Installation

### Arduino IDE
1. Download the latest `.zip` release from the [GitHub Repository](https://github.com/Bettapro/HomeAssistantArduinoMQTT).
2. Open Arduino IDE and navigate to **Sketch** -> **Include Library** -> **Add .ZIP Library...**.
3. Select the downloaded ZIP file.

### PlatformIO
Add `HomeAssistantArduinoMQTT` to your `platformio.ini` dependencies:

```ini
[env:esp32]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps =
    knolleary/PubSubClient @ ^2.8.0
    https://github.com/Bettapro/HomeAssistantArduinoMQTT.git

```

---

## 🚀 Quick Start

Here is a quick example demonstrating how to initialize the library, create a temperature sensor entity, and publish state updates.

```cpp
#include <WiFi.h>
#include <HomeAssistantArduinoMQTT.h>

WiFiClient net;
// Initialize with maximum 4 entities capacity
HomeAssistantArduinoMQTT haMqtt(4);

void setup() {
    Serial.begin(115200);
    WiFi.begin("YOUR_SSID", "YOUR_PASSWORD");
    while (WiFi.status() != WL_CONNECTED) { delay(500); }

    // Device metadata
    haMqtt.MQTTDeviceName = "living_room_node";
    haMqtt.HADeviceName   = "Living Room Sensor";
    haMqtt.Manufacturer   = "Custom DIY";
    haMqtt.Model          = "ESP32-Environment";
    haMqtt.Version        = "1.0.0";

    // Initialize MQTT connection (broker address, port)
    haMqtt.begin(net, "192.168.1.100", 1883);

    // Build and publish temperature sensor discovery config
    HAEntityBuilder tempSensor = haMqtt.newSensorEntity("temp", "Temperature");
    tempSensor.deviceClass("temperature");
    tempSensor.unit("°C");
    tempSensor.suggestedDisplayPrecision(1);
    tempSensor.publish();
}

void loop() {
    haMqtt.loop();

    static unsigned long lastSend = 0;
    if (millis() - lastSend > 10000) {
        lastSend = millis();

        float temperature = 22.5; // Read from physical sensor
        
        char buf[16];
        HAAM_FORMAT_FLOAT(buf, temperature, 1);
        
        haMqtt.setValue("temp", buf);
        haMqtt.sendValues(); // Publish updated state to MQTT
    }
}

```

---

## 📖 API Reference & Usage Guide

### 1. Initialization & Configuration

The main controller instance takes the maximum number of entities allowed for the device.

```cpp
HomeAssistantArduinoMQTT haMqtt(capacity);
```

#### Configurable Properties

| Property | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `MQTTDeviceName` | `const char*` | `""` | Base identifier used for MQTT topics and client ID. |
| `HADeviceName` | `const char*` | `""` | Friendly device name displayed in Home Assistant UI. |
| `Manufacturer` | `const char*` | `""` | Manufacturer tag for device info. |
| `Model` | `const char*` | `""` | Model identifier. |
| `Version` | `const char*` | `""` | Firmware/software version string. |
| `ConfigurationUrl` | `const char*` | `""` | Web page or admin dashboard link (optional). |
| `MqttUser` | `const char*` | `""` | MQTT broker username. |
| `MqttPassword` | `const char*` | `""` | MQTT broker password. |
| `useSharedAvailability` | `bool` | `true` | Publishes global LWT online/offline status. |
| `prefixUniqueIds` | `bool` | `true` | Prefixes entity unique IDs with the device name. |
| `enableConfigPublishing`| `bool` | `true` | Enables auto-discovery payload transmission. |
| `commandEnabled` | `bool` | `true` | Enables subscription to incoming commands. |

#### Connection Setup
```cpp
void begin(Client& client, const char* server, const uint16_t port);
void begin(Client& client, const char* server, const uint16_t port, const uint16_t bufferSize, const uint16_t keepAlive);
```

---

### 2. Entity Builder (`HAEntityBuilder`)

The library offers helper functions to instantiate entity builders for common Home Assistant domains:

* `newSensorEntity(id, name)`: Read-only numerical or text sensor.
* `newBinarySensorEntity(id, name)`: Two-state sensor (`true`/`false` -> `ON`/`OFF`).
* `newSwitchEntity(id, name)`: Controllable switch (`true`/`false`).
* `newButtonEntity(id, name)`: Trigger button action (`PRESS`).
* `newNumberEntity(id, name)`: Numeric slider or input box.
* `newSelectEntity(id, name)`: Dropdown selection entity.
* `newEntity(type, id, name)`: Generic builder for custom domains.

#### Configuring Entity Properties

Instantiate an `HAEntityBuilder` variable and call setting methods before calling `.publish()`:

```cpp
HAEntityBuilder humidity = haMqtt.newSensorEntity("humidity", "Room Humidity");
humidity.category("diagnostic");          // "diagnostic", "config", or nullptr
humidity.deviceClass("humidity");         // e.g., "temperature", "humidity", "battery"
humidity.stateClass("measurement");       // "measurement", "total", "total_increasing"
humidity.unit("%");                       // Unit of measurement badge
humidity.icon("mdi:water-percent");       // Material Design Icon
humidity.suggestedDisplayPrecision(1);    // Decimal precision in UI
humidity.startup("50.0");                 // Initial default value
humidity.independentAvailability(true);   // Has specific entity availability topic
humidity.set("min", 0);                   // Custom JSON property (int)
humidity.set("max", 100);                 // Custom JSON property (int)

humidity.publish();                       // Transmits Home Assistant Discovery JSON
```

---

### 3. Updating & Transmitting States

Values are stored internally in string format and published when triggered.

```cpp
// Set internal entity value without sending immediately
haMqtt.setValue("temp", "23.4");

// Transmit all modified entity values to MQTT
haMqtt.sendValues();

// Force publish all entity values regardless of whether they changed
haMqtt.sendValues(true);

// Transmit a specific entity value
haMqtt.sendValue("temp");

// Get the current cached string value for an entity
const char* val = haMqtt.getValue("temp");
```

---

### 4. Handling Commands & Subscriptions

To handle incoming control commands from Home Assistant (e.g. toggling a switch or pressing a button), implement the `HAMQTTCallback` interface:

```cpp
class CommandHandler : public HAMQTTCallback {
public:
    void onMQTTMessage(const char* item, const char* value, bool isState) override {
        Serial.print("Received MQTT item: ");
        Serial.print(item);
        Serial.print(" | Value: ");
        Serial.println(value);

        if (!isState) {
            // Incoming command from Home Assistant
            if (strcmp(item, "relay_switch") == 0) {
                bool state = (strcmp(value, "true") == 0);
                digitalWrite(RELAY_PIN, state ? HIGH : LOW);
                
                // Mirror updated state back to Home Assistant
                haMqtt.setValue("relay_switch", state ? "true" : "false");
                haMqtt.sendValue("relay_switch");
            }
        }
    }
};

CommandHandler cmdHandler;

void setup() {
    // ...
    haMqtt.setCallback(&cmdHandler);
}
```

---

### 5. Formatting Helper Macros

The library provides convenient C-style helper macros for formatting values into string buffers:

```cpp
char buf[32];

HAAM_FORMAT_BOOL(buf, true);         // "true"
HAAM_FORMAT_UINT(buf, 1024);         // "1024"
HAAM_FORMAT_FLOAT(buf, 23.456, 2);   // "23.46"
HAAM_FORMAT_STR(buf, "OK");          // "OK"

```

---

## 📄 License

This library is released under the [MIT License](LICENSE).

**Author:** Alberto Bettin  
**Repository:** [https://github.com/Bettapro/HomeAssistantArduinoMQTT](https://github.com/Bettapro/HomeAssistantArduinoMQTT)
