#pragma once

#include <Client.h>

#include "Arduino.h"
#include "PubSubClient.h"

#ifndef HAAM_ITEM_LEN
#define HAAM_ITEM_LEN 32  // max sanitized entity id length + 1
#endif
#ifndef HAAM_VALUE_LEN
#define HAAM_VALUE_LEN 32  // max payload length kept per entity + 1
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
#define HAAM_JSON_CHUNK 32  // TCP write coalescing window
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
#define HAAM_LIB_VERSION "2.0.0"
#define HAAM_LIB_URL "https://github.com/Bettapro/HomeAssistantArduinoMQTT"

#if defined(ESP8266) || defined(ESP32)
#define MQTT_CALLBACK_FUNCTIONAL
#endif

namespace HAAMFmt {
int uintToBuf(char* buf, size_t size, unsigned long val);
int strToBuf(char* buf, size_t size, const char* val);
// Fixed-point formatting without printf (AVR's printf has no %f support).
// dec is capped at 6 and reduced when the value would not fit 32 bits;
// NaN, infinity and |val| >= 2^32 produce an empty string (an empty value
// makes the entity unavailable).
int floatToBuf(char* buf, size_t size, double val, uint8_t dec);
}  // namespace HAAMFmt

#define HAAM_FORMAT_BOOL(buf, val) \
    HAAM_FORMAT_BOOL_SIZE(buf, sizeof(buf), val)

#define HAAM_FORMAT_BOOL_SIZE(buf, size, val) \
    strcpy_P(buf, (val) ? HAKeys::VAL_TRUE : HAKeys::VAL_FALSE)

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
        uint8_t isFirstValue : 1;
        uint8_t valueChanged : 1;
        uint8_t isConfigured : 1;
        uint8_t availabilitySent : 1;
};

namespace HAKeys {
const char VALUE_TOPIC_PREFIX[] = "haam";

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
const char ENABLED_DEFAULT[] PROGMEM = "en";
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

const char TYPE_SENSOR[] = "sensor";
const char TYPE_BINARY_SENSOR[] = "binary_sensor";
const char TYPE_SWITCH[] = "switch";
const char TYPE_BUTTON[] = "button";
const char TYPE_NUMBER[] = "number";
const char TYPE_SELECT[] = "select";

const char PAYLOAD_ON[] PROGMEM = "pl_on";
const char PAYLOAD_OFF[] PROGMEM = "pl_off";
const char PAYLOAD_PRESS[] PROGMEM = "pl_prs";

const char VAL_TRUE[] PROGMEM = "true";
const char VAL_FALSE[] PROGMEM = "false";
const char VAL_PRESS[] PROGMEM = "PRESS";

const char PREFIX[] PROGMEM = "homeassistant";
const char ONLINE_PAYLOAD[] PROGMEM = "online";
const char OFFLINE_PAYLOAD[] PROGMEM = "offline";

const char TOPIC_CONFIG[] = "config";
const char TOPIC_STATE[] = "state";
const char TOPIC_COMMAND[] = "set";
const char TOPIC_DEVICE[] = "device";

const char TOPIC_3_PH[] PROGMEM = "%s/%s/%s";
const char TOPIC_4_PH[] PROGMEM = "%s/%s/%s/%s";
const char TOPIC_5_PH[] PROGMEM = "%s/%s/%s/%s/%s";
}  // namespace HAKeys

struct HACustomProp {
        const char* key;
        union {
                const char* valStr;
                int valInt;
                bool valBool;
        };
        uint8_t type;  // 0: string, 1: int, 2: bool
};

class HAJsonStream {
    public:
        explicit HAJsonStream(Print* out);

        size_t length() const { return _len; }
        void flush();

        void openObj();
        void closeObj();
        void openArr();
        void closeArr();

        void key(const char* keyP);
        void keyStr(const char* s);  // key from a RAM string, escaped

        void str(const char* s);
        void strP(const char* sP);
        void num(long v);
        void boolean(bool v);

        void strBegin();
        void strAdd(const char* s);
        void strAddP(const char* sP);
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
        Client* _client;
        PubSubClient* mqttClient;

        char _statusTopic[HAAM_DEVICE_LEN + 16];

        char _topicBuf[HAAM_TOPIC_LEN];

        ItemValue* values;
        uint8_t maxEntityNum;

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
        void _writeDeviceFooter(HAJsonStream& js);
        bool _publishDevice(uint8_t dev, HAAMDeviceDeclarer declare);
#endif

        void connect();
        int16_t publishConfig(HAEntityBuilder* builder);
#if defined(MQTT_CALLBACK_FUNCTIONAL) && !defined(HAAM_NO_COMMANDS)
        void _mqttCallback(char* topic, byte* payload, unsigned int length);
#endif

        bool _sendSingleValue(int index, bool forceSend = false);
        void _setAvailable(int16_t index, bool isAvailable);

        int16_t _lookup(uint8_t dev, const char* item, bool allocate, bool* isNew = nullptr);
        int16_t _allocate(uint8_t dev, const char* item);
        int16_t _slot(HAAMEntity entity) const;  // -1 when the handle is not valid
        void _valueTopic(uint8_t dev, const char* a, bool aPgm, const char* b, bool bPgm);
#ifndef HAAM_NO_COMMANDS
        void _subscribeAll(const char* action);
#endif
        void _writeConfig(HAJsonStream& js, HAEntityBuilder* builder, uint8_t dev, const char* entityId, bool component);
        void _writeDevice(HAJsonStream& js, uint8_t dev);
        void _writeAvailability(HAJsonStream& js, uint8_t dev, const char* entityId);
        int16_t _registerEntity(HAEntityBuilder* builder, uint8_t dev, const char* entityId);
        void _entityId(HAEntityBuilder* builder, char* out, size_t len);

        bool _isValidDevice(uint8_t dev) const;
        uint8_t _deviceCount() const;
        uint8_t _itemDevice(uint8_t i) const;

        void _setValue(uint8_t dev, const char* item, const char* value, bool markAsChanged);
        const char* _getValue(uint8_t dev, const char* item);
        bool _sendValue(uint8_t dev, const char* item, bool force);
        void _clearSetTopic(uint8_t dev, const char* item);
        void _setEntityAvailability(uint8_t dev, const char* entityId, bool isAvailable);

        // slot based core, shared by the name and the handle APIs
        void _storeValueAt(int16_t i, const char* value, bool markAsChanged);
        void _setValueAt(int16_t i, const char* value, bool markAsChanged);
        void _clearSetTopicAt(uint8_t dev, const char* sanitizedItem);
        void _sendCommand(uint8_t dev, const char* commandTopic, const char* payload);
        void _sendEvent(uint8_t dev, const char* eventName, const char* eventType);
        HAEntityBuilder _newEntity(uint8_t dev, const char* type, const char* id, const char* name);

        unsigned long _lastReconnectAttempt = 0;
#ifndef HAAM_NO_COMMANDS
        bool _readValuesEnabled = false;
#endif

#ifndef MQTT_CALLBACK_FUNCTIONAL
    public:
#endif
#ifndef HAAM_NO_COMMANDS
        void _handleMessage(char* topic, byte* payload, unsigned int length);
        HAMQTTCallback* _callbackListener;
#endif
        int16_t _storeValue(uint8_t dev, const char* item, const char* value, bool markAsChanged);
        const char* _deviceId(uint8_t dev) const;
        char _sanitizedDeviceName[HAAM_DEVICE_LEN];

#ifdef MQTT_CALLBACK_FUNCTIONAL
    public:
#endif
        const char* mqttUser = "";
        const char* mqttPassword = "";

        const char* configurationUrl = "";

        const char* manufacturer = "";
        const char* model = "";
        const char* version = "";
        const char* haDeviceName = "";
        const char* mqttDeviceName = "";

        bool useSharedAvailability = true;
        bool prefixUniqueIds = true;
        bool enableConfigPublishing = true;
#ifndef HAAM_NO_COMMANDS
        bool commandEnabled = true;
#endif

        HomeAssistantArduinoMQTT(uint8_t maxEntityNum = 4);
        ~HomeAssistantArduinoMQTT();

        void sanitizeID(const char* input, char* output, size_t maxLen);

        void begin(Client& client, const char* server, const uint16_t port);
        void begin(Client& client, const char* server, const uint16_t port, const uint16_t bufferSize, const uint16_t keepAlive);

        void loop();
        bool connected();
        bool sendValues(bool force = false);

#ifndef HAAM_NO_COMMANDS
        void readValues();
        void setCallback(HAMQTTCallback* listener);
#endif

        // ---- root device (the only device when multi-device is disabled) ----
        bool sendValue(const char* item, bool force = false);
        void sendCommand(const char* commandTopic, const char* payload);
        void sendEvent(const char* eventName, const char* eventType);

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

        // ---- entity handles (any device) ----
        // Finds an entity already known to the library (published, or given a
        // value). It never allocates a slot: an invalid handle means not found.
        HAAMEntity getEntity(const char* item);
        uint8_t entityDevice(HAAMEntity entity) const;  // HAAM_NO_DEVICE when not valid
        const char* entityId(HAAMEntity entity) const;  // sanitized id, "" when not valid

        void setValue(HAAMEntity entity, const char* value, bool markAsChanged = true);
        const char* getValue(HAAMEntity entity);
        bool sendValue(HAAMEntity entity, bool force = false);
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
        void sendCommand(uint8_t device, const char* commandTopic, const char* payload);
        void sendEvent(uint8_t device, const char* eventName, const char* eventType);

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

#ifdef HAAM_DEVICE_DISCOVERY
        bool publishDevice(uint8_t device, HAAMDeviceDeclarer declare);
#endif
#endif
};

class HAEntityBuilder {
        friend class HomeAssistantArduinoMQTT;

    private:
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
        uint8_t _commandTopic : 1;
        uint8_t _stateTopic : 1;
        uint8_t _indAvail : 1;
        uint8_t _suggestedPrecisionEnable : 1;

    public:
        HAEntityBuilder(HomeAssistantArduinoMQTT* mqtt, const char* type, const char* id, const char* name, uint8_t device = HAAM_ROOT_DEVICE);

        void category(const char* val);
        void deviceClass(const char* val);
        void stateClass(const char* val);
        void icon(const char* val);
        void unit(const char* val);
        void command(bool enable, const char* customName = nullptr);
        void state(bool enable);
        void startup(const char* val);
        void independentAvailability(bool enable = true);
        void suggestedDisplayPrecision(uint8_t precision);
        void options(const char* const* opts, uint8_t count);
        template <size_t N>
        void options(const char* const (&opts)[N]) { options(opts, (uint8_t)N); }

        void set(const char* key, const char* value);
        void set(const char* key, int value);
        void set(const char* key, bool value);

        // Outside publishDevice(): publishes the entity on its own discovery topic.
        // Inside a HAAMDeviceDeclarer: adds the entity to the device message.
        // Returns the entity handle (also when offline: the entity is registered
        // anyway); it is invalid when the entity table is full or the id is empty.
        // In a declarer, the measuring pass of publishDevice() returns an invalid
        // handle for entities that were never registered: keep the handle of the
        // last call.
        HAAMEntity publish();
};
