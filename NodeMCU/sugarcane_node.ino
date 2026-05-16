#include <SoftwareSerial.h>

/**
 * NodeMCU UART Monitor via D7 (GPIO13)
 *
 * Wiring:
 * STM32 TX -> NodeMCU D7 (GPIO13)
 * STM32 GND -> NodeMCU GND
 *
 * Prints the full line exactly as received from STM32, e.g.:
 * 303=[TEMP:24,HUM:53,W1:1190,W2:606] 401[T=24,H=54,S1=0,S2=770]
 */

static const uint32_t STM_BAUD = 115200;

/* SoftwareSerial(rxPin, txPin). TX is unused, set to -1. */
SoftwareSerial stmSerial(D7, -1);
String rxLine;

void setup() {
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);

    Serial.begin(115200);
    stmSerial.begin(STM_BAUD);

    delay(300);
    Serial.println();
    Serial.println("[NodeMCU] Raw terminal monitor started");
    Serial.println("[NodeMCU] Listening on D7 (GPIO13) at 115200...");
}

void loop() {
    while (stmSerial.available()) {
        char c = (char)stmSerial.read();

        if (c == '\r') continue;

        if (c == '\n') {
            if (rxLine.length() > 0) Serial.println(rxLine);
            rxLine = "";
            continue;
        }

        rxLine += c;

        digitalWrite(LED_BUILTIN, LOW);
        delay(1);
        digitalWrite(LED_BUILTIN, HIGH);
    }
}
