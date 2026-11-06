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
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <errno.h>
#include <esp_bt_device.h>
#include <esp_bt_main.h>
#include <esp_gap_bt_api.h>
#include <esp_idf_version.h>
#include <esp_random.h>
#include <esp_spp_api.h>
#include <freertos/stream_buffer.h>
#include <lwip/sockets.h>
#include <new>

extern "C" {
#include "bridge_core.h"
#include "bt_output.h"
#include "tcp_frame_input.h"
#include "tcp_output.h"
}

#if !defined(CONFIG_BT_ENABLED)
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

static WiFiClient tcp;
static bridge br;
static uint8_t btPeer[6];
static bool btConnected = false;
static bool tcpWanted = false;
static bool tcpConnected = false;
static WebServer *web = nullptr;

struct BtEvent
{
    bool opened;
    uint8_t peer[6];
    int status;
    uint32_t portStatus;
    bool remote;
};
static portMUX_TYPE btEventMux = portMUX_INITIALIZER_UNLOCKED;
static BtEvent btEvents[16];
static unsigned btEventHead = 0;
static unsigned btEventCount = 0;
static uint32_t btHandle = 0;
static bool btReceiving = false;
static bool btEventOverflow = false;

/* Callback-owned state is protected by btEventMux; the loop owns btTx. */
#define BT_RX_BUFFER        2048
#define BT_TX_CHUNK         512
#define BT_WRITE_NONE       (-2)
#define BT_WRITE_TIMEOUT_MS 10000
static StreamBufferHandle_t btRx = nullptr;
static uint32_t btRxDropped = 0;
static bool btRxOverflow = false;
static bool btCongested = false;
static int btWriteResult = BT_WRITE_NONE;
static int btWriteStatus = ESP_SPP_SUCCESS;
static bt_output btTx;
static uint32_t btWriteSince = 0;
static uint32_t btWriteMaxMs = 0;
static uint32_t loopMaxMs = 0;
static uint32_t btDisconnectSince = 0;
static bool btDisconnectPending = false;
static bool btTxOverflow = false;
static size_t btTxHighWater = 0;

struct ConnectRequest
{
    String host;
    uint16_t port;
    uint32_t generation;
    bool connected;
    WiFiClient client;
};
static QueueHandle_t connectRequests = nullptr;
static QueueHandle_t connectResults = nullptr;
static bool connectPending = false;
static uint32_t connectGeneration = 0;

/* The bridge emits complete frames synchronously; retain unsent bytes in order. */
static tcp_output tcpOutput;
static bool tcpOutputFailed = false;

static bool btTxAppend(const uint8_t *d, size_t n)
{
    if (btDisconnectPending || btTxOverflow || !bt_output_append(&btTx, d, n)) {
        btTxOverflow = true;
        return false;
    }
    if (btTx.queue.len > btTxHighWater) {
        btTxHighWater = btTx.queue.len;
    }
    return true;
}

static void btDisconnect(uint32_t handle, const char *reason)
{
    if (btDisconnectPending || !handle) {
        return;
    }
    btDisconnectPending = true;
    btDisconnectSince = millis();
    Serial.printf("Bluetooth: %s; closing SPP handle %lu\n", reason, (unsigned long)handle);
    esp_err_t err = esp_spp_disconnect(handle);
    if (err != ESP_OK) {
        Serial.printf("Bluetooth disconnect request failed: %d\n", (int)err);
    }
}

static void btTxPump()
{
    uint32_t handle;
    int result, status;
    bool congested, receiving;
    portENTER_CRITICAL(&btEventMux);
    handle = btHandle;
    receiving = btReceiving;
    result = btWriteResult;
    status = btWriteStatus;
    btWriteResult = BT_WRITE_NONE;
    congested = btCongested;
    portEXIT_CRITICAL(&btEventMux);
    if (btDisconnectPending && receiving && (uint32_t)(millis() - btDisconnectSince) >= BT_WRITE_TIMEOUT_MS) {
        Serial.println("Bluetooth disconnect timed out; restarting");
        ESP.restart();
    }
    if (!handle || !receiving || btDisconnectPending) {
        return;
    }
    if (btTxOverflow) {
        btDisconnect(handle, "TX buffer full");
        return;
    }
    if (result != BT_WRITE_NONE) {
        if (btTx.inflight) {
            uint32_t elapsed = millis() - btWriteSince;
            if (elapsed > btWriteMaxMs) {
                btWriteMaxMs = elapsed;
            }
        }
        if (result <= 0 || !bt_output_complete(&btTx, (size_t)result)) {
            Serial.printf("Bluetooth write completion: status %d, length %d, pending %u\n",
                          status, result, (unsigned)btTx.inflight);
            btDisconnect(handle, "invalid or failed write completion");
            return;
        }
    }
    if (btTx.inflight && (uint32_t)(millis() - btWriteSince) >= BT_WRITE_TIMEOUT_MS) {
        btDisconnect(handle, "write completion timed out");
        return;
    }
    if (btTx.inflight || congested) {
        return;
    }
    size_t n = bt_output_begin(&btTx, BT_TX_CHUNK);
    if (!n) {
        return;
    }
    btWriteSince = millis();
    esp_err_t err = esp_spp_write(handle, (int)n, btTx.queue.data + btTx.queue.head);
    if (err != ESP_OK) {
        Serial.printf("Bluetooth write request failed: %d\n", (int)err);
        btDisconnect(handle, "write request rejected");
    }
}

static size_t btRxAvailable()
{
    return btRx ? xStreamBufferBytesAvailable(btRx) : 0;
}

static size_t btRxRead(uint8_t *buf, size_t n)
{
    return btRx ? xStreamBufferReceive(btRx, buf, n, 0) : 0;
}

/* Output stream for the Bluetooth configuration console. */
class BtPrint : public Print
{
  public:
    size_t write(uint8_t c) override
    {
        return write(&c, 1);
    }
    size_t write(const uint8_t *b, size_t n) override
    {
        return btTxAppend(b, n) ? n : 0;
    }
    void flush() override
    {
        /* bounded wait, only used right before a restart */
        for (uint32_t t0 = millis(); btTx.queue.len && millis() - t0 < 300;) {
            btTxPump();
            delay(5);
        }
    }
};
static BtPrint btOut;

static void io_dev_write(void *, const gc_u8 *d, size_t n)
{
    if (btConnected) {
        btTxAppend(d, n);
    }
}

static void io_srv_write(void *, const gc_u8 *d, size_t n)
{
    if (!tcpConnected || tcpOutputFailed) {
        return;
    }
    if (!tcp_output_append(&tcpOutput, d, n)) {
        Serial.println("TCP output buffer full; reconnecting");
        tcpOutputFailed = true;
    }
}

static void io_srv_connect(void *)
{
    tcpWanted = true;
}

static void io_srv_disconnect(void *)
{
    tcpWanted = false;
    ++connectGeneration;
    if (tcpConnected) {
        tcp.stop();
        tcpConnected = false;
    }
    tcp_output_clear(&tcpOutput);
    tcpOutputFailed = false;
}

static void io_log(void *, const char *line)
{
    if (cfg.verbose) {
        Serial.printf("[%8lu] %s\n", (unsigned long)millis(), line);
    }
}

/* SPP callbacks run on the Bluetooth task: copy or flag only, never parse or
   block. Lifecycle events retain order and peer for the main loop. */
static void sppCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    switch (event) {
    case ESP_SPP_INIT_EVT:
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
        esp_spp_start_srv(ESP_SPP_SEC_NONE, ESP_SPP_ROLE_SLAVE, 0, "SerialBT");
        return;
    case ESP_SPP_DATA_IND_EVT:
    {
        bool accept;
        portENTER_CRITICAL(&btEventMux);
        accept = btReceiving && param->data_ind.handle == btHandle && !btRxOverflow;
        if (btReceiving && param->data_ind.handle == btHandle && btRxOverflow) {
            btRxDropped += param->data_ind.len;
        }
        portEXIT_CRITICAL(&btEventMux);
        if (accept && btRx) {
            size_t sent = xStreamBufferSend(btRx, param->data_ind.data, param->data_ind.len, 0);
            if (sent < param->data_ind.len) {
                portENTER_CRITICAL(&btEventMux);
                btRxDropped += param->data_ind.len - sent;
                btRxOverflow = true;
                portEXIT_CRITICAL(&btEventMux);
            }
        }
        return;
    }
    case ESP_SPP_WRITE_EVT:
        portENTER_CRITICAL(&btEventMux);
        if (btReceiving && bt_output_event_matches(btHandle, param->write.handle,
                                                   param->write.status == ESP_SPP_SUCCESS)) {
            btWriteStatus = param->write.status;
            btWriteResult = param->write.status == ESP_SPP_SUCCESS ? param->write.len : -1;
            btCongested = param->write.cong;
        }
        portEXIT_CRITICAL(&btEventMux);
        return;
    case ESP_SPP_CONG_EVT:
        portENTER_CRITICAL(&btEventMux);
        if (btReceiving && param->cong.handle == btHandle) {
            btCongested = param->cong.cong;
        }
        portEXIT_CRITICAL(&btEventMux);
        return;
    default:
        break;
    }

    BtEvent next = {};
    bool enqueue = false;
    uint32_t extraHandle = 0;
    portENTER_CRITICAL(&btEventMux);
    if (event == ESP_SPP_SRV_OPEN_EVT && param->srv_open.status == ESP_SPP_SUCCESS && btHandle) {
        extraHandle = param->srv_open.handle; /* one device at a time */
    } else if (event == ESP_SPP_SRV_OPEN_EVT && param->srv_open.status == ESP_SPP_SUCCESS) {
        btHandle = param->srv_open.handle;
        btReceiving = true;
        btCongested = false;
        btWriteResult = BT_WRITE_NONE;
        next.opened = true;
        memcpy(next.peer, param->srv_open.rem_bda, sizeof(next.peer));
        enqueue = true;
    } else if (event == ESP_SPP_CLOSE_EVT && param->close.handle == btHandle && btHandle && btReceiving &&
               (param->close.async || param->close.status == ESP_SPP_SUCCESS)) {
        /* Reserve the handle until the loop has discarded the old RX data. */
        btReceiving = false;
        next.status = param->close.status;
        next.portStatus = param->close.port_status;
        next.remote = param->close.async;
        enqueue = true;
    }
    if (enqueue) {
        if (btEventCount == sizeof(btEvents) / sizeof(btEvents[0])) {
            btEventOverflow = true;
        } else {
            btEvents[(btEventHead + btEventCount) % (sizeof(btEvents) / sizeof(btEvents[0]))] = next;
            ++btEventCount;
        }
    }
    portEXIT_CRITICAL(&btEventMux);
    if (extraHandle) {
        esp_spp_disconnect(extraHandle);
    }
}

static void gapCallback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    if (event == ESP_BT_GAP_PIN_REQ_EVT) {
        esp_bt_pin_code_t code = {};
        size_t len = cfg.pin.length() < ESP_BT_PIN_CODE_LEN ? cfg.pin.length() : ESP_BT_PIN_CODE_LEN;
        memcpy(code, cfg.pin.c_str(), len);
        esp_bt_gap_pin_reply(param->pin_req.bda, true, (uint8_t)len, code);
    } else if (event == ESP_BT_GAP_CFM_REQ_EVT) {
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
    }
}

static bool btInit(const char *name, const String &pin)
{
    btRx = xStreamBufferCreate(BT_RX_BUFFER, 1);
    if (!btRx) {
        return false;
    }
    if (!btStarted() && !btStart()) {
        return false;
    }
    if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_UNINITIALIZED && esp_bluedroid_init() != ESP_OK) {
        return false;
    }
    if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_ENABLED && esp_bluedroid_enable() != ESP_OK) {
        return false;
    }
    if (esp_bt_gap_register_callback(gapCallback) != ESP_OK || esp_spp_register_callback(sppCallback) != ESP_OK) {
        return false;
    }
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    esp_spp_cfg_t sppCfg = {};
    sppCfg.mode = ESP_SPP_MODE_CB;
    sppCfg.enable_l2cap_ertm = true;
    sppCfg.tx_buffer_size = 0;
    if (esp_spp_enhanced_init(&sppCfg) != ESP_OK) {
#else
    if (esp_spp_init(ESP_SPP_MODE_CB) != ESP_OK) {
#endif
        return false;
    }
    esp_bt_dev_set_device_name(name);

    /* The N-Gage only supports Bluetooth 1.1 legacy PIN pairing: fixed PIN. */
    esp_bt_pin_code_t code = {};
    size_t len = pin.length() < ESP_BT_PIN_CODE_LEN ? pin.length() : ESP_BT_PIN_CODE_LEN;
    memcpy(code, pin.c_str(), len);
    if (esp_bt_gap_set_pin(len ? ESP_BT_PIN_TYPE_FIXED : ESP_BT_PIN_TYPE_VARIABLE, (uint8_t)len, code) != ESP_OK) {
        Serial.println("Bluetooth PIN could not be set");
    }

    /* the default loudspeaker class of device does not work with macOS */
    esp_bt_cod_t cod = {};
    cod.major = 0b00001;
    cod.minor = 0b000100;
    cod.service = 0b00000010110;
    return esp_bt_gap_set_cod(cod, ESP_BT_INIT_COD) == ESP_OK;
}

/* ------------------------------------------------------------------ */
/* server discovery / connection                                       */
/* ------------------------------------------------------------------ */

static bool discoverServer(IPAddress &discoveredIp, uint16_t &discoveredPort)
{
    WiFiUDP udp;
    if (!udp.begin(0)) {
        return false;
    }
    IPAddress bcast = WiFi.localIP();
    bcast[3] = 255;
    IPAddress mask = WiFi.subnetMask();
    for (int i = 0; i < 4; i++) {
        bcast[i] = (WiFi.localIP()[i] & mask[i]) | (~mask[i] & 0xFF);
    }
    udp.beginPacket(bcast, DISCOVERY_PORT);
    udp.print("GCDISCOVER1");
    udp.endPacket();
    uint32_t t0 = millis();
    while (millis() - t0 < 800) {
        int n = udp.parsePacket();
        if (n > 0) {
            char buf[96] = { 0 };
            udp.read(buf, sizeof(buf) - 1);
            int port = 0;
            if (sscanf(buf, "GCSERVER1 %d", &port) == 1 && port > 0 && port <= 65535) {
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

static void connectTask(void *)
{
    for (;;) {
        ConnectRequest *request;
        if (xQueueReceive(connectRequests, &request, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (request->host == "auto" || request->host.length() == 0) {
            IPAddress ip;
            uint16_t port;
            request->connected = discoverServer(ip, port) && request->client.connect(ip, port, 3000);
        } else {
            request->connected = request->client.connect(request->host.c_str(), request->port, 3000);
        }
        xQueueSend(connectResults, &request, portMAX_DELAY);
    }
}

static void startConnectServer()
{
    tcpWanted = false;
    ConnectRequest *request = new (std::nothrow) ConnectRequest;
    if (!request) {
        Serial.println("Cannot allocate server connection request");
        bridge_srv_disconnected(&br, millis());
        return;
    }
    request->host = cfg.host;
    request->port = cfg.port;
    request->generation = connectGeneration;
    request->connected = false;
    if (!connectRequests || xQueueSend(connectRequests, &request, 0) != pdTRUE) {
        Serial.println("Cannot start server connection task");
        delete request;
        bridge_srv_disconnected(&br, millis());
        return;
    }
    connectPending = true;
}

static void finishConnectServer()
{
    ConnectRequest *request;
    if (!connectResults || xQueueReceive(connectResults, &request, 0) != pdTRUE) {
        return;
    }
    connectPending = false;
    if (request->generation != connectGeneration || WiFi.status() != WL_CONNECTED) {
        request->client.stop();
    } else if (!request->connected) {
        tcpWanted = false;
        bridge_srv_disconnected(&br, millis());
    } else {
        tcpWanted = false;
        tcp = request->client;
        tcp.setNoDelay(true);
        tcpConnected = true;
        bridge_srv_connected(&br, millis());
    }
    delete request;
}

static void flushTcpOutput()
{
    while (tcpConnected && tcpOutput.len && !tcpOutputFailed) {
        size_t n = tcp_output_chunk(&tcpOutput);
        int sent = send(tcp.fd(), tcpOutput.data + tcpOutput.head, n, MSG_DONTWAIT);
        if (sent > 0) {
            tcp_output_consume(&tcpOutput, (size_t)sent);
        } else if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            break;
        } else {
            Serial.printf("TCP write failed (%d); reconnecting\n", errno);
            tcpOutputFailed = true;
        }
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

static void printStatus(Print &out)
{
    uint32_t drops;
    bool congested;
    portENTER_CRITICAL(&btEventMux);
    drops = btRxDropped;
    congested = btCongested;
    portEXIT_CRITICAL(&btEventMux);
    out.printf("GameComms bridge %s\n", BRIDGE_VERSION);
    out.printf("  bt name   : %s (pin %s)\n", cfg.btname.c_str(), cfg.pin.c_str());
    out.printf("  device    : %s, link state %d\n", btConnected ? "connected" : "-", br.dev.state);
    out.printf("  bt rx     : %u buffered, %lu bytes dropped, frame errors crc %lu cobs %lu oversize %lu short %lu\n",
               (unsigned)btRxAvailable(), (unsigned long)drops, (unsigned long)br.dev.rx.err_crc,
               (unsigned long)br.dev.rx.err_cobs, (unsigned long)br.dev.rx.err_oversize,
               (unsigned long)br.dev.rx.err_short);
    out.printf("  bt tx     : %u buffered, %u peak, %u in flight, congested %s\n", (unsigned)btTx.queue.len,
               (unsigned)btTxHighWater, (unsigned)btTx.inflight, congested ? "yes" : "no");
    out.printf("  wifi      : %s %s rssi %d\n", cfg.ssid.c_str(),
               WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "(down)", WiFi.RSSI());
    out.printf("  server    : %s:%u state %d link %d\n", cfg.host.c_str(), cfg.port, br.srv_state, br.srv.state);
    out.printf("  messages  : dev->srv %lu srv->dev %lu, retx dev %lu srv %lu\n", (unsigned long)br.st_dev_msgs,
               (unsigned long)br.st_srv_msgs, (unsigned long)br.dev.st_retx, (unsigned long)br.srv.st_retx);
    out.printf("  pressure  : dev %lu srv %lu, out-of-order dev %lu srv %lu\n",
               (unsigned long)br.dev.st_backpressure, (unsigned long)br.srv.st_backpressure,
               (unsigned long)br.dev.st_ooo, (unsigned long)br.srv.st_ooo);
    out.printf("  queues    : dev link %u/%u fifo %d/%d, srv link %u/%u fifo %d/%d\n",
               (unsigned)br.dev.q_count, (unsigned)GC_LINK_QUEUE, br.to_dev.count, BRIDGE_FIFO_LEN,
               (unsigned)br.srv.q_count, (unsigned)GC_LINK_QUEUE, br.to_srv.count, BRIDGE_FIFO_LEN);
    out.printf("  latency   : max BT write %lu ms, max loop interval %lu ms (since boot)\n",
               (unsigned long)btWriteMaxMs, (unsigned long)loopMaxMs);
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
    btOut.printf("OK GCCFG1 %s\r\nEND\r\n", BRIDGE_VERSION);
}

static void btOnOpen(uint32_t now)
{
    btMode = BT_SNIFF;
    btSniffLen = 0;
    btSniffSince = now;
}

static void btOnClose(uint32_t now)
{
    bool overflow;
    uint32_t drops;
    portENTER_CRITICAL(&btEventMux);
    overflow = btRxOverflow;
    drops = btRxDropped;
    portEXIT_CRITICAL(&btEventMux);
    if (overflow && !btDisconnectPending) {
        Serial.printf("Bluetooth RX buffer overflow: %lu total bytes dropped\n", (unsigned long)drops);
    }
    if (btMode == BT_GAME && btConnected) {
        btConnected = false;
        bridge_dev_disconnected(&br, now);
    }
    btMode = BT_NONE;
    btSniffLen = 0;
    bt_output_reset(&btTx);
    btTxOverflow = false;
    btDisconnectPending = false;
    if (btRx && xStreamBufferReset(btRx) != pdPASS) {
        Serial.println("Bluetooth RX reset failed; restarting");
        ESP.restart();
    }
    portENTER_CRITICAL(&btEventMux);
    btHandle = 0;
    btRxOverflow = false;
    btWriteResult = BT_WRITE_NONE;
    portEXIT_CRITICAL(&btEventMux);
}

static void btPump(uint32_t now)
{
    if (btMode == BT_SNIFF) {
        while (btMode == BT_SNIFF && btRxAvailable() > 0) {
            uint8_t c = 0;
            btRxRead(&c, 1);
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
        while ((n = btRxRead(buf, sizeof(buf))) > 0) {
            size_t pos = 0;
            while (pos < n) {
                size_t chunk = stream_frame_input_chunk(buf + pos, n - pos);
                bridge_dev_input(&br, buf + pos, chunk, millis());
                pos += chunk;
            }
        }
    } else if (btMode == BT_CONFIG) {
        while (btMode == BT_CONFIG && btRxAvailable() > 0) {
            uint8_t raw = 0;
            btRxRead(&raw, 1);
            char c = (char)raw;
            if (c == '\n' || c == '\r') {
                btCmdline.trim();
                if (btCmdline.length()) {
                    handleCommand(btCmdline, btOut);
                    btOut.print("END\r\n");
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

    connectRequests = xQueueCreate(1, sizeof(ConnectRequest *));
    connectResults = xQueueCreate(1, sizeof(ConnectRequest *));
    if (!connectRequests || !connectResults ||
        xTaskCreatePinnedToCore(connectTask, "server-connect", 4096, nullptr, 1, nullptr,
#if CONFIG_FREERTOS_UNICORE
                                0
#else
                                xPortGetCoreID() == 0 ? 1 : 0
#endif
                                ) != pdPASS) {
        Serial.println("Server connection worker could not be started");
        connectRequests = nullptr;
    }

    if (!btInit(cfg.btname.c_str(), cfg.pin)) {
        Serial.println("Bluetooth init failed");
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
    static uint32_t lastLoop = 0;
    static bool haveLastLoop = false;
    uint32_t now = millis();
    if (haveLastLoop && now - lastLoop > loopMaxMs) {
        loopMaxMs = now - lastLoop;
    }
    lastLoop = now;
    haveLastLoop = true;

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
    for (;;) {
        BtEvent event;
        bool haveEvent;
        bool overflow;
        portENTER_CRITICAL(&btEventMux);
        overflow = btEventOverflow;
        haveEvent = btEventCount != 0;
        if (haveEvent) {
            event = btEvents[btEventHead];
            btEventHead = (btEventHead + 1) % (sizeof(btEvents) / sizeof(btEvents[0]));
            --btEventCount;
        }
        portEXIT_CRITICAL(&btEventMux);
        if (overflow) {
            Serial.println("Bluetooth event queue overflow; restarting");
            delay(100);
            ESP.restart();
        }
        if (!haveEvent) {
            break;
        }
        if (event.opened) {
            memcpy(btPeer, event.peer, sizeof(btPeer));
            btOnOpen(millis());
        } else {
            Serial.printf("Bluetooth SPP closed: status %d, port status %lu, %s\n", event.status,
                          (unsigned long)event.portStatus, event.remote ? "remote" : "local");
            btOnClose(millis());
        }
    }
    {
        bool overflow;
        uint32_t handle, drops;
        portENTER_CRITICAL(&btEventMux);
        overflow = btRxOverflow;
        handle = btHandle;
        drops = btRxDropped;
        portEXIT_CRITICAL(&btEventMux);
        if (overflow && !btDisconnectPending) {
            Serial.printf("Bluetooth RX buffer overflow: %lu total bytes dropped\n", (unsigned long)drops);
            btDisconnect(handle, "RX buffer overflow");
        } else if (!overflow && !btDisconnectPending) {
            btPump(millis());
        }
    }

    /* Wi-Fi */
    if (!web) {
        bool up = WiFi.status() == WL_CONNECTED;
        bridge_set_wifi(&br, up ? 1 : 0, (gc_i8)WiFi.RSSI(), now);
    }

    /* TCP */
    finishConnectServer();
    flushTcpOutput();
    if (tcpConnected && (tcpOutputFailed || !tcp.connected())) {
        tcp.stop();
        tcpConnected = false;
        tcp_output_clear(&tcpOutput);
        tcpOutputFailed = false;
        bridge_srv_disconnected(&br, millis());
    }
    if (tcpConnected) {
        static tcp_frame_pause pause = {};
        uint8_t buf[512];
        size_t budget = 2048;
        while (budget) {
            /* A server ACK may be behind data in the TCP stream. Never
               indefinitely block it or the server link's control frames. */
            if (!tcp_frame_input_ready(&pause, br.dev_connected && br.dev.state == GC_LS_UP,
                                       br.to_dev.count, BRIDGE_FIFO_LEN, br.srv.q_sent, millis())) {
                break;
            }
            size_t chunk = budget < sizeof(buf) ? budget : sizeof(buf);
            int n = recv(tcp.fd(), buf, chunk, MSG_PEEK | MSG_DONTWAIT);
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
                break;
            }
            if (n <= 0) {
                if (n < 0) {
                    Serial.printf("TCP read failed (%d); reconnecting\n", errno);
                } else {
                    Serial.println("TCP peer closed; reconnecting");
                }
                tcpOutputFailed = true;
                break;
            }
            chunk = stream_frame_input_chunk(buf, (size_t)n);
            n = recv(tcp.fd(), buf, chunk, MSG_DONTWAIT);
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
                break;
            }
            if (n <= 0) {
                if (n < 0) {
                    Serial.printf("TCP read failed (%d); reconnecting\n", errno);
                } else {
                    Serial.println("TCP peer closed; reconnecting");
                }
                tcpOutputFailed = true;
                break;
            }
            budget -= (size_t)n;
            bridge_srv_input(&br, buf, (size_t)n, millis());
        }
    }
    if (tcpWanted && !tcpConnected && !connectPending && WiFi.status() == WL_CONNECTED) {
        startConnectServer();
    }

    bridge_poll(&br, millis());
    flushTcpOutput();
    btTxPump();

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
