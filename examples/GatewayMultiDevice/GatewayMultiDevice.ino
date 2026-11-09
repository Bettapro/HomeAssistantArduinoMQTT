/*
  GatewayMultiDevice.ino
  HomeAssistantArduinoMQTT Library Example

  One board, one MQTT connection, three Home Assistant devices:
    - the gateway itself (root device)
    - a boiler and a garden pump, child devices linked to the gateway
      through "via_device" (e.g. reached over RS485 / Modbus / radio)

  Both children expose entities with the same ids ("temperature", "power"):
  entities are scoped per device, so they do not clash.

  REQUIRED BUILD FLAGS (a #define in the sketch is NOT enough, the library
  is compiled separately and must see the same configuration):

    PlatformIO (platformio.ini):
      build_flags = -DHAAM_MAX_DEVICES=3 -DHAAM_DEVICE_DISCOVERY

    arduino-cli:
      arduino-cli compile --build-property "compiler.cpp.extra_flags=-DHAAM_MAX_DEVICES=3 -DHAAM_DEVICE_DISCOVERY" ...

  HAAM_DEVICE_DISCOVERY is optional: without it every entity is published on
  its own discovery topic, with it each device is published as one message on
  homeassistant/device/<device id>/config. The sketch works in both modes.

  Author: Alberto Bettin
  Repository: https://github.com/Bettapro/HomeAssistantArduinoMQTT
*/

#if defined(ESP8266)
  #include <ESP8266WiFi.h>
  WiFiClient netClient;
#elif defined(ESP32)
  #include <WiFi.h>
  WiFiClient netClient;
#elif defined(ARDUINO_ARCH_AVR) || defined(ARDUINO_ARCH_SAMD)
  #include <Ethernet.h>
  EthernetClient netClient;
  byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xEE };
#else
  #error "Board non supportata!"
#endif

#include <HomeAssistantArduinoMQTT.h>

#ifndef HAAM_MULTI_DEVICE
#error "Build this example with -DHAAM_MAX_DEVICES=3 (see the comment at the top of the file)"
#endif

// ==========================================
// CONFIGURATION - Update with your settings
// ==========================================
#if defined(ESP8266) || defined(ESP32)
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
#endif

const char* MQTT_BROKER   = "192.168.1.100";
const uint16_t MQTT_PORT  = 1883;
const char* MQTT_USER     = "mqtt_user";
const char* MQTT_PASS     = "mqtt_password";

// 1 gateway entity + 2 entities for each child device
HomeAssistantArduinoMQTT haMqtt(5);

uint8_t boiler = HAAM_NO_DEVICE;
uint8_t pump   = HAAM_NO_DEVICE;

// temperature entity of each child, filled by the declarer
HAAMEntity boilerTemp;
HAAMEntity pumpTemp;

bool boilerOn = false;
bool pumpOn   = false;

// ==========================================
// NETWORK
// ==========================================
void connectNetwork() {
#if defined(ESP8266) || defined(ESP32)
    if (WiFi.status() == WL_CONNECTED) return;
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) delay(500);
#else
    Ethernet.begin(mac);
#endif
}

// ==========================================
// ENTITY DECLARATIONS
// One function per device. With HAAM_DEVICE_DISCOVERY it is called more than
// once per publish: always declare the same entities, in the same order, and
// do not send anything from here.
// ==========================================
void declareGateway(HomeAssistantArduinoMQTT& ha, uint8_t dev) {
    HAEntityBuilder restart = ha.newButtonEntity(dev, "restart", "Restart gateway");
    restart.category("diagnostic");
    restart.publish();
}

// boiler and pump have the same entities: one declarer serves both
void declareChild(HomeAssistantArduinoMQTT& ha, uint8_t dev) {
    HAEntityBuilder temp = ha.newSensorEntity(dev, "temperature", "Temperature");
    temp.deviceClass("temperature");
    temp.stateClass("measurement");
    temp.unit("°C");
    temp.suggestedDisplayPrecision(1);
    // the measuring pass of publishDevice() may return an invalid handle:
    // keep only the valid ones
    HAAMEntity e = temp.publish();
    if (e) {
        if (dev == boiler) boilerTemp = e;
        if (dev == pump) pumpTemp = e;
    }

    HAEntityBuilder power = ha.newSwitchEntity(dev, "power", "Power");
    power.startup("false");
    power.publish();
}

void publishDiscovery(uint8_t dev, void (*declare)(HomeAssistantArduinoMQTT&, uint8_t)) {
#ifdef HAAM_DEVICE_DISCOVERY
    haMqtt.publishDevice(dev, declare);   // one message for the whole device
#else
    declare(haMqtt, dev);                 // one message per entity
#endif
}

void publishAllDevices() {
    publishDiscovery(HAAM_ROOT_DEVICE, declareGateway);
    publishDiscovery(boiler, declareChild);
    publishDiscovery(pump, declareChild);
}

// ==========================================
// COMMANDS FROM HOME ASSISTANT
// ==========================================
class GatewayCallback : public HAMQTTCallback {
public:
    // multi-device overload: tells which device the message is for
    void onMQTTMessage(uint8_t device, const char* item, const char* value, bool isState) override {
        if (isState) return;

        if (device == HAAM_ROOT_DEVICE && strcmp(item, "restart") == 0) {
            Serial.println(F("Restart requested"));
            return;
        }

        if (strcmp(item, "power") == 0) {
            bool on = strcmp(value, "true") == 0;
            if (device == boiler) boilerOn = on;
            if (device == pump) pumpOn = on;
            haMqtt.setValue(device, "power", on ? "true" : "false");
            haMqtt.sendValue(device, "power");
        }
    }
};

GatewayCallback callbackHandler;

// ==========================================
// SETUP & LOOP
// ==========================================
void setup() {
    Serial.begin(115200);
    connectNetwork();

    // root device: the gateway
    haMqtt.mqttDeviceName = "gateway_01";
    haMqtt.haDeviceName   = "Gateway";
    haMqtt.manufacturer   = "Custom DIY";
    haMqtt.model          = "Multi-device gateway";
    haMqtt.version        = "1.0.0";
    haMqtt.mqttUser       = MQTT_USER;
    haMqtt.mqttPassword   = MQTT_PASS;

    // child devices
    boiler = haMqtt.addDevice("boiler_01", "Boiler");
    HAAMDevice* b = haMqtt.getDevice(boiler);
    b->manufacturer    = "ACME";
    b->model           = "Boiler 3000";
    b->useAvailability = true;   // goes offline when the bus does not answer

    pump = haMqtt.addDevice("garden_pump", "Garden pump");
    haMqtt.getDevice(pump)->model = "Pump X";

    haMqtt.setCallback(&callbackHandler);
    haMqtt.begin(netClient, MQTT_BROKER, MQTT_PORT);
}

void loop() {
    haMqtt.loop();

    // (re)publish discovery on every new connection
    static bool wasConnected = false;
    bool isConnected = haMqtt.connected();
    if (isConnected && !wasConnected) publishAllDevices();
    wasConnected = isConnected;

    static unsigned long lastRead = 0;
    if (isConnected && millis() - lastRead > 10000) {
        lastRead = millis();
        char buf[16];

        // simulated bus read: the boiler sometimes does not answer
        bool boilerAnswered = random(10) > 1;
        haMqtt.setDeviceAvailability(boiler, boilerAnswered);
        if (boilerAnswered) {
            HAAM_FORMAT_FLOAT(buf, 60.0 + random(-20, 20) / 10.0, 1);
            haMqtt.setValue(boilerTemp, buf);
        }

        HAAM_FORMAT_FLOAT(buf, 18.0 + random(-20, 20) / 10.0, 1);
        haMqtt.setValue(pumpTemp, buf);

        haMqtt.sendValues();
    }
}
