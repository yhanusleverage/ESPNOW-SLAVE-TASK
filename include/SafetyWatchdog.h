#ifndef SAFETY_WATCHDOG_H
#define SAFETY_WATCHDOG_H

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <functional>
#include "Config.h"

/**
 * @brief Sistema de Watchdog de Segurança para Automação Hidropônica
 *
 * - Hardware Task WDT (reinicia ESP32 se travado)
 * - Heartbeat Master ↔ Slave
 * - Modo seguro imediato (desliga relés)
 * - Recovery escalonado L1-L4
 * - Reboot automático após SafetyMode prolongado
 * - Pulso GPIO opcional para relé watchdog externo
 */
class SafetyWatchdog {
public:
    using SafetyModeCallback = std::function<void()>;
    using RecoveryHandler = std::function<bool(uint8_t level)>;

private:
    unsigned long lastMasterPing = 0;
    unsigned long lastWiFiCheck = 0;
    unsigned long lastHeartbeatSent = 0;
    unsigned long safetyModeActivatedAt = 0;
    unsigned long lastExternalPulse = 0;
    unsigned long lastRecoveryAttempt = 0;

    bool masterOnline = false;
    bool safetyModeActive = false;
    int consecutiveFailures = 0;
    uint8_t recoveryLevel = 0;
    uint8_t lastRecoveryLevelAttempted = 0;

    SafetyModeCallback safetyModeCallback = nullptr;
    RecoveryHandler recoveryHandler = nullptr;

    const unsigned long HEARTBEAT_INTERVAL = 15000;
    const unsigned long MASTER_TIMEOUT = 45000;
    const unsigned long WIFI_CHECK_INTERVAL = 30000;
    const int MAX_CONSECUTIVE_FAILURES = 3;

    static const unsigned long SOFT_RECOVERY_DELAY = 5000;
    static const unsigned long MEDIUM_RECOVERY_DELAY = 20000;
    static const unsigned long HARD_RECOVERY_DELAY = 50000;
    static const unsigned long FULL_RECOVERY_DELAY = 110000;
    static const unsigned long SAFETY_MODE_REBOOT_TIMEOUT = 600000; // 10 min

#if defined(HW_WATCHDOG_ENABLED) && HW_WATCHDOG_ENABLED
    static const unsigned long EXTERNAL_PULSE_INTERVAL = HW_WATCHDOG_PULSE_INTERVAL_MS;
#else
    static const unsigned long EXTERNAL_PULSE_INTERVAL = 5000;
#endif

    bool externalWatchdogLevel = false;

    void pulseExternalWatchdog() {
#if defined(HW_WATCHDOG_ENABLED) && HW_WATCHDOG_ENABLED
        if (millis() - lastExternalPulse >= EXTERNAL_PULSE_INTERVAL) {
            externalWatchdogLevel = !externalWatchdogLevel;
            digitalWrite(HW_WATCHDOG_GPIO, externalWatchdogLevel ? HIGH : LOW);
            lastExternalPulse = millis();
        }
#endif
    }

    void resetRecoveryState() {
        recoveryLevel = 0;
        lastRecoveryLevelAttempted = 0;
        lastRecoveryAttempt = 0;
        safetyModeActivatedAt = 0;
    }

public:
    void begin() {
        esp_task_wdt_init(60, true);
        esp_task_wdt_add(NULL);

#if defined(HW_WATCHDOG_ENABLED) && HW_WATCHDOG_ENABLED
        pinMode(HW_WATCHDOG_GPIO, OUTPUT);
        digitalWrite(HW_WATCHDOG_GPIO, LOW);
        Serial.println("   HW Watchdog GPIO: " + String(HW_WATCHDOG_GPIO));
#endif

        lastMasterPing = millis();
        lastWiFiCheck = millis();
        lastHeartbeatSent = millis();

        Serial.println("✅ SafetyWatchdog inicializado");
        Serial.println("   Heartbeat: " + String(HEARTBEAT_INTERVAL / 1000) + "s");
        Serial.println("   Timeout Master: " + String(MASTER_TIMEOUT / 1000) + "s");
        Serial.println("   Hardware WDT: 60s");
        Serial.println("   SafetyMode reboot: " + String(SAFETY_MODE_REBOOT_TIMEOUT / 60000) + " min");
    }

    void setSafetyModeCallback(SafetyModeCallback callback) {
        safetyModeCallback = callback;
    }

    void setRecoveryHandler(RecoveryHandler handler) {
        recoveryHandler = handler;
    }

    void feed() {
        esp_task_wdt_reset();
        pulseExternalWatchdog();
    }

    void onMasterResponse() {
        lastMasterPing = millis();
        consecutiveFailures = 0;

        if (!masterOnline) {
            Serial.println("✅ Master reconectado!");
            masterOnline = true;
        }

        if (safetyModeActive) {
            Serial.println("✅ Saindo do modo seguro");
            safetyModeActive = false;
            resetRecoveryState();
        }
    }

    bool checkMasterHealth() {
        unsigned long timeSinceLastPing = millis() - lastMasterPing;

        if (timeSinceLastPing > MASTER_TIMEOUT) {
            if (masterOnline) {
                consecutiveFailures++;
                Serial.println("⚠️ MASTER NÃO RESPONDE! (" + String(consecutiveFailures) + "/" +
                               String(MAX_CONSECUTIVE_FAILURES) + ")");
                Serial.println("   Tempo sem resposta: " + String(timeSinceLastPing / 1000) + "s");

                if (consecutiveFailures >= MAX_CONSECUTIVE_FAILURES) {
                    Serial.println("🚨 MASTER OFFLINE CONFIRMADO!");
                    masterOnline = false;
                    activateSafetyMode();
                }
            }
            return false;
        }

        return true;
    }

    void activateSafetyMode() {
        if (!safetyModeActive) {
            safetyModeActive = true;
            safetyModeActivatedAt = millis();
            recoveryLevel = 0;
            lastRecoveryLevelAttempted = 0;
            lastRecoveryAttempt = 0;

            Serial.println("\n🚨 =============================");
            Serial.println("🚨 MODO SEGURO ATIVADO");
            Serial.println("🚨 =============================");
            Serial.println("   Master offline detectado");
            Serial.println("   Relés desligados imediatamente");
            Serial.println("   Recovery automático iniciado");
            Serial.println("=============================\n");

            if (safetyModeCallback) {
                safetyModeCallback();
            }
        }
    }

    void updateRecovery() {
        if (!safetyModeActive) return;

        unsigned long sinceSafety = millis() - safetyModeActivatedAt;

        if (sinceSafety >= SAFETY_MODE_REBOOT_TIMEOUT) {
            Serial.println("🔄 SafetyMode > 10 min — reiniciando sistema...");
            forceReset();
            return;
        }

        if (!recoveryHandler) return;

        uint8_t targetLevel = 0;
        if (sinceSafety >= FULL_RECOVERY_DELAY) targetLevel = 4;
        else if (sinceSafety >= HARD_RECOVERY_DELAY) targetLevel = 3;
        else if (sinceSafety >= MEDIUM_RECOVERY_DELAY) targetLevel = 2;
        else if (sinceSafety >= SOFT_RECOVERY_DELAY) targetLevel = 1;

        if (targetLevel == 0 || targetLevel <= lastRecoveryLevelAttempted) return;

        if (millis() - lastRecoveryAttempt < 3000) return;

        lastRecoveryAttempt = millis();
        lastRecoveryLevelAttempted = targetLevel;
        recoveryLevel = targetLevel;

        Serial.printf("🔄 Recovery L%d (SafetyMode há %lus)...\n", targetLevel, sinceSafety / 1000);

        if (recoveryHandler(targetLevel)) {
            Serial.printf("✅ Recovery L%d bem-sucedido\n", targetLevel);
            if (targetLevel >= 4) return;
            recoveryLevel = 0;
            lastRecoveryLevelAttempted = 0;
        }
    }

    bool isSafetyMode() const { return safetyModeActive; }
    bool isMasterOnline() const { return masterOnline; }
    uint8_t getRecoveryLevel() const { return recoveryLevel; }
    unsigned long getSafetyModeDuration() const {
        return safetyModeActive ? (millis() - safetyModeActivatedAt) : 0;
    }

    bool shouldSendHeartbeat() {
        if (millis() - lastHeartbeatSent > HEARTBEAT_INTERVAL) {
            lastHeartbeatSent = millis();
            return true;
        }
        return false;
    }

    bool shouldCheckWiFi() {
        if (millis() - lastWiFiCheck > WIFI_CHECK_INTERVAL) {
            lastWiFiCheck = millis();
            return true;
        }
        return false;
    }

    unsigned long getTimeSinceLastResponse() const {
        return millis() - lastMasterPing;
    }

    void forceReset() {
        Serial.println("🔄 Forçando reset do sistema...");
        delay(100);
        esp_restart();
    }

    void printStatus() {
        Serial.println("\n🛡️ === STATUS SAFETY WATCHDOG ===");
        Serial.println("   Master: " + String(masterOnline ? "🟢 Online" : "🔴 Offline"));
        Serial.println("   Modo Seguro: " + String(safetyModeActive ? "🔴 ATIVO" : "🟢 Normal"));
        if (safetyModeActive) {
            Serial.println("   SafetyMode há: " + String(getSafetyModeDuration() / 1000) + "s");
        }
        Serial.println("   Última resposta: " + String(getTimeSinceLastResponse() / 1000) + "s atrás");
        Serial.println("   Falhas consecutivas: " + String(consecutiveFailures) + "/" + String(MAX_CONSECUTIVE_FAILURES));
        Serial.println("   Recovery level: " + String(recoveryLevel));
        Serial.println("   Uptime: " + String(millis() / 1000) + "s");
        Serial.println("   Heap livre: " + String(ESP.getFreeHeap()) + " bytes");
        Serial.println("==================================\n");
    }

    void reset() {
        lastMasterPing = millis();
        consecutiveFailures = 0;
        masterOnline = true;
        safetyModeActive = false;
        resetRecoveryState();
        Serial.println("✅ SafetyWatchdog resetado");
    }
};

#endif // SAFETY_WATCHDOG_H
