/**
 * NodeMCU – UART relay bridge + WiFi/MQTT cloud logger
 *
 * Physical topology
 * -----------------
 *   F401  D8 (TX) ──► NodeMCU D7 (RX)   stmSerial receives F401 STATE
 *   F303  D2 (RX) ◄── NodeMCU D5 (TX)   stmSerial relays F401 STATE → F303
 *   NodeMCU USB        ──► PC debug terminal (Serial 115200)
 *
 * WiFi / MQTT
 * -----------
 *   Broker : broker.hivemq.com  (public, no auth, change below if you have
 *             your own broker)
 *   Topics published:
 *     sugarcane/state/F401      ← every STATE packet from F401
 *     sugarcane/consensus       ← every CONSENSUS packet (from either board)
 *     sugarcane/heartbeat       ← NodeMCU alive ping every 15 s
 *
 * What this code does
 * -------------------
 *   1. Connects to WiFi on boot (retries indefinitely with status blink).
 *   2. Connects to MQTT broker (auto-reconnects if connection drops).
 *   3. Receives any packet from F401 on D7.
 *      - STATE  → relay to F303 via D5  AND  publish to MQTT.
 *      - CONSENSUS → publish to MQTT only (never forwarded to STM boards).
 *   4. Every 15 s: publish heartbeat JSON to MQTT.
 *
 * ⚠️  FILL IN YOUR WiFi SSID AND PASSWORD BELOW BEFORE FLASHING.
 */

#include <Arduino.h>
/* Increase SoftwareSerial RX buffer (default=64) before the include.
 * At 115200 baud a single STATE packet is ~55 bytes; 256 gives comfortable
 * headroom so no bytes are dropped between reads.                        */
#define _SS_MAX_RX_BUFF 256
#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <string.h>

/* =========================================================================
 * ⚠️  USER CONFIGURATION — edit these before flashing
 * ========================================================================= */
static const char *WIFI_SSID     = "Galaxy A23 5G 5865";
static const char *WIFI_PASS     = "12345678";

static const char *MQTT_BROKER   = "broker.hivemq.com";
static const uint16_t MQTT_PORT  = 1883;
/* Unique client ID — change if you have multiple NodeMCUs */
static const char *MQTT_CLIENT_ID = "sugarcane_nodemcu_01";

/* MQTT topics */
static const char *TOPIC_STATE_F401  = "sugarcane/state/F401";
static const char *TOPIC_CONSENSUS   = "sugarcane/consensus";
static const char *TOPIC_HEARTBEAT   = "sugarcane/heartbeat";

/* =========================================================================
 * UART pin assignment
 *   D7 = GPIO13 = receive from F401
 *   D5 = GPIO14 = transmit to F303 D2
 * ========================================================================= */
static const uint32_t STM_BAUD = 115200;
SoftwareSerial stmSerial(D7, D5);

/* =========================================================================
 * WiFi + MQTT clients
 * ========================================================================= */
WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

/* =========================================================================
 * Runtime state
 * ========================================================================= */
static String   rxLine;
static uint32_t lastHeartbeatMs  = 0;
static uint32_t lastMqttRetryMs  = 0;
static uint32_t f401PacketsRelayed  = 0;
static uint32_t consensusPacketsSeen = 0;

struct ObservedState {
    String   nodeId;
    int      soil      = 0;
    int      tempC     = 0;
    bool     needsWater = false;
    uint32_t seq       = 0;
    String   lastAct;   /* last CONSENSUS action seen */
    bool     valid     = false;
};

static ObservedState g_f401;
static ObservedState g_f303;

/* =========================================================================
 * WiFi helpers
 * ========================================================================= */
static void WiFi_Connect()
{
    Serial.print("[WiFi] Connecting to ");
    Serial.print(WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    uint8_t blink = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(250);
        digitalWrite(LED_BUILTIN, blink++ % 2 ? HIGH : LOW);
        Serial.print(".");
    }
    digitalWrite(LED_BUILTIN, HIGH);   /* LED off (active-low on NodeMCU) */
    Serial.println();
    Serial.print("[WiFi] Connected, IP: ");
    Serial.println(WiFi.localIP());
}

/* =========================================================================
 * MQTT helpers
 * ========================================================================= */
static bool MQTT_Connect()
{
    if (mqtt.connected()) return true;

    uint32_t now = millis();
    if (now - lastMqttRetryMs < 5000) return false;   /* retry every 5 s */
    lastMqttRetryMs = now;

    Serial.print("[MQTT] Connecting to ");
    Serial.print(MQTT_BROKER);
    Serial.print("...");

    if (mqtt.connect(MQTT_CLIENT_ID)) {
        Serial.println(" OK");
        /* Publish online status */
        mqtt.publish(TOPIC_HEARTBEAT,
                     "{\"status\":\"online\",\"node\":\"NodeMCU\"}", true);
        return true;
    }

    Serial.print(" FAILED rc=");
    Serial.println(mqtt.state());
    return false;
}

static void MQTT_Publish(const char *topic, const String &payload)
{
    if (!MQTT_Connect()) return;
    mqtt.publish(topic, payload.c_str());
}

/* =========================================================================
 * Packet field parser
 * ========================================================================= */
static bool extractInt(const String &line, const String &key, int &out)
{
    int pos = line.indexOf(key + "=");
    if (pos < 0) return false;
    int start = pos + key.length() + 1;
    int end   = line.indexOf('|', start);
    if (end < 0) end = line.length();
    out = line.substring(start, end).toInt();
    return true;
}

static bool extractStr(const String &line, const String &key, String &out)
{
    int pos = line.indexOf(key + "=");
    if (pos < 0) return false;
    int start = pos + key.length() + 1;
    int end   = line.indexOf('|', start);
    if (end < 0) end = line.length();
    out = line.substring(start, end);
    return true;
}

/* =========================================================================
 * Build JSON from a raw STATE line
 *   STATE|NODE=F401|SM=442|T=30|NW=1|SEQ=5|TS=62000
 *   → {"node":"F401","soil":442,"temp":30,"needs_water":1,"seq":5,"ts":62000,"rssi":-65}
 * ========================================================================= */
static String StateToJson(const String &line, ObservedState &obs)
{
    String nodeId;
    int sm = 0, t = 0, nw = 0, seq = 0, ts = 0;

    extractStr(line, "NODE", nodeId);
    extractInt(line, "SM",   sm);
    extractInt(line, "T",    t);
    extractInt(line, "NW",   nw);
    extractInt(line, "SEQ",  seq);
    extractInt(line, "TS",   ts);

    obs.nodeId      = nodeId;
    obs.soil        = sm;
    obs.tempC       = t;
    obs.needsWater  = (nw != 0);
    obs.seq         = (uint32_t)seq;
    obs.valid       = true;

    String json = "{";
    json += "\"node\":\"" + nodeId + "\",";
    json += "\"soil\":"   + String(sm)  + ",";
    json += "\"temp\":"   + String(t)   + ",";
    json += "\"needs_water\":" + String(nw) + ",";
    json += "\"seq\":"    + String(seq) + ",";
    json += "\"ts\":"     + String(ts)  + ",";
    json += "\"rssi\":"   + String(WiFi.RSSI());
    json += "}";
    return json;
}

/* =========================================================================
 * Build JSON from a raw CONSENSUS line
 *   CONSENSUS|ACT=IRRIGATE|REASON=BOTH_NEED|BY=F303|SEQ=5|TS=62010
 *   → {"act":"IRRIGATE","reason":"BOTH_NEED","by":"F303","seq":5,"ts":62010}
 * ========================================================================= */
static String ConsensusToJson(const String &line)
{
    String act, reason, by;
    int seq = 0, ts = 0;

    extractStr(line, "ACT",    act);
    extractStr(line, "REASON", reason);
    extractStr(line, "BY",     by);
    extractInt(line, "SEQ",    seq);
    extractInt(line, "TS",     ts);

    String json = "{";
    json += "\"act\":\""    + act    + "\",";
    json += "\"reason\":\"" + reason + "\",";
    json += "\"by\":\""     + by     + "\",";
    json += "\"seq\":"      + String(seq) + ",";
    json += "\"ts\":"       + String(ts);
    json += "}";
    return json;
}

/* =========================================================================
 * Process one complete received line
 * ========================================================================= */
static void handleLine(const String &line)
{
    /* ── STATE packet ── */
    if (line.startsWith("STATE|")) {
        if (line.indexOf("NODE=F401") > 0) {
            /* 1. Relay to F303 (consensus relay — unchanged) */
            stmSerial.println(line);
            f401PacketsRelayed++;

            /* 2. Publish to MQTT as JSON */
            String json = StateToJson(line, g_f401);
            MQTT_Publish(TOPIC_STATE_F401, json);

            Serial.print("[RELAY+MQTT F401→F303] ");
            Serial.println(line);

        } else if (line.indexOf("NODE=F303") > 0) {
            StateToJson(line, g_f303);   /* observation only */
            Serial.print("[OBS F303] ");
            Serial.println(line);
        }
        return;
    }

    /* ── CONSENSUS packet ── */
    if (line.startsWith("CONSENSUS|")) {
        consensusPacketsSeen++;

        String json = ConsensusToJson(line);

        /* Update last known action */
        String act;
        extractStr(line, "ACT", act);
        String by;
        extractStr(line, "BY", by);
        if (by == "F401") g_f401.lastAct = act;
        if (by == "F303") g_f303.lastAct = act;

        MQTT_Publish(TOPIC_CONSENSUS, json);

        Serial.print("[CONSENSUS→MQTT] ");
        Serial.println(line);
        return;
    }

    /* ── Anything else ── */
    Serial.print("[RX?] ");
    Serial.println(line);
}

/* =========================================================================
 * Heartbeat JSON
 * ========================================================================= */
static void publishHeartbeat()
{
    String json = "{";
    json += "\"uptime_ms\":"   + String(millis()) + ",";
    json += "\"relayed\":"     + String(f401PacketsRelayed) + ",";
    json += "\"consensus_seen\":" + String(consensusPacketsSeen) + ",";
    json += "\"rssi\":"        + String(WiFi.RSSI()) + ",";
    json += "\"f401_nw\":"     + String(g_f401.valid ? (g_f401.needsWater ? 1 : 0) : -1) + ",";
    json += "\"f401_act\":\"" + (g_f401.lastAct.length() ? g_f401.lastAct : "?") + "\",";
    json += "\"f401_soil\":"   + String(g_f401.valid ? g_f401.soil : -1) + ",";
    json += "\"f401_temp\":"   + String(g_f401.valid ? g_f401.tempC : -1);
    json += "}";

    MQTT_Publish(TOPIC_HEARTBEAT, json);

    Serial.print("[Heartbeat] relayed=");
    Serial.print(f401PacketsRelayed);
    Serial.print(" | consensus=");
    Serial.print(consensusPacketsSeen);
    Serial.print(" | F401_NW=");
    Serial.print(g_f401.valid ? (g_f401.needsWater ? "1" : "0") : "?");
    Serial.print(" | RSSI=");
    Serial.println(WiFi.RSSI());
}

/* =========================================================================
 * Setup
 * ========================================================================= */
void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);   /* off */

    Serial.begin(115200);
    stmSerial.begin(STM_BAUD);

    delay(300);
    Serial.println();
    Serial.println("==================================");
    Serial.println("[NodeMCU] Sugarcane IoT gateway");
    Serial.println("  RX D7 (GPIO13) <- F401 D8");
    Serial.println("  TX D5 (GPIO14) -> F303 D2");
    Serial.println("==================================");

    /* WiFi */
    WiFi_Connect();

    /* MQTT */
    mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    mqtt.setBufferSize(512);   /* default 256 may truncate JSON */
    MQTT_Connect();

    lastHeartbeatMs = millis();
}

/* =========================================================================
 * Loop
 * ========================================================================= */
void loop()
{
    /* ── Keep WiFi alive ── */
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Reconnecting...");
        WiFi_Connect();
    }

    /* ── Keep MQTT alive ── */
    mqtt.loop();
    MQTT_Connect();

    /* ── Drain bytes from F401, build lines ── */
    while (stmSerial.available()) {
        char c = (char)stmSerial.read();

        if (c == '\r') continue;

        if (c == '\n') {
            if (rxLine.length() > 0) {
                handleLine(rxLine);
            }
            rxLine = "";

            /* Blink LED to show UART activity */
            digitalWrite(LED_BUILTIN, LOW);
            delay(2);
            digitalWrite(LED_BUILTIN, HIGH);
            continue;
        }

        rxLine += c;
    }

    /* ── Heartbeat every 15 s ── */
    if (millis() - lastHeartbeatMs >= 15000UL) {
        lastHeartbeatMs = millis();
        publishHeartbeat();
    }
}
