## 🚀 Release v2.0.0 – Multi-Device, Device Discovery & Entity Handles

A major release that lets one MQTT connection expose several Home Assistant devices, adds single-message device discovery, faster entity access through handles, and new build flags to trim the library on small boards. ⚠️ It contains a **breaking change** in the configuration property names (see below).

### ⚠️ Breaking Changes

* 🔤 **Configuration properties** now use camelCase. Only the names changed, the behaviour is the same:

| 1.x | 2.0 |
| :--- | :--- |
| `MQTTDeviceName` | `mqttDeviceName` |
| `HADeviceName` | `haDeviceName` |
| `Manufacturer` | `manufacturer` |
| `Model` | `model` |
| `Version` | `version` |
| `ConfigurationUrl` | `configurationUrl` |
| `MqttUser` | `mqttUser` |
| `MqttPassword` | `mqttPassword` |

* 🧩 **Entity builder**: `HAEntityBuilder::publish()` now returns an `HAAMEntity` handle instead of `void` (existing code that ignores the return value compiles unchanged).
* 🔑 **`HAKeys`** constants are now all in flash (`PROGMEM`). Code passing e.g. `HAKeys::TYPE_SENSOR` to `newEntity()` must use the `newSensorEntity()` helpers (or a plain string) instead.
* 📏 **AVR defaults**: `HAAM_ITEM_LEN` and `HAAM_VALUE_LEN` are now 24 on AVR (32 elsewhere), and `begin(client, server, port)` keeps the PubSubClient 256-byte buffer on AVR (1024 elsewhere). Override them with build flags if you need longer ids, values or credentials.

### 🔧 Key Changes

* 🏘️ **Multi-device** (`HAAM_MAX_DEVICES=N`): expose a root device plus up to `N-1` child devices over a single MQTT connection, linked in Home Assistant through `via_device` (gateway style). New `addDevice()`, `getDevice()`, `setDeviceAvailability()` and device-index overloads for every entity method; per-device availability and per-device entity ids.
* 📦 **Device discovery** (`HAAM_DEVICE_DISCOVERY`): new `publishDevice()` sends a whole device and all its entities as one retained message on `homeassistant/device/<id>/config`, including the `origin` info. The payload is streamed, so it is not limited by the PubSubClient buffer.
* 🎯 **Entity handles** (`HAAMEntity`): keep the handle returned by `publish()` (or `getEntity()`) and pass it to `setValue`, `getValue`, `sendValue`, `clearSetTopic` and `setEntityAvailability` to reach the entity directly, without searching by name.
* 🔇 **Publish-only builds** (`HAAM_NO_COMMANDS`): removes inbound message handling and subscriptions for sensors and deep-sleep devices, saving about 1.1 KB of flash on AVR.
* 📋 **Select entities**: new `options()` on the entity builder to declare the list of choices.
* 🔢 **AVR**: `HAAM_FORMAT_FLOAT` no longer uses `printf`, so it now works on AVR and does not pull `vfprintf` into the firmware. NaN/infinity give an empty value (entity unavailable).
* 🐛 **Availability**: fixed entity availability handling with `setValue()`. Value and availability are now independent: `setEntityAvailability(id, false)` keeps the stored value, and the next `setValue()` makes the entity available again.
* 🧪 **Examples & CI**: new `GatewayMultiDevice` example; CI now also compiles the multi-device / device discovery and `HAAM_NO_COMMANDS` configurations.
* 💾 **RAM / flash on small boards**: every string setter of the builder and both key and value of `set()` accept `F("...")`, so names, units, icons and classes can stay in flash; all internal keys moved to flash; PubSubClient is now a member (no heap allocation) and the status topic buffer is gone. On an Arduino Uno the `AlwaysOnDevice` example uses about 1.1 KB less RAM (it did not fit before) and 344 bytes less flash.
* 🔌 **`disconnect()`**: closes the MQTT session cleanly, optionally reporting the device offline first.
* 🐛 **Fixes**:
  * `set(key, value)` produced corrupted discovery JSON on AVR with plain strings (they were read from flash).
  * `clearSetTopic()` now really clears the retained command (retained empty message), and empty inbound payloads are ignored, so clearing no longer reaches the callback as a command (e.g. a switch turned off).
  * `readValues()` no longer stops working with `commandEnabled = false`; restored states only update entities known to the library (they no longer fill the entity table) and never replace a local value not sent yet.
  * Empty `mqttUser` / `mqttPassword` are no longer sent as credentials (brokers requiring authentication for named users rejected the connection).
  * The AVR/SAMD callback is no longer a global `mqttCallback()` function, which clashed with sketches defining their own.
  * Child device entities always get prefixed unique ids (no collisions with `prefixUniqueIds = false`); the root device omits empty device fields and falls back to its id as name; no empty `avty` list; `sendEvent()` escapes the event type and never sends truncated JSON; number formatters return an empty string instead of a truncated number.
  * `DeepSleepDevice` example: no LWT (entities used to become unavailable at every sleep), `expire_after` instead.
* 📘 **README**: rewritten API reference, with sections on availability, entity handles, build flags and a 1.x → 2.0 migration guide.

> ℹ️ The optional features change the class layout, so the build flags must be set for the **whole build** (PlatformIO `build_flags`, arduino-cli `--build-property`), not with a `#define` in the sketch.

📘 **Full Changelog**: [Compare v1.0.0 → v2.0.0](https://github.com/Bettapro/HomeAssistantArduinoMQTT/compare/v1.0.0...v2.0.0)
