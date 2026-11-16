#pragma once

#include <Client.h>

#include "Arduino.h"
#include "PubSubClient.h"

// ---------------------------------------------------------------------------
// Payload length type of the PubSubClient callback: size_t in PubSubClient3,
// unsigned int in the original knolleary/PubSubClient 2.x. MQTT_QOS0 is only
// defined by PubSubClient3. The two types are the same on AVR/ESP8266/ESP32,
// but not everywhere, so the callback signature follows the client in use.
// ---------------------------------------------------------------------------
#ifdef MQTT_QOS0
typedef size_t haam_len_t;
#else
typedef unsigned int haam_len_t;
#endif

// ---------------------------------------------------------------------------
//  Sizes. Like the optional features below, they change the class layout:
//  override them for the whole build (build_flags / --build-property).
// ---------------------------------------------------------------------------
#ifndef HAAM_ITEM_LEN
#if defined(ARDUINO_ARCH_AVR)
#define HAAM_ITEM_LEN 24  // max sanitized entity id length + 1
#else
#define HAAM_ITEM_LEN 32
#endif
#endif
#ifndef HAAM_VALUE_LEN
#if defined(ARDUINO_ARCH_AVR)
#define HAAM_VALUE_LEN 24  // max payload length kept per entity + 1
#else
#define HAAM_VALUE_LEN 32
#endif
#endif
#ifndef HAAM_DEVICE_LEN
#define HAAM_DEVICE_LEN 32  // max sanitized device name length + 1
#endif
#ifndef HAAM_TOPIC_LEN
#define HAAM_TOPIC_LEN 112  // shared scratch topic buffer
#endif
#ifndef HAAM_PAYLOAD_LEN
#define HAAM_PAYLOAD_LEN 64  // inbound payload / event payload buffer
#endif
#ifndef HAAM_MAX_CUSTOM_PROPS
#define HAAM_MAX_CUSTOM_PROPS 6  // per-entity extra config keys
#endif
#ifndef HAAM_JSON_CHUNK
#if defined(ARDUINO_ARCH_AVR)
#define HAAM_JSON_CHUNK 32  // TCP write coalescing window (stack)
#else
#define HAAM_JSON_CHUNK 128
#endif
#endif
// longest topic: homeassistant/binary_sensor/<device id>/<entity id>/config
#if HAAM_TOPIC_LEN < HAAM_DEVICE_LEN + HAAM_ITEM_LEN + 35
#error "HAAM_TOPIC_LEN is too small for HAAM_DEVICE_LEN and HAAM_ITEM_LEN: topics would be truncated"
#endif
#if HAAM_JSON_CHUNK < 1 || HAAM_JSON_CHUNK > 255
#error "HAAM_JSON_CHUNK must be between 1 and 255"
#endif

// PubSubClient buffer used by begin(client, server, port). Discovery payloads
// are streamed, so the buffer only has to hold the CONNECT packet, one state
// publish and one inbound command. On AVR the PubSubClient default (256) is
// kept: no reallocation, and 768 bytes of RAM less than 1024.
#ifndef HAAM_MQTT_BUFFER_SIZE
#if defined(ARDUINO_ARCH_AVR)
#define HAAM_MQTT_BUFFER_SIZE MQTT_MAX_PACKET_SIZE
#else
#define HAAM_MQTT_BUFFER_SIZE 1024
#endif
#endif

// With readValues(), time after a (re)connection during which sendValues()
// holds back the values that were not changed locally (startup() defaults),
// so that the retained states kept by the broker can replace them first.
// Values set with setValue() and forced sends are never delayed. 0 = no wait.
#ifndef HAAM_RESTORE_WINDOW_MS
#define HAAM_RESTORE_WINDOW_MS 500
#endif

// ---------------------------------------------------------------------------
//  Optional features. They change the class layout, so they must be defined
//  for the whole build (PlatformIO build_flags, arduino-cli --build-property),
//  never with a #define in the sketch.
// ---------------------------------------------------------------------------

// Number of Home Assistant devices exposed by this MQTT connection.
// 0 or 1 = multi-device disabled (default). N > 1 = root device + N-1 child
// devices, each linked to the root through "via_device".
#ifndef HAAM_MAX_DEVICES
#define HAAM_MAX_DEVICES 1
#endif
#if HAAM_MAX_DEVICES > 255
#error "HAAM_MAX_DEVICES must be <= 255"
#endif
#if HAAM_MAX_DEVICES > 1
#define HAAM_MULTI_DEVICE
#endif

// Define HAAM_DEVICE_DISCOVERY to enable publishDevice(): the whole device
// (device info + all its entities) is published as a single retained message
// on <prefix>/device/<device id>/config (Home Assistant device discovery).

// Define HAAM_NO_COMMANDS for devices that only publish (sensors, deep sleep):
// removes inbound message handling, the set/state subscriptions, setCallback(),
// readValues() and commandEnabled. Commands sent by Home Assistant are ignored.

#define HAAM_ROOT_DEVICE 0
#define HAAM_NO_DEVICE 0xFF

#define HAAM_LIB_NAME "HomeAssistantArduinoMQTT"
#define HAAM_LIB_VERSION "2.1.0"
#define HAAM_LIB_URL "https://github.com/Bettapro/HomeAssistantArduinoMQTT"

#if defined(ESP8266) || defined(ESP32)
#define MQTT_CALLBACK_FUNCTIONAL
#endif

namespace HAAMFmt {
// The number and bool formatters write an empty string (and return 0) when
// the result does not fit the buffer: a truncated number would be a wrong
// value, an empty value makes the entity unavailable. strToBuf truncates.
int uintToBuf(char* buf, size_t size, unsigned long val);
int strToBuf(char* buf, size_t size, const char* val);
int boolToBuf(char* buf, size_t size, bool val);  // "true" / "false"
// Fixed-point formatting without printf (AVR's printf has no %f support).
// dec is capped at 6 and reduced when the value would not fit 32 bits;
// NaN, infinity and |val| >= 2^32 produce an empty string.
int floatToBuf(char* buf, size_t size, double val, uint8_t dec);
}  // namespace HAAMFmt

#define HAAM_FORMAT_BOOL(buf, val) \
    HAAM_FORMAT_BOOL_SIZE(buf, sizeof(buf), val)

#define HAAM_FORMAT_BOOL_SIZE(buf, size, val) \
    HAAMFmt::boolToBuf(buf, size, (bool)(val))

#define HAAM_FORMAT_UINT(buf, val) \
    HAAM_FORMAT_UINT_SIZE(buf, sizeof(buf), val)

#define HAAM_FORMAT_UINT_SIZE(buf, size, val) \
    HAAMFmt::uintToBuf(buf, size, (unsigned long)(val))

#define HAAM_FORMAT_FLOAT(buf, val, dec) \
    HAAM_FORMAT_FLOAT_SIZE(buf, sizeof(buf), val, dec)

#define HAAM_FORMAT_FLOAT_SIZE(buf, size, val, dec) \
    HAAMFmt::floatToBuf(buf, size, (double)(val), (uint8_t)(dec))

#define HAAM_FORMAT_STR(buf, val) \
    HAAM_FORMAT_STR_SIZE(buf, sizeof(buf), val)

#define HAAM_FORMAT_STR_SIZE(buf, size, val) \
    HAAMFmt::strToBuf(buf, size, (const char*)(val))

class HAMQTTCallback {
    public:
        virtual ~HAMQTTCallback() = default;
#ifdef HAAM_MULTI_DEVICE
        // Messages of the root device only; override the overload below to
        // receive the messages of every device.
        virtual void onMQTTMessage(const char* item, const char* value, bool isState) {
            (void)item;
            (void)value;
            (void)isState;
        }
        virtual void onMQTTMessage(uint8_t device, const char* item, const char* value, bool isState) {
            if (device == HAAM_ROOT_DEVICE) onMQTTMessage(item, value, isState);
        }
#else
        virtual void onMQTTMessage(const char* item, const char* value, bool isState) = 0;
#endif
};

#ifdef HAAM_MULTI_DEVICE
// Child device, created with HomeAssistantArduinoMQTT::addDevice().
struct HAAMDevice {
        const char* name;  // name shown in Home Assistant
        const char* manufacturer;
        const char* model;
        const char* version;
        const char* configurationUrl;

        // adds haam/<id>/avty to the availability of every entity of this
        // device, driven by setDeviceAvailability()
        bool useAvailability;

        char id[HAAM_DEVICE_LEN];  // sanitized id, used in topics and identifiers
        uint8_t lastAvailable : 1;
        uint8_t availabilitySent : 1;
};
#endif

// Handle of an entity slot, returned by HAEntityBuilder::publish() and
// getEntity(). It already identifies the device, and the handle overloads
// (setValue, getValue, sendValue, ...) reach the slot directly, without
// searching by name. Slots are never released, so a valid handle stays valid
// for the lifetime of the HomeAssistantArduinoMQTT instance.
struct HAAMEntity {
        int16_t index;

        HAAMEntity() : index(-1) {}
        explicit HAAMEntity(int16_t i) : index(i) {}

        bool isValid() const { return index >= 0; }
        explicit operator bool() const { return index >= 0; }
        bool operator==(const HAAMEntity& o) const { return index == o.index; }
        bool operator!=(const HAAMEntity& o) const { return index != o.index; }
};

struct ItemValue {
        char item[HAAM_ITEM_LEN];
        char value[HAAM_VALUE_LEN];
#ifdef HAAM_MULTI_DEVICE
        uint8_t device;
#endif

        uint8_t hasIndAvail : 1;
        uint8_t lastAvailable : 1;
        uint8_t stateSent : 1;     // the broker has the current value (sent, or received from it)
        uint8_t valueChanged : 1;  // set locally and not sent yet
        uint8_t isConfigured : 1;
        uint8_t availabilitySent : 1;
        uint8_t availabilitySet : 1;  // set by setEntityAvailability(): publish() keeps it
        uint8_t isEvent : 1;          // event entity: sendEvent() only, no stored state
};

// All the keys live in flash (PROGMEM): on AVR and ESP8266 a plain
// const char[] would be copied to RAM.
namespace HAKeys {
const char VALUE_TOPIC_PREFIX[] PROGMEM = "haam";

const char AVAILABILITY[] PROGMEM = "avty";
const char TOPIC[] PROGMEM = "t";
const char DEVICE[] PROGMEM = "dev";
const char IDENTIFIERS[] PROGMEM = "ids";
const char MANUFACTURER[] PROGMEM = "mf";
const char CONFIGURATION_URL[] PROGMEM = "cu";
const char MODEL[] PROGMEM = "mdl";
const char NAME[] PROGMEM = "name";
const char SW_VERSION[] PROGMEM = "sw";
const char UNIQUE_ID[] PROGMEM = "uniq_id";
const char COMMAND_TOPIC[] PROGMEM = "cmd_t";
const char STATE_TOPIC[] PROGMEM = "stat_t";
const char ENTITY_CATEGORY[] PROGMEM = "ent_cat";
const char DEVICE_CLASS[] PROGMEM = "dev_cla";
const char STATE_CLASS[] PROGMEM = "stat_cla";
const char ICON[] PROGMEM = "ic";
const char UNIT_OF_MEASUREMENT[] PROGMEM = "unit_of_meas";
const char SUGGESTED_DISPLAY_PRECISION[] PROGMEM = "sug_dsp_prc";
const char OPTIONS[] PROGMEM = "ops";
const char VIA_DEVICE[] PROGMEM = "via_device";
const char ORIGIN[] PROGMEM = "o";
const char COMPONENTS[] PROGMEM = "cmps";
const char PLATFORM[] PROGMEM = "p";
const char URL[] PROGMEM = "url";
const char ORIGIN_NAME[] PROGMEM = HAAM_LIB_NAME;
const char ORIGIN_VERSION[] PROGMEM = HAAM_LIB_VERSION;
const char ORIGIN_URL[] PROGMEM = HAAM_LIB_URL;

const char AVAILABILITY_MODE[] PROGMEM = "avty_mode";
const char AVAILABILITY_MODE_ALL[] PROGMEM = "all";

const char TYPE_SENSOR[] PROGMEM = "sensor";
const char TYPE_BINARY_SENSOR[] PROGMEM = "binary_sensor";
const char TYPE_SWITCH[] PROGMEM = "switch";
const char TYPE_BUTTON[] PROGMEM = "button";
const char TYPE_NUMBER[] PROGMEM = "number";
const char TYPE_SELECT[] PROGMEM = "select";
const char TYPE_EVENT[] PROGMEM = "event";
const char EVENT_TYPES[] PROGMEM = "event_types";

const char PAYLOAD_ON[] PROGMEM = "pl_on";
const char PAYLOAD_OFF[] PROGMEM = "pl_off";
const char PAYLOAD_PRESS[] PROGMEM = "pl_prs";

const char VAL_TRUE[] PROGMEM = "true";
const char VAL_FALSE[] PROGMEM = "false";
const char VAL_PRESS[] PROGMEM = "PRESS";

const char PREFIX[] PROGMEM = "homeassistant";
const char ONLINE_PAYLOAD[] PROGMEM = "online";
const char OFFLINE_PAYLOAD[] PROGMEM = "offline";

const char TOPIC_CONFIG[] PROGMEM = "config";
const char TOPIC_STATE[] PROGMEM = "state";
const char TOPIC_COMMAND[] PROGMEM = "set";
const char TOPIC_DEVICE[] PROGMEM = "device";
}  // namespace HAKeys

struct HACustomProp {
        const char* key;
        union {
                const char* valStr;
                long valInt;
                bool valBool;
        };
        uint8_t type : 2;  // 0: string, 1: int, 2: bool
        uint8_t keyPgm : 1;
        uint8_t valPgm : 1;
};

class HAJsonStream {
    public:
        explicit HAJsonStream(Print* out);

        size_t length() const { return _len; }
        bool ok() const { return !_err; }  // false when a write to the output failed
        void flush();

        void openObj();
        void closeObj();
        void openArr();
        void closeArr();

        void key(const char* keyP);
        void keyStr(const char* s);  // key from a RAM string, escaped
        void keyX(const char* s, bool pgm);

        void str(const char* s);
        void strP(const char* sP);
        void strX(const char* s, bool pgm);
        void num(long v);
        void boolean(bool v);

        void strBegin();
        void strAdd(const char* s);
        void strAddP(const char* sP);
        void strAddX(const char* s, bool pgm);
        void strAddChar(char c);
        void strEnd();

    private:
        void sep();
        void put(char c);
        void putP(const char* sP);
        void esc(char c);

        Print* _out;
        size_t _len;
        uint8_t _n;
        bool _first;
        bool _afterKey;
        bool _err;
        char _buf[HAAM_JSON_CHUNK];
};

class HAEntityBuilder;
class HomeAssistantArduinoMQTT;

#ifdef HAAM_DEVICE_DISCOVERY
// Declares all the entities of one device: create them with the builders and
// call publish() on each. It is called more than once per publishDevice()
// (measure + write), so it must declare the same entities in the same order
// every time and must not send anything over MQTT.
typedef void (*HAAMDeviceDeclarer)(HomeAssistantArduinoMQTT& ha, uint8_t device);
#endif

class HomeAssistantArduinoMQTT {
        friend class HAEntityBuilder;

    private:
        PubSubClient _mqtt;

        char _topicBuf[HAAM_TOPIC_LEN];
        char _sanitizedDeviceName[HAAM_DEVICE_LEN];

        ItemValue* values;
        uint8_t maxEntityNum;
        bool _started = false;

#ifdef HAAM_MULTI_DEVICE
        HAAMDevice _subDevices[HAAM_MAX_DEVICES - 1];
        uint8_t _subDeviceCount;
#endif

#ifdef HAAM_DEVICE_DISCOVERY
        HAJsonStream* _ddStream;  // current component stream, nullptr = register only
        uint8_t _ddDevice;
        bool _ddActive;
        bool _ddRegister;

        void _writeDeviceHeader(HAJsonStream& js, uint8_t dev);
        bool _publishDevice(uint8_t dev, HAAMDeviceDeclarer declare);
#endif

        void connect();
        int16_t publishConfig(HAEntityBuilder* builder);

        bool _sendSingleValue(int16_t index, bool forceSend = false);
        void _setAvailable(int16_t index, bool isAvailable);
        bool _publishAvailability(bool isAvailable);

        int16_t _lookup(uint8_t dev, const char* item, bool allocate, bool* isNew = nullptr);
        int16_t _allocate(uint8_t dev, const char* item);
        int16_t _slot(HAAMEntity entity) const;  // -1 when the handle is not valid
        void _valueTopic(uint8_t dev, const char* a, bool aPgm, const char* b, bool bPgm);
#ifndef HAAM_NO_COMMANDS
        void _subscribe(uint8_t dev, const char* actionP);
        void _handleMessage(char* topic, byte* payload, haam_len_t length);
#ifndef MQTT_CALLBACK_FUNCTIONAL
        static HomeAssistantArduinoMQTT* _instance;
        static void _staticCallback(char* topic, byte* payload, haam_len_t length);
#endif
        HAMQTTCallback* _callbackListener;
        bool _readValuesEnabled = false;
        unsigned long _restoreStart = 0;  // start of the readValues() restore window
        bool _stateSubscribed = false;    // subscribed to the state topics in this session
        bool _restorePending() const;
        void _unsubscribeStates();
#endif
        bool _configSent = false;
        void _writeConfig(HAJsonStream& js, HAEntityBuilder* builder, uint8_t dev, const char* entityId, bool component);
        void _writeDevice(HAJsonStream& js, uint8_t dev);
        void _writeAvailability(HAJsonStream& js, uint8_t dev, const char* entityId);
        int16_t _registerEntity(HAEntityBuilder* builder, uint8_t dev, const char* entityId);
        void _entityId(HAEntityBuilder* builder, char* out, size_t len);
        static void _sanitize(const char* input, bool pgm, char* output, size_t maxLen);

        bool _isValidDevice(uint8_t dev) const;
        uint8_t _deviceCount() const;
        uint8_t _itemDevice(uint8_t i) const;
        const char* _deviceId(uint8_t dev) const;

        void _setValue(uint8_t dev, const char* item, const char* value, bool markAsChanged);
        const char* _getValue(uint8_t dev, const char* item);
        bool _sendValue(uint8_t dev, const char* item, bool force);
        void _clearSetTopic(uint8_t dev, const char* item);
        void _setEntityAvailability(uint8_t dev, const char* entityId, bool isAvailable);

        // slot based core, shared by the name and the handle APIs
        void _storeValueAt(int16_t i, const char* value, bool markAsChanged);
        void _setValueAt(int16_t i, const char* value, bool markAsChanged);
        void _clearSetTopicAt(uint8_t dev, const char* sanitizedItem);
        bool _sendCommand(uint8_t dev, const char* commandTopic, const char* payload);
        bool _sendEvent(uint8_t dev, const char* eventName, const char* eventType);
        HAEntityBuilder _make(uint8_t dev, uint8_t kind, const char* type, const char* id, const char* name);

        unsigned long _lastReconnectAttempt = 0;

    public:
        // entity kinds for _make()
        enum : uint8_t { KIND_GENERIC,
                         KIND_SENSOR,
                         KIND_BINARY_SENSOR,
                         KIND_SWITCH,
                         KIND_BUTTON,
                         KIND_NUMBER,
                         KIND_SELECT,
                         KIND_EVENT };

        const char* mqttUser = "";      // empty: connect without credentials
        const char* mqttPassword = "";  // empty: connect without password

        const char* configurationUrl = "";

        const char* manufacturer = "";
        const char* model = "";
        const char* version = "";
        const char* haDeviceName = "";  // empty: the device id is shown
        const char* mqttDeviceName = "";

        bool useSharedAvailability = true;
        bool prefixUniqueIds = true;  // always on for child devices
        bool enableConfigPublishing = true;
#ifndef HAAM_NO_COMMANDS
        bool commandEnabled = true;
#endif

        HomeAssistantArduinoMQTT(uint8_t maxEntityNum = 4);
        ~HomeAssistantArduinoMQTT();
        HomeAssistantArduinoMQTT(const HomeAssistantArduinoMQTT&) = delete;
        HomeAssistantArduinoMQTT& operator=(const HomeAssistantArduinoMQTT&) = delete;

        void sanitizeID(const char* input, char* output, size_t maxLen);

        // begin(client, server, port) uses a HAAM_MQTT_BUFFER_SIZE buffer.
        // bufferSize 0 keeps the current PubSubClient buffer.
        // Return false when the requested buffer could not be allocated (out of
        // RAM): the client keeps its previous buffer, check it before relying on
        // large messages. Also false, and nothing is started, when mqttDeviceName
        // is empty once sanitized: it is the client id and part of every topic.
        bool begin(Client& client, const char* server, const uint16_t port);
        bool begin(Client& client, const char* server, const uint16_t port, const uint16_t bufferSize, const uint16_t keepAlive);

        void loop();
        bool connected();
        // PubSubClient state (MQTT_CONNECTED, MQTT_CONNECT_BAD_CREDENTIALS, ...):
        // tells why the connection is down
        int state() { return _mqtt.state(); }
        // true when the last publish() outside publishDevice() sent its discovery
        // config; false when offline, disabled, not registered or the write failed
        bool lastConfigSent() const { return _configSent; }
        // Closes the MQTT session. With publishOffline (and useSharedAvailability)
        // the device is reported offline first; otherwise its availability is
        // left as it is (e.g. deep sleep with expire_after). loop() does not
        // reconnect afterwards: call begin() again to start a new session.
        void disconnect(bool publishOffline = true);
        bool sendValues(bool force = false);

#ifndef HAAM_NO_COMMANDS
        void readValues();
        void setCallback(HAMQTTCallback* listener);
#endif

        // ---- root device (the only device when multi-device is disabled) ----
        bool sendValue(const char* item, bool force = false);
        // false when the message was not sent (offline, or event type too long)
        bool sendCommand(const char* commandTopic, const char* payload);
        bool sendEvent(const char* eventName, const char* eventType);

        void setValue(const char* item, const char* value, bool markAsChanged = true);
        const char* getValue(const char* item);
        void clearSetTopic(const char* item);

        void setEntityAvailability(const char* entityId, bool isAvailable);

        HAEntityBuilder newEntity(const char* type, const char* id, const char* name = nullptr);
        HAEntityBuilder newSensorEntity(const char* id, const char* name = nullptr);
        HAEntityBuilder newBinarySensorEntity(const char* id, const char* name = nullptr);
        HAEntityBuilder newSwitchEntity(const char* id, const char* name = nullptr);
        HAEntityBuilder newButtonEntity(const char* id, const char* name = nullptr);
        HAEntityBuilder newNumberEntity(const char* id, const char* name = nullptr);
        HAEntityBuilder newSelectEntity(const char* id, const char* name = nullptr);
        // Event entity: declare its types with eventTypes(), fire it with
        // sendEvent(handle, type).
        HAEntityBuilder newEventEntity(const char* id, const char* name = nullptr);

        // ---- entity handles (any device) ----
        // Finds an entity already known to the library (published, or given a
        // value). It never allocates a slot: an invalid handle means not found.
        HAAMEntity getEntity(const char* item);
        uint8_t entityDevice(HAAMEntity entity) const;  // HAAM_NO_DEVICE when not valid
        const char* entityId(HAAMEntity entity) const;  // sanitized id, "" when not valid

        void setValue(HAAMEntity entity, const char* value, bool markAsChanged = true);
        const char* getValue(HAAMEntity entity);
        bool sendValue(HAAMEntity entity, bool force = false);
        // Fires an event entity created with newEventEntity(): eventType must be
        // one of its eventTypes(). Not retained; false when it was not sent.
        bool sendEvent(HAAMEntity entity, const char* eventType);
        void clearSetTopic(HAAMEntity entity);
        void setEntityAvailability(HAAMEntity entity, bool isAvailable);

#ifdef HAAM_DEVICE_DISCOVERY
        // Publishes the root device with all the entities declared by `declare`
        // as one retained message on <prefix>/device/<device id>/config.
        // Entities are registered even when offline. Returns true when sent.
        bool publishDevice(HAAMDeviceDeclarer declare);
#endif

#ifdef HAAM_MULTI_DEVICE
        // ---- child devices (index 1..HAAM_MAX_DEVICES-1, 0 is the root) ----
        // Returns the new device index, or HAAM_NO_DEVICE when the table is
        // full or the sanitized id is empty / already in use.
        uint8_t addDevice(const char* id, const char* name);
        HAAMDevice* getDevice(uint8_t device);  // child devices only, nullptr otherwise
        uint8_t deviceCount() const { return _deviceCount(); }
        void setDeviceAvailability(uint8_t device, bool isAvailable);

        HAAMEntity getEntity(uint8_t device, const char* item);

        bool sendValue(uint8_t device, const char* item, bool force = false);
        bool sendCommand(uint8_t device, const char* commandTopic, const char* payload);
        bool sendEvent(uint8_t device, const char* eventName, const char* eventType);

        void setValue(uint8_t device, const char* item, const char* value, bool markAsChanged = true);
        const char* getValue(uint8_t device, const char* item);
        void clearSetTopic(uint8_t device, const char* item);
        void setEntityAvailability(uint8_t device, const char* entityId, bool isAvailable);

        HAEntityBuilder newEntity(uint8_t device, const char* type, const char* id, const char* name = nullptr);
        HAEntityBuilder newSensorEntity(uint8_t device, const char* id, const char* name = nullptr);
        HAEntityBuilder newBinarySensorEntity(uint8_t device, const char* id, const char* name = nullptr);
        HAEntityBuilder newSwitchEntity(uint8_t device, const char* id, const char* name = nullptr);
        HAEntityBuilder newButtonEntity(uint8_t device, const char* id, const char* name = nullptr);
        HAEntityBuilder newNumberEntity(uint8_t device, const char* id, const char* name = nullptr);
        HAEntityBuilder newSelectEntity(uint8_t device, const char* id, const char* name = nullptr);
        HAEntityBuilder newEventEntity(uint8_t device, const char* id, const char* name = nullptr);

#ifdef HAAM_DEVICE_DISCOVERY
        bool publishDevice(uint8_t device, HAAMDeviceDeclarer declare);
#endif
#endif
};

// Every string setter has a const char* (RAM) and an F("...") (flash)
// overload. On AVR and ESP8266 string literals are copied to RAM: F() keeps
// them in flash only. The strings must stay valid until publish().
class HAEntityBuilder {
        friend class HomeAssistantArduinoMQTT;

    private:
        enum : uint8_t { P_TYPE = 1,
                         P_NAME = 2,
                         P_CATEGORY = 4,
                         P_DEVICE_CLASS = 8,
                         P_STATE_CLASS = 16,
                         P_ICON = 32,
                         P_UNIT = 64,
                         P_STARTUP = 128 };

        HomeAssistantArduinoMQTT* _mqtt;

        const char* _type;
        const char* _name;
        const char* _id;
        const char* _commandTopicName;
        const char* _startupValue;

        const char* _category;
        const char* _deviceClass;
        const char* _stateClass;
        const char* _icon;
        const char* _unit;

        const char* const* _options;
        uint8_t _optionCount;

        HACustomProp _customProps[HAAM_MAX_CUSTOM_PROPS];
        uint8_t _customPropCount;

        uint8_t _device;
        uint8_t _suggestedPrecision;
        uint8_t _pgm;  // P_xxx: the field points to flash
        uint8_t _commandTopic : 1;
        uint8_t _stateTopic : 1;
        uint8_t _indAvail : 1;
        uint8_t _suggestedPrecisionEnable : 1;
        uint8_t _event : 1;  // event entity: options are written as event_types

        void _str(const char*& field, const char* val, uint8_t bit, bool pgm);
        HACustomProp* _prop(const char* key, bool keyPgm, uint8_t type);
        void _propStr(const char* key, bool keyPgm, const char* val, bool valPgm);
        void _propInt(const char* key, bool keyPgm, long val);
        void _propBool(const char* key, bool keyPgm, bool val);
        bool _isPgm(uint8_t bit) const { return (_pgm & bit) != 0; }

        typedef const __FlashStringHelper* FStr;
        static const char* _p(FStr s) { return reinterpret_cast<const char*>(s); }

    public:
        HAEntityBuilder(HomeAssistantArduinoMQTT* mqtt, const char* type, const char* id, const char* name, uint8_t device = HAAM_ROOT_DEVICE);

        void name(const char* val) { _str(_name, val, P_NAME, false); }
        void name(FStr val) { _str(_name, _p(val), P_NAME, true); }
        void category(const char* val) { _str(_category, val, P_CATEGORY, false); }
        void category(FStr val) { _str(_category, _p(val), P_CATEGORY, true); }
        void deviceClass(const char* val) { _str(_deviceClass, val, P_DEVICE_CLASS, false); }
        void deviceClass(FStr val) { _str(_deviceClass, _p(val), P_DEVICE_CLASS, true); }
        void stateClass(const char* val) { _str(_stateClass, val, P_STATE_CLASS, false); }
        void stateClass(FStr val) { _str(_stateClass, _p(val), P_STATE_CLASS, true); }
        void icon(const char* val) { _str(_icon, val, P_ICON, false); }
        void icon(FStr val) { _str(_icon, _p(val), P_ICON, true); }
        void unit(const char* val) { _str(_unit, val, P_UNIT, false); }
        void unit(FStr val) { _str(_unit, _p(val), P_UNIT, true); }
        void startup(const char* val) { _str(_startupValue, val, P_STARTUP, false); }
        void startup(FStr val) { _str(_startupValue, _p(val), P_STARTUP, true); }

        void command(bool enable, const char* customName = nullptr);
        void state(bool enable);
        void independentAvailability(bool enable = true);
        void suggestedDisplayPrecision(uint8_t precision);
        void options(const char* const* opts, uint8_t count);
        template <size_t N>
        void options(const char* const (&opts)[N]) { options(opts, (uint8_t)N); }
        // event entities (newEventEntity()): the types sendEvent() can fire
        void eventTypes(const char* const* types, uint8_t count) { options(types, count); }
        template <size_t N>
        void eventTypes(const char* const (&types)[N]) { options(types, (uint8_t)N); }

        // custom discovery keys, e.g. set(F("min"), 0) or set("mode", "box")
        void set(const char* key, const char* value) { _propStr(key, false, value, false); }
        void set(const char* key, FStr value) { _propStr(key, false, _p(value), true); }
        void set(FStr key, const char* value) { _propStr(_p(key), true, value, false); }
        void set(FStr key, FStr value) { _propStr(_p(key), true, _p(value), true); }
        void set(const char* key, int value) { _propInt(key, false, value); }
        void set(FStr key, int value) { _propInt(_p(key), true, value); }
        void set(const char* key, long value) { _propInt(key, false, value); }
        void set(FStr key, long value) { _propInt(_p(key), true, value); }
        void set(const char* key, bool value) { _propBool(key, false, value); }
        void set(FStr key, bool value) { _propBool(_p(key), true, value); }

        // Outside publishDevice(): publishes the entity on its own discovery topic.
        // Inside a HAAMDeviceDeclarer: adds the entity to the device message.
        // Returns the entity handle (also when offline: the entity is registered
        // anyway); it is invalid when the entity table is full or the id is empty.
        // In a declarer, the measuring pass of publishDevice() returns an invalid
        // handle for entities that were never registered: keep the handle of the
        // last call.
        HAAMEntity publish();
};

// ---- entity factories: thin wrappers around one out-of-line builder ----
inline HAEntityBuilder HomeAssistantArduinoMQTT::newEntity(const char* type, const char* id, const char* name) {
    return _make(HAAM_ROOT_DEVICE, KIND_GENERIC, type, id, name);
}
inline HAEntityBuilder HomeAssistantArduinoMQTT::newSensorEntity(const char* id, const char* name) {
    return _make(HAAM_ROOT_DEVICE, KIND_SENSOR, nullptr, id, name);
}
inline HAEntityBuilder HomeAssistantArduinoMQTT::newBinarySensorEntity(const char* id, const char* name) {
    return _make(HAAM_ROOT_DEVICE, KIND_BINARY_SENSOR, nullptr, id, name);
}
inline HAEntityBuilder HomeAssistantArduinoMQTT::newSwitchEntity(const char* id, const char* name) {
    return _make(HAAM_ROOT_DEVICE, KIND_SWITCH, nullptr, id, name);
}
inline HAEntityBuilder HomeAssistantArduinoMQTT::newButtonEntity(const char* id, const char* name) {
    return _make(HAAM_ROOT_DEVICE, KIND_BUTTON, nullptr, id, name);
}
inline HAEntityBuilder HomeAssistantArduinoMQTT::newNumberEntity(const char* id, const char* name) {
    return _make(HAAM_ROOT_DEVICE, KIND_NUMBER, nullptr, id, name);
}
inline HAEntityBuilder HomeAssistantArduinoMQTT::newSelectEntity(const char* id, const char* name) {
    return _make(HAAM_ROOT_DEVICE, KIND_SELECT, nullptr, id, name);
}
inline HAEntityBuilder HomeAssistantArduinoMQTT::newEventEntity(const char* id, const char* name) {
    return _make(HAAM_ROOT_DEVICE, KIND_EVENT, nullptr, id, name);
}

#ifdef HAAM_MULTI_DEVICE
inline HAEntityBuilder HomeAssistantArduinoMQTT::newEntity(uint8_t device, const char* type, const char* id, const char* name) {
    return _make(device, KIND_GENERIC, type, id, name);
}
inline HAEntityBuilder HomeAssistantArduinoMQTT::newSensorEntity(uint8_t device, const char* id, const char* name) {
    return _make(device, KIND_SENSOR, nullptr, id, name);
}
inline HAEntityBuilder HomeAssistantArduinoMQTT::newBinarySensorEntity(uint8_t device, const char* id, const char* name) {
    return _make(device, KIND_BINARY_SENSOR, nullptr, id, name);
}
inline HAEntityBuilder HomeAssistantArduinoMQTT::newSwitchEntity(uint8_t device, const char* id, const char* name) {
    return _make(device, KIND_SWITCH, nullptr, id, name);
}
inline HAEntityBuilder HomeAssistantArduinoMQTT::newButtonEntity(uint8_t device, const char* id, const char* name) {
    return _make(device, KIND_BUTTON, nullptr, id, name);
}
inline HAEntityBuilder HomeAssistantArduinoMQTT::newNumberEntity(uint8_t device, const char* id, const char* name) {
    return _make(device, KIND_NUMBER, nullptr, id, name);
}
inline HAEntityBuilder HomeAssistantArduinoMQTT::newSelectEntity(uint8_t device, const char* id, const char* name) {
    return _make(device, KIND_SELECT, nullptr, id, name);
}
inline HAEntityBuilder HomeAssistantArduinoMQTT::newEventEntity(uint8_t device, const char* id, const char* name) {
    return _make(device, KIND_EVENT, nullptr, id, name);
}
#endif