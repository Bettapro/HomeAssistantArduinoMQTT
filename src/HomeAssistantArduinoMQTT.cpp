#include "HomeAssistantArduinoMQTT.h"

#define VALUE_TOPIC_PREFIX "haam"

const char HAKeys::AVAILABILITY[] PROGMEM = "avty";
const char HAKeys::TOPIC[] PROGMEM = "t";
const char HAKeys::DEVICE[] PROGMEM = "dev";
const char HAKeys::IDENTIFIERS[] PROGMEM = "ids";
const char HAKeys::MANUFACTURER[] PROGMEM = "mf";
const char HAKeys::CONFIGURATION_URL[] PROGMEM = "cu";
const char HAKeys::MODEL[] PROGMEM = "mdl";
const char HAKeys::NAME[] PROGMEM = "name";
const char HAKeys::SW_VERSION[] PROGMEM = "sw";
const char HAKeys::UNIQUE_ID[] PROGMEM = "uniq_id";
const char HAKeys::ENABLED_DEFAULT[] PROGMEM = "en";
const char HAKeys::COMMAND_TOPIC[] PROGMEM = "cmd_t";
const char HAKeys::STATE_TOPIC[] PROGMEM = "stat_t";
const char HAKeys::ENTITY_CATEGORY[] PROGMEM = "ent_cat";
const char HAKeys::DEVICE_CLASS[] PROGMEM = "dev_cla";
const char HAKeys::STATE_CLASS[] PROGMEM = "stat_cla";
const char HAKeys::ICON[] PROGMEM = "ic";
const char HAKeys::UNIT_OF_MEASUREMENT[] PROGMEM = "unit_of_meas";

const char HAKeys::TYPE_SENSOR[] PROGMEM = "sensor";
const char HAKeys::TYPE_BINARY_SENSOR[] PROGMEM = "binary_sensor";
const char HAKeys::TYPE_SWITCH[] PROGMEM = "switch";
const char HAKeys::TYPE_BUTTON[] PROGMEM = "button";
const char HAKeys::TYPE_NUMBER[] PROGMEM = "number";
const char HAKeys::TYPE_SELECT[] PROGMEM = "select";

const char HAKeys::PAYLOAD_ON[] PROGMEM = "pl_on";
const char HAKeys::PAYLOAD_OFF[] PROGMEM = "pl_off";
const char HAKeys::PAYLOAD_PRESS[] PROGMEM = "pl_prs";

const char HAKeys::VAL_TRUE[] PROGMEM = "true";
const char HAKeys::VAL_FALSE[] PROGMEM = "false";
const char HAKeys::VAL_PRESS[] PROGMEM = "PRESS";

const char HAKeys::PREFIX[] PROGMEM = "homeassistant";
const char HAKeys::ONLINE_PAYLOAD[] PROGMEM = "online";
const char HAKeys::OFFLINE_PAYLOAD[] PROGMEM = "offline";

const char HAKeys::TOPIC_CONFIG[] PROGMEM = "config";
const char HAKeys::TOPIC_STATE[] PROGMEM = "state";
const char HAKeys::TOPIC_COMMAND[] PROGMEM = "set";

const char HAKeys::TOPIC_3_PH[] PROGMEM = "%s/%s/%s";
const char HAKeys::TOPIC_4_PH[] PROGMEM = "%s/%s/%s/%s";
const char HAKeys::TOPIC_5_PH[] PROGMEM = "%s/%s/%s/%s/%s";

HomeAssistantArduinoMQTT::HomeAssistantArduinoMQTT(uint8_t maxN) {
    mqttClient = nullptr;
    wifiClient = nullptr;
    maxEntityNum = maxN;
    values = new ItemValue[maxEntityNum]();
    
    for (int i = 0; i < maxEntityNum; i++) {
        values[i].isFirstValue = 1;
        values[i].valueChanged = 1;
    }
    
    StatusTopic[0] = '\0';
    _sanitizedDeviceName[0] = '\0';
    _sharedTopicBuffer[0] = '\0';
    _forcePublishAll = true;
    _lastReconnectAttempt = 0;
    _readValuesEnabled = false;
}

HomeAssistantArduinoMQTT::~HomeAssistantArduinoMQTT() {
    delete mqttClient;
    delete wifiClient;
    delete[] values;
}

void HomeAssistantArduinoMQTT::sanitizeID(const char* input, char* output, size_t maxLen) {
    unsigned int writeIdx = 0;
    bool lastWasUnderscore = false;
    size_t len = strlen(input);

    for (unsigned int i = 0; i < len && writeIdx < maxLen - 1; i++) {
        char c = input[i];
        if (isalnum(c)) {
            output[writeIdx++] = tolower(c);
            lastWasUnderscore = false;
        } else if ((c == ' ' || c == '-' || c == '_') && writeIdx > 0 && !lastWasUnderscore) {
            output[writeIdx++] = '_';
            lastWasUnderscore = true;
        }
    }
    if (writeIdx > 0 && output[writeIdx - 1] == '_') writeIdx--;
    output[writeIdx] = '\0';
}

void HomeAssistantArduinoMQTT::setCallback(HAMQTT_CALLBACK_SIGNATURE) {
    this->cb_callback = cb_callback;
}

void HomeAssistantArduinoMQTT::begin(const char* server, const uint16_t port) {
    begin(server, port, 1024, 15);
}

void HomeAssistantArduinoMQTT::begin(const char* server, const uint16_t port, const uint16_t bufferSize, const uint16_t keepAlive) {
    sanitizeID(MQTTDeviceName, _sanitizedDeviceName, sizeof(_sanitizedDeviceName));
    snprintf_P(StatusTopic, sizeof(StatusTopic), HAKeys::TOPIC_3_PH, VALUE_TOPIC_PREFIX, _sanitizedDeviceName, FPSTR(HAKeys::AVAILABILITY));

    delete wifiClient;
    wifiClient = new WiFiClient();

    delete mqttClient;
    mqttClient = new PubSubClient(*wifiClient);
    mqttClient->setBufferSize(bufferSize);
    mqttClient->setServer(server, port);
    mqttClient->setCallback(std::bind(&HomeAssistantArduinoMQTT::MqttCallback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    mqttClient->setKeepAlive(keepAlive);
}

void HomeAssistantArduinoMQTT::loop() {
    if (!mqttClient->connected()) {
        connect();
    }
    if (mqttClient->connected()) {
        mqttClient->loop();
    }
}

void HomeAssistantArduinoMQTT::connect() {
    unsigned long now = millis();
    if (now - _lastReconnectAttempt < 5000 && _lastReconnectAttempt != 0) {
        return;
    }
    _lastReconnectAttempt = now;

    bool success = false;
    if (useSharedAvailability) {
        success = mqttClient->connect(_sanitizedDeviceName, MqttUser, MqttPassword, StatusTopic, 1, true, HAKeys::OFFLINE_PAYLOAD);
    } else {
        success = mqttClient->connect(_sanitizedDeviceName, MqttUser, MqttPassword);
    }

    if (success) {
        if (useSharedAvailability) {
            mqttClient->publish(StatusTopic, HAKeys::ONLINE_PAYLOAD, true);
        }

        if (_readValuesEnabled) {
            snprintf_P(_sharedTopicBuffer, sizeof(_sharedTopicBuffer), HAKeys::TOPIC_4_PH, VALUE_TOPIC_PREFIX, _sanitizedDeviceName, "+", FPSTR(HAKeys::TOPIC_STATE));
            mqttClient->subscribe(_sharedTopicBuffer);
        }

        for (int i = 0; i < maxEntityNum; i++) {
            if (values[i].isConfigured) {
                snprintf_P(_sharedTopicBuffer, sizeof(_sharedTopicBuffer), HAKeys::TOPIC_4_PH, VALUE_TOPIC_PREFIX, _sanitizedDeviceName, values[i].item, FPSTR(HAKeys::TOPIC_COMMAND));
                mqttClient->subscribe(_sharedTopicBuffer);
            }
        }
    }
}

bool HomeAssistantArduinoMQTT::connected() {
    return mqttClient->connected();
}

HAEntityBuilder HomeAssistantArduinoMQTT::newEntity(const char* type, const char* id, const char* name) {
    return HAEntityBuilder(this, type, id, name);
}

HAEntityBuilder HomeAssistantArduinoMQTT::newSensorEntity(const char* id, const char* name) {
    return newEntity(HAKeys::TYPE_SENSOR, id, name).state(true).independentAvailability(true);
}

HAEntityBuilder HomeAssistantArduinoMQTT::newBinarySensorEntity(const char* id, const char* name) {
    return newEntity(HAKeys::TYPE_BINARY_SENSOR, id, name)
        .independentAvailability(true)
        .state(true)
        .set(HAKeys::PAYLOAD_ON, HAKeys::VAL_TRUE)
        .set(HAKeys::PAYLOAD_OFF, HAKeys::VAL_FALSE);
}

HAEntityBuilder HomeAssistantArduinoMQTT::newSwitchEntity(const char* id, const char* name) {
    return newEntity(HAKeys::TYPE_SWITCH, id, name)
        .independentAvailability(true)
        .command(true)
        .state(true)
        .set(HAKeys::PAYLOAD_ON, HAKeys::VAL_TRUE)
        .set(HAKeys::PAYLOAD_OFF, HAKeys::VAL_FALSE);
}

HAEntityBuilder HomeAssistantArduinoMQTT::newButtonEntity(const char* id, const char* name) {
    return newEntity(HAKeys::TYPE_BUTTON, id, name)
        .independentAvailability(true)
        .command(true)
        .state(false)
        .set(HAKeys::PAYLOAD_PRESS, HAKeys::VAL_PRESS);
}

HAEntityBuilder HomeAssistantArduinoMQTT::newNumberEntity(const char* id, const char* name) {
    return newEntity(HAKeys::TYPE_NUMBER, id, name)
        .independentAvailability(true)
        .command(true)
        .state(true);
}

HAEntityBuilder HomeAssistantArduinoMQTT::newSelectEntity(const char* id, const char* name) {
    return newEntity(HAKeys::TYPE_SELECT, id, name)
        .independentAvailability(true)
        .command(true)
        .state(true);
}

void HomeAssistantArduinoMQTT::publishConfig(const char* type, const char* id, const char* name, JsonDocument& doc, bool commandTopic, bool stateTopic, const char* commandTopicName, const char* startupValue, bool independentAvailability) {
    char entityId[32];

    if (id && strlen(id) > 0) {
        sanitizeID(id, entityId, sizeof(entityId));
    } else if (name && strlen(name) > 0) {
        sanitizeID(name, entityId, sizeof(entityId));
    } else {
        entityId[0] = '\0';
    }

    char configTopic[112];
    snprintf_P(configTopic, sizeof(configTopic), HAKeys::TOPIC_5_PH,
               FPSTR(HAKeys::PREFIX), type, _sanitizedDeviceName, entityId, FPSTR(HAKeys::TOPIC_CONFIG));

    JsonArray availArray = doc[HAKeys::AVAILABILITY].to<JsonArray>();

    if (useSharedAvailability) {
        JsonObject availObj = availArray.add<JsonObject>();
        availObj[HAKeys::TOPIC] = StatusTopic;
    }

    if (independentAvailability) {
        JsonObject indAvailObj = availArray.add<JsonObject>();
        char indAvailTopic[80];
        snprintf_P(indAvailTopic, sizeof(indAvailTopic), HAKeys::TOPIC_4_PH, VALUE_TOPIC_PREFIX, _sanitizedDeviceName, entityId, FPSTR(HAKeys::AVAILABILITY));
        indAvailObj[HAKeys::TOPIC] = indAvailTopic;
    }

    JsonObject device = doc[HAKeys::DEVICE].to<JsonObject>();
    JsonArray identifiers = device[HAKeys::IDENTIFIERS].to<JsonArray>();
    identifiers.add(_sanitizedDeviceName);
    device[HAKeys::MANUFACTURER] = Manufacturer;
    device[HAKeys::MODEL] = Model;
    device[HAKeys::NAME] = HADeviceName;
    device[HAKeys::SW_VERSION] = Version;
    if (name && strlen(name) > 0) device[HAKeys::CONFIGURATION_URL] = ConfigurationUrl;
    if (name && strlen(name) > 0) doc[HAKeys::NAME] = name;

    char uniqueId[72];
    if (prefixUniqueIds) {
        snprintf_P(uniqueId, sizeof(uniqueId), PSTR("%s_%s"), _sanitizedDeviceName, entityId);
    } else {
        snprintf(uniqueId, sizeof(uniqueId), "%s", entityId);
    }
    doc[HAKeys::UNIQUE_ID] = uniqueId;
    doc[HAKeys::ENABLED_DEFAULT] = true;

    char cmdTopic[80] = {0};
    if (commandTopic) {
        const char* cmdName = (commandTopicName && strlen(commandTopicName) > 0) ? commandTopicName : entityId;
        snprintf_P(cmdTopic, sizeof(cmdTopic), HAKeys::TOPIC_4_PH, VALUE_TOPIC_PREFIX, _sanitizedDeviceName, cmdName, FPSTR(HAKeys::TOPIC_COMMAND));
        doc[HAKeys::COMMAND_TOPIC] = cmdTopic;
    }

    if (stateTopic) {
        char statTopic[80];
        snprintf_P(statTopic, sizeof(statTopic), HAKeys::TOPIC_4_PH, VALUE_TOPIC_PREFIX, _sanitizedDeviceName, entityId, FPSTR(HAKeys::TOPIC_STATE));
        doc[HAKeys::STATE_TOPIC] = statTopic;
    }

    int slot = -1;
    for (int i = 0; i < maxEntityNum; i++) {
        if (values[i].item[0] != '\0' && strcmp(values[i].item, entityId) == 0) {
            slot = i;
            break;
        }
        if (slot == -1 && values[i].item[0] == '\0') {
            slot = i;
        }
    }

    if (slot != -1) {
        strncpy(values[slot].item, entityId, sizeof(values[slot].item) - 1);
        values[slot].item[sizeof(values[slot].item) - 1] = '\0';
        values[slot].isConfigured = 1;
    }

    if (mqttClient->connected()) {
        if (enableConfigPublishing) {
            size_t jsonLen = measureJson(doc);
            if (mqttClient->beginPublish(configTopic, jsonLen, true)) {
                serializeJson(doc, *mqttClient);
                mqttClient->endPublish();
            }
        }
        if (commandTopic && cmdTopic[0] != '\0') mqttClient->subscribe(cmdTopic);
    }

    if (independentAvailability) setEntityAvailability(entityId, true);
    if (stateTopic && startupValue && strlen(startupValue) > 0) {
        setValue(entityId, startupValue);
    }
}

void HomeAssistantArduinoMQTT::clearSetTopic(const char* item) {
    char sanitizedItem[32];
    sanitizeID(item, sanitizedItem, sizeof(sanitizedItem));
    snprintf_P(_sharedTopicBuffer, sizeof(_sharedTopicBuffer), HAKeys::TOPIC_4_PH, VALUE_TOPIC_PREFIX, _sanitizedDeviceName, sanitizedItem, FPSTR(HAKeys::TOPIC_COMMAND));
    mqttClient->publish(_sharedTopicBuffer, "", false);
}

void HomeAssistantArduinoMQTT::setValue(const char* item, const char* value) {
    char sanitizedItem[32];
    sanitizeID(item, sanitizedItem, sizeof(sanitizedItem));

    int emptySlot = -1;
    for (int i = 0; i < maxEntityNum; i++) {
        if (values[i].item[0] != '\0' && strcmp(values[i].item, sanitizedItem) == 0) {
            if (strcmp(values[i].value, value) != 0) {
                strncpy(values[i].value, value, sizeof(values[i].value) - 1);
                values[i].value[sizeof(values[i].value) - 1] = '\0';
                values[i].valueChanged = 1;
            }
            return;
        }
        if (emptySlot == -1 && values[i].item[0] == '\0') {
            emptySlot = i;
        }
    }

    if (emptySlot != -1) {
        strncpy(values[emptySlot].item, sanitizedItem, sizeof(values[emptySlot].item) - 1);
        values[emptySlot].item[sizeof(values[emptySlot].item) - 1] = '\0';

        strncpy(values[emptySlot].value, value, sizeof(values[emptySlot].value) - 1);
        values[emptySlot].value[sizeof(values[emptySlot].value) - 1] = '\0';

        values[emptySlot].valueChanged = 1;
        values[emptySlot].lastAvailable = 0;
        values[emptySlot].isFirstValue = 1;
        values[emptySlot].isConfigured = 0;
    }
}

const char* HomeAssistantArduinoMQTT::getValue(const char* item) {
    char sanitizedItem[32];
    sanitizeID(item, sanitizedItem, sizeof(sanitizedItem));

    for (int i = 0; i < maxEntityNum; i++) {
        if (values[i].item[0] != '\0' && strcmp(values[i].item, sanitizedItem) == 0) return values[i].value;
    }
    return "";
}

void HomeAssistantArduinoMQTT::setEntityAvailability(const char* entityId, bool isAvailable) {
    char sanitizedItem[32];
    sanitizeID(entityId, sanitizedItem, sizeof(sanitizedItem));

    int targetIndex = -1;
    for (int i = 0; i < maxEntityNum; i++) {
        if (values[i].item[0] != '\0' && strcmp(values[i].item, sanitizedItem) == 0) {
            targetIndex = i;
            break; 
        }
        if (targetIndex == -1 && values[i].item[0] == '\0') {
            targetIndex = i;
        }
    }

    if (targetIndex != -1) {
        if (values[targetIndex].item[0] == '\0') {
            strncpy(values[targetIndex].item, sanitizedItem, sizeof(values[targetIndex].item) - 1);
            values[targetIndex].item[sizeof(values[targetIndex].item) - 1] = '\0';
            values[targetIndex].value[0] = '\0';
        } else {
            if (values[targetIndex].lastAvailable == isAvailable && !_forcePublishAll) {
                return;
            }
        }

        snprintf_P(_sharedTopicBuffer, sizeof(_sharedTopicBuffer), HAKeys::TOPIC_4_PH, VALUE_TOPIC_PREFIX, _sanitizedDeviceName, sanitizedItem, FPSTR(HAKeys::AVAILABILITY));
        const char* payload = isAvailable ? HAKeys::ONLINE_PAYLOAD : HAKeys::OFFLINE_PAYLOAD;

        if (mqttClient->publish(_sharedTopicBuffer, payload, true)) {
            values[targetIndex].lastAvailable = isAvailable ? 1 : 0;
        }
    }
}

void HomeAssistantArduinoMQTT::readValues() {
    _readValuesEnabled = true;
    if (mqttClient->connected()) {
        snprintf_P(_sharedTopicBuffer, sizeof(_sharedTopicBuffer), HAKeys::TOPIC_4_PH, VALUE_TOPIC_PREFIX, _sanitizedDeviceName, "+", FPSTR(HAKeys::TOPIC_STATE));
        mqttClient->subscribe(_sharedTopicBuffer);
    }
}

void HomeAssistantArduinoMQTT::sendValues() {
    for (int i = 0; i < maxEntityNum; i++) {
        if (values[i].item[0] != '\0') {
            if (_forcePublishAll) {
                snprintf_P(_sharedTopicBuffer, sizeof(_sharedTopicBuffer), HAKeys::TOPIC_4_PH, VALUE_TOPIC_PREFIX, _sanitizedDeviceName, values[i].item, FPSTR(HAKeys::AVAILABILITY));
                const char* payload = values[i].lastAvailable ? HAKeys::ONLINE_PAYLOAD : HAKeys::OFFLINE_PAYLOAD;
                mqttClient->publish(_sharedTopicBuffer, payload, true);
            }

            if (!values[i].valueChanged && !_forcePublishAll && !values[i].isFirstValue) {
                continue;
            }

            snprintf_P(_sharedTopicBuffer, sizeof(_sharedTopicBuffer), HAKeys::TOPIC_4_PH, VALUE_TOPIC_PREFIX, _sanitizedDeviceName, values[i].item, FPSTR(HAKeys::TOPIC_STATE));

            if (mqttClient->publish(_sharedTopicBuffer, values[i].value, true)) {
                values[i].isFirstValue = 0;
                values[i].valueChanged = 0;
            }
        }
    }
    _forcePublishAll = false;
}

void HomeAssistantArduinoMQTT::sendValue(const char* item) {
    char sanitizedItem[32];
    sanitizeID(item, sanitizedItem, sizeof(sanitizedItem));

    for (int i = 0; i < maxEntityNum; i++) {
        if (values[i].item[0] != '\0' && strcmp(values[i].item, sanitizedItem) == 0) {
            if (!values[i].valueChanged && !_forcePublishAll && !values[i].isFirstValue) {
                return;
            }

            snprintf_P(_sharedTopicBuffer, sizeof(_sharedTopicBuffer), HAKeys::TOPIC_4_PH, VALUE_TOPIC_PREFIX, _sanitizedDeviceName, sanitizedItem, FPSTR(HAKeys::TOPIC_STATE));
            if (mqttClient->publish(_sharedTopicBuffer, values[i].value, true)) {
                values[i].isFirstValue = 0;
                values[i].valueChanged = 0;
            }
            break; 
        }
    }
}

void HomeAssistantArduinoMQTT::sendCommand(const char* commandTopic, const char* payload) {
    snprintf_P(_sharedTopicBuffer, sizeof(_sharedTopicBuffer), HAKeys::TOPIC_3_PH, VALUE_TOPIC_PREFIX, _sanitizedDeviceName, commandTopic);
    mqttClient->publish(_sharedTopicBuffer, payload, false);
}

void HomeAssistantArduinoMQTT::sendEvent(const char* eventName, const char* eventType) {
    char payload[64];
    snprintf_P(payload, sizeof(payload), PSTR("{\"event_type\":\"%s\"}"), eventType);
    sendCommand(eventName, payload);
}

void HomeAssistantArduinoMQTT::MqttCallback(char* topic, byte* payload, unsigned int length) {
    char cPayload[64]; 
    unsigned int copyLen = (length < sizeof(cPayload) - 1) ? length : (sizeof(cPayload) - 1);
    memcpy(cPayload, payload, copyLen);
    cPayload[copyLen] = '\0';

    constexpr size_t prefixPrefixLen = sizeof(VALUE_TOPIC_PREFIX) - 1; 
    size_t devLen = strlen(_sanitizedDeviceName);

    if (strncmp(topic, VALUE_TOPIC_PREFIX, prefixPrefixLen) == 0 && topic[prefixPrefixLen] == '/') {
        const char* devPtr = topic + prefixPrefixLen + 1;
        
        if (strncmp(devPtr, _sanitizedDeviceName, devLen) == 0 && devPtr[devLen] == '/') {
            const char* remainder = devPtr + devLen + 1;
            const char* lastSlash = strrchr(remainder, '/');

            if (lastSlash != nullptr) {
                size_t itemLen = lastSlash - remainder;

                if (itemLen < 32) {
                    char item[32];
                    strncpy(item, remainder, itemLen);
                    item[itemLen] = '\0';

                    const char* action = lastSlash + 1;

                    if (strcmp(action, HAKeys::TOPIC_COMMAND) == 0) {
                        if (cb_callback != nullptr) cb_callback(item, cPayload, false);
                    } else if (strcmp(action, HAKeys::TOPIC_STATE) == 0) {
                        setValue(item, cPayload);
                        if (cb_callback != nullptr) cb_callback(item, cPayload, true);
                    }
                }
            }
        }
    }
}

HAEntityBuilder::HAEntityBuilder(HomeAssistantArduinoMQTT* mqtt, const char* type, const char* id, const char* name)
    : _mqtt(mqtt), _type(type), _name(name), _id(id), _commandTopicName(nullptr), _startupValue(nullptr), _commandTopic(false), _stateTopic(true), _indAvail(false) {}

HAEntityBuilder& HAEntityBuilder::category(const char* val) {
    _doc[HAKeys::ENTITY_CATEGORY] = val;
    return *this;
}
HAEntityBuilder& HAEntityBuilder::deviceClass(const char* val) {
    _doc[HAKeys::DEVICE_CLASS] = val;
    return *this;
}
HAEntityBuilder& HAEntityBuilder::stateClass(const char* val) {
    _doc[HAKeys::STATE_CLASS] = val;
    return *this;
}
HAEntityBuilder& HAEntityBuilder::icon(const char* val) {
    _doc[HAKeys::ICON] = val;
    return *this;
}
HAEntityBuilder& HAEntityBuilder::unit(const char* val) {
    _doc[HAKeys::UNIT_OF_MEASUREMENT] = val;
    return *this;
}
HAEntityBuilder& HAEntityBuilder::startup(const char* val) {
    _startupValue = val;
    return *this;
}

HAEntityBuilder& HAEntityBuilder::command(bool enable, const char* customName) {
    _commandTopic = enable;
    _commandTopicName = customName;
    return *this;
}

HAEntityBuilder& HAEntityBuilder::state(bool enable) {
    _stateTopic = enable;
    return *this;
}

HAEntityBuilder& HAEntityBuilder::independentAvailability(bool enable) {
    _indAvail = enable;
    return *this;
}

void HAEntityBuilder::publish() {
    if (_mqtt) {
        _mqtt->publishConfig(_type, _id, _name, _doc, _commandTopic, _stateTopic, _commandTopicName, _startupValue, _indAvail);
    }
}