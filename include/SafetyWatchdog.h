#ifndef SAFETY_WATCHDOG_H
#define SAFETY_WATCHDOG_H

#include <Arduino.h>
#include <esp_task_wdt.h>

/**
 * @brief Sistema de Watchdog de Segurança para Automação Hidropônica
 * 
 * Características:
 * - Hardware Watchdog (reinicia ESP32 se travado)
 * - Heartbeat bidirecional Master ↔ Slave
 * - Modo seguro automático (desliga bombas se Master offline)
 * - Monitoramento de WiFi
 * - Simples e eficaz para aplicações críticas
 */
class SafetyWatchdog {
private:
    unsigned long lastMasterPing = 0;
    unsigned long lastWiFiCheck = 0;
    unsigned long lastHeartbeatSent = 0;
    bool masterOnline = false;
    bool safetyModeActive = false;
    int consecutiveFailures = 0;
    
    // Configuração conservadora para hidroponia
    const unsigned long HEARTBEAT_INTERVAL = 15000;  // Enviar ping a cada 15s
    const unsigned long MASTER_TIMEOUT = 45000;      // Master offline após 45s
    const unsigned long WIFI_CHECK_INTERVAL = 30000; // Verificar WiFi a cada 30s
    const unsigned long RECONNECT_COOLDOWN = 10000;  // Tentar reconectar a cada 10s
    const int MAX_CONSECUTIVE_FAILURES = 3;          // Máximo de falhas antes de modo seguro
    
public:
    /**
     * @brief Inicializa o watchdog de segurança
     */
    void begin() {
        // Inicializar Hardware Watchdog do ESP32 (60 segundos timeout)
        esp_task_wdt_init(60, true);
        esp_task_wdt_add(NULL);
        
        lastMasterPing = millis();
        lastWiFiCheck = millis();
        lastHeartbeatSent = millis();
        
        Serial.println("✅ SafetyWatchdog inicializado");
        Serial.println("   Heartbeat: " + String(HEARTBEAT_INTERVAL/1000) + "s");
        Serial.println("   Timeout Master: " + String(MASTER_TIMEOUT/1000) + "s");
        Serial.println("   Hardware WDT: 60s");
    }
    
    /**
     * @brief Alimenta o watchdog de hardware (DEVE ser chamado no loop)
     */
    void feed() {
        esp_task_wdt_reset();
    }
    
    /**
     * @brief Registra resposta do Master (chamar quando receber PONG)
     */
    void onMasterResponse() {
        lastMasterPing = millis();
        consecutiveFailures = 0;
        
        if (!masterOnline) {
            Serial.println("✅ Master reconectado!");
            masterOnline = true;
        }
        
        // Sair do modo seguro se estava ativo
        if (safetyModeActive) {
            Serial.println("✅ Saindo do modo seguro");
            safetyModeActive = false;
        }
    }
    
    /**
     * @brief Verifica saúde do Master
     * @return true se Master está online
     */
    bool checkMasterHealth() {
        unsigned long timeSinceLastPing = millis() - lastMasterPing;
        
        if (timeSinceLastPing > MASTER_TIMEOUT) {
            if (masterOnline) {
                consecutiveFailures++;
                Serial.println("⚠️ MASTER NÃO RESPONDE! (" + String(consecutiveFailures) + "/" + 
                              String(MAX_CONSECUTIVE_FAILURES) + ")");
                Serial.println("   Tempo sem resposta: " + String(timeSinceLastPing/1000) + "s");
                
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
    
    /**
     * @brief Ativa modo de segurança (CRÍTICO para hidroponia)
     */
    void activateSafetyMode() {
        if (!safetyModeActive) {
            safetyModeActive = true;
            Serial.println("\n🚨 =============================");
            Serial.println("🚨 MODO SEGURO ATIVADO");
            Serial.println("🚨 =============================");
            Serial.println("   Master offline detectado");
            Serial.println("   Bombas serão desligadas por segurança");
            Serial.println("   Sistema aguardando reconexão...");
            Serial.println("=============================\n");
        }
    }
    
    /**
     * @brief Verifica se está em modo seguro
     */
    bool isSafetyMode() {
        return safetyModeActive;
    }
    
    /**
     * @brief Verifica se Master está online
     */
    bool isMasterOnline() {
        return masterOnline;
    }
    
    /**
     * @brief Verifica se deve enviar heartbeat
     */
    bool shouldSendHeartbeat() {
        if (millis() - lastHeartbeatSent > HEARTBEAT_INTERVAL) {
            lastHeartbeatSent = millis();
            return true;
        }
        return false;
    }
    
    /**
     * @brief Verifica se deve checar WiFi
     */
    bool shouldCheckWiFi() {
        if (millis() - lastWiFiCheck > WIFI_CHECK_INTERVAL) {
            lastWiFiCheck = millis();
            return true;
        }
        return false;
    }
    
    /**
     * @brief Obtém tempo desde última resposta do Master
     */
    unsigned long getTimeSinceLastResponse() {
        return millis() - lastMasterPing;
    }
    
    /**
     * @brief Força reset do watchdog (usar com cuidado)
     */
    void forceReset() {
        Serial.println("🔄 Forçando reset do sistema...");
        delay(100);
        esp_restart();
    }
    
    /**
     * @brief Imprime status do watchdog
     */
    void printStatus() {
        Serial.println("\n🛡️ === STATUS SAFETY WATCHDOG ===");
        Serial.println("   Master: " + String(masterOnline ? "🟢 Online" : "🔴 Offline"));
        Serial.println("   Modo Seguro: " + String(safetyModeActive ? "🔴 ATIVO" : "🟢 Normal"));
        Serial.println("   Última resposta: " + String((millis() - lastMasterPing)/1000) + "s atrás");
        Serial.println("   Falhas consecutivas: " + String(consecutiveFailures) + "/" + String(MAX_CONSECUTIVE_FAILURES));
        Serial.println("   Uptime: " + String(millis()/1000) + "s");
        Serial.println("   Heap livre: " + String(ESP.getFreeHeap()) + " bytes");
        Serial.println("==================================\n");
    }
    
    /**
     * @brief Reseta contadores (útil após reconexão manual)
     */
    void reset() {
        lastMasterPing = millis();
        consecutiveFailures = 0;
        masterOnline = true;
        safetyModeActive = false;
        Serial.println("✅ SafetyWatchdog resetado");
    }
};

#endif // SAFETY_WATCHDOG_H

