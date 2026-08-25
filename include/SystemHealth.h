#ifndef SYSTEM_HEALTH_H
#define SYSTEM_HEALTH_H

#include <Arduino.h>
#include <esp_system.h>
#include <Preferences.h>

/**
 * @brief Diagnóstico de boot e saúde do sistema (reset reason, NVS counters)
 */
class SystemHealth {
public:
    static String resetReasonToString(esp_reset_reason_t reason) {
        switch (reason) {
            case ESP_RST_POWERON:   return "POWERON";
            case ESP_RST_EXT:       return "EXT";
            case ESP_RST_SW:        return "SW";
            case ESP_RST_PANIC:     return "PANIC";
            case ESP_RST_INT_WDT:   return "INT_WDT";
            case ESP_RST_TASK_WDT:  return "TASK_WDT";
            case ESP_RST_WDT:       return "WDT";
            case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
            case ESP_RST_BROWNOUT:  return "BROWNOUT";
            case ESP_RST_SDIO:      return "SDIO";
            default:                return "UNKNOWN";
        }
    }

    static uint32_t getWdtResetCount() {
        Preferences prefs;
        if (!prefs.begin("sys_health", true)) return 0;
        uint32_t count = prefs.getUInt("wdt_resets", 0);
        prefs.end();
        return count;
    }

    static void incrementWdtResetCount() {
        Preferences prefs;
        if (!prefs.begin("sys_health", false)) return;
        uint32_t count = prefs.getUInt("wdt_resets", 0) + 1;
        prefs.putUInt("wdt_resets", count);
        prefs.end();
    }

    static void logBootInfo() {
        esp_reset_reason_t reason = esp_reset_reason();
        Serial.println("\n🏥 === SYSTEM HEALTH (BOOT) ===");
        Serial.println("   Reset reason: " + resetReasonToString(reason));
        Serial.println("   Free heap: " + String(ESP.getFreeHeap()) + " bytes");
        Serial.println("   Min free heap: " + String(ESP.getMinFreeHeap()) + " bytes");

        if (reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT || reason == ESP_RST_INT_WDT) {
            incrementWdtResetCount();
        }
        Serial.println("   WDT reset count (NVS): " + String(getWdtResetCount()));
        Serial.println("================================\n");
    }

    static void printStatus(unsigned long uptimeMs, bool safetyMode, bool masterOnline,
                            unsigned long lastResponseSec, uint32_t freeHeap,
                            uint8_t recoveryLevel) {
        Serial.println("\n🏥 === SYSTEM HEALTH ===");
        Serial.println("   Reset reason: " + resetReasonToString(esp_reset_reason()));
        Serial.println("   WDT resets (NVS): " + String(getWdtResetCount()));
        Serial.println("   Uptime: " + String(uptimeMs / 1000) + "s");
        Serial.println("   Free heap: " + String(freeHeap) + " bytes");
        Serial.println("   Min free heap: " + String(ESP.getMinFreeHeap()) + " bytes");
        Serial.println("   Master: " + String(masterOnline ? "Online" : "Offline"));
        Serial.println("   SafetyMode: " + String(safetyMode ? "ATIVO" : "Normal"));
        Serial.println("   Última resposta Master: " + String(lastResponseSec) + "s atrás");
        Serial.println("   Recovery level: " + String(recoveryLevel));
        Serial.println("==========================\n");
    }
};

#endif // SYSTEM_HEALTH_H
