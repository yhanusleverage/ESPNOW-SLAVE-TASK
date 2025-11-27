/**
 * @file AutoCommunicationManager.h
 * @brief Sistema de Comunicação Automática, Persistente e Inteligente
 * @version 1.0
 * @date 2025-10-28
 * 
 * Gerencia automaticamente:
 * - Descoberta de dispositivos
 * - Sincronização de canal
 * - Heartbeat bidirecional
 * - Reconexão automática
 * - Monitoramento de saúde
 */

#ifndef AUTO_COMMUNICATION_MANAGER_H
#define AUTO_COMMUNICATION_MANAGER_H

#include <Arduino.h>
#include <vector>
#include <Preferences.h>
#include "ESPNowController.h"
#include "WiFiCredentialsManager.h"

// ===== CONFIGURAÇÕES DE TIMING =====
#define AUTO_DISCOVERY_INTERVAL 30000        // Discovery a cada 30s
#define CREDENTIAL_BROADCAST_INTERVAL 60000  // Broadcast credenciais a cada 60s
#define CREDENTIAL_RETRY_INTERVAL 30000      // Retry a cada 30s
#define CREDENTIAL_MAX_RETRIES 3             // Máximo 3 tentativas

#define MASTER_HEARTBEAT_INTERVAL 15000      // PING a cada 15s
#define MASTER_HEARTBEAT_TIMEOUT 45000       // Timeout após 45s (3 heartbeats)
#define HEALTH_CHECK_INTERVAL 10000          // Verificar saúde a cada 10s

#define SOFT_RECOVERY_TIMEOUT 5000           // Recovery nível 1: 5s
#define MEDIUM_RECOVERY_TIMEOUT 15000        // Recovery nível 2: 15s
#define HARD_RECOVERY_TIMEOUT 30000          // Recovery nível 3: 30s
#define FULL_RECOVERY_TIMEOUT 60000          // Recovery nível 4: 60s

// ===== THRESHOLDS =====
#define RSSI_GOOD_THRESHOLD -60              // RSSI bom
#define RSSI_FAIR_THRESHOLD -70              // RSSI razoável
#define RSSI_POOR_THRESHOLD -80              // RSSI ruim
#define RSSI_CRITICAL_THRESHOLD -90          // RSSI crítico

#define PACKET_LOSS_WARNING 0.05             // 5% perda
#define PACKET_LOSS_CRITICAL 0.10            // 10% perda

#define HEALTH_SCORE_EXCELLENT 90            // Excelente
#define HEALTH_SCORE_GOOD 70                 // Bom
#define HEALTH_SCORE_FAIR 50                 // Razoável
#define HEALTH_SCORE_POOR 30                 // Ruim

/**
 * @brief Estados da máquina de estados
 */
enum class CommState {
    INIT,                    // Inicializando
    WIFI_CONNECTING,         // Conectando WiFi
    ESPNOW_INIT,            // Inicializando ESP-NOW
    WAITING_SLAVES,          // Aguardando Slaves (Master)
    WAITING_CREDENTIALS,     // Aguardando credenciais (Slave)
    CREDENTIALS_BROADCAST,   // Enviando credenciais (Master)
    CHANNEL_SYNC,           // Sincronizando canal
    DISCOVERY_ACTIVE,        // Discovery ativo
    DISCOVERY_RESPONSE,      // Respondendo discovery (Slave)
    CONNECTED,              // Conectado e operacional
    MONITORING,             // Monitorando conexão
    SOFT_RECOVERY,          // Recovery nível 1
    MEDIUM_RECOVERY,        // Recovery nível 2
    HARD_RECOVERY,          // Recovery nível 3
    FULL_RECOVERY           // Recovery nível 4
};

/**
 * @brief Métricas de conexão
 */
struct ConnectionMetrics {
    // Timing
    unsigned long lastPingSent = 0;
    unsigned long lastPongReceived = 0;
    unsigned long connectionEstablished = 0;
    unsigned long connectionUptime = 0;
    
    // Quality
    int8_t rssi = -100;
    float packetLossRate = 0.0;
    uint32_t avgLatency = 0;
    
    // Counters
    uint32_t messagesSent = 0;
    uint32_t messagesReceived = 0;
    uint32_t messagesLost = 0;
    uint32_t recoveryAttempts = 0;
    uint32_t successfulRecoveries = 0;
    
    // Status
    bool isHealthy = false;
    uint8_t healthScore = 0;
    String lastError = "";
    CommState currentState = CommState::INIT;
    
    /**
     * @brief Calcula health score baseado nas métricas
     */
    void calculateHealthScore() {
        healthScore = 100;
        
        // RSSI Impact (30%)
        if (rssi > RSSI_GOOD_THRESHOLD) {
            healthScore -= 0;
        } else if (rssi > RSSI_FAIR_THRESHOLD) {
            healthScore -= 10;
        } else if (rssi > RSSI_POOR_THRESHOLD) {
            healthScore -= 20;
        } else {
            healthScore -= 30;
        }
        
        // Packet Loss Impact (30%)
        healthScore -= (packetLossRate * 100) * 0.3;
        
        // Latency Impact (20%)
        if (avgLatency < 50) {
            healthScore -= 0;
        } else if (avgLatency < 100) {
            healthScore -= 10;
        } else {
            healthScore -= 20;
        }
        
        // Heartbeat Impact (20%)
        unsigned long timeSinceLastPong = millis() - lastPongReceived;
        if (timeSinceLastPong < 20000) {
            healthScore -= 0;
        } else if (timeSinceLastPong < 35000) {
            healthScore -= 10;
        } else {
            healthScore -= 20;
        }
        
        healthScore = constrain(healthScore, 0, 100);
        isHealthy = (healthScore >= HEALTH_SCORE_FAIR);
    }
    
    /**
     * @brief Atualiza taxa de perda de pacotes
     */
    void updatePacketLoss() {
        uint32_t totalMessages = messagesSent + messagesReceived;
        if (totalMessages > 0) {
            packetLossRate = (float)messagesLost / (float)totalMessages;
        }
    }
    
    /**
     * @brief Reseta métricas
     */
    void reset() {
        messagesSent = 0;
        messagesReceived = 0;
        messagesLost = 0;
        packetLossRate = 0.0;
        avgLatency = 0;
    }
};

/**
 * @brief Gerenciador de Comunicação Automática
 */
class AutoCommunicationManager {
private:
    ESPNowController* espNowController;
    WiFiCredentialsManager* wifiManager;
    Preferences prefs;
    
    // Estado
    CommState currentState;
    bool isMaster;
    bool autoMode;
    
    // Timing
    unsigned long lastDiscovery;
    unsigned long lastCredentialBroadcast;
    unsigned long lastHeartbeat;
    unsigned long lastHealthCheck;
    unsigned long recoveryStartTime;
    
    // Counters
    uint8_t credentialRetries;
    uint8_t recoveryLevel;
    
    // Métricas por dispositivo
    std::vector<ConnectionMetrics> deviceMetrics;
    
    // Callbacks
    void (*onStateChangeCallback)(CommState oldState, CommState newState) = nullptr;
    void (*onDeviceDiscoveredCallback)(const uint8_t* mac, const String& name) = nullptr;
    void (*onConnectionLostCallback)(const uint8_t* mac) = nullptr;
    void (*onRecoverySuccessCallback)(uint8_t level) = nullptr;
    
public:
    /**
     * @brief Construtor
     */
    AutoCommunicationManager(ESPNowController* controller, WiFiCredentialsManager* wifi, bool master = true) 
        : espNowController(controller), wifiManager(wifi), isMaster(master), autoMode(true),
          currentState(CommState::INIT), lastDiscovery(0), lastCredentialBroadcast(0),
          lastHeartbeat(0), lastHealthCheck(0), recoveryStartTime(0),
          credentialRetries(0), recoveryLevel(0) {}
    
    /**
     * @brief Inicializa sistema automático
     */
    bool begin() {
        Serial.println("\n🤖 ==========================================");
        Serial.println("🤖 SISTEMA DE COMUNICAÇÃO AUTOMÁTICA");
        Serial.println("🤖 ==========================================");
        Serial.println("📡 Modo: " + String(isMaster ? "MASTER" : "SLAVE"));
        Serial.println("🔧 Auto-Discovery: ATIVADO");
        Serial.println("🔧 Auto-Recovery: ATIVADO");
        Serial.println("🔧 Heartbeat: ATIVADO");
        Serial.println("==========================================\n");
        
        changeState(CommState::INIT);
        return true;
    }
    
    /**
     * @brief Loop principal - chamar continuamente
     */
    void update() {
        if (!autoMode) return;
        
        unsigned long now = millis();
        
        // Executar lógica baseada no estado atual
        switch (currentState) {
            case CommState::INIT:
                handleInit();
                break;
                
            case CommState::WIFI_CONNECTING:
                handleWiFiConnecting();
                break;
                
            case CommState::ESPNOW_INIT:
                handleESPNowInit();
                break;
                
            case CommState::WAITING_SLAVES:
                handleWaitingSlaves(now);
                break;
                
            case CommState::WAITING_CREDENTIALS:
                handleWaitingCredentials();
                break;
                
            case CommState::CREDENTIALS_BROADCAST:
                handleCredentialsBroadcast(now);
                break;
                
            case CommState::CHANNEL_SYNC:
                handleChannelSync();
                break;
                
            case CommState::DISCOVERY_ACTIVE:
                handleDiscoveryActive(now);
                break;
                
            case CommState::CONNECTED:
            case CommState::MONITORING:
                handleMonitoring(now);
                break;
                
            case CommState::SOFT_RECOVERY:
            case CommState::MEDIUM_RECOVERY:
            case CommState::HARD_RECOVERY:
            case CommState::FULL_RECOVERY:
                handleRecovery(now);
                break;
        }
        
        // Verificação de saúde periódica
        if (now - lastHealthCheck > HEALTH_CHECK_INTERVAL) {
            performHealthCheck();
            lastHealthCheck = now;
        }
    }
    
    /**
     * @brief Ativa/desativa modo automático
     */
    void setAutoMode(bool enabled) {
        autoMode = enabled;
        Serial.println("🤖 Modo automático: " + String(enabled ? "ATIVADO" : "DESATIVADO"));
    }
    
    /**
     * @brief Obtém estado atual
     */
    CommState getState() const {
        return currentState;
    }
    
    /**
     * @brief Obtém métricas de um dispositivo
     */
    ConnectionMetrics* getDeviceMetrics(const uint8_t* mac) {
        for (auto& metrics : deviceMetrics) {
            // Comparar MAC (precisa implementar)
            return &metrics;
        }
        return nullptr;
    }
    
    /**
     * @brief Força discovery imediato
     */
    void forceDiscovery() {
        if (isMaster) {
            Serial.println("🔍 Forçando discovery...");
            espNowController->sendDiscoveryBroadcast();
            lastDiscovery = millis();
        }
    }
    
    /**
     * @brief Força envio de credenciais
     */
    void forceCredentialBroadcast() {
        if (isMaster && WiFi.isConnected()) {
            Serial.println("📡 Forçando broadcast de credenciais...");
            String ssid = WiFi.SSID();
            // Precisaria da senha salva
            lastCredentialBroadcast = millis();
        }
    }
    
    /**
     * @brief Imprime status detalhado
     */
    void printStatus() {
        Serial.println("\n╔════════════════════════════════════════════════════════════╗");
        Serial.println("║  ESP-NOW COMUNICAÇÃO AUTOMÁTICA - STATUS                  ║");
        Serial.println("╠════════════════════════════════════════════════════════════╣");
        Serial.printf("║  Modo: %-20s Canal: %-5d               ║\n", 
                     isMaster ? "MASTER" : "SLAVE", espNowController ? espNowController->getPeerCount() : 0);
        Serial.printf("║  WiFi: %-20s RSSI: %-6d dBm          ║\n",
                     WiFi.isConnected() ? "✅ CONECTADO" : "❌ DESCONECTADO", WiFi.RSSI());
        Serial.printf("║  ESP-NOW: %-17s Peers: %-4d              ║\n",
                     espNowController && espNowController->isInitialized() ? "✅ ATIVO" : "❌ INATIVO",
                     espNowController ? espNowController->getPeerCount() : 0);
        Serial.println("╠════════════════════════════════════════════════════════════╣");
        Serial.printf("║  Estado: %-46s║\n", getStateName(currentState).c_str());
        Serial.printf("║  Modo Auto: %-43s║\n", autoMode ? "✅ ATIVADO" : "❌ DESATIVADO");
        Serial.println("╠════════════════════════════════════════════════════════════╣");
        Serial.printf("║  Uptime: %-47s║\n", formatUptime(millis()).c_str());
        Serial.println("╚════════════════════════════════════════════════════════════╝\n");
    }
    
    /**
     * @brief Define callback para mudança de estado
     */
    void onStateChange(void (*callback)(CommState, CommState)) {
        onStateChangeCallback = callback;
    }
    
    /**
     * @brief Define callback para dispositivo descoberto
     */
    void onDeviceDiscovered(void (*callback)(const uint8_t*, const String&)) {
        onDeviceDiscoveredCallback = callback;
    }
    
    /**
     * @brief Define callback para conexão perdida
     */
    void onConnectionLost(void (*callback)(const uint8_t*)) {
        onConnectionLostCallback = callback;
    }
    
    /**
     * @brief Define callback para recovery bem-sucedido
     */
    void onRecoverySuccess(void (*callback)(uint8_t)) {
        onRecoverySuccessCallback = callback;
    }

private:
    // ===== HANDLERS DE ESTADO =====
    
    void handleInit() {
        Serial.println("🔄 Inicializando sistema automático...");
        if (WiFi.isConnected()) {
            changeState(CommState::ESPNOW_INIT);
        } else {
            changeState(CommState::WIFI_CONNECTING);
        }
    }
    
    void handleWiFiConnecting() {
        if (WiFi.isConnected()) {
            Serial.println("✅ WiFi conectado");
            changeState(CommState::ESPNOW_INIT);
        }
    }
    
    void handleESPNowInit() {
        if (espNowController && espNowController->isInitialized()) {
            Serial.println("✅ ESP-NOW inicializado");
            if (isMaster) {
                changeState(CommState::WAITING_SLAVES);
            } else {
                if (wifiManager->hasCredentials()) {
                    changeState(CommState::DISCOVERY_ACTIVE);
                } else {
                    changeState(CommState::WAITING_CREDENTIALS);
                }
            }
        }
    }
    
    void handleWaitingSlaves(unsigned long now) {
        // Master aguardando Slaves aparecerem
        if (now - lastDiscovery > AUTO_DISCOVERY_INTERVAL) {
            Serial.println("🔍 Auto-discovery: Procurando Slaves...");
            espNowController->sendDiscoveryBroadcast();
            lastDiscovery = now;
        }
        
        // Se tem Slaves, mudar para connected
        if (espNowController->getPeerCount() > 0) {
            changeState(CommState::CONNECTED);
        }
        
        // Se não tem Slaves há muito tempo, tentar broadcast credenciais
        if (now - lastCredentialBroadcast > CREDENTIAL_BROADCAST_INTERVAL) {
            changeState(CommState::CREDENTIALS_BROADCAST);
        }
    }
    
    void handleWaitingCredentials() {
        // Slave aguardando credenciais WiFi do Master
        Serial.println("⏳ Aguardando credenciais WiFi do Master...");
        delay(5000); // Aguardar 5s antes de verificar novamente
    }
    
    void handleCredentialsBroadcast(unsigned long now) {
        if (credentialRetries >= CREDENTIAL_MAX_RETRIES) {
            Serial.println("⚠️ Máximo de tentativas de broadcast atingido");
            changeState(CommState::WAITING_SLAVES);
            credentialRetries = 0;
            return;
        }
        
        if (now - lastCredentialBroadcast > CREDENTIAL_RETRY_INTERVAL) {
            Serial.println("📡 Enviando credenciais WiFi em broadcast...");
            // Implementar envio de credenciais
            lastCredentialBroadcast = now;
            credentialRetries++;
        }
    }
    
    void handleChannelSync() {
        Serial.println("🔄 Sincronizando canal...");
        // Implementar sincronização
        changeState(CommState::DISCOVERY_ACTIVE);
    }
    
    void handleDiscoveryActive(unsigned long now) {
        if (now - lastDiscovery > AUTO_DISCOVERY_INTERVAL) {
            espNowController->sendDiscoveryBroadcast();
            lastDiscovery = now;
        }
        
        // ✅ CORREÇÃO BUG #3: Lógica diferente para MASTER e SLAVE
        if (isMaster) {
            // MASTER: Verifica se tem peers (Slaves conectados)
            if (espNowController->getPeerCount() > 0) {
                Serial.println("✅ Slaves detectados - Mudando para CONNECTED");
                changeState(CommState::CONNECTED);
            }
        } else {
            // SLAVE: Muda para CONNECTED se ESP-NOW está inicializado
            // (não precisa ter peers, apenas responder ao Master)
            if (espNowController && espNowController->isInitialized()) {
                Serial.println("✅ ESP-NOW ativo - Mudando para CONNECTED");
                Serial.println("   Aguardando comandos do Master...");
                changeState(CommState::CONNECTED);
            }
        }
    }
    
    void handleMonitoring(unsigned long now) {
        // Enviar heartbeat periódico (Master)
        if (isMaster && now - lastHeartbeat > MASTER_HEARTBEAT_INTERVAL) {
            sendHeartbeat();
            lastHeartbeat = now;
        }
        
        // Verificar timeout de heartbeat
        for (auto& metrics : deviceMetrics) {
            if (now - metrics.lastPongReceived > MASTER_HEARTBEAT_TIMEOUT) {
                Serial.println("⚠️ Heartbeat timeout detectado!");
                initiateRecovery(1); // Soft recovery
                return;
            }
        }
    }
    
    void handleRecovery(unsigned long now) {
        unsigned long recoveryTime = now - recoveryStartTime;
        
        switch (currentState) {
            case CommState::SOFT_RECOVERY:
                if (recoveryTime > SOFT_RECOVERY_TIMEOUT) {
                    if (attemptSoftRecovery()) {
                        changeState(CommState::CONNECTED);
                    } else {
                        initiateRecovery(2);
                    }
                }
                break;
                
            case CommState::MEDIUM_RECOVERY:
                if (recoveryTime > MEDIUM_RECOVERY_TIMEOUT) {
                    if (attemptMediumRecovery()) {
                        changeState(CommState::CONNECTED);
                    } else {
                        initiateRecovery(3);
                    }
                }
                break;
                
            case CommState::HARD_RECOVERY:
                if (recoveryTime > HARD_RECOVERY_TIMEOUT) {
                    if (attemptHardRecovery()) {
                        changeState(CommState::CONNECTED);
                    } else {
                        initiateRecovery(4);
                    }
                }
                break;
                
            case CommState::FULL_RECOVERY:
                if (recoveryTime > FULL_RECOVERY_TIMEOUT) {
                    if (attemptFullRecovery()) {
                        changeState(CommState::CONNECTED);
                    } else {
                        Serial.println("❌ Recovery completo falhou - reiniciando");
                        changeState(CommState::INIT);
                    }
                }
                break;
                
            default:
                break;
        }
    }
    
    // ===== MÉTODOS AUXILIARES =====
    
    void changeState(CommState newState) {
        CommState oldState = currentState;
        currentState = newState;
        
        Serial.println("🔄 Estado: " + getStateName(oldState) + " → " + getStateName(newState));
        
        if (onStateChangeCallback) {
            onStateChangeCallback(oldState, newState);
        }
    }
    
    String getStateName(CommState state) const {
        switch (state) {
            case CommState::INIT: return "INIT";
            case CommState::WIFI_CONNECTING: return "WIFI_CONNECTING";
            case CommState::ESPNOW_INIT: return "ESPNOW_INIT";
            case CommState::WAITING_SLAVES: return "WAITING_SLAVES";
            case CommState::WAITING_CREDENTIALS: return "WAITING_CREDENTIALS";
            case CommState::CREDENTIALS_BROADCAST: return "CREDENTIALS_BROADCAST";
            case CommState::CHANNEL_SYNC: return "CHANNEL_SYNC";
            case CommState::DISCOVERY_ACTIVE: return "DISCOVERY_ACTIVE";
            case CommState::CONNECTED: return "CONNECTED";
            case CommState::MONITORING: return "MONITORING";
            case CommState::SOFT_RECOVERY: return "SOFT_RECOVERY";
            case CommState::MEDIUM_RECOVERY: return "MEDIUM_RECOVERY";
            case CommState::HARD_RECOVERY: return "HARD_RECOVERY";
            case CommState::FULL_RECOVERY: return "FULL_RECOVERY";
            default: return "UNKNOWN";
        }
    }
    
    String formatUptime(unsigned long ms) const {
        unsigned long seconds = ms / 1000;
        unsigned long minutes = seconds / 60;
        unsigned long hours = minutes / 60;
        
        seconds %= 60;
        minutes %= 60;
        
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%luh %lum %lus", hours, minutes, seconds);
        return String(buffer);
    }
    
    void sendHeartbeat() {
        auto peers = espNowController->getPeerList();
        for (const auto& peer : peers) {
            if (peer.online) {
                espNowController->sendPing(peer.macAddress);
            }
        }
    }
    
    void performHealthCheck() {
        for (auto& metrics : deviceMetrics) {
            metrics.calculateHealthScore();
            
            if (metrics.healthScore < HEALTH_SCORE_POOR) {
                Serial.printf("⚠️ Health crítico: %d/100\n", metrics.healthScore);
                initiateRecovery(1);
            }
        }
    }
    
    void initiateRecovery(uint8_t level) {
        recoveryLevel = level;
        recoveryStartTime = millis();
        
        Serial.printf("🔄 Iniciando recovery nível %d...\n", level);
        
        switch (level) {
            case 1: changeState(CommState::SOFT_RECOVERY); break;
            case 2: changeState(CommState::MEDIUM_RECOVERY); break;
            case 3: changeState(CommState::HARD_RECOVERY); break;
            case 4: changeState(CommState::FULL_RECOVERY); break;
        }
    }
    
    bool attemptSoftRecovery() {
        Serial.println("🔧 Soft Recovery: Reenviando última mensagem...");
        // Implementar
        return false; // Temporário
    }
    
    bool attemptMediumRecovery() {
        Serial.println("🔧 Medium Recovery: Re-discovery...");
        espNowController->sendDiscoveryBroadcast();
        delay(2000);
        return espNowController->getPeerCount() > 0;
    }
    
    bool attemptHardRecovery() {
        Serial.println("🔧 Hard Recovery: Reinicializando ESP-NOW...");
        espNowController->end();
        delay(500);
        return espNowController->begin();
    }
    
    bool attemptFullRecovery() {
        Serial.println("🔧 Full Recovery: Reconectando WiFi + ESP-NOW...");
        // Implementar reconexão completa
        return false; // Temporário
    }
};

#endif // AUTO_COMMUNICATION_MANAGER_H

