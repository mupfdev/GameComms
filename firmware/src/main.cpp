/** @file main.cpp
 *
 *  GameComms ESP32 bridge - Arduino front end.
 *
 *  Hardware glue around bridge_core: Bluetooth SPP towards the N-Gage,
 *  Wi-Fi + TCP towards the endpoint server, persistent configuration
 *  (NVS), a serial configuration console and a minimal web setup page.
 *
 *  Copyright (c) 2023-2026, Michael Fitzmayer. All rights reserved.
 *  SPDX-License-Identifier: MIT
 *
 **/

#include <Arduino.h>
#include <BluetoothSerial.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_bt_device.h>
#include <esp_random.h>
#include <esp_spp_api.h>
#include <freertos/queue.h>

#include "async_server.h"
#include "bt_event_filter.h"
#include "bt_rx_buffer.h"

extern "C" {
#include "bridge_core.h"
}

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth Classic is required (use an ESP32, not S2/S3/C3)
#endif

#ifndef LED_PIN
#define LED_PIN 2
#endif

#ifndef SETUP_BUTTON_PIN
#define SETUP_BUTTON_PIN 0 /* BOOT button on most ESP32 boards, low when pressed */
#endif
#define SETUP_HOLD_MS 3000

#define DISCOVERY_PORT 9888

/* Survives a software restart, not a power cycle: requests setup mode once. */
#define SETUP_MAGIC 0x53455455u
RTC_NOINIT_ATTR static uint32_t setupRequest;
static bool setupMode = false;

static void restartIntoSetup()
{
    setupRequest = SETUP_MAGIC;
    Serial.println("restarting into setup mode");
    delay(100);
    ESP.restart();
}

/* ------------------------------------------------------------------ */
/* configuration                                                       */
/* ------------------------------------------------------------------ */

struct Config
{
    String ssid;
    String pass;
    String host; /* "auto" = LAN discovery */
    uint16_t port;
    String key;
    String btname;
    String pin;
    uint8_t verbose;
};

static Config cfg;
static Preferences prefs;

static void loadConfig()
{
    prefs.begin("gcbridge", true);
    cfg.ssid = prefs.getString("ssid", "");
    cfg.pass = prefs.getString("pass", "");
    cfg.host = prefs.getString("host", "auto");
    cfg.port = prefs.getUShort("port", 9888);
    cfg.key = prefs.getString("key", "");
    cfg.btname = prefs.getString("btname", "GameCommsBridge");
    cfg.pin = prefs.getString("pin", "1234");
    cfg.verbose = prefs.getUChar("verbose", 1);
    prefs.end();
}

static void saveConfig()
{
    prefs.begin("gcbridge", false);
    prefs.putString("ssid", cfg.ssid);
    prefs.putString("pass", cfg.pass);
    prefs.putString("host", cfg.host);
    prefs.putUShort("port", cfg.port);
    prefs.putString("key", cfg.key);
    prefs.putString("btname", cfg.btname);
    prefs.putString("pin", cfg.pin);
    prefs.putUChar("verbose", cfg.verbose);
    prefs.end();
}

/* ------------------------------------------------------------------ */
/* bridge glue                                                         */
/* ------------------------------------------------------------------ */

static BluetoothSerial SerialBT;
static AsyncServer tcp;
static bridge br;
static uint8_t btPeer[6];
static bool btConnected = false;
static bool tcpWanted = false;
static WebServer *web = nullptr;
static uint32_t btWriteFailures = 0;
static uint32_t tcpWriteFailures = 0;

struct BtEvent
{
    bool open;
    int status;
    uint32_t handle;
    uint32_t portStatus;
    bool asynchronous;
    uint32_t time;
    uint32_t generation;
    uint8_t peer[6];
};

#define BT_EVENT_QUEUE_LEN 16
static StaticQueue_t btEventQueueStorage;
static uint8_t btEventQueueBuffer[BT_EVENT_QUEUE_LEN * sizeof(BtEvent)];
static QueueHandle_t btEventQueue;
static portMUX_TYPE btEventMux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t btEventsDropped = 0;
static bt_event_filter btFilter = { 0 };
static bt_event_filter btCallbackFilter = { 0 };
static bt_rx_buffer btRx;
static uint32_t btGeneration = 0;
static uint32_t btReportedDropped = 0;

static void io_dev_write(void *, const gc_u8 *d, size_t n)
{
    if (btConnected) {
        size_t written = SerialBT.write(d, n);
        if (written != n) {
            btWriteFailures++;
            Serial.printf("[%8lu] Bluetooth write accepted %u of %u bytes\n", (unsigned long)millis(),
                          (unsigned)written, (unsigned)n);
        }
    }
}

static void io_srv_write(void *, const gc_u8 *d, size_t n)
{
    AsyncServer::Error error = tcp.write(d, n);
    if (error != AsyncServer::OK) {
        tcpWriteFailures++;
        Serial.printf("[%8lu] TCP frame enqueue failed (%d, %u bytes)\n", (unsigned long)millis(),
                      (int)error, (unsigned)n);
    }
}

static void io_srv_connect(void *)
{
    tcpWanted = true;
}

static void io_srv_disconnect(void *)
{
    tcpWanted = false;
    tcp.stop();
}

static void io_log(void *, const char *line)
{
    if (cfg.verbose) {
        Serial.printf("[%8lu] %s\n", (unsigned long)millis(), line);
    }
}

/* SPP callbacks run on the Bluetooth task; process events in the main loop. */
static void sppCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    BtEvent e = {};
    if (event == ESP_SPP_DATA_IND_EVT) {
        portENTER_CRITICAL(&btEventMux);
        if (btCallbackFilter.active_handle == param->data_ind.handle && btCallbackFilter.active_handle != 0) {
            bt_rx_write(&btRx, param->data_ind.data, param->data_ind.len);
        }
        portEXIT_CRITICAL(&btEventMux);
        return;
    }
    if (event == ESP_SPP_SRV_OPEN_EVT) {
        e.open = true;
        e.status = param->srv_open.status;
        e.handle = param->srv_open.handle;
        if (e.status == ESP_SPP_SUCCESS) {
            memcpy(e.peer, param->srv_open.rem_bda, sizeof(e.peer));
        }
    } else if (event == ESP_SPP_CLOSE_EVT) {
        e.status = param->close.status;
        e.handle = param->close.handle;
        e.portStatus = param->close.port_status;
        e.asynchronous = param->close.async;
    } else {
        return;
    }
    portENTER_CRITICAL(&btEventMux);
    if ((e.open && bt_event_open(&btCallbackFilter, e.handle, e.status == ESP_SPP_SUCCESS)) ||
        (!e.open && bt_event_close(&btCallbackFilter, e.handle, e.status == ESP_SPP_SUCCESS, e.asynchronous))) {
        bt_rx_reset(&btRx);
    }
    e.generation = btRx.generation;
    portEXIT_CRITICAL(&btEventMux);
    e.time = millis();
    if (xQueueSend(btEventQueue, &e, 0) != pdTRUE) {
        portENTER_CRITICAL(&btEventMux);
        btEventsDropped++;
        portEXIT_CRITICAL(&btEventMux);
    }
}

/* Disable BluetoothSerial's small RX queue; sppCallback receives the data with its handle. */
static void btDataCallback(const uint8_t *, size_t)
{
}

static size_t btRead(uint8_t *data, size_t len)
{
    size_t n;
    portENTER_CRITICAL(&btEventMux);
    n = bt_rx_read(&btRx, btGeneration, data, len);
    portEXIT_CRITICAL(&btEventMux);
    return n;
}

static uint32_t btDroppedBytes()
{
    uint32_t dropped;
    portENTER_CRITICAL(&btEventMux);
    dropped = btRx.dropped;
    portEXIT_CRITICAL(&btEventMux);
    return dropped;
}

/* ------------------------------------------------------------------ */
/* server discovery / connection                                       */
/* ------------------------------------------------------------------ */

static void tryConnectServer(uint32_t now)
{
    uint8_t broadcast[4];
    IPAddress local = WiFi.localIP();
    IPAddress mask = WiFi.subnetMask();
    tcpWanted = false;
    for (int i = 0; i < 4; i++) {
        broadcast[i] = (local[i] & mask[i]) | (~mask[i] & 0xFF);
    }
    AsyncServer::Error error = tcp.start(cfg.host.c_str(), cfg.port, DISCOVERY_PORT, broadcast, now);
    if (error == AsyncServer::INVALID_ARGUMENT) {
        Serial.printf("[%8lu] invalid TCP server configuration\n", (unsigned long)now);
        bridge_srv_disconnected(&br, now);
    }
}

/* ------------------------------------------------------------------ */
/* setup web page (only in access point mode)                          */
/* ------------------------------------------------------------------ */

static String htmlEscape(const String &s)
{
    String o;
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '<') {
            o += "&lt;";
        } else if (c == '>') {
            o += "&gt;";
        } else if (c == '"') {
            o += "&quot;";
        } else if (c == '&') {
            o += "&amp;";
        } else {
            o += c;
        }
    }
    return o;
}

static void webRoot()
{
    String p = "<!doctype html><meta name=viewport content='width=device-width'>"
               "<title>GameComms Bridge</title><h2>GameComms Bridge setup</h2><form method=post action=/save>";
    p += "Wi-Fi SSID<br><input name=ssid value=\"" + htmlEscape(cfg.ssid) + "\"><br>";
    p += "Wi-Fi password<br><input name=pass type=password><br>";
    p += "Server host (auto = LAN discovery)<br><input name=host value=\"" + htmlEscape(cfg.host) + "\"><br>";
    p += "Server port<br><input name=port value=\"" + String(cfg.port) + "\"><br>";
    p += "Access key<br><input name=key value=\"" + htmlEscape(cfg.key) + "\"><br>";
    p += "Bluetooth name<br><input name=btname value=\"" + htmlEscape(cfg.btname) + "\"><br>";
    p += "Bluetooth PIN<br><input name=pin value=\"" + htmlEscape(cfg.pin) + "\"><br><br>";
    p += "<button>Save and reboot</button></form>";
    p += "<p>Leave the password empty to keep the saved one.</p>";
    p += "<form method=post action=/reboot><button>Reboot without changes</button></form>";
    web->send(200, "text/html", p);
}

static void webSave()
{
    if (web->hasArg("ssid")) {
        cfg.ssid = web->arg("ssid");
    }
    if (web->hasArg("pass") && web->arg("pass").length()) {
        cfg.pass = web->arg("pass");
    }
    if (web->hasArg("host")) {
        cfg.host = web->arg("host");
    }
    if (web->hasArg("port")) {
        cfg.port = (uint16_t)web->arg("port").toInt();
    }
    if (web->hasArg("key")) {
        cfg.key = web->arg("key");
    }
    if (web->hasArg("btname") && web->arg("btname").length()) {
        cfg.btname = web->arg("btname");
    }
    if (web->hasArg("pin") && web->arg("pin").length()) {
        cfg.pin = web->arg("pin");
    }
    saveConfig();
    web->send(200, "text/html", "Saved. Rebooting...");
    delay(500);
    ESP.restart();
}

static void webReboot()
{
    web->send(200, "text/html", "Rebooting...");
    delay(500);
    ESP.restart();
}

/* ------------------------------------------------------------------ */
/* serial console                                                      */
/* ------------------------------------------------------------------ */

static void printLinkDiagnostics(Print &out, const char *name, const gc_link &link)
{
    out.printf("  %s link   : state %d tx %lu rx %lu retx %lu dup %lu ooo %lu backpressure %lu\n", name, link.state,
               (unsigned long)link.st_tx_msgs, (unsigned long)link.st_rx_msgs, (unsigned long)link.st_retx,
               (unsigned long)link.st_dup, (unsigned long)link.st_ooo, (unsigned long)link.st_backpressure);
    out.printf("  %s frames : ok %lu crc %lu cobs %lu oversize %lu short %lu\n", name,
               (unsigned long)link.rx.frames_ok, (unsigned long)link.rx.err_crc, (unsigned long)link.rx.err_cobs,
               (unsigned long)link.rx.err_oversize, (unsigned long)link.rx.err_short);
}

static void printStatus(Print &out)
{
    out.printf("GameComms bridge %s\n", BRIDGE_VERSION);
    out.printf("  bt name   : %s (pin %s)\n", cfg.btname.c_str(), cfg.pin.c_str());
    out.printf("  device    : %s, link state %d\n", btConnected ? "connected" : "-", br.dev.state);
    out.printf("  wifi      : %s %s rssi %d\n", cfg.ssid.c_str(),
               WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "(down)", WiFi.RSSI());
    out.printf("  server    : %s:%u state %d link %d\n", cfg.host.c_str(), cfg.port, br.srv_state, br.srv.state);
    out.printf("  messages  : dev->srv %lu srv->dev %lu, retx dev %lu srv %lu\n", (unsigned long)br.st_dev_msgs,
               (unsigned long)br.st_srv_msgs, (unsigned long)br.dev.st_retx, (unsigned long)br.srv.st_retx);
    printLinkDiagnostics(out, "bt", br.dev);
    printLinkDiagnostics(out, "tcp", br.srv);
    out.printf("  writes    : bt failures %lu tcp failures %lu\n", (unsigned long)btWriteFailures,
               (unsigned long)tcpWriteFailures);
    out.printf("  bt rx     : dropped %lu bytes\n", (unsigned long)btDroppedBytes());
    out.printf("  free heap : %u (lowest %u)\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
}

/* Restart after the reply had time to leave (Bluetooth console). */
static void restartNow(Print &out)
{
    out.flush();
    delay(300);
    ESP.restart();
}

/* One console command. The same command set is served on the serial port and
   on the Bluetooth configuration channel (see btPump). */
static void handleCommand(String line, Print &out)
{
    line.trim();
    if (line.length() == 0) {
        return;
    }
    int sp = line.indexOf(' ');
    String cmd = sp < 0 ? line : line.substring(0, sp);
    String rest = sp < 0 ? "" : line.substring(sp + 1);
    if (cmd == "help") {
        out.println("commands: status | show | set <ssid|pass|host|port|key|btname|pin|verbose> <value> | save | reboot | setup | factory");
    } else if (cmd == "status") {
        printStatus(out);
    } else if (cmd == "show") {
        out.printf("ssid=%s\npass=%s\nhost=%s\nport=%u\nkey=%s\nbtname=%s\npin=%s\nverbose=%u\n", cfg.ssid.c_str(),
                   cfg.pass.length() ? "(set)" : "", cfg.host.c_str(), cfg.port, cfg.key.length() ? "(set)" : "",
                   cfg.btname.c_str(), cfg.pin.c_str(), cfg.verbose);
    } else if (cmd == "set") {
        int sp2 = rest.indexOf(' ');
        String k = sp2 < 0 ? rest : rest.substring(0, sp2);
        String v = sp2 < 0 ? "" : rest.substring(sp2 + 1);
        if (k == "ssid") {
            cfg.ssid = v;
        } else if (k == "pass") {
            cfg.pass = v;
        } else if (k == "host") {
            cfg.host = v;
        } else if (k == "port") {
            cfg.port = (uint16_t)v.toInt();
        } else if (k == "key") {
            cfg.key = v;
        } else if (k == "btname") {
            cfg.btname = v;
        } else if (k == "pin") {
            cfg.pin = v;
        } else if (k == "verbose") {
            cfg.verbose = (uint8_t)v.toInt();
        } else {
            out.println("unknown key");
            return;
        }
        out.println("ok (use 'save' and 'reboot' to apply)");
    } else if (cmd == "save") {
        saveConfig();
        out.println("saved");
    } else if (cmd == "reboot") {
        out.println("rebooting");
        restartNow(out);
    } else if (cmd == "setup") {
        out.println("restarting into setup mode");
        out.flush();
        restartIntoSetup();
    } else if (cmd == "factory") {
        prefs.begin("gcbridge", false);
        prefs.clear();
        prefs.end();
        out.println("factory reset, rebooting");
        restartNow(out);
    } else {
        out.println("unknown command, try 'help'");
    }
}

/* ------------------------------------------------------------------ */
/* Bluetooth configuration channel                                     */
/* ------------------------------------------------------------------ */

/* The configuration client (the PhantomLink app on the N-Gage) uses the
   same SPP channel as the game. A connection that starts with BT_CFG_MAGIC
   becomes a text console with the commands of the serial console, one
   command per line. Every reply ends with a line "END". Anything else is
   handed to the bridge unchanged (GCL frames never start with this text
   in practice, see docs/PROTOCOL.md). */
static const char BT_CFG_MAGIC[] = "GCCFG1";
#define BT_CFG_MAGIC_LEN 6
#define BT_SNIFF_MS      400

enum BtMode
{
    BT_NONE,
    BT_SNIFF,
    BT_GAME,
    BT_CONFIG
};
static BtMode btMode = BT_NONE;
static uint8_t btSniff[BT_CFG_MAGIC_LEN];
static size_t btSniffLen = 0;
static uint32_t btSniffSince = 0;
static String btCmdline;

static void btStartGame(uint32_t now)
{
    btMode = BT_GAME;
    btConnected = true;
    bridge_dev_connected(&br, btPeer, now);
    if (cfg.verbose) {
        Serial.printf("[%8lu] free heap %u (lowest %u)\n", (unsigned long)now, (unsigned)ESP.getFreeHeap(),
                      (unsigned)ESP.getMinFreeHeap());
    }
    if (btSniffLen) {
        bridge_dev_input(&br, btSniff, btSniffLen, now);
        btSniffLen = 0;
    }
}

static void btStartConfig()
{
    btMode = BT_CONFIG;
    btSniffLen = 0;
    btCmdline = "";
    Serial.println("Bluetooth configuration session");
    SerialBT.printf("OK GCCFG1 %s\r\nEND\r\n", BRIDGE_VERSION);
}

static void btOnOpen(uint32_t now)
{
    btMode = BT_SNIFF;
    btSniffLen = 0;
    btSniffSince = now;
}

static void btOnClose(uint32_t now)
{
    if (btMode == BT_GAME && btConnected) {
        btConnected = false;
        bridge_dev_disconnected(&br, now);
    }
    btMode = BT_NONE;
    btSniffLen = 0;
}

static void btProcessEvents()
{
    BtEvent e;
    uint32_t dropped;
    portENTER_CRITICAL(&btEventMux);
    dropped = btEventsDropped;
    btEventsDropped = 0;
    portEXIT_CRITICAL(&btEventMux);
    if (dropped) {
        Serial.printf("[%8lu] ERROR: lost %lu SPP events; connection state may be wrong, reboot required\n",
                      (unsigned long)millis(), (unsigned long)dropped);
    }
    while (xQueueReceive(btEventQueue, &e, 0) == pdTRUE) {
        uint32_t now = millis();
        if (e.open) {
            int accepted = bt_event_open(&btFilter, e.handle, e.status == ESP_SPP_SUCCESS);
            Serial.printf("[%8lu] SPP open handle %lu status %d accepted %d (processed at %lu)\n",
                          (unsigned long)e.time, (unsigned long)e.handle, e.status, accepted, (unsigned long)now);
            if (accepted) {
                memcpy(btPeer, e.peer, sizeof(btPeer));
                btGeneration = e.generation;
                btOnOpen(now);
            }
        } else {
            int active = bt_event_close(&btFilter, e.handle, e.status == ESP_SPP_SUCCESS, e.asynchronous);
            Serial.printf("[%8lu] SPP close handle %lu status %d port_status %lu async %d active %d (processed at %lu)\n",
                          (unsigned long)e.time, (unsigned long)e.handle, e.status, (unsigned long)e.portStatus,
                          e.asynchronous ? 1 : 0, active, (unsigned long)now);
            if (active) {
                printLinkDiagnostics(Serial, "bt", br.dev);
                printLinkDiagnostics(Serial, "tcp", br.srv);
                Serial.printf("  writes: bt failures %lu tcp failures %lu; bt rx dropped %lu; heap %u (lowest %u)\n",
                              (unsigned long)btWriteFailures, (unsigned long)tcpWriteFailures,
                              (unsigned long)btDroppedBytes(), (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
                btOnClose(now);
            }
        }
    }
}

static void btPump(uint32_t now)
{
    uint8_t c;
    uint32_t dropped = btDroppedBytes();
    if (dropped != btReportedDropped) {
        Serial.printf("[%8lu] Bluetooth RX overflow: dropped %lu bytes (total %lu)\n", (unsigned long)now,
                      (unsigned long)(dropped - btReportedDropped), (unsigned long)dropped);
        btReportedDropped = dropped;
    }
    if (btMode == BT_SNIFF) {
        while (btMode == BT_SNIFF && btRead(&c, 1) == 1) {
            btSniff[btSniffLen++] = c;
            if (c != (uint8_t)BT_CFG_MAGIC[btSniffLen - 1]) {
                btStartGame(now);
            } else if (btSniffLen == BT_CFG_MAGIC_LEN) {
                btStartConfig();
            }
        }
        if (btMode == BT_SNIFF && now - btSniffSince >= BT_SNIFF_MS) {
            btStartGame(now); /* the device waits for us or sends slowly */
        }
    }
    if (btMode == BT_GAME) {
        uint8_t buf[256];
        size_t n;
        for (int i = 0; i < 8 && (n = btRead(buf, sizeof(buf))) > 0; i++) {
            bridge_dev_input(&br, buf, n, millis());
        }
    } else if (btMode == BT_CONFIG) {
        for (int i = 0; i < 256 && btMode == BT_CONFIG && btRead(&c, 1) == 1; i++) {
            if (c == '\n' || c == '\r') {
                btCmdline.trim();
                if (btCmdline.length()) {
                    handleCommand(btCmdline, SerialBT);
                    SerialBT.print("END\r\n");
                }
                btCmdline = "";
            } else if (btCmdline.length() < 200) {
                btCmdline += c;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Arduino entry points                                                */
/* ------------------------------------------------------------------ */

void setup()
{
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    pinMode(SETUP_BUTTON_PIN, INPUT_PULLUP);
    setupMode = setupRequest == SETUP_MAGIC;
    setupRequest = 0;
    loadConfig();
    Serial.printf("\nGameComms bridge %s\n", BRIDGE_VERSION);

    bridge_cfg bc;
    bridge_io io;
    bridge_default_cfg(&bc);
    bc.server_link_id = esp_random() | 1;
    strncpy(bc.access_key, cfg.key.c_str(), GC_MAX_NAME);
    io.dev_write = io_dev_write;
    io.srv_write = io_srv_write;
    io.srv_connect = io_srv_connect;
    io.srv_disconnect = io_srv_disconnect;
    io.log = io_log;
    bridge_init(&br, &bc, &io, nullptr);

    /* The N-Gage only supports Bluetooth 1.1 legacy PIN pairing.
       disableSSP() must come before begin(), setPin() after it (it talks to
       the running Bluetooth stack). */
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    SerialBT.disableSSP();
#endif
    btEventQueue = xQueueCreateStatic(BT_EVENT_QUEUE_LEN, sizeof(BtEvent), btEventQueueBuffer, &btEventQueueStorage);
    if (!btEventQueue) {
        Serial.println("Bluetooth event queue initialization failed");
        return;
    }
    SerialBT.onData(btDataCallback);
    SerialBT.register_callback(sppCallback);
    if (!SerialBT.begin(cfg.btname)) {
        Serial.println("Bluetooth init failed");
    }
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    bool pinOk = SerialBT.setPin(cfg.pin.c_str(), cfg.pin.length());
#else
    bool pinOk = SerialBT.setPin(cfg.pin.c_str());
#endif
    if (!pinOk) {
        Serial.println("Bluetooth PIN could not be set");
    }
    Serial.printf("Bluetooth SPP '%s' ready\n", cfg.btname.c_str());

    if (cfg.ssid.length() == 0 || setupMode) {
        setupMode = true;
        WiFi.mode(WIFI_AP);
        WiFi.softAP("GameCommsBridge-Setup");
        web = new WebServer(80);
        web->on("/", HTTP_GET, webRoot);
        web->on("/save", HTTP_POST, webSave);
        web->on("/reboot", HTTP_POST, webReboot);
        web->begin();
        Serial.println("Setup mode: join 'GameCommsBridge-Setup' and open http://192.168.4.1/");
        Serial.println("or use the console: set ssid <name> / set pass <pw> / save / reboot");
        Serial.println("or use the PhantomLink app on the N-Gage (Bluetooth)");
    } else {
        WiFi.mode(WIFI_STA);
        /* Modem sleep must stay on: with Bluetooth running, the Wi-Fi driver
           aborts if it is disabled (WiFi.setSleep(false)). */
        WiFi.setSleep(true);
        WiFi.setAutoReconnect(true);
        WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
    }
}

void loop()
{
    static String cmdline;
    static uint32_t lastLed = 0;
    uint32_t now = millis();

    /* console */
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            handleCommand(cmdline, Serial);
            cmdline = "";
        } else if (cmdline.length() < 200) {
            cmdline += c;
        }
    }
    if (web) {
        web->handleClient();
    }

    /* hold the BOOT button for 3 seconds: restart into setup mode */
    {
        static uint32_t pressedSince = 0;
        if (digitalRead(SETUP_BUTTON_PIN) == LOW) {
            if (pressedSince == 0) {
                pressedSince = now | 1;
            } else if (!setupMode && now - pressedSince >= SETUP_HOLD_MS) {
                restartIntoSetup();
            }
        } else {
            pressedSince = 0;
        }
    }

    /* Bluetooth transport events */
    if (btEventQueue) {
        btProcessEvents();
        btPump(millis());
    }

    /* Wi-Fi */
    if (!web) {
        bool up = WiFi.status() == WL_CONNECTED;
        bridge_set_wifi(&br, up ? 1 : 0, (gc_i8)WiFi.RSSI(), millis());
    }

    /* TCP */
    if (tcpWanted && WiFi.status() == WL_CONNECTED) {
        tryConnectServer(millis());
    }
    AsyncServer::Result result = tcp.poll(millis());
    if (result.event == AsyncServer::CONNECTED) {
        bridge_srv_connected(&br, millis());
    } else if (result.event == AsyncServer::DISCONNECTED) {
        Serial.printf("[%8lu] TCP transport closed (error %d, system %d)\n", (unsigned long)millis(),
                      (int)result.error, result.system_error);
        bridge_srv_disconnected(&br, millis());
    }
    if (tcp.connected()) {
        uint8_t buf[512];
        for (int i = 0; i < 4 && tcp.connected(); i++) {
            int n = tcp.read(buf, sizeof(buf));
            if (n <= 0) {
                break;
            }
            bridge_srv_input(&br, buf, (size_t)n, millis());
        }
    }

    bridge_poll(&br, millis());

    /* warn once when memory gets tight (crashes follow when it runs out) */
    {
        static bool warned = false;
        uint32_t lowest = ESP.getMinFreeHeap();
        if (!warned && lowest < 12000) {
            warned = true;
            Serial.printf("[%8lu] warning: free heap went down to %u bytes\n", (unsigned long)now, (unsigned)lowest);
        }
    }

    /* LED: off = no Wi-Fi, slow blink = server, solid = device + server,
       fast blink = setup mode */
    if (now - lastLed > (setupMode ? 80u : 250u)) {
        static bool on = false;
        lastLed = now;
        if (setupMode) {
            on = !on;
        } else if (br.srv_state == GC_BS_UP && btConnected) {
            on = true;
        } else if (br.srv_state == GC_BS_UP) {
            on = !on;
        } else {
            on = false;
        }
        digitalWrite(LED_PIN, on ? HIGH : LOW);
    }
    delay(1);
}
