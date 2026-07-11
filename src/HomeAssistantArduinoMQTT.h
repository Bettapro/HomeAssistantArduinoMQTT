#pragma once

#include "Arduino.h"
#include "ArduinoJson.h"
#include "PubSubClient.h"

#ifdef ESP8266
#include "ESP8266WiFi.h"
#endif

#ifdef ESP32
#include "WiFi.h"
#endif

#if defined(ESP8266) || defined(ESP32)
#include <functional>
#define HAMQTT_CALLBACK_SIGNATURE std::function<void(const char*, const char*, bool)> cb_callback
#else
#define HAMQTT_CALLBACK_SIGNATURE void (*cb_callback)(const char*, const char*, bool)
#endif

struct ItemValue {
        char item[64];
        char value[16];
        bool lastAvailable = false;
        bool isFirstValue = true;
        bool valueChanged = true;
};

namespace HAKeys {
extern const char AVAILABILITY[] PROGMEM;
extern const char TOPIC[] PROGMEM;
extern const char DEVICE[] PROGMEM;
extern const char IDENTIFIERS[] PROGMEM;
extern const char MANUFACTURER[] PROGMEM;
extern const char CONFIGURATION_URL[] PROGMEM;
extern const char MODEL[] PROGMEM;
extern const char NAME[] PROGMEM;
extern const char SW_VERSION[] PROGMEM;
extern const char ORIGIN[] PROGMEM;
extern const char SW[] PROGMEM;
extern const char UNIQUE_ID[] PROGMEM;
extern const char ENABLED_DEFAULT[] PROGMEM;
extern const char COMMAND_TOPIC[] PROGMEM;
extern const char STATE_TOPIC[] PROGMEM;
extern const char ENTITY_CATEGORY[] PROGMEM;
extern const char DEVICE_CLASS[] PROGMEM;
extern const char STATE_CLASS[] PROGMEM;
extern const char ICON[] PROGMEM;
extern const char UNIT_OF_MEASUREMENT[] PROGMEM;

extern const char TYPE_SENSOR[] PROGMEM;
extern const char TYPE_BINARY_SENSOR[] PROGMEM;
extern const char TYPE_SWITCH[] PROGMEM;
extern const char TYPE_BUTTON[] PROGMEM;
extern const char TYPE_NUMBER[] PROGMEM;
extern const char TYPE_SELECT[] PROGMEM;

extern const char PAYLOAD_ON[] PROGMEM;
extern const char PAYLOAD_OFF[] PROGMEM;
extern const char PAYLOAD_PRESS[] PROGMEM;

extern const char VAL_TRUE[] PROGMEM;
extern const char VAL_FALSE[] PROGMEM;
extern const char VAL_PRESS[] PROGMEM;

extern const char PREFIX[] PROGMEM;
extern const char SUFFIX_CONFIG[] PROGMEM;
extern const char ONLINE_PAYLOAD[] PROGMEM;
extern const char OFFLINE_PAYLOAD[] PROGMEM;

// extern const char TOPIC_STATUS[] PROGMEM;
extern const char TOPIC_STATE[] PROGMEM;
extern const char TOPIC_COMMAND[] PROGMEM;

extern const char TOPIC_3_PH[] PROGMEM;
extern const char TOPIC_4_PH[] PROGMEM;

}  // namespace HAKeys

class HAEntityBuilder;

class HomeAssistantArduinoMQTT {
        friend class HAEntityBuilder;

    private:
        WiFiClient* wifiClient;
        PubSubClient* mqttClient;

        char StatusTopic[128];
        char _sanitizedDeviceName[64];

        ItemValue* values;

        void connect();
        void publishConfig(const char* type, const char* id, const char* name, JsonDocument& doc, bool commandTopic, bool stateTopic, const char* commandTopicName, const char* startupValue, bool independentAvailability);
        void MqttCallback(char* topic, byte* payload, unsigned int length);

        HAMQTT_CALLBACK_SIGNATURE;
        uint8_t maxEntityNum;

        bool _forcePublishAll = false;

    public:
        const char* MqttUser = "";
        const char* MqttPassword = "";

        const char* ConfigurationUrl = "";

        const char* OriginName = "";
        const char* OriginVersion = "";

        const char* Manufacturer = "";
        const char* Model = "";
        const char* Version = "";
        const char* HADeviceName = "";
        const char* MQTTDeviceName = "";

        bool useSharedAvailability = true;
        bool prefixUniqueIds = true;

        HomeAssistantArduinoMQTT(uint8_t maxEntityNum = 24);
        ~HomeAssistantArduinoMQTT();

        void sanitizeID(const char* input, char* output, size_t maxLen);

        void begin(const char* server, const uint16_t port);
        void begin(const char* server, const uint16_t port, const uint16_t bufferSize, const uint16_t keepAlive);
        void loop();

        bool connected();
        void readValues();
        void sendValues();

        void sendValue(const char* item);
        void sendCommand(const char* commandTopic, const char* payload);
        void sendEvent(const char* eventName, const char* eventType);
        void setCallback(HAMQTT_CALLBACK_SIGNATURE);

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
        bool _commandTopic;
        bool _stateTopic;
        bool _indAvail;

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

        template <typename T>
        HAEntityBuilder& set(const char* key, T value) {
            _doc[key] = value;
            return *this;
        }

        void publish();
};