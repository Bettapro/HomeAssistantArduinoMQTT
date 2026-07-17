#include "HomeAssistantArduinoMQTT.h"

constexpr const char VALUE_TOPIC_PREFIX[] = "haam";

HomeAssistantArduinoMQTT::HomeAssistantArduinoMQTT(uint8_t maxN) {
    mqttClient = nullptr;
    _client = nullptr;
    _callbackListener = nullptr;
    maxEntityNum = maxN;
    
    values = new ItemValue[maxEntityNum]();

    for (int i = 0; i < maxEntityNum; i++) {
        values[i].isFirstValue = 1;
        values[i].valueChanged = 1;
        values[i].isConfigured = 0;
        values[i].availabilitySent = 0;
    }

    StatusTopic[0] = '\0';
    _sanitizedDeviceName[0] = '\0';
    _lastReconnectAttempt = 0;
    _readValuesEnabled = false;
}

HomeAssistantArduinoMQTT::~HomeAssistantArduinoMQTT() {
    delete mqttClient;
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

void HomeAssistantArduinoMQTT::setCallback(HAMQTTCallback* listener) {
    _callbackListener = listener;
}

void HomeAssistantArduinoMQTT::begin(Client& client, const char* server, const uint16_t port) {
    begin(client, server, port, 1024, 15);
}

void HomeAssistantArduinoMQTT::begin(Client& client, const char* server, const uint16_t port, const uint16_t bufferSize, const uint16_t keepAlive) {
    _client = &client;

    sanitizeID(MQTTDeviceName, _sanitizedDeviceName, sizeof(_sanitizedDeviceName));

    snprintf(StatusTopic, sizeof(StatusTopic), HAKeys::TOPIC_3_PH, VALUE_TOPIC_PREFIX, _sanitizedDeviceName, HAKeys::AVAILABILITY);

    delete mqttClient;
    mqttClient = new PubSubClient(*_client);
    mqttClient->setBufferSize(bufferSize);
    mqttClient->setServer(server, port);

    mqttClient->setCallback([this](char* topic, byte* payload, unsigned int length) {
        this->MqttCallback(topic, payload, length);
    });

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
        for (int i = 0; i < maxEntityNum; i++) {
            if (values[i].item[0] != '\0') {
                values[i].valueChanged = 1;
                values[i].availabilitySent = 0;
            }
        }

        if (useSharedAvailability) {
            mqttClient->publish(StatusTopic, HAKeys::ONLINE_PAYLOAD, true);
        }
        
        char topicBuffer[96]; 
        
        if (_readValuesEnabled) {
            snprintf(topicBuffer, sizeof(topicBuffer), HAKeys::TOPIC_4_PH, VALUE_TOPIC_PREFIX, _sanitizedDeviceName, "+", HAKeys::TOPIC_STATE);
            mqttClient->subscribe(topicBuffer);
        }

        if (commandEnabled) {
            snprintf(topicBuffer, sizeof(topicBuffer), HAKeys::TOPIC_4_PH, VALUE_TOPIC_PREFIX, _sanitizedDeviceName, "+", HAKeys::TOPIC_COMMAND);
            mqttClient->subscribe(topicBuffer);
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
    HAEntityBuilder builder = newEntity(HAKeys::TYPE_SENSOR, id, name);
    builder.state(true);
    builder.independentAvailability(true);
    return builder;
}

HAEntityBuilder HomeAssistantArduinoMQTT::newBinarySensorEntity(const char* id, const char* name) {
    HAEntityBuilder builder = newEntity(HAKeys::TYPE_BINARY_SENSOR, id, name);
    builder.independentAvailability(true);
    builder.state(true);
    builder.set(HAKeys::PAYLOAD_ON, HAKeys::VAL_TRUE);
    builder.set(HAKeys::PAYLOAD_OFF, HAKeys::VAL_FALSE);
    return builder;
}

HAEntityBuilder HomeAssistantArduinoMQTT::newSwitchEntity(const char* id, const char* name) {
    HAEntityBuilder builder = newEntity(HAKeys::TYPE_SWITCH, id, name);
    builder.independentAvailability(true);
    builder.command(true);
    builder.state(true);
    builder.set(HAKeys::PAYLOAD_ON, HAKeys::VAL_TRUE);
    builder.set(HAKeys::PAYLOAD_OFF, HAKeys::VAL_FALSE);
    return builder;
}

HAEntityBuilder HomeAssistantArduinoMQTT::newButtonEntity(const char* id, const char* name) {
    HAEntityBuilder builder = newEntity(HAKeys::TYPE_BUTTON, id, name);
    builder.independentAvailability(true);
    builder.command(true);
    builder.state(false);
    builder.set(HAKeys::PAYLOAD_PRESS, HAKeys::VAL_PRESS);
    return builder;
}

HAEntityBuilder HomeAssistantArduinoMQTT::newNumberEntity(const char* id, const char* name) {
    HAEntityBuilder builder = newEntity(HAKeys::TYPE_NUMBER, id, name);
    builder.independentAvailability(true);
    builder.command(true);
    builder.state(true);
    return builder;
}

HAEntityBuilder HomeAssistantArduinoMQTT::newSelectEntity(const char* id, const char* name) {
    HAEntityBuilder builder = newEntity(HAKeys::TYPE_SELECT, id, name);
    builder.independentAvailability(true);
    builder.command(true);
    builder.state(true);
    return builder;
}

void HomeAssistantArduinoMQTT::publishConfig(HAEntityBuilder* builder) {
    char entityId[32];

    if (builder->_id && strlen(builder->_id) > 0) {
        sanitizeID(builder->_id, entityId, sizeof(entityId));
    } else if (builder->_name && strlen(builder->_name) > 0) {
        sanitizeID(builder->_name, entityId, sizeof(entityId));
    } else {
        entityId[0] = '\0';
    }

    if (enableConfigPublishing && mqttClient && mqttClient->connected()) {
        JsonDocument doc; 

        char configTopic[112];
        snprintf(configTopic, sizeof(configTopic), HAKeys::TOPIC_5_PH,
                 HAKeys::PREFIX, builder->_type, _sanitizedDeviceName, entityId, HAKeys::TOPIC_CONFIG);

        JsonArray availArray = doc[HAKeys::AVAILABILITY].to<JsonArray>();
        uint8_t avtyCount = 0;

        if (useSharedAvailability) {
            JsonObject availObj = availArray.add<JsonObject>();
            availObj[HAKeys::TOPIC] = StatusTopic;
            avtyCount++;
        }

        if (builder->_indAvail) {
            JsonObject indAvailObj = availArray.add<JsonObject>();
            char indAvailTopic[80];
            snprintf(indAvailTopic, sizeof(indAvailTopic), HAKeys::TOPIC_4_PH, VALUE_TOPIC_PREFIX, _sanitizedDeviceName, entityId, HAKeys::AVAILABILITY);
            indAvailObj[HAKeys::TOPIC] = indAvailTopic;
            avtyCount++;
        }

        if (avtyCount > 1) {
            doc[HAKeys::AVAILABILITY_MODE] = HAKeys::AVAILABILITY_MODE_ALL;
        }

        JsonObject device = doc[HAKeys::DEVICE].to<JsonObject>();
        JsonArray identifiers = device[HAKeys::IDENTIFIERS].to<JsonArray>();
        identifiers.add(_sanitizedDeviceName);
        device[HAKeys::MANUFACTURER] = Manufacturer;
        device[HAKeys::MODEL] = Model;
        device[HAKeys::NAME] = HADeviceName;
        device[HAKeys::SW_VERSION] = Version;
        
        if (ConfigurationUrl && strlen(ConfigurationUrl) > 0) device[HAKeys::CONFIGURATION_URL] = ConfigurationUrl;
        if (builder->_name && strlen(builder->_name) > 0) doc[HAKeys::NAME] = builder->_name;
        if (builder->_suggestedPrecisionEnable) doc[HAKeys::SUGGESTED_DISPLAY_PRECISION] = builder->_suggestedPrecision;

        if (builder->_category) doc[HAKeys::ENTITY_CATEGORY] = builder->_category;
        if (builder->_deviceClass) doc[HAKeys::DEVICE_CLASS] = builder->_deviceClass;
        if (builder->_stateClass) doc[HAKeys::STATE_CLASS] = builder->_stateClass;
        if (builder->_icon) doc[HAKeys::ICON] = builder->_icon;
        if (builder->_unit) doc[HAKeys::UNIT_OF_MEASUREMENT] = builder->_unit;

        for (uint8_t i = 0; i < builder->_customPropCount; i++) {
            if (builder->_customProps[i].type == 0) {
                doc[builder->_customProps[i].key] = builder->_customProps[i].valStr;
            } else if (builder->_customProps[i].type == 1) {
                doc[builder->_customProps[i].key] = builder->_customProps[i].valInt;
            } else if (builder->_customProps[i].type == 2) {
                doc[builder->_customProps[i].key] = builder->_customProps[i].valBool;
            }
        }

        char uniqueId[72];
        if (prefixUniqueIds) {
            snprintf(uniqueId, sizeof(uniqueId), "%s_%s", _sanitizedDeviceName, entityId);
        } else {
            snprintf(uniqueId, sizeof(uniqueId), "%s", entityId);
        }
        doc[HAKeys::UNIQUE_ID] = uniqueId;
        doc[HAKeys::ENABLED_DEFAULT] = true;

        char cmdTopic[80] = {0};
        if (builder->_commandTopic) {
            const char* cmdName = (builder->_commandTopicName && strlen(builder->_commandTopicName) > 0) ? builder->_commandTopicName : entityId;
            snprintf(cmdTopic, sizeof(cmdTopic), HAKeys::TOPIC_4_PH, VALUE_TOPIC_PREFIX, _sanitizedDeviceName, cmdName, HAKeys::TOPIC_COMMAND);
            doc[HAKeys::COMMAND_TOPIC] = cmdTopic;
        }

        if (builder->_stateTopic) {
            char statTopic[80];
            snprintf(statTopic, sizeof(statTopic), HAKeys::TOPIC_4_PH, VALUE_TOPIC_PREFIX, _sanitizedDeviceName, entityId, HAKeys::TOPIC_STATE);
            doc[HAKeys::STATE_TOPIC] = statTopic;
        }

        size_t jsonLen = measureJson(doc);
        if (mqttClient->beginPublish(configTopic, jsonLen, true)) {
            serializeJson(doc, *mqttClient);
            mqttClient->endPublish();
        }
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

    bool hasValue = builder->_stateTopic && builder->_startupValue && strlen(builder->_startupValue) > 0;

    if (builder->_indAvail) setEntityAvailability(entityId, hasValue);

    if (hasValue) {
        setValue(entityId, builder->_startupValue);
    }
}

void HomeAssistantArduinoMQTT::clearSetTopic(const char* item) {
    char sanitizedItem[32];
    sanitizeID(item, sanitizedItem, sizeof(sanitizedItem));
    
    char topicBuffer[80];
    snprintf(topicBuffer, sizeof(topicBuffer), HAKeys::TOPIC_4_PH, VALUE_TOPIC_PREFIX, _sanitizedDeviceName, sanitizedItem, HAKeys::TOPIC_COMMAND);
    mqttClient->publish(topicBuffer, "", false);
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
        values[emptySlot].availabilitySent = 0;
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
        bool previousAvailable = values[targetIndex].lastAvailable;

        if (values[targetIndex].item[0] == '\0') {
            strncpy(values[targetIndex].item, sanitizedItem, sizeof(values[targetIndex].item) - 1);
            values[targetIndex].item[sizeof(values[targetIndex].item) - 1] = '\0';
            values[targetIndex].value[0] = '\0';
            values[targetIndex].lastAvailable = isAvailable ? 1 : 0;
            values[targetIndex].availabilitySent = 0;
        } else {
            if (previousAvailable != isAvailable) {
                values[targetIndex].lastAvailable = isAvailable ? 1 : 0;
                values[targetIndex].availabilitySent = 0;
            }
        }
    }
}

void HomeAssistantArduinoMQTT::readValues() {
    _readValuesEnabled = true;
    if (mqttClient && mqttClient->connected()) {
        char topicBuffer[80];
        snprintf(topicBuffer, sizeof(topicBuffer), HAKeys::TOPIC_4_PH, VALUE_TOPIC_PREFIX, _sanitizedDeviceName, "+", HAKeys::TOPIC_STATE);
        mqttClient->subscribe(topicBuffer);
    }
}

void HomeAssistantArduinoMQTT::_sendSingleValue(int i) {
    char topicBuffer[96]; 

    if (!values[i].availabilitySent) {
        snprintf(topicBuffer, sizeof(topicBuffer), HAKeys::TOPIC_4_PH, VALUE_TOPIC_PREFIX, _sanitizedDeviceName, values[i].item, HAKeys::AVAILABILITY);
        const char* payload = values[i].lastAvailable ? HAKeys::ONLINE_PAYLOAD : HAKeys::OFFLINE_PAYLOAD;
        if (mqttClient->publish(topicBuffer, payload, true)) {
            values[i].availabilitySent = 1;
        }
    }

    if (!values[i].valueChanged && !values[i].isFirstValue) {
        return;
    }

    snprintf(topicBuffer, sizeof(topicBuffer), HAKeys::TOPIC_4_PH, VALUE_TOPIC_PREFIX, _sanitizedDeviceName, values[i].item, HAKeys::TOPIC_STATE);
    if (mqttClient->publish(topicBuffer, values[i].value, true)) {
        values[i].isFirstValue = 0;
        values[i].valueChanged = 0;
    }
}

void HomeAssistantArduinoMQTT::sendValues() {
    if (!mqttClient || !mqttClient->connected()) return;

    for (int i = 0; i < maxEntityNum; i++) {
        if (values[i].item[0] != '\0') {
            _sendSingleValue(i);
        }
    }
}

void HomeAssistantArduinoMQTT::sendValue(const char* item) {
    if (!mqttClient || !mqttClient->connected()) return;

    char sanitizedItem[32];
    sanitizeID(item, sanitizedItem, sizeof(sanitizedItem));

    for (int i = 0; i < maxEntityNum; i++) {
        if (values[i].item[0] != '\0' && strcmp(values[i].item, sanitizedItem) == 0) {
            _sendSingleValue(i);
            break;
        }
    }
}

void HomeAssistantArduinoMQTT::sendCommand(const char* commandTopic, const char* payload) {
    char topicBuffer[80];
    snprintf(topicBuffer, sizeof(topicBuffer), HAKeys::TOPIC_3_PH, VALUE_TOPIC_PREFIX, _sanitizedDeviceName, commandTopic);
    mqttClient->publish(topicBuffer, payload, false);
}

void HomeAssistantArduinoMQTT::sendEvent(const char* eventName, const char* eventType) {
    char payload[64];
    snprintf(payload, sizeof(payload), "{\"event_type\":\"%s\"}", eventType);
    sendCommand(eventName, payload);
}

void HomeAssistantArduinoMQTT::MqttCallback(char* topic, byte* payload, unsigned int length) {
    if (!commandEnabled) {
        return;
    }

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
                        if (_callbackListener != nullptr) _callbackListener->onMQTTMessage(item, cPayload, false);
                    } else if (strcmp(action, HAKeys::TOPIC_STATE) == 0) {
                        setValue(item, cPayload);
                        if (_callbackListener != nullptr) _callbackListener->onMQTTMessage(item, cPayload, true);
                    }
                }
            }
        }
    }
}


HAEntityBuilder::HAEntityBuilder(HomeAssistantArduinoMQTT* mqtt, const char* type, const char* id, const char* name)
    : _mqtt(mqtt), _type(type), _name(name), _id(id), _commandTopicName(nullptr), _startupValue(nullptr) {}

void HAEntityBuilder::category(const char* val) { _category = val; }
void HAEntityBuilder::deviceClass(const char* val) { _deviceClass = val; }
void HAEntityBuilder::stateClass(const char* val) { _stateClass = val; }
void HAEntityBuilder::icon(const char* val) { _icon = val; }
void HAEntityBuilder::unit(const char* val) { _unit = val; }
void HAEntityBuilder::startup(const char* val) { _startupValue = val; }

void HAEntityBuilder::command(bool enable, const char* customName) {
    _commandTopic = enable;
    _commandTopicName = customName;
}

void HAEntityBuilder::state(bool enable) { _stateTopic = enable; }
void HAEntityBuilder::independentAvailability(bool enable) { _indAvail = enable; }

void HAEntityBuilder::suggestedDisplayPrecision(uint8_t precision) {
    _suggestedPrecision = precision;
    _suggestedPrecisionEnable = true;
}

void HAEntityBuilder::set(const char* key, const char* value) {
    if (_customPropCount < 6) _customProps[_customPropCount++] = {key, value, 0, false, 0};
}

void HAEntityBuilder::set(const char* key, int value) {
    if (_customPropCount < 6) _customProps[_customPropCount++] = {key, nullptr, value, false, 1};
}

void HAEntityBuilder::set(const char* key, bool value) {
    if (_customPropCount < 6) _customProps[_customPropCount++] = {key, nullptr, 0, value, 2};
}

void HAEntityBuilder::publish() {
    if (_mqtt) {
        _mqtt->publishConfig(this);
    }
}