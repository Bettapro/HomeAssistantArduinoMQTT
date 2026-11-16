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

inline size_t appX(char* d, size_t cap, size_t p, const char* s, bool pgm) {
    return pgm ? appPgm(d, cap, p, s) : appRam(d, cap, p, s);
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

inline bool isEmptyX(const char* s, bool pgm) {
    return s == nullptr || (pgm ? pgm_read_byte(s) == 0 : s[0] == '\0');
}

// string equality, each side in RAM or in flash
bool eqX(const char* a, bool aPgm, const char* b, bool bPgm) {
    for (;; a++, b++) {
        char ca = aPgm ? (char)pgm_read_byte(a) : *a;
        char cb = bPgm ? (char)pgm_read_byte(b) : *b;
        if (ca != cb) return false;
        if (ca == '\0') return true;
    }
}

// A custom command name is one topic level: it has to be matched by the "+"
// of the subscription and to fit the item buffer when a command arrives.
bool isValidCommandName(const char* s) {
    for (size_t n = 0; s[n]; n++) {
        if (s[n] == '/' || s[n] == '+' || s[n] == '#') return false;
        if (n + 1 >= HAAM_ITEM_LEN) return false;
    }
    return true;
}

// {"event_type":"<type>"}; false when it does not fit: a truncated payload
// would not be valid JSON
bool buildEventPayload(char* payload, size_t cap, const char* eventType) {
    size_t p = appPgm(payload, cap, 0, PSTR("{\"event_type\":\""));
    for (const char* s = eventType; s && *s; s++) {
        char c = *s;
        if ((uint8_t)c < 0x20) continue;
        if (c == '"' || c == '\\') p = appChar(payload, cap, p, '\\');
        p = appChar(payload, cap, p, c);
    }
    p = appPgm(payload, cap, p, PSTR("\"}"));
    if (p + 1 >= cap) return false;
    payload[p] = '\0';
    return true;
}

// "haam/<dev>/<a>[/<b>]" as a JSON string
void writeTopicValue(HAJsonStream& js, const char* dev,
                     const char* a, bool aPgm,
                     const char* b, bool bPgm) {
    js.strBegin();
    js.strAddP(HAKeys::VALUE_TOPIC_PREFIX);
    js.strAddChar('/');
    js.strAdd(dev);
    js.strAddChar('/');
    js.strAddX(a, aPgm);
    if (b) {
        js.strAddChar('/');
        js.strAddX(b, bPgm);
    }
    js.strEnd();
}

}  // namespace

// ===========================================================================
//  Formatters
// ===========================================================================
int HAAMFmt::uintToBuf(char* buf, size_t size, unsigned long val) {
    if (!size) return 0;
    char tmp[3 * sizeof(unsigned long)];  // enough digits for any unsigned long
    uint8_t n = 0;
    do {
        tmp[n++] = (char)('0' + (val % 10));
        val /= 10;
    } while (val);

    if ((size_t)n + 1 > size) {  // does not fit: no truncated number
        buf[0] = '\0';
        return 0;
    }
    size_t p = 0;
    while (n) buf[p++] = tmp[--n];
    buf[p] = '\0';
    return (int)p;
}

int HAAMFmt::strToBuf(char* buf, size_t size, const char* val) {
    if (!size) return 0;
    size_t p = appRam(buf, size, 0, val);
    buf[p] = '\0';
    return (int)p;
}

int HAAMFmt::boolToBuf(char* buf, size_t size, bool val) {
    if (!size) return 0;
    const char* sP = val ? HAKeys::VAL_TRUE : HAKeys::VAL_FALSE;
    if (strlen_P(sP) + 1 > size) {
        buf[0] = '\0';
        return 0;
    }
    strcpy_P(buf, sP);
    return (int)strlen(buf);
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
    if (neg && n != 0) buf[p++] = '-';  // no "-0.0"
    if (p + 1 >= size) {
        buf[0] = '\0';
        return 0;
    }
    int w = HAAMFmt::uintToBuf(buf + p, size - p, n / mul);
    if (w == 0 || p + w + (dec ? dec + 1u : 0u) + 1 > size) {  // does not fit
        buf[0] = '\0';
        return 0;
    }
    p += w;

    if (dec) {
        buf[p++] = '.';
        unsigned long frac = n % mul;
        for (unsigned long d = mul / 10; d; d /= 10) buf[p++] = (char)('0' + (frac / d) % 10);
    }
    buf[p] = '\0';
    return (int)p;
}

// ===========================================================================
//  HAJsonStream
// ===========================================================================
HAJsonStream::HAJsonStream(Print* out)
    : _out(out), _len(0), _n(0), _first(true), _afterKey(false), _err(false) {}

void HAJsonStream::put(char c) {
    _len++;
    if (!_out) return;
    _buf[_n++] = c;
    if (_n == sizeof(_buf)) flush();
}

void HAJsonStream::flush() {
    if (_out && _n && _out->write((const uint8_t*)_buf, _n) != _n) _err = true;
    _n = 0;
}

void HAJsonStream::putP(const char* sP) {
    char c;
    while ((c = (char)pgm_read_byte(sP++)) != '\0') put(c);
}

void HAJsonStream::esc(char c) {
    char e;
    switch (c) {
        case '"':
        case '\\':
            e = c;
            break;
        case '\n':
            e = 'n';
            break;
        case '\r':
            e = 'r';
            break;
        case '\t':
            e = 't';
            break;
        default:
            if ((uint8_t)c >= 0x20) put(c);
            return;
    }
    put('\\');
    put(e);
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

void HAJsonStream::keyX(const char* s, bool pgm) {
    if (!_first) put(',');
    _first = false;
    put('"');
    strAddX(s, pgm);
    put('"');
    put(':');
    _afterKey = true;
}
void HAJsonStream::key(const char* keyP) { keyX(keyP, true); }
void HAJsonStream::keyStr(const char* s) { keyX(s, false); }

void HAJsonStream::strBegin() {
    sep();
    put('"');
}
void HAJsonStream::strEnd() { put('"'); }
void HAJsonStream::strAddChar(char c) { esc(c); }
void HAJsonStream::strAddX(const char* s, bool pgm) {
    if (!s) return;
    char c;
    while ((c = pgm ? (char)pgm_read_byte(s) : *s) != '\0') {
        esc(c);
        s++;
    }
}
void HAJsonStream::strAdd(const char* s) { strAddX(s, false); }
void HAJsonStream::strAddP(const char* sP) { strAddX(sP, true); }

void HAJsonStream::strX(const char* s, bool pgm) {
    strBegin();
    strAddX(s, pgm);
    strEnd();
}
void HAJsonStream::str(const char* s) { strX(s, false); }
void HAJsonStream::strP(const char* sP) { strX(sP, true); }

void HAJsonStream::num(long v) {
    sep();
    unsigned long u = (unsigned long)v;
    if (v < 0) {
        put('-');
        u = 0UL - u;  // also correct for LONG_MIN
    }
    char tmp[3 * sizeof(unsigned long) + 1];
    HAAMFmt::uintToBuf(tmp, sizeof(tmp), u);
    for (const char* c = tmp; *c; c++) put(*c);
}

void HAJsonStream::boolean(bool v) {
    sep();
    putP(v ? HAKeys::VAL_TRUE : HAKeys::VAL_FALSE);
}

// ===========================================================================
//  HomeAssistantArduinoMQTT
// ===========================================================================

#if !defined(MQTT_CALLBACK_FUNCTIONAL) && !defined(HAAM_NO_COMMANDS)
HomeAssistantArduinoMQTT* HomeAssistantArduinoMQTT::_instance = nullptr;

void HomeAssistantArduinoMQTT::_staticCallback(char* topic, byte* payload, haam_len_t length) {
    if (_instance) _instance->_handleMessage(topic, payload, length);
}
#endif

HomeAssistantArduinoMQTT::HomeAssistantArduinoMQTT(uint8_t maxN) {
#ifndef HAAM_NO_COMMANDS
    _callbackListener = nullptr;
#endif
    // zero-initialized: a free slot has an empty item
    values = new ItemValue[maxN]();
    maxEntityNum = values ? maxN : 0;

    _sanitizedDeviceName[0] = '\0';
    _topicBuf[0] = '\0';

#ifdef HAAM_MULTI_DEVICE
    _subDeviceCount = 0;
#endif
#ifdef HAAM_DEVICE_DISCOVERY
    _ddStream = nullptr;
    _ddDevice = HAAM_ROOT_DEVICE;
    _ddActive = false;
    _ddRegister = false;
#endif
}

HomeAssistantArduinoMQTT::~HomeAssistantArduinoMQTT() {
#if !defined(MQTT_CALLBACK_FUNCTIONAL) && !defined(HAAM_NO_COMMANDS)
    if (_instance == this) _instance = nullptr;
#endif
    delete[] values;
}

void HomeAssistantArduinoMQTT::_sanitize(const char* input, bool pgm, char* output, size_t maxLen) {
    if (!output || maxLen == 0) return;
    size_t w = 0;
    if (input) {
        bool lastWasUnderscore = false;
        char c;
        for (const char* p = input; (c = pgm ? (char)pgm_read_byte(p) : *p) != '\0' && w + 1 < maxLen; p++) {
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
    }
    output[w] = '\0';
}

void HomeAssistantArduinoMQTT::sanitizeID(const char* input, char* output, size_t maxLen) {
    _sanitize(input, false, output, maxLen);
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
    if (_mqtt.connected()) {
        if (_readValuesEnabled) {
            // new restore window, so the retained states of this device can arrive
            _subscribe(dev, HAKeys::TOPIC_STATE);
            _restoreStart = millis();
            _stateSubscribed = true;
        }
        if (commandEnabled) _subscribe(dev, HAKeys::TOPIC_COMMAND);
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
void HomeAssistantArduinoMQTT::_handleMessage(char* topic, byte* payload, haam_len_t length) {
    // empty payload: retained message cleared (e.g. clearSetTopic()), not a command
    if (length == 0) return;

    constexpr size_t prefixLen = sizeof(HAKeys::VALUE_TOPIC_PREFIX) - 1;
    if (strncmp_P(topic, HAKeys::VALUE_TOPIC_PREFIX, prefixLen) != 0 || topic[prefixLen] != '/') return;

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
    bool isCommand = (strcmp_P(action, HAKeys::TOPIC_COMMAND) == 0);
    bool isState = (!isCommand && strcmp_P(action, HAKeys::TOPIC_STATE) == 0);
    if (isCommand ? !commandEnabled : (!isState || !_readValuesEnabled)) return;

    char item[HAAM_ITEM_LEN];
    memcpy(item, remainder, itemLen);
    item[itemLen] = '\0';

    char cPayload[HAAM_PAYLOAD_LEN];
    haam_len_t copyLen = (length < sizeof(cPayload) - 1) ? length : (haam_len_t)(sizeof(cPayload) - 1);
    memcpy(cPayload, payload, copyLen);
    cPayload[copyLen] = '\0';

    if (isState) {
        // retained state kept by the broker: restore it into a known entity,
        // unless a newer local value is waiting to be sent. Unknown items never
        // take a slot. Availability is not touched.
        int16_t i = _lookup(dev, item, false);
        if (i >= 0) {
            // our own event echoed back: an event is not a state to restore
            if (values[i].isEvent) return;
            // value already sent in this session: the message is the broker echoing
            // our own publish, or a retained state older than it. Neither is news.
            if (values[i].stateSent) return;
            // a newer local value is waiting to be sent: the retained state is
            // older than it, and is not news for the application either
            if (values[i].valueChanged) return;
            _storeValueAt(i, cPayload, false);
            values[i].stateSent = 1;
        }
    }
    if (_callbackListener != nullptr) {
#ifdef HAAM_MULTI_DEVICE
        _callbackListener->onMQTTMessage(dev, item, cPayload, isState);
#else
        _callbackListener->onMQTTMessage(item, cPayload, isState);
#endif
    }
}

void HomeAssistantArduinoMQTT::_subscribe(uint8_t dev, const char* actionP) {
    _valueTopic(dev, "+", false, actionP, true);
    _mqtt.subscribe(_topicBuf);
}
#endif  // HAAM_NO_COMMANDS

void HomeAssistantArduinoMQTT::_valueTopic(uint8_t dev, const char* a, bool aPgm,
                                           const char* b, bool bPgm) {
    const size_t cap = sizeof(_topicBuf);
    size_t p = 0;
    p = appPgm(_topicBuf, cap, p, HAKeys::VALUE_TOPIC_PREFIX);
    p = appChar(_topicBuf, cap, p, '/');
    p = appRam(_topicBuf, cap, p, _deviceId(dev));
    p = appChar(_topicBuf, cap, p, '/');
    p = appX(_topicBuf, cap, p, a, aPgm);
    if (b) {
        p = appChar(_topicBuf, cap, p, '/');
        p = appX(_topicBuf, cap, p, b, bPgm);
    }
    _topicBuf[p] = '\0';
}

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
        v.hasIndAvail = 0;
        v.valueChanged = 0;
        v.lastAvailable = 0;
        v.stateSent = 0;
        v.isConfigured = 0;
        v.isEvent = 0;
        v.availabilitySent = 0;
        v.availabilitySet = 0;
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

bool HomeAssistantArduinoMQTT::begin(Client& client, const char* server, const uint16_t port) {
#if HAAM_MQTT_BUFFER_SIZE != MQTT_MAX_PACKET_SIZE
    return begin(client, server, port, HAAM_MQTT_BUFFER_SIZE, 15);
#else
    return begin(client, server, port, 0, 15);  // keep the PubSubClient buffer, no realloc
#endif
}

bool HomeAssistantArduinoMQTT::begin(Client& client, const char* server, const uint16_t port, const uint16_t bufferSize, const uint16_t keepAlive) {
    sanitizeID(mqttDeviceName, _sanitizedDeviceName, sizeof(_sanitizedDeviceName));
    // the device id is the MQTT client id and a level of every topic
    if (_sanitizedDeviceName[0] == '\0') {
        _started = false;
        return false;
    }

    _mqtt.setClient(client);
    bool bufferOk = true;
    if (bufferSize && bufferSize != _mqtt.getBufferSize()) bufferOk = _mqtt.setBufferSize(bufferSize);
    _mqtt.setServer(server, port);

#ifndef HAAM_NO_COMMANDS
#ifdef MQTT_CALLBACK_FUNCTIONAL
    _mqtt.setCallback([this](char* topic, byte* payload, haam_len_t length) {
        _handleMessage(topic, payload, length);
    });
#else
    _instance = this;
    _mqtt.setCallback(_staticCallback);
#endif
#endif

    _mqtt.setKeepAlive(keepAlive);
    _started = true;
    return bufferOk;
}

void HomeAssistantArduinoMQTT::loop() {
    if (!_started) return;
    if (_mqtt.connected()) {
        _mqtt.loop();
#if !defined(HAAM_NO_COMMANDS) && HAAM_RESTORE_WINDOW_MS > 0
        // restore window over: without the subscription the broker no longer
        // echoes back every state this device publishes
        if (_stateSubscribed && !_restorePending()) _unsubscribeStates();
#endif
    } else {
        connect();
    }
}

bool HomeAssistantArduinoMQTT::_publishAvailability(bool isAvailable) {
    const char* payloadP = isAvailable ? HAKeys::ONLINE_PAYLOAD : HAKeys::OFFLINE_PAYLOAD;
    _valueTopic(HAAM_ROOT_DEVICE, HAKeys::AVAILABILITY, true, nullptr, false);
    return _mqtt.publish_P(_topicBuf, (const uint8_t*)payloadP, strlen_P(payloadP), true);
}

void HomeAssistantArduinoMQTT::connect() {
    unsigned long now = millis();
    if (_lastReconnectAttempt != 0 && (now - _lastReconnectAttempt) < 5000) return;

    // empty credentials: anonymous connection (an empty user name is not anonymous)
    const char* user = isEmptyStr(mqttUser) ? nullptr : mqttUser;
    const char* pass = (user && !isEmptyStr(mqttPassword)) ? mqttPassword : nullptr;

    bool success;
    if (useSharedAvailability) {
        char offlinePayload[sizeof(HAKeys::OFFLINE_PAYLOAD)];
        strcpy_P(offlinePayload, HAKeys::OFFLINE_PAYLOAD);
        _valueTopic(HAAM_ROOT_DEVICE, HAKeys::AVAILABILITY, true, nullptr, false);
        success = _mqtt.connect(_sanitizedDeviceName, user, pass, _topicBuf, 1, true, offlinePayload);
    } else {
        success = _mqtt.connect(_sanitizedDeviceName, user, pass);
    }

    // connect() can block for the whole socket timeout: the delay before the
    // next attempt starts when this one ends (0 means "no attempt yet")
    _lastReconnectAttempt = millis();
    if (_lastReconnectAttempt == 0) _lastReconnectAttempt = 1;

    if (!success) return;

#ifndef HAAM_NO_COMMANDS
    _restoreStart = millis();
#endif

    // new session: send everything again (values set while offline stay "changed")
    for (uint8_t i = 0; i < maxEntityNum; i++) {
        values[i].stateSent = 0;
        values[i].availabilitySent = 0;
    }
#ifdef HAAM_MULTI_DEVICE
    for (uint8_t d = 0; d < _subDeviceCount; d++) _subDevices[d].availabilitySent = 0;
#endif

    if (useSharedAvailability) _publishAvailability(true);

#ifndef HAAM_NO_COMMANDS
    for (uint8_t d = 0; d < _deviceCount(); d++) {
        if (_readValuesEnabled) _subscribe(d, HAKeys::TOPIC_STATE);
        if (commandEnabled) _subscribe(d, HAKeys::TOPIC_COMMAND);
    }
    _stateSubscribed = _readValuesEnabled;
#endif
}

bool HomeAssistantArduinoMQTT::connected() {
    return _mqtt.connected();
}

void HomeAssistantArduinoMQTT::disconnect(bool publishOffline) {
    // loop() no longer reconnects: begin() starts again, without the reconnect delay
    _started = false;
    _lastReconnectAttempt = 0;
    if (!_mqtt.connected()) return;
    if (publishOffline && useSharedAvailability) _publishAvailability(false);
    _mqtt.disconnect();
}

// ---------------------------------------------------------------------------
//  Entity factory
// ---------------------------------------------------------------------------
HAEntityBuilder HomeAssistantArduinoMQTT::_make(uint8_t dev, uint8_t kind, const char* type, const char* id, const char* name) {
    HAEntityBuilder b(this, type, id, name, dev);
    if (kind == KIND_GENERIC) return b;

    b._pgm |= HAEntityBuilder::P_TYPE;
    b._indAvail = 1;
    switch (kind) {
        case KIND_SENSOR:
            b._type = HAKeys::TYPE_SENSOR;
            break;
        case KIND_BINARY_SENSOR:
            b._type = HAKeys::TYPE_BINARY_SENSOR;
            b._propStr(HAKeys::PAYLOAD_ON, true, HAKeys::VAL_TRUE, true);
            b._propStr(HAKeys::PAYLOAD_OFF, true, HAKeys::VAL_FALSE, true);
            break;
        case KIND_SWITCH:
            b._type = HAKeys::TYPE_SWITCH;
            b._commandTopic = 1;
            b._propStr(HAKeys::PAYLOAD_ON, true, HAKeys::VAL_TRUE, true);
            b._propStr(HAKeys::PAYLOAD_OFF, true, HAKeys::VAL_FALSE, true);
            break;
        case KIND_BUTTON:
            b._type = HAKeys::TYPE_BUTTON;
            b._commandTopic = 1;
            b._stateTopic = 0;
            b._propStr(HAKeys::PAYLOAD_PRESS, true, HAKeys::VAL_PRESS, true);
            break;
        case KIND_NUMBER:
            b._type = HAKeys::TYPE_NUMBER;
            b._commandTopic = 1;
            break;
        case KIND_EVENT:
            b._type = HAKeys::TYPE_EVENT;
            b._event = 1;
            break;
        default:  // KIND_SELECT
            b._type = HAKeys::TYPE_SELECT;
            b._commandTopic = 1;
            break;
    }
    return b;
}

// ---------------------------------------------------------------------------
//  Discovery payloads
// ---------------------------------------------------------------------------
void HomeAssistantArduinoMQTT::_writeDevice(HAJsonStream& js, uint8_t dev) {
    const char* id = _deviceId(dev);
    const char* name = haDeviceName;
    const char* vals[4] = {manufacturer, model, version, configurationUrl};
    const char* via = nullptr;
#ifdef HAAM_MULTI_DEVICE
    if (dev != HAAM_ROOT_DEVICE) {
        const HAAMDevice& d = _subDevices[dev - 1];
        name = d.name;
        vals[0] = d.manufacturer;
        vals[1] = d.model;
        vals[2] = d.version;
        vals[3] = d.configurationUrl;
        via = _sanitizedDeviceName;
    }
#endif
    static const char* const keys[4] PROGMEM = {HAKeys::MANUFACTURER, HAKeys::MODEL, HAKeys::SW_VERSION, HAKeys::CONFIGURATION_URL};

    js.key(HAKeys::DEVICE);
    js.openObj();
    js.key(HAKeys::IDENTIFIERS);
    js.str(id);
    js.key(HAKeys::NAME);
    js.str(isEmptyStr(name) ? id : name);
    for (uint8_t i = 0; i < 4; i++) {
        if (isEmptyStr(vals[i])) continue;
        js.key((const char*)pgm_read_ptr(&keys[i]));
        js.str(vals[i]);
    }
    if (via) {
        js.key(HAKeys::VIA_DEVICE);
        js.str(via);
    }
    js.closeObj();
}

// connection (LWT) + child device + entity availability, all required
void HomeAssistantArduinoMQTT::_writeAvailability(HAJsonStream& js, uint8_t dev, const char* entityId) {
    bool childAvty = false;
#ifdef HAAM_MULTI_DEVICE
    childAvty = dev != HAAM_ROOT_DEVICE && _subDevices[dev - 1].useAvailability;
#endif
    uint8_t avtyCount = (useSharedAvailability ? 1 : 0) + (childAvty ? 1 : 0) + (entityId ? 1 : 0);
    if (!avtyCount) return;

    js.key(HAKeys::AVAILABILITY);
    js.openArr();
    for (uint8_t k = 0; k < 3; k++) {
        if (k == 0 && !useSharedAvailability) continue;
        if (k == 1 && !childAvty) continue;
        if (k == 2 && !entityId) continue;
        js.openObj();
        js.key(HAKeys::TOPIC);
        if (k == 0)
            writeTopicValue(js, _sanitizedDeviceName, HAKeys::AVAILABILITY, true, nullptr, false);
        else if (k == 1)
            writeTopicValue(js, _deviceId(dev), HAKeys::AVAILABILITY, true, nullptr, false);
        else
            writeTopicValue(js, _deviceId(dev), entityId, false, HAKeys::AVAILABILITY, true);
        js.closeObj();
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
        js.strX(b->_type, b->_isPgm(HAEntityBuilder::P_TYPE));
    }

    _writeAvailability(js, dev, b->_indAvail ? entityId : nullptr);

    if (!component) _writeDevice(js, dev);

    if (!isEmptyX(b->_name, b->_isPgm(HAEntityBuilder::P_NAME))) {
        js.key(HAKeys::NAME);
        js.strX(b->_name, b->_isPgm(HAEntityBuilder::P_NAME));
    }
    if (b->_suggestedPrecisionEnable) {
        js.key(HAKeys::SUGGESTED_DISPLAY_PRECISION);
        js.num(b->_suggestedPrecision);
    }

    // optional string fields, in output order
    const char* const fields[5] = {b->_category, b->_deviceClass, b->_stateClass, b->_icon, b->_unit};
    static const char* const keys[5] PROGMEM = {HAKeys::ENTITY_CATEGORY, HAKeys::DEVICE_CLASS, HAKeys::STATE_CLASS,
                                                HAKeys::ICON, HAKeys::UNIT_OF_MEASUREMENT};
    uint8_t bit = HAEntityBuilder::P_CATEGORY;
    for (uint8_t i = 0; i < 5; i++, bit <<= 1) {
        if (isEmptyX(fields[i], b->_isPgm(bit))) continue;  // an empty value is not valid for Home Assistant
        js.key((const char*)pgm_read_ptr(&keys[i]));
        js.strX(fields[i], b->_isPgm(bit));
    }

    if (b->_options && b->_optionCount) {
        js.key(b->_event ? HAKeys::EVENT_TYPES : HAKeys::OPTIONS);
        js.openArr();
        for (uint8_t i = 0; i < b->_optionCount; i++) js.str(b->_options[i]);
        js.closeArr();
    }

    for (uint8_t i = 0; i < b->_customPropCount; i++) {
        const HACustomProp& cp = b->_customProps[i];
        js.keyX(cp.key, cp.keyPgm);
        switch (cp.type) {
            case 0:
                js.strX(cp.valStr, cp.valPgm);
                break;
            case 1:
                js.num(cp.valInt);
                break;
            default:
                js.boolean(cp.valBool);
                break;
        }
    }

    // child devices are always prefixed: their entity ids are only unique per device
    js.key(HAKeys::UNIQUE_ID);
    js.strBegin();
    if (prefixUniqueIds || dev != HAAM_ROOT_DEVICE) {
        js.strAdd(devId);
        js.strAddChar('_');
    }
    js.strAdd(entityId);
    js.strEnd();

    if (b->_commandTopic) {
        const char* cmdName = isEmptyStr(b->_commandTopicName) ? entityId : b->_commandTopicName;
        js.key(HAKeys::COMMAND_TOPIC);
        writeTopicValue(js, devId, cmdName, false, HAKeys::TOPIC_COMMAND, true);
    }

    if (b->_stateTopic) {
        js.key(HAKeys::STATE_TOPIC);
        writeTopicValue(js, devId, entityId, false, HAKeys::TOPIC_STATE, true);
    }

    js.closeObj();
}

void HomeAssistantArduinoMQTT::_entityId(HAEntityBuilder* builder, char* out, size_t len) {
    if (!isEmptyStr(builder->_id)) {
        _sanitize(builder->_id, false, out, len);
    } else {
        _sanitize(builder->_name, builder->_isPgm(HAEntityBuilder::P_NAME), out, len);
    }
}

int16_t HomeAssistantArduinoMQTT::publishConfig(HAEntityBuilder* builder) {
    char entityId[HAAM_ITEM_LEN];
    _entityId(builder, entityId, sizeof(entityId));
    uint8_t dev = builder->_device;

    // a command name that could never receive a command: nothing is published
    bool commandOk = !builder->_commandTopic || isEmptyStr(builder->_commandTopicName) || isValidCommandName(builder->_commandTopicName);

#ifdef HAAM_DEVICE_DISCOVERY
    if (_ddActive) {
        // inside publishDevice(): the entity belongs to the device being published
        if (entityId[0] == '\0' || !commandOk) return -1;
        dev = _ddDevice;
        if (_ddStream) {
            _ddStream->keyStr(entityId);
            _writeConfig(*_ddStream, builder, dev, entityId, true);
        }
        // measuring pass: nothing is registered, return the slot if it exists
        return _ddRegister ? _registerEntity(builder, dev, entityId) : _lookup(dev, entityId, false);
    }
#endif

    _configSent = false;
    if (!_isValidDevice(dev) || !commandOk) return -1;

    // registered first: with the entity table full (or an empty id) nothing is
    // published, Home Assistant would get an entity that can never have a value
    int16_t slot = _registerEntity(builder, dev, entityId);
    if (slot < 0) return -1;

    if (enableConfigPublishing && _mqtt.connected()) {
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
        p = appX(_topicBuf, cap, p, builder->_type, builder->_isPgm(HAEntityBuilder::P_TYPE));
        p = appChar(_topicBuf, cap, p, '/');
        p = appRam(_topicBuf, cap, p, _deviceId(dev));
        p = appChar(_topicBuf, cap, p, '/');
        p = appRam(_topicBuf, cap, p, entityId);
        p = appChar(_topicBuf, cap, p, '/');
        p = appPgm(_topicBuf, cap, p, HAKeys::TOPIC_CONFIG);
        _topicBuf[p] = '\0';

        if (_mqtt.beginPublish(_topicBuf, payloadLen, true)) {
            HAJsonStream out(&_mqtt);
            _writeConfig(out, builder, dev, entityId, false);
            out.flush();
            _configSent = _mqtt.endPublish() != 0 && out.ok();
        }
    }

    return slot;
}

int16_t HomeAssistantArduinoMQTT::_registerEntity(HAEntityBuilder* builder, uint8_t dev, const char* entityId) {
    int16_t slot = _allocate(dev, entityId);
    if (slot < 0) return -1;

    ItemValue& v = values[slot];
    bool wasConfigured = v.isConfigured;
    v.isConfigured = 1;
    v.hasIndAvail = builder->_indAvail ? 1 : 0;
    v.isEvent = builder->_event ? 1 : 0;

    // a value stored before publish() (e.g. deep sleep) or since a previous publish() wins over startup()
    bool startupPgm = builder->_isPgm(HAEntityBuilder::P_STARTUP);
    bool hasStored = builder->_stateTopic && v.value[0] != '\0';
    bool hasStartup = builder->_stateTopic && !isEmptyX(builder->_startupValue, startupPgm);

    if (hasStartup && !hasStored) {
        const char* sv = builder->_startupValue;
        char tmp[HAAM_VALUE_LEN];
        if (startupPgm) {
            tmp[appPgm(tmp, sizeof(tmp), 0, sv)] = '\0';
            sv = tmp;
        }
        // a default, not a local change: a state restored by readValues() replaces it.
        // An availability set by the application wins over the default one.
        _storeValueAt(slot, sv, false);
        if (!v.availabilitySet) _setAvailable(slot, v.value[0] != '\0');
        v.stateSent = 0;
    } else if (!wasConfigured && builder->_indAvail && !v.availabilitySet) {
        // first publish(): initial availability; later ones keep what the application set.
        // Entities without a state (e.g. buttons) and events have no value to wait for: start available.
        _setAvailable(slot, hasStored || !builder->_stateTopic || builder->_event);
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

bool HomeAssistantArduinoMQTT::_publishDevice(uint8_t dev, HAAMDeviceDeclarer declare) {
    if (!declare || _ddActive || !_isValidDevice(dev)) return false;

    _ddActive = true;
    _ddDevice = dev;
    bool registered = false;
    bool published = false;

    if (enableConfigPublishing && _mqtt.connected()) {
        // pass 1: measure the payload, nothing is sent nor registered
        size_t payloadLen;
        {
            HAJsonStream measure(nullptr);
            _ddStream = &measure;
            _ddRegister = false;
            _writeDeviceHeader(measure, dev);
            declare(*this, dev);
            measure.closeObj();  // cmps
            measure.closeObj();  // root
            payloadLen = measure.length();
        }

        const size_t cap = sizeof(_topicBuf);
        size_t p = 0;
        p = appPgm(_topicBuf, cap, p, HAKeys::PREFIX);
        p = appChar(_topicBuf, cap, p, '/');
        p = appPgm(_topicBuf, cap, p, HAKeys::TOPIC_DEVICE);
        p = appChar(_topicBuf, cap, p, '/');
        p = appRam(_topicBuf, cap, p, _deviceId(dev));
        p = appChar(_topicBuf, cap, p, '/');
        p = appPgm(_topicBuf, cap, p, HAKeys::TOPIC_CONFIG);
        _topicBuf[p] = '\0';

        // pass 2: stream the payload and register the entities
        if (_mqtt.beginPublish(_topicBuf, payloadLen, true)) {
            HAJsonStream out(&_mqtt);
            _ddStream = &out;
            _ddRegister = true;
            _writeDeviceHeader(out, dev);
            declare(*this, dev);
            out.closeObj();  // cmps
            out.closeObj();  // root
            out.flush();
            published = _mqtt.endPublish() != 0 && out.ok() && out.length() == payloadLen;
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
    if (!_isValidDevice(dev)) return;

    char sanitizedItem[HAAM_ITEM_LEN];
    sanitizeID(item, sanitizedItem, sizeof(sanitizedItem));
    if (sanitizedItem[0] != '\0') _clearSetTopicAt(dev, sanitizedItem);
}

void HomeAssistantArduinoMQTT::_clearSetTopicAt(uint8_t dev, const char* sanitizedItem) {
    // an empty retained message removes the retained command from the broker
    // (inbound empty payloads are ignored, so this is not seen as a command)
    _valueTopic(dev, sanitizedItem, false, HAKeys::TOPIC_COMMAND, true);
    _mqtt.publish(_topicBuf, "", true);
}

void HomeAssistantArduinoMQTT::_storeValueAt(int16_t i, const char* value, bool markAsChanged) {
    if (!value) value = "";
    ItemValue& v = values[i];
    // compared as it would be stored: a value longer than the buffer is
    // truncated, and must not look like a change on every call
    if (strncmp(v.value, value, sizeof(v.value) - 1) != 0) {
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
    if (i >= 0) {
        _setAvailable(i, isAvailable);
        values[i].availabilitySet = 1;
    }
}

#ifndef HAAM_NO_COMMANDS
void HomeAssistantArduinoMQTT::readValues() {
    _readValuesEnabled = true;
    if (!_mqtt.connected()) return;
    _restoreStart = millis();
    _stateSubscribed = true;
    for (uint8_t d = 0; d < _deviceCount(); d++) _subscribe(d, HAKeys::TOPIC_STATE);
}

void HomeAssistantArduinoMQTT::_unsubscribeStates() {
    _stateSubscribed = false;
    for (uint8_t d = 0; d < _deviceCount(); d++) {
        _valueTopic(d, "+", false, HAKeys::TOPIC_STATE, true);
        _mqtt.unsubscribe(_topicBuf);
    }
}

// true while the retained states requested by readValues() may still arrive
bool HomeAssistantArduinoMQTT::_restorePending() const {
#if HAAM_RESTORE_WINDOW_MS > 0
    return _readValuesEnabled && (millis() - _restoreStart) < (unsigned long)(HAAM_RESTORE_WINDOW_MS);
#else
    return false;
#endif
}
#endif

bool HomeAssistantArduinoMQTT::_sendSingleValue(int16_t i, bool force) {
    ItemValue& v = values[i];
    uint8_t dev = _itemDevice((uint8_t)i);

    // state: only when there is a value to send
    // events are sent by sendEvent() only, never as a retained state
    bool sendState = !v.isEvent && v.value[0] != '\0' && (force || v.valueChanged || !v.stateSent);
#ifndef HAAM_NO_COMMANDS
    // a value that was not changed locally (startup() default, or the one of
    // the previous session) waits for the retained state: sending it now would
    // overwrite on the broker the state readValues() is about to restore
    if (sendState && !force && !v.valueChanged && _restorePending()) sendState = false;
#endif
    if (sendState) {
        _valueTopic(dev, v.item, false, HAKeys::TOPIC_STATE, true);
        if (!_mqtt.publish(_topicBuf, v.value, true)) return false;
        v.stateSent = 1;
        v.valueChanged = 0;
    }

    // availability: exactly as requested by setValue / setEntityAvailability
    if (v.hasIndAvail && (force || !v.availabilitySent)) {
        const char* payloadP = v.lastAvailable ? HAKeys::ONLINE_PAYLOAD : HAKeys::OFFLINE_PAYLOAD;
        _valueTopic(dev, v.item, false, HAKeys::AVAILABILITY, true);
        if (!_mqtt.publish_P(_topicBuf, (const uint8_t*)payloadP, strlen_P(payloadP), true)) {
            return false;
        }
        v.availabilitySent = 1;
    }

    return true;
}

bool HomeAssistantArduinoMQTT::sendValues(bool force) {
    if (!_mqtt.connected()) return false;

    bool sent = true;

#ifdef HAAM_MULTI_DEVICE
    for (uint8_t d = 0; d < _subDeviceCount; d++) {
        HAAMDevice& sd = _subDevices[d];
        if (!sd.useAvailability || (!force && sd.availabilitySent)) continue;
        const char* payloadP = sd.lastAvailable ? HAKeys::ONLINE_PAYLOAD : HAKeys::OFFLINE_PAYLOAD;
        _valueTopic((uint8_t)(d + 1), HAKeys::AVAILABILITY, true, nullptr, false);
        if (_mqtt.publish_P(_topicBuf, (const uint8_t*)payloadP, strlen_P(payloadP), true)) {
            sd.availabilitySent = 1;
        } else {
            sent = false;
        }
    }
#endif

    for (uint8_t i = 0; i < maxEntityNum; i++) {
        if (values[i].item[0] == '\0') continue;
        if (!_sendSingleValue(i, force)) {
            sent = false;
            if (!_mqtt.connected()) break;  // connection lost: the others would fail too
        }
    }
    return sent;
}

bool HomeAssistantArduinoMQTT::_sendValue(uint8_t dev, const char* item, bool force) {
    if (!_mqtt.connected()) return false;

    int16_t i = _lookup(dev, item, false);
    return (i >= 0) ? _sendSingleValue(i, force) : false;
}

bool HomeAssistantArduinoMQTT::_sendCommand(uint8_t dev, const char* commandTopic, const char* payload) {
    if (!_isValidDevice(dev)) return false;
    _valueTopic(dev, commandTopic, false, nullptr, false);
    return _mqtt.publish(_topicBuf, payload, false);
}

bool HomeAssistantArduinoMQTT::_sendEvent(uint8_t dev, const char* eventName, const char* eventType) {
    char payload[HAAM_PAYLOAD_LEN];
    if (!buildEventPayload(payload, sizeof(payload), eventType)) return false;
    return _sendCommand(dev, eventName, payload);
}

// Event entity: published on its state topic, not retained (an event is not a
// state) and never stored, so it is not sent again on the next connection.
bool HomeAssistantArduinoMQTT::sendEvent(HAAMEntity entity, const char* eventType) {
    int16_t i = _slot(entity);
    if (i < 0 || !_mqtt.connected()) return false;

    char payload[HAAM_PAYLOAD_LEN];
    if (!buildEventPayload(payload, sizeof(payload), eventType)) return false;
    _valueTopic(_itemDevice((uint8_t)i), values[i].item, false, HAKeys::TOPIC_STATE, true);
    return _mqtt.publish(_topicBuf, payload, false);
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
bool HomeAssistantArduinoMQTT::sendCommand(const char* commandTopic, const char* payload) {
    return _sendCommand(HAAM_ROOT_DEVICE, commandTopic, payload);
}
bool HomeAssistantArduinoMQTT::sendEvent(const char* eventName, const char* eventType) {
    return _sendEvent(HAAM_ROOT_DEVICE, eventName, eventType);
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
    if (!_mqtt.connected()) return false;
    int16_t i = _slot(entity);
    return (i >= 0) ? _sendSingleValue(i, force) : false;
}
void HomeAssistantArduinoMQTT::clearSetTopic(HAAMEntity entity) {
    int16_t i = _slot(entity);
    if (i >= 0) _clearSetTopicAt(_itemDevice((uint8_t)i), values[i].item);
}
void HomeAssistantArduinoMQTT::setEntityAvailability(HAAMEntity entity, bool isAvailable) {
    int16_t i = _slot(entity);
    if (i >= 0) {
        _setAvailable(i, isAvailable);
        values[i].availabilitySet = 1;
    }
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
bool HomeAssistantArduinoMQTT::sendCommand(uint8_t dev, const char* commandTopic, const char* payload) {
    return _sendCommand(dev, commandTopic, payload);
}
bool HomeAssistantArduinoMQTT::sendEvent(uint8_t dev, const char* eventName, const char* eventType) {
    return _sendEvent(dev, eventName, eventType);
}
#endif

// ===========================================================================
//  HAEntityBuilder
// ===========================================================================
HAEntityBuilder::HAEntityBuilder(HomeAssistantArduinoMQTT* mqtt, const char* type, const char* id, const char* name, uint8_t device)
    : _mqtt(mqtt), _type(type), _name(name), _id(id), _commandTopicName(nullptr), _startupValue(nullptr), _category(nullptr), _deviceClass(nullptr), _stateClass(nullptr), _icon(nullptr), _unit(nullptr), _options(nullptr), _optionCount(0), _customPropCount(0), _device(device), _suggestedPrecision(0), _pgm(0), _commandTopic(0), _stateTopic(1), _indAvail(0), _suggestedPrecisionEnable(0), _event(0) {}

void HAEntityBuilder::_str(const char*& field, const char* val, uint8_t bit, bool pgm) {
    field = val;
    if (pgm && val)
        _pgm |= bit;
    else
        _pgm &= (uint8_t)~bit;
}

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

HACustomProp* HAEntityBuilder::_prop(const char* key, bool keyPgm, uint8_t type) {
    if (!key) return nullptr;
    HACustomProp* cp = nullptr;
    // a key set again replaces its previous value (e.g. the default pl_on of
    // a switch) instead of being written twice in the payload
    for (uint8_t i = 0; i < _customPropCount && !cp; i++) {
        if (eqX(_customProps[i].key, _customProps[i].keyPgm, key, keyPgm)) cp = &_customProps[i];
    }
    if (!cp) {
        if (_customPropCount >= HAAM_MAX_CUSTOM_PROPS) return nullptr;
        cp = &_customProps[_customPropCount++];
    }
    cp->key = key;
    cp->keyPgm = keyPgm ? 1 : 0;
    cp->valPgm = 0;
    cp->type = type;
    return cp;
}

void HAEntityBuilder::_propStr(const char* key, bool keyPgm, const char* val, bool valPgm) {
    HACustomProp* cp = _prop(key, keyPgm, 0);
    if (!cp) return;
    cp->valStr = val;
    cp->valPgm = valPgm ? 1 : 0;
}

void HAEntityBuilder::_propInt(const char* key, bool keyPgm, long val) {
    HACustomProp* cp = _prop(key, keyPgm, 1);
    if (cp) cp->valInt = val;
}

void HAEntityBuilder::_propBool(const char* key, bool keyPgm, bool val) {
    HACustomProp* cp = _prop(key, keyPgm, 2);
    if (cp) cp->valBool = val;
}

HAAMEntity HAEntityBuilder::publish() {
    return HAAMEntity(_mqtt ? _mqtt->publishConfig(this) : (int16_t)-1);
}