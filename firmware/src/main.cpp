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
#include <WiFiUdp.h>
#include <esp_bt_device.h>
#include <esp_random.h>
#include <esp_spp_api.h>

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
    String host;    /* "auto" = LAN discovery */
    uint16_t port;
    String key;
    String btname;
    String pin;
    uint8_t verbose;
};

static Config      cfg;
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
static WiFiClient      tcp;
static bridge          br;
static volatile bool   btOpened = false;
static volatile bool   btClosed = false;
static uint8_t         btPeer[6];
static bool            btConnected = false;
static bool            tcpWanted = false;
static bool            tcpConnected = false;
static WebServer      *web = nullptr;
static IPAddress       discoveredIp;
static uint16_t        discoveredPort = 0;

static void io_dev_write(void *, const gc_u8 *d, size_t n)
{
    if (btConnected)
    {
        SerialBT.write(d, n);
    }
}

static void io_srv_write(void *, const gc_u8 *d, size_t n)
{
    if (tcpConnected && tcp.connected())
    {
        tcp.write(d, n);
    }
}

static void io_srv_connect(void *)
{
    tcpWanted = true;
}

static void io_srv_disconnect(void *)
{
    tcpWanted = false;
    if (tcpConnected)
    {
        tcp.stop();
        tcpConnected = false;
    }
}

static void io_log(void *, const char *line)
{
    if (cfg.verbose)
    {
        Serial.printf("[%8lu] %s\n", (unsigned long)millis(), line);
    }
}

/* SPP events arrive on the Bluetooth task: only set flags here */
static void sppCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    if (event == ESP_SPP_SRV_OPEN_EVT)
    {
        memcpy(btPeer, param->srv_open.rem_bda, 6);
        btOpened = true;
    }
    else if (event == ESP_SPP_CLOSE_EVT)
    {
        btClosed = true;
    }
}

/* ------------------------------------------------------------------ */
/* server discovery / connection                                       */
/* ------------------------------------------------------------------ */

static bool discoverServer()
{
    WiFiUDP udp;
    if (!udp.begin(0))
    {
        return false;
    }
    IPAddress bcast = WiFi.localIP();
    bcast[3] = 255;
    IPAddress mask = WiFi.subnetMask();
    for (int i = 0; i < 4; i++)
    {
        bcast[i] = (WiFi.localIP()[i] & mask[i]) | (~mask[i] & 0xFF);
    }
    udp.beginPacket(bcast, DISCOVERY_PORT);
    udp.print("GCDISCOVER1");
    udp.endPacket();
    uint32_t t0 = millis();
    while (millis() - t0 < 800)
    {
        int n = udp.parsePacket();
        if (n > 0)
        {
            char buf[96] = {0};
            udp.read(buf, sizeof(buf) - 1);
            int port = 0;
            if (sscanf(buf, "GCSERVER1 %d", &port) == 1 && port > 0)
            {
                discoveredIp = udp.remoteIP();
                discoveredPort = (uint16_t)port;
                udp.stop();
                Serial.printf("server discovered at %s:%u\n", discoveredIp.toString().c_str(), discoveredPort);
                return true;
            }
        }
        delay(10);
    }
    udp.stop();
    return false;
}

static void tryConnectServer()
{
    bool ok;
    tcpWanted = false;
    if (cfg.host == "auto" || cfg.host.length() == 0)
    {
        if (!discoverServer())
        {
            bridge_srv_disconnected(&br, millis());
            return;
        }
        ok = tcp.connect(discoveredIp, discoveredPort, 3000);
    }
    else
    {
        ok = tcp.connect(cfg.host.c_str(), cfg.port, 3000);
    }
    if (!ok)
    {
        bridge_srv_disconnected(&br, millis());
        return;
    }
    tcp.setNoDelay(true);
    tcpConnected = true;
    bridge_srv_connected(&br, millis());
}

/* ------------------------------------------------------------------ */
/* setup web page (only in access point mode)                          */
/* ------------------------------------------------------------------ */

static String htmlEscape(const String &s)
{
    String o;
    for (size_t i = 0; i < s.length(); i++)
    {
        char c = s[i];
        if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else if (c == '"') o += "&quot;";
        else if (c == '&') o += "&amp;";
        else o += c;
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
    if (web->hasArg("ssid")) cfg.ssid = web->arg("ssid");
    if (web->hasArg("pass") && web->arg("pass").length()) cfg.pass = web->arg("pass");
    if (web->hasArg("host")) cfg.host = web->arg("host");
    if (web->hasArg("port")) cfg.port = (uint16_t)web->arg("port").toInt();
    if (web->hasArg("key")) cfg.key = web->arg("key");
    if (web->hasArg("btname") && web->arg("btname").length()) cfg.btname = web->arg("btname");
    if (web->hasArg("pin") && web->arg("pin").length()) cfg.pin = web->arg("pin");
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

static void printStatus()
{
    Serial.printf("GameComms bridge %s\n", BRIDGE_VERSION);
    Serial.printf("  bt name   : %s (pin %s)\n", cfg.btname.c_str(), cfg.pin.c_str());
    Serial.printf("  device    : %s, link state %d\n", btConnected ? "connected" : "-", br.dev.state);
    Serial.printf("  wifi      : %s %s rssi %d\n", cfg.ssid.c_str(),
                  WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "(down)", WiFi.RSSI());
    Serial.printf("  server    : %s:%u state %d link %d\n", cfg.host.c_str(), cfg.port, br.srv_state, br.srv.state);
    Serial.printf("  messages  : dev->srv %lu srv->dev %lu, retx dev %lu srv %lu\n", (unsigned long)br.st_dev_msgs,
                  (unsigned long)br.st_srv_msgs, (unsigned long)br.dev.st_retx, (unsigned long)br.srv.st_retx);
    Serial.printf("  free heap : %u\n", (unsigned)ESP.getFreeHeap());
}

static void handleCommand(String line)
{
    line.trim();
    if (line.length() == 0)
    {
        return;
    }
    int    sp = line.indexOf(' ');
    String cmd = sp < 0 ? line : line.substring(0, sp);
    String rest = sp < 0 ? "" : line.substring(sp + 1);
    if (cmd == "help")
    {
        Serial.println("commands: status | show | set <ssid|pass|host|port|key|btname|pin|verbose> <value> | save | reboot | setup | factory");
    }
    else if (cmd == "status")
    {
        printStatus();
    }
    else if (cmd == "show")
    {
        Serial.printf("ssid=%s\nhost=%s\nport=%u\nkey=%s\nbtname=%s\npin=%s\nverbose=%u\n", cfg.ssid.c_str(),
                      cfg.host.c_str(), cfg.port, cfg.key.length() ? "(set)" : "", cfg.btname.c_str(),
                      cfg.pin.c_str(), cfg.verbose);
    }
    else if (cmd == "set")
    {
        int    sp2 = rest.indexOf(' ');
        String k = sp2 < 0 ? rest : rest.substring(0, sp2);
        String v = sp2 < 0 ? "" : rest.substring(sp2 + 1);
        if (k == "ssid") cfg.ssid = v;
        else if (k == "pass") cfg.pass = v;
        else if (k == "host") cfg.host = v;
        else if (k == "port") cfg.port = (uint16_t)v.toInt();
        else if (k == "key") cfg.key = v;
        else if (k == "btname") cfg.btname = v;
        else if (k == "pin") cfg.pin = v;
        else if (k == "verbose") cfg.verbose = (uint8_t)v.toInt();
        else { Serial.println("unknown key"); return; }
        Serial.println("ok (use 'save' and 'reboot' to apply)");
    }
    else if (cmd == "save")
    {
        saveConfig();
        Serial.println("saved");
    }
    else if (cmd == "reboot")
    {
        ESP.restart();
    }
    else if (cmd == "setup")
    {
        restartIntoSetup();
    }
    else if (cmd == "factory")
    {
        prefs.begin("gcbridge", false);
        prefs.clear();
        prefs.end();
        ESP.restart();
    }
    else
    {
        Serial.println("unknown command, try 'help'");
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
    bridge_io  io;
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
    SerialBT.register_callback(sppCallback);
    if (!SerialBT.begin(cfg.btname))
    {
        Serial.println("Bluetooth init failed");
    }
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    bool pinOk = SerialBT.setPin(cfg.pin.c_str(), cfg.pin.length());
#else
    bool pinOk = SerialBT.setPin(cfg.pin.c_str());
#endif
    if (!pinOk)
    {
        Serial.println("Bluetooth PIN could not be set");
    }
    Serial.printf("Bluetooth SPP '%s' ready\n", cfg.btname.c_str());

    if (cfg.ssid.length() == 0 || setupMode)
    {
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
    }
    else
    {
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
    static String   cmdline;
    static uint32_t lastLed = 0;
    uint32_t        now = millis();

    /* console */
    while (Serial.available())
    {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r')
        {
            handleCommand(cmdline);
            cmdline = "";
        }
        else if (cmdline.length() < 200)
        {
            cmdline += c;
        }
    }
    if (web)
    {
        web->handleClient();
    }

    /* hold the BOOT button for 3 seconds: restart into setup mode */
    {
        static uint32_t pressedSince = 0;
        if (digitalRead(SETUP_BUTTON_PIN) == LOW)
        {
            if (pressedSince == 0)
            {
                pressedSince = now | 1;
            }
            else if (!setupMode && now - pressedSince >= SETUP_HOLD_MS)
            {
                restartIntoSetup();
            }
        }
        else
        {
            pressedSince = 0;
        }
    }

    /* Bluetooth transport events */
    if (btClosed)
    {
        btClosed = false;
        if (btConnected)
        {
            btConnected = false;
            bridge_dev_disconnected(&br, now);
        }
    }
    if (btOpened)
    {
        btOpened = false;
        btConnected = true;
        bridge_dev_connected(&br, btPeer, now);
    }
    if (btConnected)
    {
        uint8_t buf[256];
        int     n;
        while ((n = SerialBT.available()) > 0)
        {
            n = SerialBT.readBytes(buf, n > (int)sizeof(buf) ? sizeof(buf) : n);
            bridge_dev_input(&br, buf, (size_t)n, millis());
        }
    }

    /* Wi-Fi */
    if (!web)
    {
        bool up = WiFi.status() == WL_CONNECTED;
        bridge_set_wifi(&br, up ? 1 : 0, (gc_i8)WiFi.RSSI(), now);
    }

    /* TCP */
    if (tcpConnected && !tcp.connected())
    {
        tcpConnected = false;
        tcp.stop();
        bridge_srv_disconnected(&br, now);
    }
    if (tcpConnected)
    {
        uint8_t buf[512];
        int     n;
        while ((n = tcp.available()) > 0)
        {
            n = tcp.read(buf, n > (int)sizeof(buf) ? sizeof(buf) : n);
            if (n <= 0)
            {
                break;
            }
            bridge_srv_input(&br, buf, (size_t)n, millis());
        }
    }
    if (tcpWanted && !tcpConnected && WiFi.status() == WL_CONNECTED)
    {
        tryConnectServer();
    }

    bridge_poll(&br, millis());

    /* LED: off = no Wi-Fi, slow blink = server, solid = device + server,
       fast blink = setup mode */
    if (now - lastLed > (setupMode ? 80u : 250u))
    {
        static bool on = false;
        lastLed = now;
        if (setupMode)
        {
            on = !on;
        }
        else if (br.srv_state == GC_BS_UP && btConnected)
        {
            on = true;
        }
        else if (br.srv_state == GC_BS_UP)
        {
            on = !on;
        }
        else
        {
            on = false;
        }
        digitalWrite(LED_PIN, on ? HIGH : LOW);
    }
    delay(1);
}
