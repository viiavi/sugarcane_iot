#include <Arduino.h>
#include <SoftwareSerial.h>
#include <string.h>

/**
 * NodeMCU distributed consensus irrigation (UART prototype).
 *
 * This version runs the consensus logic locally and exchanges compact
 * text packets with a peer over UART so you can validate behavior offline.
 * Transport can later be swapped to ESP-NOW/UDP while keeping protocol/logic.
 *
 * Packet format:
 *   STATE|NODE=<id>|SM=<soil>|T=<temp_c>|NW=<0/1>|SEQ=<n>|TS=<ms>
 *   CONSENSUS|ACT=<IRRIGATE/WAIT/SLEEP>|REASON=<text>|SEQ=<n>|TS=<ms>
 */

static const uint32_t STM_BAUD = 115200;
SoftwareSerial stmSerial(D7, -1);
String rxLine;

static const char *kNodeId = "F303";
static const uint16_t SOIL_NEED_WATER_THRESHOLD = 1200;
static const uint32_t WAIT_WINDOW_MS = 30UL * 60UL * 1000UL;
static const uint32_t SLEEP_WINDOW_MS = 2UL * 60UL * 60UL * 1000UL;
static const uint32_t PEER_STALE_MS = 10UL * 60UL * 1000UL;
static const uint32_t BROADCAST_PERIOD_MS = 15UL * 1000UL;

enum Action { ACTION_IRRIGATE, ACTION_WAIT, ACTION_SLEEP };

struct NodeState {
    String nodeId;
    int soil = 0;
    int tempC = 0;
    bool needsWater = false;
    uint32_t seq = 0;
    uint32_t ts = 0;
    bool valid = false;
};

NodeState localState;
NodeState peerState;
Action currentAction = ACTION_WAIT;
uint32_t actionSinceMs = 0;
uint32_t lastBroadcastMs = 0;
uint32_t localSeq = 0;

int simulatedSoil = 1500;
int simulatedTemp = 32;

const char *actionToStr(Action action) {
    if (action == ACTION_IRRIGATE) return "IRRIGATE";
    if (action == ACTION_SLEEP) return "SLEEP";
    return "WAIT";
}

void publishConsensus(Action action, const char *reason) {
    String msg = "CONSENSUS|ACT=";
    msg += actionToStr(action);
    msg += "|REASON=";
    msg += reason;
    msg += "|SEQ=";
    msg += String(localSeq);
    msg += "|TS=";
    msg += String(millis());
    Serial.println(msg);
    stmSerial.println(msg);
}

bool extractIntField(const String &line, const String &key, int &valueOut) {
    int keyPos = line.indexOf(key + "=");
    if (keyPos < 0) return false;
    int valueStart = keyPos + key.length() + 1;
    int valueEnd = line.indexOf('|', valueStart);
    if (valueEnd < 0) valueEnd = line.length();
    valueOut = line.substring(valueStart, valueEnd).toInt();
    return true;
}

bool extractStringField(const String &line, const String &key, String &valueOut) {
    int keyPos = line.indexOf(key + "=");
    if (keyPos < 0) return false;
    int valueStart = keyPos + key.length() + 1;
    int valueEnd = line.indexOf('|', valueStart);
    if (valueEnd < 0) valueEnd = line.length();
    valueOut = line.substring(valueStart, valueEnd);
    return true;
}

bool parsePeerState(const String &line, NodeState &out) {
    if (!line.startsWith("STATE|")) return false;
    String nodeId;
    int sm = 0, t = 0, nw = 0, seq = 0, ts = 0;
    if (!extractStringField(line, "NODE", nodeId)) return false;
    if (!extractIntField(line, "SM", sm)) return false;
    if (!extractIntField(line, "T", t)) return false;
    if (!extractIntField(line, "NW", nw)) return false;
    if (!extractIntField(line, "SEQ", seq)) return false;
    if (!extractIntField(line, "TS", ts)) return false;

    out.nodeId = nodeId;
    out.soil = sm;
    out.tempC = t;
    out.needsWater = (nw != 0);
    out.seq = (uint32_t)seq;
    out.ts = (uint32_t)ts;
    out.valid = true;
    return true;
}

void updateLocalState() {
    localState.nodeId = kNodeId;
    localState.soil = simulatedSoil;
    localState.tempC = simulatedTemp;
    localState.needsWater = (localState.soil < SOIL_NEED_WATER_THRESHOLD);
    localState.seq = ++localSeq;
    localState.ts = millis();
    localState.valid = true;
}

void broadcastLocalState() {
    String msg = "STATE|NODE=";
    msg += localState.nodeId;
    msg += "|SM=";
    msg += String(localState.soil);
    msg += "|T=";
    msg += String(localState.tempC);
    msg += "|NW=";
    msg += String(localState.needsWater ? 1 : 0);
    msg += "|SEQ=";
    msg += String(localState.seq);
    msg += "|TS=";
    msg += String(localState.ts);
    Serial.println(msg);
    stmSerial.println(msg);
}

Action evaluateConsensus(const char *&reasonOut) {
    const bool peerFresh = peerState.valid && (millis() - peerState.ts <= PEER_STALE_MS);
    if (!peerFresh) {
        reasonOut = "PEER_STALE_FALLBACK_LOCAL";
        return localState.needsWater ? ACTION_WAIT : ACTION_SLEEP;
    }

    if (localState.needsWater && peerState.needsWater) {
        reasonOut = "BOTH_NEED";
        return ACTION_IRRIGATE;
    }
    if (localState.needsWater || peerState.needsWater) {
        reasonOut = "ONE_NEEDS";
        return ACTION_WAIT;
    }
    reasonOut = "NONE_NEED";
    return ACTION_SLEEP;
}

void applyAction(Action nextAction, const char *reason) {
    const bool changed = (nextAction != currentAction);
    if (changed) actionSinceMs = millis();
    currentAction = nextAction;
    publishConsensus(currentAction, reason);
}

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);

    Serial.begin(115200);
    stmSerial.begin(STM_BAUD);

    delay(300);
    Serial.println();
    Serial.println("[NodeMCU] Distributed consensus irrigation started");
    Serial.println("[NodeMCU] UART on D7 (GPIO13) at 115200");
    Serial.println("[NodeMCU] Send peer STATE packets to negotiate.");
    actionSinceMs = millis();
}

void loop() {
    while (stmSerial.available()) {
        char c = (char)stmSerial.read();

        if (c == '\r') continue;

        if (c == '\n') {
            if (rxLine.length() > 0) {
                NodeState incoming;
                if (parsePeerState(rxLine, incoming)) {
                    peerState = incoming;
                    Serial.print("[Peer] ");
                    Serial.println(rxLine);
                } else {
                    Serial.print("[RX] ");
                    Serial.println(rxLine);
                }
            }
            rxLine = "";
            continue;
        }

        rxLine += c;

        digitalWrite(LED_BUILTIN, LOW);
        delay(1);
        digitalWrite(LED_BUILTIN, HIGH);
    }

    if (millis() - lastBroadcastMs >= BROADCAST_PERIOD_MS) {
        updateLocalState();
        broadcastLocalState();

        const char *reason = "UNKNOWN";
        Action next = evaluateConsensus(reason);

        if (next == ACTION_WAIT) {
            bool gateOpen = (millis() - actionSinceMs) >= WAIT_WINDOW_MS;
            if (strcmp(reason, "ONE_NEEDS") == 0 && !gateOpen) {
                applyAction(ACTION_WAIT, "ONE_NEEDS_WAITING_WINDOW");
            } else {
                applyAction(next, reason);
            }
        } else if (next == ACTION_SLEEP) {
            bool sleepWindowElapsed = (millis() - actionSinceMs) >= SLEEP_WINDOW_MS;
            if (!sleepWindowElapsed) {
                applyAction(ACTION_SLEEP, "NONE_NEED_SLEEP_WINDOW");
            } else {
                applyAction(next, reason);
            }
        } else {
            applyAction(next, reason);
        }

        lastBroadcastMs = millis();
    }
}
