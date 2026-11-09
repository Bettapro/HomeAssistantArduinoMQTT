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
    if (src)
        while (src[p] && p + 1 < cap) {
            dst[p] = src[p];
            p++;
        }
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
    if (aPgm)
        js.strAddP(a);
    else
        js.strAdd(a);
    if (b) {
        js.strAddChar('/');
        if (bPgm)
            js.strAddP(b);
        else
            js.strAdd(b);
    }
    js.strEnd();
}

}  // namespace

int HAAMFmt::uintToBuf(char* buf, size_t size, unsigned long val) {
    char tmp[11];
    uint8_t n = 0;
    do {
        tmp[n++] = (char)('0' + (val % 10));
        val /= 10;
    } while (val);

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
    if (val)
        while (val[p] && p + 1 < size) {
            buf[p] = val[p];
            p++;
        }
    buf[p] = '\0';
    return (int)p;
}

int HAAMFmt::floatToBuf(char* buf, size_t size, double val, uint8_t dec) {
    if (!size) return 0;
    buf[0] = '\0';
    if (val != val) return 0;  // NaN

    if (dec > 6) dec = 6;
    unsigned long mul = 1;
    for (uint8_t i = 0; i < dec; i++) mul *= 10;

    bool neg = val < 0;
    if (neg) val = -val;
    double scaled = val * (double)mul + 0.5;
    while (!(scaled < 4294967295.0) && dec) {  // too large: drop decimals
        dec--;
        mul /= 10;
        scaled = val * (double)mul + 0.5;
    }
    if (!(scaled < 4294967295.0)) return 0;  // still too large, or infinity

    unsigned long n = (unsigned long)scaled;
    size_t p = 0;
    if (neg && n != 0 && size > 1) buf[p++] = '-';  // no "-0.0"
    p += HAAMFmt::uintToBuf(buf + p, size - p, n / mul);

    if (dec && p + 1 < size) {
        buf[p++] = '.';
        unsigned long frac = n % mul;
        for (unsigned long d = mul / 10; d && p + 1 < size; d /= 10) {
            buf[p++] = (char)('0' + (frac / d) % 10);
        }
    }
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
        case '"':
            put('\\');
            put('"');
            break;
        case '\\':
            put('\\');
            put('\\');
            break;
        case '\n':
            put('\\');
            put('n');
            break;
        case '\r':
            put('\\');
            put('r');
            break;
        case '\t':
            put('\\');
            put('t');
            break;
        default:
            if ((uint8_t)c >= 0x20) put(c);
            break;
    }
}

void HAJsonStream::sep() {
    if (_afterKey) {
        _afterKey = false;
        return;
    }
    if (!_first) put(',');
    _first = false;
}

void HAJsonStream::openObj() {
    sep();
    put('{');
    _first = true;
}
void HAJsonStream::closeObj() {
    put('}');
    _first = false;
}
void HAJsonStream::openArr() {
    sep();
    put('[');
    _first = true;
}
void HAJsonStream::closeArr() {
    put(']');
    _first = false;
}

void HAJsonStream::key(const char* keyP) {
    if (!_first) put(',');
    _first = false;
    put('"');
    putP(keyP);
    put('"');
    put(':');
    _afterKey = true;
}

void HAJsonStream::keyStr(const char* s) {
    if (!_first) put(',');
    _first = false;
    put('"');
    strAdd(s);
    put('"');
    put(':');
    _afterKey = true;
}

void HAJsonStream::strBegin() {
    sep();
    put('"');
}
void HAJsonStream::strEnd() { put('"'); }
void HAJsonStream::strAddChar(char c) { esc(c); }
void HAJsonStream::strAdd(const char* s) {
    if (s)
        while (*s) esc(*s++);
}
void HAJsonStream::strAddP(const char* sP) {
    if (!sP) return;
    char c;
    while ((c = (char)pgm_read_byte(sP++)) != '\0') esc(c);
}

void HAJsonStream::str(const char* s) {
    strBegin();
    strAdd(s);
    strEnd();
}
void HAJsonStream::strP(const char* sP) {
    strBegin();
    strAddP(sP);
    strEnd();
}

void HAJsonStream::num(long v) {
    sep();
    if (v < 0) {
        put('-');
        v = -v;
    }
    char tmp[11];
    uint8_t n = 0;
    do {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    } while (v);
    while (n) put(tmp[--n]);
}

void HAJsonStream::boolean(bool v) {
    sep();
    putP(v ? PSTR("true") : PSTR("false"));
}

// ===========================================================================
//  HomeAssistantArduinoMQTT
// ===========================================================================

namespace {

typedef void (*EntityInit)(HAEntityBuilder& b);

HAEntityBuilder makeEntity(HomeAssistantArduinoMQTT* ha, uint8_t dev, const char* type,
                           const char* id, const char* name, EntityInit init) {
    HAEntityBuilder b(ha, type, id, name, dev);
    if (init) init(b);
    return b;
}

void initSensor(HAEntityBuilder& b) {
    b.state(true);
    b.independentAvailability(true);
}

void initBinarySensor(HAEntityBuilder& b) {
    b.independentAvailability(true);
    b.state(true);
    b.set(HAKeys::PAYLOAD_ON, HAKeys::VAL_TRUE);
    b.set(HAKeys::PAYLOAD_OFF, HAKeys::VAL_FALSE);
}

void initSwitch(HAEntityBuilder& b) {
    b.independentAvailability(true);
    b.command(true);
    b.state(true);
    b.set(HAKeys::PAYLOAD_ON, HAKeys::VAL_TRUE);
    b.set(HAKeys::PAYLOAD_OFF, HAKeys::VAL_FALSE);
}

void initButton(HAEntityBuilder& b) {
    b.independentAvailability(true);
    b.command(true);
    b.state(false);
    b.set(HAKeys::PAYLOAD_PRESS, HAKeys::VAL_PRESS);
}

void initCommandState(HAEntityBuilder& b) {  // number, select
    b.independentAvailability(true);
    b.command(true);
    b.state(true);
}

}  // namespace

#if !defined(MQTT_CALLBACK_FUNCTIONAL) && !defined(HAAM_NO_COMMANDS)
HomeAssistantArduinoMQTT* _myHaamInstance;

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    if (_myHaamInstance) _myHaamInstance->_handleMessage(topic, payload, length);
}
#endif

HomeAssistantArduinoMQTT::HomeAssistantArduinoMQTT(uint8_t maxN) {
    mqttClient = nullptr;
    _client = nullptr;
#ifndef HAAM_NO_COMMANDS
    _callbackListener = nullptr;
    _readValuesEnabled = false;
#endif
    maxEntityNum = maxN;

    values = new ItemValue[maxEntityNum]();

    for (uint8_t i = 0; i < maxEntityNum; i++) {
        values[i].isFirstValue = 1;
        values[i].valueChanged = 1;
    }

    _statusTopic[0] = '\0';
    _sanitizedDeviceName[0] = '\0';
    _topicBuf[0] = '\0';
    _lastReconnectAttempt = 0;

#ifdef HAAM_MULTI_DEVICE
    _subDeviceCount = 0;
#endif
#ifdef HAAM_DEVICE_DISCOVERY
    _ddStream = nullptr;
    _ddDevice = HAAM_ROOT_DEVICE;
    _ddActive = false;
    _ddRegister = false;
#endif

#if !defined(MQTT_CALLBACK_FUNCTIONAL) && !defined(HAAM_NO_COMMANDS)
    _myHaamInstance = this;
#endif
}

HomeAssistantArduinoMQTT::~HomeAssistantArduinoMQTT() {
    delete mqttClient;
    delete[] values;
}

void HomeAssistantArduinoMQTT::sanitizeID(const char* input, char* output, size_t maxLen) {
    if (!output || maxLen == 0) return;
    if (!input) {
        output[0] = '\0';
        return;
    }

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

// ---------------------------------------------------------------------------
//  Devices
// ---------------------------------------------------------------------------
uint8_t HomeAssistantArduinoMQTT::_deviceCount() const {
#ifdef HAAM_MULTI_DEVICE
    return (uint8_t)(1 + _subDeviceCount);
#else
    return 1;
#endif
}

bool HomeAssistantArduinoMQTT::_isValidDevice(uint8_t dev) const {
    return dev < _deviceCount();
}

const char* HomeAssistantArduinoMQTT::_deviceId(uint8_t dev) const {
#ifdef HAAM_MULTI_DEVICE
    if (dev != HAAM_ROOT_DEVICE && dev <= _subDeviceCount) return _subDevices[dev - 1].id;
#else
    (void)dev;
#endif
    return _sanitizedDeviceName;
}

uint8_t HomeAssistantArduinoMQTT::_itemDevice(uint8_t i) const {
#ifdef HAAM_MULTI_DEVICE
    return values[i].device;
#else
    (void)i;
    return HAAM_ROOT_DEVICE;
#endif
}

#ifdef HAAM_MULTI_DEVICE
uint8_t HomeAssistantArduinoMQTT::addDevice(const char* id, const char* name) {
    if (_subDeviceCount >= HAAM_MAX_DEVICES - 1) return HAAM_NO_DEVICE;

    char sid[HAAM_DEVICE_LEN];
    sanitizeID(id, sid, sizeof(sid));
    if (sid[0] == '\0') return HAAM_NO_DEVICE;

    // ids are used in topics and as HA identifiers: they must be unique
    char rootId[HAAM_DEVICE_LEN];
    sanitizeID(mqttDeviceName, rootId, sizeof(rootId));
    if (strcmp(sid, rootId) == 0) return HAAM_NO_DEVICE;
    for (uint8_t i = 0; i < _subDeviceCount; i++) {
        if (strcmp(sid, _subDevices[i].id) == 0) return HAAM_NO_DEVICE;
    }

    HAAMDevice& d = _subDevices[_subDeviceCount];
    d.name = name ? name : "";
    d.manufacturer = "";
    d.model = "";
    d.version = "";
    d.configurationUrl = "";
    d.useAvailability = false;
    copyStr(d.id, sizeof(d.id), sid);
    d.lastAvailable = 1;
    d.availabilitySent = 0;

    uint8_t dev = ++_subDeviceCount;

#ifndef HAAM_NO_COMMANDS
    // added while connected: subscribe now, connect() covers the next sessions
    if (mqttClient && mqttClient->connected()) {
        if (_readValuesEnabled) {
            _valueTopic(dev, "+", false, HAKeys::TOPIC_STATE, false);
            mqttClient->subscribe(_topicBuf);
        }
        if (commandEnabled) {
            _valueTopic(dev, "+", false, HAKeys::TOPIC_COMMAND, false);
            mqttClient->subscribe(_topicBuf);
        }
    }
#endif
    return dev;
}

HAAMDevice* HomeAssistantArduinoMQTT::getDevice(uint8_t dev) {
    if (dev == HAAM_ROOT_DEVICE || dev > _subDeviceCount) return nullptr;
    return &_subDevices[dev - 1];
}

void HomeAssistantArduinoMQTT::setDeviceAvailability(uint8_t dev, bool isAvailable) {
    HAAMDevice* d = getDevice(dev);
    if (!d) return;
    if ((bool)d->lastAvailable != isAvailable) {
        d->lastAvailable = isAvailable ? 1 : 0;
        d->availabilitySent = 0;
    }
}
#endif

// ---------------------------------------------------------------------------
//  Inbound messages: haam/<device id>/<item>/<set|state>
// ---------------------------------------------------------------------------
#ifndef HAAM_NO_COMMANDS
#ifdef MQTT_CALLBACK_FUNCTIONAL
void HomeAssistantArduinoMQTT::_mqttCallback(char* topic, byte* payload, unsigned int length) {
    _handleMessage(topic, payload, length);
}
#endif

void HomeAssistantArduinoMQTT::_handleMessage(char* topic, byte* payload, unsigned int length) {
    if (!commandEnabled) return;

    constexpr size_t prefixLen = sizeof(HAKeys::VALUE_TOPIC_PREFIX) - 1;
    if (strncmp(topic, HAKeys::VALUE_TOPIC_PREFIX, prefixLen) != 0 || topic[prefixLen] != '/') return;

    const char* devPtr = topic + prefixLen + 1;
    uint8_t dev = HAAM_NO_DEVICE;
    size_t devLen = 0;
    for (uint8_t d = 0; d < _deviceCount(); d++) {
        const char* id = _deviceId(d);
        size_t len = strlen(id);
        if (strncmp(devPtr, id, len) == 0 && devPtr[len] == '/') {
            dev = d;
            devLen = len;
            break;
        }
    }
    if (dev == HAAM_NO_DEVICE) return;

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

    // retained state echoed by the broker: keep the value, do not touch availability
    if (isState) _storeValue(dev, item, cPayload, false);
    if (_callbackListener != nullptr) {
#ifdef HAAM_MULTI_DEVICE
        _callbackListener->onMQTTMessage(dev, item, cPayload, isState);
#else
        _callbackListener->onMQTTMessage(item, cPayload, isState);
#endif
    }
}
#endif  // HAAM_NO_COMMANDS

void HomeAssistantArduinoMQTT::_valueTopic(uint8_t dev, const char* a, bool aPgm,
                                           const char* b, bool bPgm) {
    const size_t cap = sizeof(_topicBuf);
    size_t p = 0;
    p = appRam(_topicBuf, cap, p, HAKeys::VALUE_TOPIC_PREFIX);
    p = appChar(_topicBuf, cap, p, '/');
    p = appRam(_topicBuf, cap, p, _deviceId(dev));
    p = appChar(_topicBuf, cap, p, '/');
    p = aPgm ? appPgm(_topicBuf, cap, p, a) : appRam(_topicBuf, cap, p, a);
    if (b) {
        p = appChar(_topicBuf, cap, p, '/');
        p = bPgm ? appPgm(_topicBuf, cap, p, b) : appRam(_topicBuf, cap, p, b);
    }
    _topicBuf[p] = '\0';
}

#ifndef HAAM_NO_COMMANDS
void HomeAssistantArduinoMQTT::_subscribeAll(const char* action) {
    for (uint8_t d = 0; d < _deviceCount(); d++) {
        _valueTopic(d, "+", false, action, false);
        mqttClient->subscribe(_topicBuf);
    }
}
#endif

int16_t HomeAssistantArduinoMQTT::_lookup(uint8_t dev, const char* item, bool allocate, bool* isNew) {
    if (isNew) *isNew = false;
    if (isEmptyStr(item) || !_isValidDevice(dev)) return -1;

    int16_t empty = -1;
    for (uint8_t i = 0; i < maxEntityNum; i++) {
        if (values[i].item[0] == '\0') {
            if (empty < 0) empty = (int16_t)i;
        } else if (_itemDevice(i) == dev && strcmp(values[i].item, item) == 0) {
            return (int16_t)i;
        }
    }

    char sanitized[HAAM_ITEM_LEN];
    sanitizeID(item, sanitized, sizeof(sanitized));
    if (sanitized[0] == '\0') return -1;

    if (strcmp(sanitized, item) != 0) {
        for (uint8_t i = 0; i < maxEntityNum; i++) {
            if (values[i].item[0] != '\0' && _itemDevice(i) == dev && strcmp(values[i].item, sanitized) == 0) {
                return (int16_t)i;
            }
        }
    }

    if (!allocate || empty < 0) return -1;

    copyStr(values[empty].item, sizeof(values[empty].item), sanitized);
    values[empty].value[0] = '\0';
#ifdef HAAM_MULTI_DEVICE
    values[empty].device = dev;
#endif
    if (isNew) *isNew = true;
    return empty;
}

// finds or allocates the slot of an item; a new slot has no value and is unavailable
int16_t HomeAssistantArduinoMQTT::_allocate(uint8_t dev, const char* item) {
    bool isNew = false;
    int16_t i = _lookup(dev, item, true, &isNew);
    if (i >= 0 && isNew) {
        ItemValue& v = values[i];
        v.valueChanged = 0;
        v.lastAvailable = 0;
        v.isFirstValue = 1;
        v.isConfigured = 0;
        v.availabilitySent = 0;
    }
    return i;
}

int16_t HomeAssistantArduinoMQTT::_slot(HAAMEntity entity) const {
    int16_t i = entity.index;
    return (i >= 0 && i < (int16_t)maxEntityNum && values[i].item[0] != '\0') ? i : -1;
}

#ifndef HAAM_NO_COMMANDS
void HomeAssistantArduinoMQTT::setCallback(HAMQTTCallback* listener) {
    _callbackListener = listener;
}
#endif

void HomeAssistantArduinoMQTT::begin(Client& client, const char* server, const uint16_t port) {
    begin(client, server, port, 1024, 15);
}

void HomeAssistantArduinoMQTT::begin(Client& client, const char* server, const uint16_t port, const uint16_t bufferSize, const uint16_t keepAlive) {
    _client = &client;

    sanitizeID(mqttDeviceName, _sanitizedDeviceName, sizeof(_sanitizedDeviceName));

    _valueTopic(HAAM_ROOT_DEVICE, HAKeys::AVAILABILITY, true, nullptr, false);
    copyStr(_statusTopic, sizeof(_statusTopic), _topicBuf);

    if (mqttClient == nullptr) {
        mqttClient = new PubSubClient(client);
    } else {
        mqttClient->setClient(client);
    }
    mqttClient->setBufferSize(bufferSize);
    mqttClient->setServer(server, port);

#ifndef HAAM_NO_COMMANDS
#ifdef MQTT_CALLBACK_FUNCTIONAL
    mqttClient->setCallback([this](char* topic, byte* payload, unsigned int length) {
        this->_mqttCallback(topic, payload, length);
    });
#else
    mqttClient->setCallback([](char* topic, byte* payload, unsigned int length) {
        mqttCallback(topic, payload, length);
    });
#endif
#endif

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
        success = mqttClient->connect(_sanitizedDeviceName, mqttUser, mqttPassword, _statusTopic, 1, true, offlinePayload);
    } else {
        success = mqttClient->connect(_sanitizedDeviceName, mqttUser, mqttPassword);
    }

    if (!success) return;

    for (uint8_t i = 0; i < maxEntityNum; i++) {
        if (values[i].item[0] != '\0') {
            values[i].valueChanged = 1;
            values[i].availabilitySent = 0;
        }
    }
#ifdef HAAM_MULTI_DEVICE
    for (uint8_t d = 0; d < _subDeviceCount; d++) _subDevices[d].availabilitySent = 0;
#endif

    if (useSharedAvailability) {
        mqttClient->publish_P(_statusTopic, (const uint8_t*)HAKeys::ONLINE_PAYLOAD, strlen_P(HAKeys::ONLINE_PAYLOAD), true);
    }

#ifndef HAAM_NO_COMMANDS
    if (_readValuesEnabled) _subscribeAll(HAKeys::TOPIC_STATE);
    if (commandEnabled) _subscribeAll(HAKeys::TOPIC_COMMAND);
#endif
}

bool HomeAssistantArduinoMQTT::connected() {
    return mqttClient && mqttClient->connected();
}

// ---------------------------------------------------------------------------
//  Entity factories
// ---------------------------------------------------------------------------
HAEntityBuilder HomeAssistantArduinoMQTT::_newEntity(uint8_t dev, const char* type, const char* id, const char* name) {
    return HAEntityBuilder(this, type, id, name, dev);
}

HAEntityBuilder HomeAssistantArduinoMQTT::newEntity(const char* type, const char* id, const char* name) {
    return _newEntity(HAAM_ROOT_DEVICE, type, id, name);
}
HAEntityBuilder HomeAssistantArduinoMQTT::newSensorEntity(const char* id, const char* name) {
    return makeEntity(this, HAAM_ROOT_DEVICE, HAKeys::TYPE_SENSOR, id, name, initSensor);
}
HAEntityBuilder HomeAssistantArduinoMQTT::newBinarySensorEntity(const char* id, const char* name) {
    return makeEntity(this, HAAM_ROOT_DEVICE, HAKeys::TYPE_BINARY_SENSOR, id, name, initBinarySensor);
}
HAEntityBuilder HomeAssistantArduinoMQTT::newSwitchEntity(const char* id, const char* name) {
    return makeEntity(this, HAAM_ROOT_DEVICE, HAKeys::TYPE_SWITCH, id, name, initSwitch);
}
HAEntityBuilder HomeAssistantArduinoMQTT::newButtonEntity(const char* id, const char* name) {
    return makeEntity(this, HAAM_ROOT_DEVICE, HAKeys::TYPE_BUTTON, id, name, initButton);
}
HAEntityBuilder HomeAssistantArduinoMQTT::newNumberEntity(const char* id, const char* name) {
    return makeEntity(this, HAAM_ROOT_DEVICE, HAKeys::TYPE_NUMBER, id, name, initCommandState);
}
HAEntityBuilder HomeAssistantArduinoMQTT::newSelectEntity(const char* id, const char* name) {
    return makeEntity(this, HAAM_ROOT_DEVICE, HAKeys::TYPE_SELECT, id, name, initCommandState);
}

#ifdef HAAM_MULTI_DEVICE
HAEntityBuilder HomeAssistantArduinoMQTT::newEntity(uint8_t dev, const char* type, const char* id, const char* name) {
    return _newEntity(dev, type, id, name);
}
HAEntityBuilder HomeAssistantArduinoMQTT::newSensorEntity(uint8_t dev, const char* id, const char* name) {
    return makeEntity(this, dev, HAKeys::TYPE_SENSOR, id, name, initSensor);
}
HAEntityBuilder HomeAssistantArduinoMQTT::newBinarySensorEntity(uint8_t dev, const char* id, const char* name) {
    return makeEntity(this, dev, HAKeys::TYPE_BINARY_SENSOR, id, name, initBinarySensor);
}
HAEntityBuilder HomeAssistantArduinoMQTT::newSwitchEntity(uint8_t dev, const char* id, const char* name) {
    return makeEntity(this, dev, HAKeys::TYPE_SWITCH, id, name, initSwitch);
}
HAEntityBuilder HomeAssistantArduinoMQTT::newButtonEntity(uint8_t dev, const char* id, const char* name) {
    return makeEntity(this, dev, HAKeys::TYPE_BUTTON, id, name, initButton);
}
HAEntityBuilder HomeAssistantArduinoMQTT::newNumberEntity(uint8_t dev, const char* id, const char* name) {
    return makeEntity(this, dev, HAKeys::TYPE_NUMBER, id, name, initCommandState);
}
HAEntityBuilder HomeAssistantArduinoMQTT::newSelectEntity(uint8_t dev, const char* id, const char* name) {
    return makeEntity(this, dev, HAKeys::TYPE_SELECT, id, name, initCommandState);
}
#endif

// ---------------------------------------------------------------------------
//  Discovery payloads
// ---------------------------------------------------------------------------
void HomeAssistantArduinoMQTT::_writeDevice(HAJsonStream& js, uint8_t dev) {
    js.key(HAKeys::DEVICE);
    js.openObj();
    js.key(HAKeys::IDENTIFIERS);
    js.openArr();
    js.str(_deviceId(dev));
    js.closeArr();

#ifdef HAAM_MULTI_DEVICE
    if (dev != HAAM_ROOT_DEVICE) {
        const HAAMDevice& d = _subDevices[dev - 1];
        if (!isEmptyStr(d.manufacturer)) {
            js.key(HAKeys::MANUFACTURER);
            js.str(d.manufacturer);
        }
        if (!isEmptyStr(d.model)) {
            js.key(HAKeys::MODEL);
            js.str(d.model);
        }
        js.key(HAKeys::NAME);
        js.str(isEmptyStr(d.name) ? d.id : d.name);
        if (!isEmptyStr(d.version)) {
            js.key(HAKeys::SW_VERSION);
            js.str(d.version);
        }
        if (!isEmptyStr(d.configurationUrl)) {
            js.key(HAKeys::CONFIGURATION_URL);
            js.str(d.configurationUrl);
        }
        js.key(HAKeys::VIA_DEVICE);
        js.str(_sanitizedDeviceName);
        js.closeObj();
        return;
    }
#endif

    js.key(HAKeys::MANUFACTURER);
    js.str(manufacturer);
    js.key(HAKeys::MODEL);
    js.str(model);
    js.key(HAKeys::NAME);
    js.str(haDeviceName);
    js.key(HAKeys::SW_VERSION);
    js.str(version);
    if (!isEmptyStr(configurationUrl)) {
        js.key(HAKeys::CONFIGURATION_URL);
        js.str(configurationUrl);
    }
    js.closeObj();
}

// connection (LWT) + child device + entity availability, all required
void HomeAssistantArduinoMQTT::_writeAvailability(HAJsonStream& js, uint8_t dev, const char* entityId) {
    js.key(HAKeys::AVAILABILITY);
    js.openArr();
    uint8_t avtyCount = 0;
    if (useSharedAvailability) {
        js.openObj();
        js.key(HAKeys::TOPIC);
        js.str(_statusTopic);
        js.closeObj();
        avtyCount++;
    }
#ifdef HAAM_MULTI_DEVICE
    if (dev != HAAM_ROOT_DEVICE && _subDevices[dev - 1].useAvailability) {
        js.openObj();
        js.key(HAKeys::TOPIC);
        writeTopicValue(js, _deviceId(dev), HAKeys::AVAILABILITY, true, nullptr, false);
        js.closeObj();
        avtyCount++;
    }
#endif
    if (entityId) {
        js.openObj();
        js.key(HAKeys::TOPIC);
        writeTopicValue(js, _deviceId(dev), entityId, false, HAKeys::AVAILABILITY, true);
        js.closeObj();
        avtyCount++;
    }
    js.closeArr();

    if (avtyCount > 1) {
        js.key(HAKeys::AVAILABILITY_MODE);
        js.strP(HAKeys::AVAILABILITY_MODE_ALL);
    }
}

// component = true: entry of a device discovery "cmps" map (platform, no device block)
void HomeAssistantArduinoMQTT::_writeConfig(HAJsonStream& js, HAEntityBuilder* b, uint8_t dev, const char* entityId, bool component) {
    const char* devId = _deviceId(dev);

    js.openObj();

    if (component) {
        js.key(HAKeys::PLATFORM);
        js.str(b->_type);
    }

    _writeAvailability(js, dev, b->_indAvail ? entityId : nullptr);

    if (!component) _writeDevice(js, dev);

    if (!isEmptyStr(b->_name)) {
        js.key(HAKeys::NAME);
        js.str(b->_name);
    }
    if (b->_suggestedPrecisionEnable) {
        js.key(HAKeys::SUGGESTED_DISPLAY_PRECISION);
        js.num(b->_suggestedPrecision);
    }
    if (b->_category) {
        js.key(HAKeys::ENTITY_CATEGORY);
        js.str(b->_category);
    }
    if (b->_deviceClass) {
        js.key(HAKeys::DEVICE_CLASS);
        js.str(b->_deviceClass);
    }
    if (b->_stateClass) {
        js.key(HAKeys::STATE_CLASS);
        js.str(b->_stateClass);
    }
    if (b->_icon) {
        js.key(HAKeys::ICON);
        js.str(b->_icon);
    }
    if (b->_unit) {
        js.key(HAKeys::UNIT_OF_MEASUREMENT);
        js.str(b->_unit);
    }
    if (b->_options && b->_optionCount) {
        js.key(HAKeys::OPTIONS);
        js.openArr();
        for (uint8_t i = 0; i < b->_optionCount; i++) js.str(b->_options[i]);
        js.closeArr();
    }

    for (uint8_t i = 0; i < b->_customPropCount; i++) {
        const HACustomProp& cp = b->_customProps[i];
        js.key(cp.key);
        switch (cp.type) {
            case 0:
                js.strP(cp.valStr);
                break;
            case 1:
                js.num(cp.valInt);
                break;
            default:
                js.boolean(cp.valBool);
                break;
        }
    }

    js.key(HAKeys::UNIQUE_ID);
    js.strBegin();
    if (prefixUniqueIds) {
        js.strAdd(devId);
        js.strAddChar('_');
    }
    js.strAdd(entityId);
    js.strEnd();

    js.key(HAKeys::ENABLED_DEFAULT);
    js.boolean(true);

    if (b->_commandTopic) {
        const char* cmdName = isEmptyStr(b->_commandTopicName) ? entityId : b->_commandTopicName;
        js.key(HAKeys::COMMAND_TOPIC);
        writeTopicValue(js, devId, cmdName, false, HAKeys::TOPIC_COMMAND, false);
    }

    if (b->_stateTopic) {
        js.key(HAKeys::STATE_TOPIC);
        writeTopicValue(js, devId, entityId, false, HAKeys::TOPIC_STATE, false);
    }

    js.closeObj();
}

void HomeAssistantArduinoMQTT::_entityId(HAEntityBuilder* builder, char* out, size_t len) {
    if (!isEmptyStr(builder->_id)) {
        sanitizeID(builder->_id, out, len);
    } else if (!isEmptyStr(builder->_name)) {
        sanitizeID(builder->_name, out, len);
    } else {
        out[0] = '\0';
    }
}

int16_t HomeAssistantArduinoMQTT::publishConfig(HAEntityBuilder* builder) {
    char entityId[HAAM_ITEM_LEN];
    _entityId(builder, entityId, sizeof(entityId));
    uint8_t dev = builder->_device;

#ifdef HAAM_DEVICE_DISCOVERY
    if (_ddActive) {
        // inside publishDevice(): the entity belongs to the device being published
        if (entityId[0] == '\0') return -1;
        dev = _ddDevice;
        if (_ddStream) {
            _ddStream->keyStr(entityId);
            _writeConfig(*_ddStream, builder, dev, entityId, true);
        }
        // measuring pass: nothing is registered, return the slot if it exists
        return _ddRegister ? _registerEntity(builder, dev, entityId) : _lookup(dev, entityId, false);
    }
#endif

    if (!_isValidDevice(dev)) return -1;

    if (enableConfigPublishing && mqttClient && mqttClient->connected()) {
        size_t payloadLen;
        {
            HAJsonStream measure(nullptr);
            _writeConfig(measure, builder, dev, entityId, false);
            payloadLen = measure.length();
        }

        const size_t cap = sizeof(_topicBuf);
        size_t p = 0;
        p = appPgm(_topicBuf, cap, p, HAKeys::PREFIX);
        p = appChar(_topicBuf, cap, p, '/');
        p = appRam(_topicBuf, cap, p, builder->_type);
        p = appChar(_topicBuf, cap, p, '/');
        p = appRam(_topicBuf, cap, p, _deviceId(dev));
        p = appChar(_topicBuf, cap, p, '/');
        p = appRam(_topicBuf, cap, p, entityId);
        p = appChar(_topicBuf, cap, p, '/');
        p = appRam(_topicBuf, cap, p, HAKeys::TOPIC_CONFIG);
        _topicBuf[p] = '\0';

        if (mqttClient->beginPublish(_topicBuf, payloadLen, true)) {
            HAJsonStream out(mqttClient);
            _writeConfig(out, builder, dev, entityId, false);
            out.flush();
            mqttClient->endPublish();
        }
    }

    return _registerEntity(builder, dev, entityId);
}

int16_t HomeAssistantArduinoMQTT::_registerEntity(HAEntityBuilder* builder, uint8_t dev, const char* entityId) {
    int16_t slot = _allocate(dev, entityId);
    if (slot < 0) return -1;

    bool wasConfigured = values[slot].isConfigured;
    values[slot].isConfigured = 1;
    values[slot].hasIndAvail = builder->_indAvail ? 1 : 0;

    // a value stored before publish() (e.g. deep sleep) or since a previous publish() wins over startup()
    bool hasStored = builder->_stateTopic && values[slot].value[0] != '\0';
    bool hasStartup = builder->_stateTopic && !isEmptyStr(builder->_startupValue);

    if (hasStartup && !hasStored) {
        _setValueAt(slot, builder->_startupValue, true);
    } else if (!wasConfigured && builder->_indAvail) {
        // first publish(): initial availability; later ones keep what the application set.
        // Entities without a state (e.g. buttons) have no value to wait for: start available.
        _setAvailable(slot, hasStored || !builder->_stateTopic);
    }
    return slot;
}

#ifdef HAAM_DEVICE_DISCOVERY
void HomeAssistantArduinoMQTT::_writeDeviceHeader(HAJsonStream& js, uint8_t dev) {
    js.openObj();
    _writeDevice(js, dev);

    js.key(HAKeys::ORIGIN);
    js.openObj();
    js.key(HAKeys::NAME);
    js.strP(HAKeys::ORIGIN_NAME);
    js.key(HAKeys::SW_VERSION);
    js.strP(HAKeys::ORIGIN_VERSION);
    js.key(HAKeys::URL);
    js.strP(HAKeys::ORIGIN_URL);
    js.closeObj();

    js.key(HAKeys::COMPONENTS);
    js.openObj();
}

void HomeAssistantArduinoMQTT::_writeDeviceFooter(HAJsonStream& js) {
    js.closeObj();  // cmps
    js.closeObj();  // root
}

bool HomeAssistantArduinoMQTT::_publishDevice(uint8_t dev, HAAMDeviceDeclarer declare) {
    if (!declare || _ddActive || !_isValidDevice(dev)) return false;

    _ddActive = true;
    _ddDevice = dev;
    bool registered = false;
    bool published = false;

    if (enableConfigPublishing && mqttClient && mqttClient->connected()) {
        // pass 1: measure the payload, nothing is sent nor registered
        size_t payloadLen;
        {
            HAJsonStream measure(nullptr);
            _ddStream = &measure;
            _ddRegister = false;
            _writeDeviceHeader(measure, dev);
            declare(*this, dev);
            _writeDeviceFooter(measure);
            payloadLen = measure.length();
            _ddStream = nullptr;
        }

        const size_t cap = sizeof(_topicBuf);
        size_t p = 0;
        p = appPgm(_topicBuf, cap, p, HAKeys::PREFIX);
        p = appChar(_topicBuf, cap, p, '/');
        p = appRam(_topicBuf, cap, p, HAKeys::TOPIC_DEVICE);
        p = appChar(_topicBuf, cap, p, '/');
        p = appRam(_topicBuf, cap, p, _deviceId(dev));
        p = appChar(_topicBuf, cap, p, '/');
        p = appRam(_topicBuf, cap, p, HAKeys::TOPIC_CONFIG);
        _topicBuf[p] = '\0';

        // pass 2: stream the payload and register the entities
        if (mqttClient->beginPublish(_topicBuf, payloadLen, true)) {
            HAJsonStream out(mqttClient);
            _ddStream = &out;
            _ddRegister = true;
            _writeDeviceHeader(out, dev);
            declare(*this, dev);
            _writeDeviceFooter(out);
            out.flush();
            _ddStream = nullptr;
            published = mqttClient->endPublish() != 0;
            registered = true;
        }
    }

    if (!registered) {
        // offline or publishing disabled: register the entities only
        _ddStream = nullptr;
        _ddRegister = true;
        declare(*this, dev);
    }

    _ddStream = nullptr;
    _ddRegister = false;
    _ddActive = false;
    return published;
}

bool HomeAssistantArduinoMQTT::publishDevice(HAAMDeviceDeclarer declare) {
    return _publishDevice(HAAM_ROOT_DEVICE, declare);
}

#ifdef HAAM_MULTI_DEVICE
bool HomeAssistantArduinoMQTT::publishDevice(uint8_t dev, HAAMDeviceDeclarer declare) {
    return _publishDevice(dev, declare);
}
#endif
#endif

// ---------------------------------------------------------------------------
//  Values & availability
// ---------------------------------------------------------------------------
void HomeAssistantArduinoMQTT::_clearSetTopic(uint8_t dev, const char* item) {
    if (!mqttClient || !_isValidDevice(dev)) return;

    char sanitizedItem[HAAM_ITEM_LEN];
    sanitizeID(item, sanitizedItem, sizeof(sanitizedItem));
    _clearSetTopicAt(dev, sanitizedItem);
}

void HomeAssistantArduinoMQTT::_clearSetTopicAt(uint8_t dev, const char* sanitizedItem) {
    _valueTopic(dev, sanitizedItem, false, HAKeys::TOPIC_COMMAND, false);
    mqttClient->publish(_topicBuf, "", false);
}

int16_t HomeAssistantArduinoMQTT::_storeValue(uint8_t dev, const char* item, const char* value, bool markAsChanged) {
    int16_t i = _allocate(dev, item);
    if (i >= 0) _storeValueAt(i, value, markAsChanged);
    return i;
}

void HomeAssistantArduinoMQTT::_storeValueAt(int16_t i, const char* value, bool markAsChanged) {
    if (!value) value = "";
    ItemValue& v = values[i];
    if (strcmp(v.value, value) != 0) {
        copyStr(v.value, sizeof(v.value), value);
        if (markAsChanged) v.valueChanged = 1;
    }
}

void HomeAssistantArduinoMQTT::_setValueAt(int16_t i, const char* value, bool markAsChanged) {
    _storeValueAt(i, value, markAsChanged);
    // a value means the entity is available, an empty value means it is not
    _setAvailable(i, values[i].value[0] != '\0');
}

void HomeAssistantArduinoMQTT::_setValue(uint8_t dev, const char* item, const char* value, bool markAsChanged) {
    int16_t i = _allocate(dev, item);
    if (i >= 0) _setValueAt(i, value, markAsChanged);
}

const char* HomeAssistantArduinoMQTT::_getValue(uint8_t dev, const char* item) {
    int16_t i = _lookup(dev, item, false);
    return (i >= 0) ? values[i].value : "";
}

void HomeAssistantArduinoMQTT::_setAvailable(int16_t i, bool isAvailable) {
    if ((bool)values[i].lastAvailable != isAvailable) {
        values[i].lastAvailable = isAvailable ? 1 : 0;
        values[i].availabilitySent = 0;
    }
}

void HomeAssistantArduinoMQTT::_setEntityAvailability(uint8_t dev, const char* entityId, bool isAvailable) {
    int16_t i = _allocate(dev, entityId);
    if (i >= 0) _setAvailable(i, isAvailable);
}

#ifndef HAAM_NO_COMMANDS
void HomeAssistantArduinoMQTT::readValues() {
    _readValuesEnabled = true;
    if (mqttClient && mqttClient->connected()) _subscribeAll(HAKeys::TOPIC_STATE);
}
#endif

bool HomeAssistantArduinoMQTT::_sendSingleValue(int i, bool force) {
    ItemValue& v = values[i];
    uint8_t dev = _itemDevice((uint8_t)i);

    // state: only when there is a value to send
    if (v.value[0] != '\0' && (force || v.valueChanged || v.isFirstValue)) {
        _valueTopic(dev, v.item, false, HAKeys::TOPIC_STATE, false);
        if (!mqttClient->publish(_topicBuf, v.value, true)) return false;
        v.isFirstValue = 0;
        v.valueChanged = 0;
    }

    // availability: exactly as requested by setValue / setEntityAvailability
    if (v.hasIndAvail && (force || !v.availabilitySent)) {
        const char* payloadP = v.lastAvailable ? HAKeys::ONLINE_PAYLOAD : HAKeys::OFFLINE_PAYLOAD;
        _valueTopic(dev, v.item, false, HAKeys::AVAILABILITY, true);
        if (!mqttClient->publish_P(_topicBuf, (const uint8_t*)payloadP, strlen_P(payloadP), true)) {
            return false;
        }
        v.availabilitySent = 1;
    }

    return true;
}

bool HomeAssistantArduinoMQTT::sendValues(bool force) {
    if (!mqttClient || !mqttClient->connected()) return false;

    bool sent = true;

#ifdef HAAM_MULTI_DEVICE
    for (uint8_t d = 0; d < _subDeviceCount; d++) {
        HAAMDevice& sd = _subDevices[d];
        if (!sd.useAvailability || (!force && sd.availabilitySent)) continue;
        const char* payloadP = sd.lastAvailable ? HAKeys::ONLINE_PAYLOAD : HAKeys::OFFLINE_PAYLOAD;
        _valueTopic((uint8_t)(d + 1), HAKeys::AVAILABILITY, true, nullptr, false);
        if (mqttClient->publish_P(_topicBuf, (const uint8_t*)payloadP, strlen_P(payloadP), true)) {
            sd.availabilitySent = 1;
        } else {
            sent = false;
        }
    }
#endif

    for (uint8_t i = 0; i < maxEntityNum; i++) {
        if (values[i].item[0] != '\0') sent &= _sendSingleValue(i, force);
    }
    return sent;
}

bool HomeAssistantArduinoMQTT::_sendValue(uint8_t dev, const char* item, bool force) {
    if (!mqttClient || !mqttClient->connected()) return false;

    int16_t i = _lookup(dev, item, false);
    return (i >= 0) ? _sendSingleValue(i, force) : false;
}

void HomeAssistantArduinoMQTT::_sendCommand(uint8_t dev, const char* commandTopic, const char* payload) {
    if (!mqttClient || !_isValidDevice(dev)) return;
    _valueTopic(dev, commandTopic, false, nullptr, false);
    mqttClient->publish(_topicBuf, payload, false);
}

void HomeAssistantArduinoMQTT::_sendEvent(uint8_t dev, const char* eventName, const char* eventType) {
    char payload[HAAM_PAYLOAD_LEN];
    const size_t cap = sizeof(payload);
    size_t p = 0;
    p = appPgm(payload, cap, p, PSTR("{\"event_type\":\""));
    p = appRam(payload, cap, p, eventType);
    p = appPgm(payload, cap, p, PSTR("\"}"));
    payload[p] = '\0';
    _sendCommand(dev, eventName, payload);
}

// ---- root device API ----
void HomeAssistantArduinoMQTT::setValue(const char* item, const char* value, bool markAsChanged) {
    _setValue(HAAM_ROOT_DEVICE, item, value, markAsChanged);
}
const char* HomeAssistantArduinoMQTT::getValue(const char* item) {
    return _getValue(HAAM_ROOT_DEVICE, item);
}
void HomeAssistantArduinoMQTT::clearSetTopic(const char* item) {
    _clearSetTopic(HAAM_ROOT_DEVICE, item);
}
void HomeAssistantArduinoMQTT::setEntityAvailability(const char* entityId, bool isAvailable) {
    _setEntityAvailability(HAAM_ROOT_DEVICE, entityId, isAvailable);
}
bool HomeAssistantArduinoMQTT::sendValue(const char* item, bool force) {
    return _sendValue(HAAM_ROOT_DEVICE, item, force);
}
void HomeAssistantArduinoMQTT::sendCommand(const char* commandTopic, const char* payload) {
    _sendCommand(HAAM_ROOT_DEVICE, commandTopic, payload);
}
void HomeAssistantArduinoMQTT::sendEvent(const char* eventName, const char* eventType) {
    _sendEvent(HAAM_ROOT_DEVICE, eventName, eventType);
}

// ---- entity handles ----
HAAMEntity HomeAssistantArduinoMQTT::getEntity(const char* item) {
    return HAAMEntity(_lookup(HAAM_ROOT_DEVICE, item, false));
}
uint8_t HomeAssistantArduinoMQTT::entityDevice(HAAMEntity entity) const {
    int16_t i = _slot(entity);
    return (i >= 0) ? _itemDevice((uint8_t)i) : HAAM_NO_DEVICE;
}
const char* HomeAssistantArduinoMQTT::entityId(HAAMEntity entity) const {
    int16_t i = _slot(entity);
    return (i >= 0) ? values[i].item : "";
}
void HomeAssistantArduinoMQTT::setValue(HAAMEntity entity, const char* value, bool markAsChanged) {
    int16_t i = _slot(entity);
    if (i >= 0) _setValueAt(i, value, markAsChanged);
}
const char* HomeAssistantArduinoMQTT::getValue(HAAMEntity entity) {
    int16_t i = _slot(entity);
    return (i >= 0) ? values[i].value : "";
}
bool HomeAssistantArduinoMQTT::sendValue(HAAMEntity entity, bool force) {
    if (!mqttClient || !mqttClient->connected()) return false;
    int16_t i = _slot(entity);
    return (i >= 0) ? _sendSingleValue(i, force) : false;
}
void HomeAssistantArduinoMQTT::clearSetTopic(HAAMEntity entity) {
    int16_t i = _slot(entity);
    if (i >= 0 && mqttClient) _clearSetTopicAt(_itemDevice((uint8_t)i), values[i].item);
}
void HomeAssistantArduinoMQTT::setEntityAvailability(HAAMEntity entity, bool isAvailable) {
    int16_t i = _slot(entity);
    if (i >= 0) _setAvailable(i, isAvailable);
}

#ifdef HAAM_MULTI_DEVICE
// ---- any device ----
HAAMEntity HomeAssistantArduinoMQTT::getEntity(uint8_t dev, const char* item) {
    return HAAMEntity(_lookup(dev, item, false));
}
void HomeAssistantArduinoMQTT::setValue(uint8_t dev, const char* item, const char* value, bool markAsChanged) {
    _setValue(dev, item, value, markAsChanged);
}
const char* HomeAssistantArduinoMQTT::getValue(uint8_t dev, const char* item) {
    return _getValue(dev, item);
}
void HomeAssistantArduinoMQTT::clearSetTopic(uint8_t dev, const char* item) {
    _clearSetTopic(dev, item);
}
void HomeAssistantArduinoMQTT::setEntityAvailability(uint8_t dev, const char* entityId, bool isAvailable) {
    _setEntityAvailability(dev, entityId, isAvailable);
}
bool HomeAssistantArduinoMQTT::sendValue(uint8_t dev, const char* item, bool force) {
    return _sendValue(dev, item, force);
}
void HomeAssistantArduinoMQTT::sendCommand(uint8_t dev, const char* commandTopic, const char* payload) {
    _sendCommand(dev, commandTopic, payload);
}
void HomeAssistantArduinoMQTT::sendEvent(uint8_t dev, const char* eventName, const char* eventType) {
    _sendEvent(dev, eventName, eventType);
}
#endif

// ===========================================================================
//  HAEntityBuilder
// ===========================================================================
HAEntityBuilder::HAEntityBuilder(HomeAssistantArduinoMQTT* mqtt, const char* type, const char* id, const char* name, uint8_t device)
    : _mqtt(mqtt), _type(type), _name(name), _id(id), _commandTopicName(nullptr), _startupValue(nullptr), _category(nullptr), _deviceClass(nullptr), _stateClass(nullptr), _icon(nullptr), _unit(nullptr), _options(nullptr), _optionCount(0), _customPropCount(0), _device(device), _suggestedPrecision(0), _commandTopic(0), _stateTopic(1), _indAvail(0), _suggestedPrecisionEnable(0) {}

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

void HAEntityBuilder::options(const char* const* opts, uint8_t count) {
    _options = opts;
    _optionCount = opts ? count : 0;
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

HAAMEntity HAEntityBuilder::publish() {
    return HAAMEntity(_mqtt ? _mqtt->publishConfig(this) : (int16_t)-1);
}
