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

#if defined(ESP8266) || defined(ESP32)
#define MQTT_CALLBACK_FUNCTIONAL
#endif

namespace HAAMFmt {
int uintToBuf(char* buf, size_t size, unsigned long val);
int strToBuf(char* buf, size_t size, const char* val);
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
    snprintf(buf, size, "%.*f", (int)(dec), (double)(val))

#define HAAM_FORMAT_STR(buf, val) \
    HAAM_FORMAT_STR_SIZE(buf, sizeof(buf), val)

#define HAAM_FORMAT_STR_SIZE(buf, size, val) \
    HAAMFmt::strToBuf(buf, size, (const char*)(val))

class HAMQTTCallback {
    public:
        virtual ~HAMQTTCallback() = default;
        virtual void onMQTTMessage(const char* item, const char* value, bool isState) = 0;
};

struct ItemValue {
        char item[HAAM_ITEM_LEN];
        char value[HAAM_VALUE_LEN];

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

class HomeAssistantArduinoMQTT {
        friend class HAEntityBuilder;

    private:
        Client* _client;
        PubSubClient* mqttClient;

        char StatusTopic[HAAM_DEVICE_LEN + 16];

        char _topicBuf[HAAM_TOPIC_LEN];

        ItemValue* values;
        uint8_t maxEntityNum;

        void connect();
        void publishConfig(HAEntityBuilder* builder);
#ifdef MQTT_CALLBACK_FUNCTIONAL
        void MqttCallback(char* topic, byte* payload, unsigned int length);
#endif

        bool _sendSingleValue(int index, bool forceSend = false);

        int16_t _lookup(const char* item, bool allocate, bool* isNew = nullptr);
        void _valueTopic(const char* a, bool aPgm, const char* b, bool bPgm);
        void _writeConfig(HAJsonStream& js, HAEntityBuilder* builder, const char* entityId);

        unsigned long _lastReconnectAttempt = 0;
        bool _readValuesEnabled = false;

#ifndef MQTT_CALLBACK_FUNCTIONAL
    public:
#endif
        char _sanitizedDeviceName[HAAM_DEVICE_LEN];
        HAMQTTCallback* _callbackListener;

#ifdef MQTT_CALLBACK_FUNCTIONAL
    public:
#endif
        const char* MqttUser = "";
        const char* MqttPassword = "";

        const char* ConfigurationUrl = "";

        const char* Manufacturer = "";
        const char* Model = "";
        const char* Version = "";
        const char* HADeviceName = "";
        const char* MQTTDeviceName = "";

        bool useSharedAvailability = true;
        bool prefixUniqueIds = true;
        bool enableConfigPublishing = true;
        bool commandEnabled = true;

        HomeAssistantArduinoMQTT(uint8_t maxEntityNum = 4);
        ~HomeAssistantArduinoMQTT();

        void sanitizeID(const char* input, char* output, size_t maxLen);

        void begin(Client& client, const char* server, const uint16_t port);
        void begin(Client& client, const char* server, const uint16_t port, const uint16_t bufferSize, const uint16_t keepAlive);

        void loop();
        bool connected();
        void readValues();
        bool sendValues(bool force = false);

        bool sendValue(const char* item, bool force = false);
        void sendCommand(const char* commandTopic, const char* payload);
        void sendEvent(const char* eventName, const char* eventType);

        void setCallback(HAMQTTCallback* listener);

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

        HACustomProp _customProps[HAAM_MAX_CUSTOM_PROPS];
        uint8_t _customPropCount;

        uint8_t _suggestedPrecision;
        uint8_t _commandTopic : 1;
        uint8_t _stateTopic : 1;
        uint8_t _indAvail : 1;
        uint8_t _suggestedPrecisionEnable : 1;

    public:
        HAEntityBuilder(HomeAssistantArduinoMQTT* mqtt, const char* type, const char* id, const char* name);

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

        void set(const char* key, const char* value);
        void set(const char* key, int value);
        void set(const char* key, bool value);

        void publish();
};