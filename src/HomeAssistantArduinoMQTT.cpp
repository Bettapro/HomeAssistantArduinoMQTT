#include "HomeAssistantArduinoMQTT.h"


namespace {

inline size_t appChar(char* d, size_t cap, size_t p, char c) {
    if (p + 1 < cap) d[p++] = c;
    return p;
}

size_t appRam(char* d, size_t cap, size_t p, const char* s) {
    if (!s) return p;
    while (*s && p + 1 < cap) d[p++] = *s++;
    return p;
}

size_t appPgm(char* d, size_t cap, size_t p, const char* sP) {
    if (!sP) return p;
    char c;
    while ((c = (char)pgm_read_byte(sP++)) != '\0' && p + 1 < cap) d[p++] = c;
    return p;
}

void copyStr(char* dst, size_t cap, const char* src) {
    size_t p = 0;
    if (!cap) return;
    if (src) while (src[p] && p + 1 < cap) { dst[p] = src[p]; p++; }
    dst[p] = '\0';
}

inline bool isEmptyStr(const char* s) { return s == nullptr || s[0] == '\0'; }

void writeTopicValue(HAJsonStream& js, const char* dev,
                     const char* a, bool aPgm,
                     const char* b, bool bPgm) {
    js.strBegin();
    js.strAdd(HAKeys::VALUE_TOPIC_PREFIX);
    js.strAddChar('/');
    js.strAdd(dev);
    js.strAddChar('/');
    if (aPgm) js.strAddP(a); else js.strAdd(a);
    if (b) {
        js.strAddChar('/');
        if (bPgm) js.strAddP(b); else js.strAdd(b);
    }
    js.strEnd();
}

}  // namespace


int HAAMFmt::uintToBuf(char* buf, size_t size, unsigned long val) {
    char tmp[11];
    uint8_t n = 0;
    do { tmp[n++] = (char)('0' + (val % 10)); val /= 10; } while (val);

    size_t p = 0;
    if (size) {
        while (n && p + 1 < size) buf[p++] = tmp[--n];
        buf[p] = '\0';
    }
    return (int)p;
}

int HAAMFmt::strToBuf(char* buf, size_t size, const char* val) {
    size_t p = 0;
    if (!size) return 0;
    if (val) while (val[p] && p + 1 < size) { buf[p] = val[p]; p++; }
    buf[p] = '\0';
    return (int)p;
}

// ===========================================================================
//  HAJsonStream
// ===========================================================================
HAJsonStream::HAJsonStream(Print* out)
    : _out(out), _len(0), _n(0), _first(true), _afterKey(false) {}

void HAJsonStream::put(char c) {
    _len++;
    if (!_out) return;                       
    _buf[_n++] = c;
    if (_n == sizeof(_buf)) flush();
}

void HAJsonStream::flush() {
    if (_out && _n) _out->write((const uint8_t*)_buf, _n);
    _n = 0;
}

void HAJsonStream::putP(const char* sP) {
    char c;
    while ((c = (char)pgm_read_byte(sP++)) != '\0') put(c);
}

void HAJsonStream::esc(char c) {
    switch (c) {
        case '"':  put('\\'); put('"');  break;
        case '\\': put('\\'); put('\\'); break;
        case '\n': put('\\'); put('n');  break;
        case '\r': put('\\'); put('r');  break;
        case '\t': put('\\'); put('t');  break;
        default:   if ((uint8_t)c >= 0x20) put(c); break;
    }
}

void HAJsonStream::sep() {
    if (_afterKey) { _afterKey = false; return; }
    if (!_first) put(',');
    _first = false;
}

void HAJsonStream::openObj()  { sep(); put('{'); _first = true; }
void HAJsonStream::closeObj() { put('}'); _first = false; }
void HAJsonStream::openArr()  { sep(); put('['); _first = true; }
void HAJsonStream::closeArr() { put(']'); _first = false; }

void HAJsonStream::key(const char* keyP) {
    if (!_first) put(',');
    _first = false;
    put('"'); putP(keyP); put('"'); put(':');
    _afterKey = true;
}

void HAJsonStream::strBegin()               { sep(); put('"'); }
void HAJsonStream::strEnd()                 { put('"'); }
void HAJsonStream::strAddChar(char c)       { esc(c); }
void HAJsonStream::strAdd(const char* s)    { if (s) while (*s) esc(*s++); }
void HAJsonStream::strAddP(const char* sP)  {
    if (!sP) return;
    char c;
    while ((c = (char)pgm_read_byte(sP++)) != '\0') esc(c);
}

void HAJsonStream::str(const char* s)   { strBegin(); strAdd(s);  strEnd(); }
void HAJsonStream::strP(const char* sP) { strBegin(); strAddP(sP); strEnd(); }

void HAJsonStream::num(long v) {
    sep();
    if (v < 0) { put('-'); v = -v; }
    char tmp[11];
    uint8_t n = 0;
    do { tmp[n++] = (char)('0' + (v % 10)); v /= 10; } while (v);
    while (n) put(tmp[--n]);
}

void HAJsonStream::boolean(bool v) {
    sep();
    putP(v ? PSTR("true") : PSTR("false"));
}

// ===========================================================================
//  HomeAssistantArduinoMQTT
// ===========================================================================
HomeAssistantArduinoMQTT::HomeAssistantArduinoMQTT(uint8_t maxN) {
    mqttClient = nullptr;
    _client = nullptr;
    _callbackListener = nullptr;
    maxEntityNum = maxN;

    values = new ItemValue[maxEntityNum]();  

    for (uint8_t i = 0; i < maxEntityNum; i++) {
        values[i].isFirstValue = 1;
        values[i].valueChanged = 1;
    }

    StatusTopic[0] = '\0';
    _sanitizedDeviceName[0] = '\0';
    _topicBuf[0] = '\0';
    _lastReconnectAttempt = 0;
    _readValuesEnabled = false;
}

HomeAssistantArduinoMQTT::~HomeAssistantArduinoMQTT() {
    delete mqttClient;
    delete[] values;
}

void HomeAssistantArduinoMQTT::sanitizeID(const char* input, char* output, size_t maxLen) {
    if (!output || maxLen == 0) return;
    if (!input) { output[0] = '\0'; return; }

    size_t w = 0;
    bool lastWasUnderscore = false;

    for (const char* p = input; *p && w + 1 < maxLen; p++) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c += 32;
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            output[w++] = c;
            lastWasUnderscore = false;
        } else if ((c == ' ' || c == '-' || c == '_') && w > 0 && !lastWasUnderscore) {
            output[w++] = '_';
            lastWasUnderscore = true;
        }
    }
    if (w > 0 && output[w - 1] == '_') w--;
    output[w] = '\0';
}

void HomeAssistantArduinoMQTT::_valueTopic(const char* a, bool aPgm,
                                           const char* b, bool bPgm) {
    const size_t cap = sizeof(_topicBuf);
    size_t p = 0;
    p = appRam(_topicBuf, cap, p, HAKeys::VALUE_TOPIC_PREFIX);
    p = appChar(_topicBuf, cap, p, '/');
    p = appRam(_topicBuf, cap, p, _sanitizedDeviceName);
    p = appChar(_topicBuf, cap, p, '/');
    p = aPgm ? appPgm(_topicBuf, cap, p, a) : appRam(_topicBuf, cap, p, a);
    if (b) {
        p = appChar(_topicBuf, cap, p, '/');
        p = bPgm ? appPgm(_topicBuf, cap, p, b) : appRam(_topicBuf, cap, p, b);
    }
    _topicBuf[p] = '\0';
}

int16_t HomeAssistantArduinoMQTT::_lookup(const char* item, bool allocate, bool* isNew) {
    if (isNew) *isNew = false;
    if (isEmptyStr(item)) return -1;

    int16_t empty = -1;
    for (uint8_t i = 0; i < maxEntityNum; i++) {
        if (values[i].item[0] == '\0') {
            if (empty < 0) empty = (int16_t)i;
        } else if (strcmp(values[i].item, item) == 0) {
            return (int16_t)i;
        }
    }

    char sanitized[HAAM_ITEM_LEN];
    sanitizeID(item, sanitized, sizeof(sanitized));
    if (sanitized[0] == '\0') return -1;

    if (strcmp(sanitized, item) != 0) {
        for (uint8_t i = 0; i < maxEntityNum; i++) {
            if (values[i].item[0] != '\0' && strcmp(values[i].item, sanitized) == 0) {
                return (int16_t)i;
            }
        }
    }

    if (!allocate || empty < 0) return -1;

    copyStr(values[empty].item, sizeof(values[empty].item), sanitized);
    values[empty].value[0] = '\0';
    if (isNew) *isNew = true;
    return empty;
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

    _valueTopic(HAKeys::AVAILABILITY, true, nullptr, false);
    copyStr(StatusTopic, sizeof(StatusTopic), _topicBuf);

    if (mqttClient == nullptr) {
        mqttClient = new PubSubClient(*_client);
    } else {
        mqttClient->setClient(*_client);
    }
    mqttClient->setBufferSize(bufferSize);
    mqttClient->setServer(server, port);

    mqttClient->setCallback([this](char* topic, byte* payload, unsigned int length) {
        this->MqttCallback(topic, payload, length);
    });

    mqttClient->setKeepAlive(keepAlive);
}

void HomeAssistantArduinoMQTT::loop() {
    if (!mqttClient) return;
    if (mqttClient->connected()) {
        mqttClient->loop();
    } else {
        connect();
    }
}

void HomeAssistantArduinoMQTT::connect() {
    unsigned long now = millis();
    if (_lastReconnectAttempt != 0 && (now - _lastReconnectAttempt) < 5000) return;
    _lastReconnectAttempt = now;

    bool success;

    if (useSharedAvailability) {
        char offlinePayload[16];
        strcpy_P(offlinePayload, HAKeys::OFFLINE_PAYLOAD);
        success = mqttClient->connect(_sanitizedDeviceName, MqttUser, MqttPassword, StatusTopic, 1, true, offlinePayload);
    } else {
        success = mqttClient->connect(_sanitizedDeviceName, MqttUser, MqttPassword);
    }

    if (!success) return;

    for (uint8_t i = 0; i < maxEntityNum; i++) {
        if (values[i].item[0] != '\0') {
            values[i].valueChanged = 1;
            values[i].availabilitySent = 0;
        }
    }

    if (useSharedAvailability) {
        mqttClient->publish_P(StatusTopic, (const uint8_t*)HAKeys::ONLINE_PAYLOAD, strlen_P(HAKeys::ONLINE_PAYLOAD), true);
    }

    if (_readValuesEnabled) {
        _valueTopic("+", false, HAKeys::TOPIC_STATE, false);
        mqttClient->subscribe(_topicBuf);
    }

    if (commandEnabled) {
        _valueTopic("+", false, HAKeys::TOPIC_COMMAND, false);
        mqttClient->subscribe(_topicBuf);
    }
}

bool HomeAssistantArduinoMQTT::connected() {
    return mqttClient && mqttClient->connected();
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


void HomeAssistantArduinoMQTT::_writeConfig(HAJsonStream& js, HAEntityBuilder* b, const char* entityId) {
    js.openObj();

    js.key(HAKeys::AVAILABILITY);
    js.openArr();
    uint8_t avtyCount = 0;
    if (useSharedAvailability) {
        js.openObj();
        js.key(HAKeys::TOPIC);
        js.str(StatusTopic);
        js.closeObj();
        avtyCount++;
    }
    if (b->_indAvail) {
        js.openObj();
        js.key(HAKeys::TOPIC);
        writeTopicValue(js, _sanitizedDeviceName, entityId, false, HAKeys::AVAILABILITY, true);
        js.closeObj();
        avtyCount++;
    }
    js.closeArr();

    if (avtyCount > 1) {
        js.key(HAKeys::AVAILABILITY_MODE);
        js.strP(HAKeys::AVAILABILITY_MODE_ALL);
    }

    js.key(HAKeys::DEVICE);
    js.openObj();
        js.key(HAKeys::IDENTIFIERS);
        js.openArr();
        js.str(_sanitizedDeviceName);
        js.closeArr();
        js.key(HAKeys::MANUFACTURER); js.str(Manufacturer);
        js.key(HAKeys::MODEL);        js.str(Model);
        js.key(HAKeys::NAME);         js.str(HADeviceName);
        js.key(HAKeys::SW_VERSION);   js.str(Version);
        if (!isEmptyStr(ConfigurationUrl)) {
            js.key(HAKeys::CONFIGURATION_URL); js.str(ConfigurationUrl);
        }
    js.closeObj();

    if (!isEmptyStr(b->_name))          { js.key(HAKeys::NAME); js.str(b->_name); }
    if (b->_suggestedPrecisionEnable)   { js.key(HAKeys::SUGGESTED_DISPLAY_PRECISION); js.num(b->_suggestedPrecision); }
    if (b->_category)                   { js.key(HAKeys::ENTITY_CATEGORY); js.str(b->_category); }
    if (b->_deviceClass)                { js.key(HAKeys::DEVICE_CLASS); js.str(b->_deviceClass); }
    if (b->_stateClass)                 { js.key(HAKeys::STATE_CLASS); js.str(b->_stateClass); }
    if (b->_icon)                       { js.key(HAKeys::ICON); js.str(b->_icon); }
    if (b->_unit)                       { js.key(HAKeys::UNIT_OF_MEASUREMENT); js.str(b->_unit); }

    for (uint8_t i = 0; i < b->_customPropCount; i++) {
        const HACustomProp& cp = b->_customProps[i];
        js.key(cp.key);
        switch (cp.type) {
            case 0:  js.strP(cp.valStr); break;          
            case 1:  js.num(cp.valInt); break;
            default: js.boolean(cp.valBool); break;
        }
    }

    js.key(HAKeys::UNIQUE_ID);
    js.strBegin();
    if (prefixUniqueIds) { js.strAdd(_sanitizedDeviceName); js.strAddChar('_'); }
    js.strAdd(entityId);
    js.strEnd();

    js.key(HAKeys::ENABLED_DEFAULT);
    js.boolean(true);

    if (b->_commandTopic) {
        const char* cmdName = isEmptyStr(b->_commandTopicName) ? entityId : b->_commandTopicName;
        js.key(HAKeys::COMMAND_TOPIC);
        writeTopicValue(js, _sanitizedDeviceName, cmdName, false, HAKeys::TOPIC_COMMAND, false);
    }

    if (b->_stateTopic) {
        js.key(HAKeys::STATE_TOPIC);
        writeTopicValue(js, _sanitizedDeviceName, entityId, false, HAKeys::TOPIC_STATE, false);
    }

    js.closeObj();
}

void HomeAssistantArduinoMQTT::publishConfig(HAEntityBuilder* builder) {
    char entityId[HAAM_ITEM_LEN];

    if (!isEmptyStr(builder->_id)) {
        sanitizeID(builder->_id, entityId, sizeof(entityId));
    } else if (!isEmptyStr(builder->_name)) {
        sanitizeID(builder->_name, entityId, sizeof(entityId));
    } else {
        entityId[0] = '\0';
    }

    if (enableConfigPublishing && mqttClient && mqttClient->connected()) {
        size_t payloadLen;
        {   
            HAJsonStream measure(nullptr);
            _writeConfig(measure, builder, entityId);
            payloadLen = measure.length();
        }

        const size_t cap = sizeof(_topicBuf);
        size_t p = 0;
        p = appPgm(_topicBuf, cap, p, HAKeys::PREFIX);
        p = appChar(_topicBuf, cap, p, '/');
        p = appRam(_topicBuf, cap, p, builder->_type);
        p = appChar(_topicBuf, cap, p, '/');
        p = appRam(_topicBuf, cap, p, _sanitizedDeviceName);
        p = appChar(_topicBuf, cap, p, '/');
        p = appRam(_topicBuf, cap, p, entityId);
        p = appChar(_topicBuf, cap, p, '/');
        p = appRam(_topicBuf, cap, p, HAKeys::TOPIC_CONFIG);
        _topicBuf[p] = '\0';

        if (mqttClient->beginPublish(_topicBuf, payloadLen, true)) {
            HAJsonStream out(mqttClient);
            _writeConfig(out, builder, entityId);
            out.flush();
            mqttClient->endPublish();
        }
    }

    int16_t slot = _lookup(entityId, true);
    if (slot >= 0) {
        values[slot].isConfigured = 1;
        values[slot].hasIndAvail = builder->_indAvail ? 1 : 0;
    }

    bool hasValue = builder->_stateTopic && !isEmptyStr(builder->_startupValue);

    if (builder->_indAvail) setEntityAvailability(entityId, hasValue);
    if (hasValue) setValue(entityId, builder->_startupValue);
}

void HomeAssistantArduinoMQTT::clearSetTopic(const char* item) {
    if (!mqttClient) return;

    char sanitizedItem[HAAM_ITEM_LEN];
    sanitizeID(item, sanitizedItem, sizeof(sanitizedItem));

    _valueTopic(sanitizedItem, false, HAKeys::TOPIC_COMMAND, false);
    mqttClient->publish(_topicBuf, "", false);
}

void HomeAssistantArduinoMQTT::setValue(const char* item, const char* value, bool markAsChanged) {
    if (!value) value = "";

    bool isNew = false;
    int16_t i = _lookup(item, true, &isNew);
    if (i < 0) return;

    ItemValue& v = values[i];

    if (isNew) {
        copyStr(v.value, sizeof(v.value), value);
        v.valueChanged = markAsChanged ? 1 : 0;
        v.lastAvailable = 0;
        v.isFirstValue = 1;
        v.isConfigured = 0;
        v.availabilitySent = 0;
        return;
    }

    if (strcmp(v.value, value) != 0) {
        copyStr(v.value, sizeof(v.value), value);
        if (markAsChanged) v.valueChanged = 1;
    }
}

const char* HomeAssistantArduinoMQTT::getValue(const char* item) {
    int16_t i = _lookup(item, false);
    return (i >= 0) ? values[i].value : "";
}

void HomeAssistantArduinoMQTT::setEntityAvailability(const char* entityId, bool isAvailable) {
    bool isNew = false;
    int16_t i = _lookup(entityId, true, &isNew);
    if (i < 0) return;

    if (isNew) {
        values[i].lastAvailable = isAvailable ? 1 : 0;
        values[i].availabilitySent = 0;
    } else if ((bool)values[i].lastAvailable != isAvailable) {
        values[i].lastAvailable = isAvailable ? 1 : 0;
        values[i].availabilitySent = 0;
    }
}

void HomeAssistantArduinoMQTT::readValues() {
    _readValuesEnabled = true;
    if (mqttClient && mqttClient->connected()) {
        _valueTopic("+", false, HAKeys::TOPIC_STATE, false);
        mqttClient->subscribe(_topicBuf);
    }
}

bool HomeAssistantArduinoMQTT::_sendSingleValue(int i, bool force) {
    ItemValue& v = values[i];

    if (v.value[0] == '\0') {
        if (v.hasIndAvail && (force || !v.availabilitySent || v.lastAvailable)) {
            _valueTopic(v.item, false, HAKeys::AVAILABILITY, true);
            if (!mqttClient->publish_P(_topicBuf, (const uint8_t*)HAKeys::OFFLINE_PAYLOAD, strlen_P(HAKeys::OFFLINE_PAYLOAD), true)) {
                return false;
            }
            v.availabilitySent = 1;
            v.lastAvailable = 0;
        }
        return true;
    }

    if (force || v.valueChanged || v.isFirstValue) {
        _valueTopic(v.item, false, HAKeys::TOPIC_STATE, false);
        if (!mqttClient->publish(_topicBuf, v.value, true)) return false;
        v.isFirstValue = 0;
        v.valueChanged = 0;
    }

    if (v.hasIndAvail && (force || !v.availabilitySent || !v.lastAvailable)) {
        _valueTopic(v.item, false, HAKeys::AVAILABILITY, true);
        if (!mqttClient->publish_P(_topicBuf, (const uint8_t*)HAKeys::ONLINE_PAYLOAD, strlen_P(HAKeys::ONLINE_PAYLOAD), true)) {
            return false;
        }
        v.availabilitySent = 1;
        v.lastAvailable = 1;
    }

    return true;
}

bool HomeAssistantArduinoMQTT::sendValues(bool force) {
    if (!mqttClient || !mqttClient->connected()) return false;

    bool sent = true;
    for (uint8_t i = 0; i < maxEntityNum; i++) {
        if (values[i].item[0] != '\0') sent &= _sendSingleValue(i, force);
    }
    return sent;
}

bool HomeAssistantArduinoMQTT::sendValue(const char* item, bool force) {
    if (!mqttClient || !mqttClient->connected()) return false;

    int16_t i = _lookup(item, false);
    return (i >= 0) ? _sendSingleValue(i, force) : false;
}

void HomeAssistantArduinoMQTT::sendCommand(const char* commandTopic, const char* payload) {
    if (!mqttClient) return;
    _valueTopic(commandTopic, false, nullptr, false);
    mqttClient->publish(_topicBuf, payload, false);
}

void HomeAssistantArduinoMQTT::sendEvent(const char* eventName, const char* eventType) {
    char payload[HAAM_PAYLOAD_LEN];
    const size_t cap = sizeof(payload);
    size_t p = 0;
    p = appPgm(payload, cap, p, PSTR("{\"event_type\":\""));
    p = appRam(payload, cap, p, eventType);
    p = appPgm(payload, cap, p, PSTR("\"}"));
    payload[p] = '\0';
    sendCommand(eventName, payload);
}

void HomeAssistantArduinoMQTT::MqttCallback(char* topic, byte* payload, unsigned int length) {
    if (!commandEnabled) return;

    constexpr size_t prefixLen = sizeof(HAKeys::VALUE_TOPIC_PREFIX) - 1;
    if (strncmp(topic, HAKeys::VALUE_TOPIC_PREFIX, prefixLen) != 0 || topic[prefixLen] != '/') return;

    const char* devPtr = topic + prefixLen + 1;
    size_t devLen = strlen(_sanitizedDeviceName);
    if (strncmp(devPtr, _sanitizedDeviceName, devLen) != 0 || devPtr[devLen] != '/') return;

    const char* remainder = devPtr + devLen + 1;
    const char* lastSlash = strrchr(remainder, '/');
    if (lastSlash == nullptr) return;

    size_t itemLen = (size_t)(lastSlash - remainder);
    if (itemLen >= HAAM_ITEM_LEN) return;

    const char* action = lastSlash + 1;
    bool isCommand = (strcmp(action, HAKeys::TOPIC_COMMAND) == 0);
    bool isState = (!isCommand && strcmp(action, HAKeys::TOPIC_STATE) == 0);
    if (!isCommand && !isState) return;

    char item[HAAM_ITEM_LEN];
    memcpy(item, remainder, itemLen);
    item[itemLen] = '\0';

    char cPayload[HAAM_PAYLOAD_LEN];
    unsigned int copyLen = (length < sizeof(cPayload) - 1) ? length : (unsigned int)(sizeof(cPayload) - 1);
    memcpy(cPayload, payload, copyLen);
    cPayload[copyLen] = '\0';

    if (isState) setValue(item, cPayload, false);
    if (_callbackListener != nullptr) _callbackListener->onMQTTMessage(item, cPayload, isState);
}

// ===========================================================================
//  HAEntityBuilder
// ===========================================================================
HAEntityBuilder::HAEntityBuilder(HomeAssistantArduinoMQTT* mqtt, const char* type, const char* id, const char* name)
    : _mqtt(mqtt), _type(type), _name(name), _id(id),
      _commandTopicName(nullptr), _startupValue(nullptr),
      _category(nullptr), _deviceClass(nullptr), _stateClass(nullptr),
      _icon(nullptr), _unit(nullptr),
      _customPropCount(0), _suggestedPrecision(0),
      _commandTopic(0), _stateTopic(1), _indAvail(0), _suggestedPrecisionEnable(0) {}

void HAEntityBuilder::category(const char* val) { _category = val; }
void HAEntityBuilder::deviceClass(const char* val) { _deviceClass = val; }
void HAEntityBuilder::stateClass(const char* val) { _stateClass = val; }
void HAEntityBuilder::icon(const char* val) { _icon = val; }
void HAEntityBuilder::unit(const char* val) { _unit = val; }
void HAEntityBuilder::startup(const char* val) { _startupValue = val; }

void HAEntityBuilder::command(bool enable, const char* customName) {
    _commandTopic = enable ? 1 : 0;
    _commandTopicName = customName;
}

void HAEntityBuilder::state(bool enable) { _stateTopic = enable ? 1 : 0; }
void HAEntityBuilder::independentAvailability(bool enable) { _indAvail = enable ? 1 : 0; }

void HAEntityBuilder::suggestedDisplayPrecision(uint8_t precision) {
    _suggestedPrecision = precision;
    _suggestedPrecisionEnable = 1;
}

void HAEntityBuilder::set(const char* key, const char* value) {
    if (_customPropCount >= HAAM_MAX_CUSTOM_PROPS) return;
    HACustomProp& cp = _customProps[_customPropCount++];
    cp.key = key;
    cp.valStr = value;
    cp.type = 0;
}

void HAEntityBuilder::set(const char* key, int value) {
    if (_customPropCount >= HAAM_MAX_CUSTOM_PROPS) return;
    HACustomProp& cp = _customProps[_customPropCount++];
    cp.key = key;
    cp.valInt = value;
    cp.type = 1;
}

void HAEntityBuilder::set(const char* key, bool value) {
    if (_customPropCount >= HAAM_MAX_CUSTOM_PROPS) return;
    HACustomProp& cp = _customProps[_customPropCount++];
    cp.key = key;
    cp.valBool = value;
    cp.type = 2;
}

void HAEntityBuilder::publish() {
    if (_mqtt) _mqtt->publishConfig(this);
}
