#ifndef DATA_TYPES_H
#define DATA_TYPES_H

#include <Arduino.h>
#include "Config.h"

/**
 * Placeholders — o corte real é só o timer do comando.
 * Política: durationSec==0 → permanente (sem timer/relógio UI);
 *           durationSec>0 → countdown = valor exato do comando.
 * Sem teto fantasma (3600 / 86400): maxDuration não aplica OFF automático.
 */
struct RelayConfig {
    bool autoMode;          // Reservado (legado)
    uint32_t maxDuration;   // Ignorado em runtime (0)
    bool safetyLock;        // Ignorado — não força timer

    bool isValid() const { return true; }
    String getValidationError() const { return ""; }
};

// Identidade no Atlas: só índice. Nomes de produto vivem no Master/UI.
static const char* const RELAY_NAMES[MAX_RELAYS] = {
    "Relé 0", "Relé 1", "Relé 2", "Relé 3",
    "Relé 4", "Relé 5", "Relé 6", "Relé 7"
};

static const RelayConfig RELAY_CONFIGS[MAX_RELAYS] = {
    {false, 0, false}, {false, 0, false}, {false, 0, false}, {false, 0, false},
    {false, 0, false}, {false, 0, false}, {false, 0, false}, {false, 0, false}
};

static_assert(sizeof(RELAY_NAMES)/sizeof(RELAY_NAMES[0]) == MAX_RELAYS,
              "RELAY_NAMES deve ter exatamente MAX_RELAYS elementos");
static_assert(sizeof(RELAY_CONFIGS)/sizeof(RELAY_CONFIGS[0]) == MAX_RELAYS,
              "RELAY_CONFIGS deve ter exatamente MAX_RELAYS elementos");

struct SensorData {
    float environmentTemp = 0.0;
    float environmentHumidity = 0.0;
    float waterTemp = 0.0;
    float ph = 7.0;
    float ec = 0.0;                 // EC em µS/cm (não ppm)
    bool waterLevelOk = false;
    unsigned long timestamp = 0;
    bool valid = false;

    bool isValid() const {
        return environmentTemp >= MIN_TEMP && environmentTemp <= MAX_TEMP &&
               environmentHumidity >= MIN_HUMIDITY && environmentHumidity <= MAX_HUMIDITY &&
               waterTemp >= MIN_TEMP && waterTemp <= MAX_TEMP &&
               ph >= MIN_PH && ph <= MAX_PH &&
               ec >= MIN_EC && ec <= MAX_EC;
    }
};

struct SystemStatus {
    bool wifiConnected = false;
    bool apiConnected = false;
    bool sensorsOk = false;
    bool relaysOk = false;
    unsigned long uptime = 0;
    uint32_t freeHeap = 0;
    int wifiRSSI = 0;
    String lastError = "";

    bool isHealthy() const {
        return wifiConnected && apiConnected && sensorsOk && relaysOk && freeHeap > 10000;
    }
};

struct RelayState {
    bool isOn = false;
    unsigned long startTime = 0;
    int timerSeconds = 0;
    bool hasTimer = false;
    String name = "";
    RelayConfig config;
    bool inCycle = false;
    bool cyclePhaseOn = true;
    uint32_t cycleOnSec = 0;
    uint32_t cycleOffSec = 0;
};

#endif // DATA_TYPES_H
