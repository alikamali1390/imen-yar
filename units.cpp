// Unit ESP32 device: reads MQ135 sensor, activates local siren/buzzer,
// and sends alert/clear events to the central device over a Bluetooth-mesh
// (BLE advertising flood-relay) network instead of WiFi/HTTP.
//
// No WiFi credentials or IP address needed on this board any more — it only
// needs to be within mesh range of the central unit or of any other unit
// that is (each unit also relays for the others, extending range).

#include <Arduino.h>
#include "mesh_common.h"
#include "mesh_node.h"

// ---- This unit's identity on the mesh (1, 2, 3, ...) ----
// Overridable at build time via `-D UNIT_ID=2` (see platformio.ini envs).
#ifndef UNIT_ID
#define UNIT_ID 1
#endif

// Pins
const int MQ135_PIN = 34; // sensor analog pin
const int SIREN_PIN = 25; // siren
const int BUZZER_PIN = 26; // buzzer

// Gas detection threshold
const int GAS_THRESHOLD = 1500; // set this after sensor calibration

// Repeat-send control
bool alertSent = false;
bool currentlyAlerting = false;
unsigned long lastSendTime = 0;
const unsigned long SEND_COOLDOWN = 15000; // once every 15s while in alert state

MeshNode mesh;

// This unit doesn't need to react to packets from others, but the mesh
// callback still fires for anything it relays — used here just for logging.
void onMeshPacket(const MeshPacket &pkt, bool isRelay) {
  Serial.printf("[mesh] relaying msg from unit %u type=%u ttl=%u\n",
                pkt.srcUnit, pkt.msgType, pkt.ttl);
}

void setup() {
  Serial.begin(115200);
  pinMode(SIREN_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(SIREN_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  randomSeed(esp_random());
  mesh.begin(UNIT_ID, onMeshPacket);
  Serial.println("Unit BLE-mesh node started.");
}

void loop() {
  int gasValue = analogRead(MQ135_PIN);
  Serial.print("Gas sensor value: ");
  Serial.println(gasValue);

  if (gasValue > GAS_THRESHOLD) {
    // activate local siren and buzzer
    digitalWrite(SIREN_PIN, HIGH);
    digitalWrite(BUZZER_PIN, HIGH);
    currentlyAlerting = true;

    unsigned long now = millis();
    if (!alertSent || (now - lastSendTime > SEND_COOLDOWN)) {
      mesh.send(MESH_MSG_GAS_ALERT);
      Serial.println("Broadcast gas-alert over BLE mesh.");
      alertSent = true;
      lastSendTime = now;
    }
  } else {
    // return to normal state
    digitalWrite(SIREN_PIN, LOW);
    digitalWrite(BUZZER_PIN, LOW);
    if (currentlyAlerting) {
      mesh.send(MESH_MSG_GAS_CLEAR);
      Serial.println("Broadcast gas-clear over BLE mesh.");
      currentlyAlerting = false;
    }
    alertSent = false;
  }

  delay(1000); // read every 1 second
}
