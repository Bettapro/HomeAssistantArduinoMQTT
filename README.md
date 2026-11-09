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
* **Multi-Device (optional)**: Expose several Home Assistant devices (a gateway and its children) over a single MQTT connection.
* **Device Discovery (optional)**: Publish a whole device and its entities as one message on a dedicated discovery topic.
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
    haMqtt.mqttDeviceName = "living_room_node";
    haMqtt.haDeviceName   = "Living Room Sensor";
    haMqtt.manufacturer   = "Custom DIY";
    haMqtt.model          = "ESP32-Environment";
    haMqtt.version        = "1.0.0";

    // Initialize MQTT client (broker address, port); haMqtt.loop() connects
    haMqtt.begin(net, "192.168.1.100", 1883);
}

// Discovery configs are sent only while connected: publish them on every (re)connection
void publishEntities() {
    HAEntityBuilder tempSensor = haMqtt.newSensorEntity("temp", "Temperature");
    tempSensor.deviceClass("temperature");
    tempSensor.unit("°C");
    tempSensor.suggestedDisplayPrecision(1);
    tempSensor.publish();
}

void loop() {
    haMqtt.loop();

    static bool wasConnected = false;
    bool isConnected = haMqtt.connected();
    if (isConnected && !wasConnected) publishEntities();
    wasConnected = isConnected;

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
| `mqttDeviceName` | `const char*` | `""` | Base identifier used for MQTT topics and client ID. |
| `haDeviceName` | `const char*` | `""` | Friendly device name displayed in Home Assistant UI. |
| `manufacturer` | `const char*` | `""` | Manufacturer tag for device info. |
| `model` | `const char*` | `""` | Model identifier. |
| `version` | `const char*` | `""` | Firmware/software version string. |
| `configurationUrl` | `const char*` | `""` | Web page or admin dashboard link (optional). |
| `mqttUser` | `const char*` | `""` | MQTT broker username. |
| `mqttPassword` | `const char*` | `""` | MQTT broker password. |
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

#### Select Entities

A select requires a list of options. Pass a `const char*` array (it must stay valid until `.publish()` is called):

```cpp
const char* modes[] = {"Off", "Eco", "Comfort"};

HAEntityBuilder mode = haMqtt.newSelectEntity("mode", "Heating Mode");
mode.options(modes);                      // or mode.options(modes, 3);
mode.icon("mdi:radiator");
mode.startup("Off");
mode.publish();
```

When an option is chosen in Home Assistant, `onMQTTMessage` receives the option text as `value` (with `isState == false`). Reflect it back with `setValue("mode", value)` + `sendValue("mode")`.

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

#### Entity Availability

Entities with `independentAvailability(true)` (the default for the `newXxxEntity()` helpers) have their own `avty` topic. Availability and value are independent:

* `setValue(id, "...")` with a non-empty value marks the entity **available**; an empty value (or `nullptr`) marks it **unavailable**.
* `setEntityAvailability(id, false)` marks the entity **unavailable** and keeps its value (`getValue()` still returns it). The next `setValue()` makes it available again, even with the same value.
* `setEntityAvailability(id, true)` marks it available, also without a value (Home Assistant then shows "unknown").
* On the first `publish()`, an entity is available if it already has a value (set before `publish()`) or a `startup()` value. Entities without a state topic (e.g. buttons) start available. Later `publish()` calls do not change availability or overwrite a stored value with `startup()`.
* With `useSharedAvailability` (default), an entity is also unavailable whenever the device is offline (LWT).
* States received via `readValues()` update the stored value but never availability.

```cpp
// e.g. a sensor read over a bus that can fail
if (readOk) haMqtt.setValue("temp", buf);
else        haMqtt.setEntityAvailability("temp", false);
haMqtt.sendValue("temp");
```

#### Entity Handles (`HAAMEntity`)

Every call with an entity name searches the entity table (and sanitizes the name when it is not already sanitized). For entities updated often, keep the handle returned by `publish()` and pass it instead of the name: the slot is reached directly, and the handle already knows its device.

```cpp
HAAMEntity tempEntity;

void setupEntities() {
    HAEntityBuilder t = haMqtt.newSensorEntity("temp", "Temperature");
    t.unit("°C");
    tempEntity = t.publish();   // valid also when offline
}

void loop() {
    haMqtt.setValue(tempEntity, buf);
    haMqtt.sendValue(tempEntity);
}
```

* Handle overloads: `setValue`, `getValue`, `sendValue`, `clearSetTopic`, `setEntityAvailability`.
* `getEntity(id)` / `getEntity(device, id)` finds an existing entity (published or given a value) and never allocates a slot.
* `entityDevice(handle)` and `entityId(handle)` return the device index and the sanitized id.
* A handle is invalid (`if (!handle)`, `handle.isValid()`) when the entity table is full or the id is empty; calls with an invalid handle do nothing. Slots are never released, so a valid handle stays valid.
* Handles are internal: topics and discovery ids still use the entity names.

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

`HAAM_FORMAT_FLOAT` does not use `printf`, so it works on AVR too (whose `printf` has no `%f` support) and does not pull `vfprintf` into the firmware. At most 6 decimals. Decimals are reduced when the value does not fit 32 bits. NaN and infinity give an empty string, which makes the entity unavailable.

---

### 6. Optional Features (Build Flags)

Optional features are enabled or removed with compiler directives. They change the library's class layout, so **they must be defined for the whole build** (library included). A `#define` in the sketch is not enough: the library is compiled separately and would not see it.

| Directive | Effect |
| :--- | :--- |
| `HAAM_MAX_DEVICES=N` | `0` or `1` (default) = single device. `N > 1` = the root device plus up to `N-1` child devices on the same MQTT connection. |
| `HAAM_DEVICE_DISCOVERY` | Adds `publishDevice()`: a whole device (device info + all its entities) is published as a single retained message on `homeassistant/device/<device id>/config`. |
| `HAAM_NO_COMMANDS` | For devices that only publish (sensors, deep sleep). Removes inbound message handling and the `set`/`state` subscriptions, together with `setCallback()`, `readValues()` and `commandEnabled`. Commands sent by Home Assistant are ignored. Saves about 1.1 KB of flash on AVR. |

```ini
; PlatformIO
build_flags = -DHAAM_MAX_DEVICES=3 -DHAAM_DEVICE_DISCOVERY
```
```bash
# arduino-cli
arduino-cli compile --build-property "compiler.cpp.extra_flags=-DHAAM_MAX_DEVICES=3 -DHAAM_DEVICE_DISCOVERY" ...
```

The two features are independent and can be combined. See [`examples/GatewayMultiDevice`](examples/GatewayMultiDevice/GatewayMultiDevice.ino).

#### Multi-device (`HAAM_MAX_DEVICES`)

The device configured with the usual properties (`mqttDeviceName`, `haDeviceName`, ...) is the **root device** (index `HAAM_ROOT_DEVICE`, i.e. `0`). It owns the MQTT connection and the LWT. Child devices are linked to it in Home Assistant through `via_device`, as in a gateway.

```cpp
uint8_t boiler = haMqtt.addDevice("boiler_01", "Boiler");   // HAAM_NO_DEVICE on failure
HAAMDevice* d = haMqtt.getDevice(boiler);
d->manufacturer    = "ACME";
d->model           = "Boiler 3000";
d->useAvailability = true;            // optional per-device availability

HAEntityBuilder t = haMqtt.newSensorEntity(boiler, "temperature", "Temperature");
t.unit("°C");
t.publish();

haMqtt.setValue(boiler, "temperature", "61.5");
haMqtt.setDeviceAvailability(boiler, false);   // e.g. the bus does not answer
haMqtt.sendValues();
```

* Every `newXxxEntity`, `setValue`, `getValue`, `sendValue`, `setEntityAvailability`, `clearSetTopic`, `sendCommand` and `sendEvent` has an overload that takes the device index first. The overloads without it work on the root device, so existing code compiles unchanged.
* Entity ids are scoped per device: two children can both have a `temperature` entity. The capacity passed to the constructor is shared by all devices.
* Topics: `haam/<device id>/<entity>/{state,set,avty}`. Child device ids must be unique and different from the root's.
* Availability of a child entity = connection (LWT) + child device (`useAvailability`) + entity (`independentAvailability`), combined with `avty_mode: all`.
* Callback: override `onMQTTMessage(uint8_t device, const char* item, const char* value, bool isState)` to receive the messages of every device. The 3-argument version only receives those of the root device.
* RAM cost: one byte per entity slot, plus `HAAM_DEVICE_LEN + ~12` bytes per child device.

#### Device discovery (`HAAM_DEVICE_DISCOVERY`)

The entities of a device are declared in a function, and `publishDevice()` sends them in one message:

```cpp
void declareBoiler(HomeAssistantArduinoMQTT& ha, uint8_t dev) {
    HAEntityBuilder t = ha.newSensorEntity(dev, "temperature", "Temperature");
    t.unit("°C");
    t.publish();          // adds the entity to the device message
}

// once connected (and on every reconnection)
haMqtt.publishDevice(boiler, declareBoiler);   // multi-device
haMqtt.publishDevice(declareRoot);             // root device
```

Without `HAAM_MAX_DEVICES`, only `publishDevice(declare)` exists, and the declarer uses the overloads without the device index (`ha.newSensorEntity("temperature", ...)`).

The payload is streamed and never buffered, so its size is not limited by the PubSubClient buffer. To stream it, the library first measures the payload and then writes it, so **the function is called more than once**. It must declare the same entities in the same order every time, and must not send MQTT messages. When offline, `publishDevice()` only registers the entities and returns `false`.

`publish()` returns the entity handle inside a declarer too. During the measuring pass the entity is not registered yet, so the first time the handle is invalid: keep only valid ones (`HAAMEntity e = t.publish(); if (e) boilerTemp = e;`), or call `getEntity(dev, "temperature")` after `publishDevice()`.

Notes:
* Unique ids are the same as with per-entity discovery (`<device id>_<entity>`).
* Switching an existing installation from per-entity discovery requires the [Home Assistant migration procedure](https://www.home-assistant.io/integrations/mqtt/): send `{"migrate_discovery": true}` to the old `homeassistant/<type>/<device>/<entity>/config` topics, publish the device, then clear the old topics with an empty retained payload.
* To remove an entity, publish the device once with that entity reduced to its platform only (`{"p": "sensor"}`), then stop declaring it. An empty payload on the old per-entity topic does not remove it.

---

## 🔁 Migrating from 1.x to 2.0

The configuration properties now use camelCase. Only the names changed:

| 1.x | now |
| :--- | :--- |
| `MQTTDeviceName` | `mqttDeviceName` |
| `HADeviceName` | `haDeviceName` |
| `Manufacturer` | `manufacturer` |
| `Model` | `model` |
| `Version` | `version` |
| `ConfigurationUrl` | `configurationUrl` |
| `MqttUser` | `mqttUser` |
| `MqttPassword` | `mqttPassword` |

---

## 📄 License

This library is released under the [MIT License](LICENSE).

**Author:** Alberto Bettin  
**Repository:** [https://github.com/Bettapro/HomeAssistantArduinoMQTT](https://github.com/Bettapro/HomeAssistantArduinoMQTT)
