#pragma once

#include <Client.h>

#include "Arduino.h"
#include "ArduinoJson.h"
#include "PubSubClient.h"

#define HAAM_FORMAT_BOOL(buf, val) \
    HAAM_FORMAT_BOOL_SIZE(buf, sizeof(buf), val)

#define HAAM_FORMAT_BOOL_SIZE(buf, size, val) \
    snprintf(buf, size, "%s", (val) ? HAKeys::VAL_TRUE : HAKeys::VAL_FALSE)

#define HAAM_FORMAT_UINT(buf, val) \
    HAAM_FORMAT_UINT_SIZE(buf, sizeof(buf), val)

#define HAAM_FORMAT_UINT_SIZE(buf, size, val) \
    snprintf(buf, size, "%lu", (unsigned long)(val))

#define HAAM_FORMAT_FLOAT(buf, val, dec) \
    HAAM_FORMAT_FLOAT_SIZE(buf, sizeof(buf), val, dec)

#define HAAM_FORMAT_FLOAT_SIZE(buf, size, val, dec) \
    snprintf(buf, size, "%.*f", (int)(dec), (double)(val))

#define HAAM_FORMAT_STR(buf, val) \
    HAAM_FORMAT_STR_SIZE(buf, sizeof(buf), val)

#define HAAM_FORMAT_STR_SIZE(buf, size, val) \
    snprintf(buf, size, "%s", (const char*)(val))

class HAMQTTCallback {
    public:
        virtual void onMQTTMessage(const char* item, const char* value, bool isState) = 0;

    protected:
        ~HAMQTTCallback() {}
};

struct ItemValue {
        char item[32];
        char value[16];

        uint8_t lastAvailable : 1;
        uint8_t isFirstValue : 1;
        uint8_t valueChanged : 1;
        uint8_t isConfigured : 1;
        uint8_t availabilitySent : 1;
};

namespace HAKeys {
extern const char AVAILABILITY[];
extern const char TOPIC[];
extern const char DEVICE[];
extern const char IDENTIFIERS[];
extern const char MANUFACTURER[];
extern const char CONFIGURATION_URL[];
extern const char MODEL[];
extern const char NAME[];
extern const char SW_VERSION[];
extern const char UNIQUE_ID[];
extern const char ENABLED_DEFAULT[];
extern const char COMMAND_TOPIC[];
extern const char STATE_TOPIC[];
extern const char ENTITY_CATEGORY[];
extern const char DEVICE_CLASS[];
extern const char STATE_CLASS[];
extern const char ICON[];
extern const char UNIT_OF_MEASUREMENT[];
extern const char SUGGESTED_DISPLAY_PRECISION[];

extern const char TYPE_SENSOR[];
extern const char TYPE_BINARY_SENSOR[];
extern const char TYPE_SWITCH[];
extern const char TYPE_BUTTON[];
extern const char TYPE_NUMBER[];
extern const char TYPE_SELECT[];

extern const char PAYLOAD_ON[];
extern const char PAYLOAD_OFF[];
extern const char PAYLOAD_PRESS[];

extern const char VAL_TRUE[];
extern const char VAL_FALSE[];
extern const char VAL_PRESS[];

extern const char PREFIX[];
extern const char ONLINE_PAYLOAD[];
extern const char OFFLINE_PAYLOAD[];

extern const char TOPIC_CONFIG[];
extern const char TOPIC_STATE[];
extern const char TOPIC_COMMAND[];

extern const char TOPIC_3_PH[];
extern const char TOPIC_4_PH[];
extern const char TOPIC_5_PH[];
}  // namespace HAKeys

class HAEntityBuilder;

class HomeAssistantArduinoMQTT {
        friend class HAEntityBuilder;

    private:
        Client* _client;
        PubSubClient* mqttClient;

        char StatusTopic[64];
        char _sanitizedDeviceName[32];
        char _sharedTopicBuffer[80];

        ItemValue* values;

        HAMQTTCallback* _callbackListener;

        void connect();
        void publishConfig(
            const char* type,
            const char* id,
            const char* name,
            JsonDocument& doc,
            bool commandTopic,
            bool stateTopic,
            const char* commandTopicName,
            const char* startupValue,
            bool independentAvailability,
            bool precisionEnable,
            uint8_t precision); 
        void MqttCallback(char* topic, byte* payload, unsigned int length);

        uint8_t maxEntityNum;

        unsigned long _lastReconnectAttempt = 0;
        bool _readValuesEnabled = false;

    public:
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

        HomeAssistantArduinoMQTT(uint8_t maxEntityNum = 24);
        ~HomeAssistantArduinoMQTT();

        void sanitizeID(const char* input, char* output, size_t maxLen);

        void begin(Client& client, const char* server, const uint16_t port);
        void begin(Client& client, const char* server, const uint16_t port, const uint16_t bufferSize, const uint16_t keepAlive);

        void loop();
        bool connected();
        void readValues();
        void sendValues();

        void sendValue(const char* item);
        void sendCommand(const char* commandTopic, const char* payload);
        void sendEvent(const char* eventName, const char* eventType);

        void setCallback(HAMQTTCallback* listener);

        void setValue(const char* item, const char* value);
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
    private:
        HomeAssistantArduinoMQTT* _mqtt;
        JsonDocument _doc;
        const char* _type;
        const char* _name;
        const char* _id;
        const char* _commandTopicName;
        const char* _startupValue;
        uint8_t _suggestedPrecision;
        bool _commandTopic;
        bool _stateTopic;
        bool _indAvail;
        bool _suggestedPrecisionEnable;

    public:
        HAEntityBuilder(HomeAssistantArduinoMQTT* mqtt, const char* type, const char* id, const char* name);

        HAEntityBuilder& category(const char* val);
        HAEntityBuilder& deviceClass(const char* val);
        HAEntityBuilder& stateClass(const char* val);
        HAEntityBuilder& icon(const char* val);
        HAEntityBuilder& unit(const char* val);
        HAEntityBuilder& command(bool enable, const char* customName = nullptr);
        HAEntityBuilder& state(bool enable);
        HAEntityBuilder& startup(const char* val);
        HAEntityBuilder& independentAvailability(bool enable = true);
        HAEntityBuilder& suggestedDisplayPrecision(uint8_t precision); 
        template <typename T>
        HAEntityBuilder& set(const char* key, T value) {
            _doc[key] = value;
            return *this;
        }

        void publish();
};