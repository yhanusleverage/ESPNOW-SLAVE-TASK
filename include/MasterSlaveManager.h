#ifndef MASTER_SLAVE_MANAGER_H
#define MASTER_SLAVE_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <vector>
#include <functional>
#include "ESPNowController.h"
#include "ESPNowTypes.h"  // 🔄 FASE 2: Para RelayCommandAck

/**
 * @brief Status de um Slave na lista confiável
 */
enum class SlaveStatus : uint8_t {
    UNKNOWN = 0,        // Status desconhecido
    DISCOVERED = 1,     // Descoberto via broadcast
    PING_RECEIVED = 2,  // Recebeu PING do Slave
    HANDSHAKE_OK = 3,   // Handshake completado
    ONLINE = 4,         // Online e operacional
    OFFLINE = 5,        // Offline (timeout)
    ERROR = 6          // Erro de comunicação
};

/**
 * @brief Informações detalhadas de um Slave confiável
 */
struct TrustedSlave {
    uint8_t macAddress[6];      // MAC address do Slave
    String deviceName;          // Nome do dispositivo
    String deviceType;          // Tipo (RelayBox, SensorBox, etc.)
    SlaveStatus status;         // Status atual
    unsigned long lastSeen;     // Último contato (timestamp)
    unsigned long firstSeen;     // Primeiro contato (timestamp)
    int rssi;                   // Força do sinal
    uint8_t numRelays;          // Número de relés disponíveis
    bool operational;           // Status operacional
    uint32_t uptime;           // Uptime do Slave
    uint32_t freeHeap;         // Memória livre do Slave
    uint32_t messageCount;     // Contador de mensagens recebidas
    uint32_t lastPingId;       // ID do último PING recebido
    uint32_t lastPongId;       // ID do último PONG enviado
    bool waitingForPong;       // Se está aguardando PONG
    unsigned long pongTimeout; // Timeout para PONG
    
    // Estatísticas de comunicação
    uint32_t pingsReceived;    // PINGs recebidos
    uint32_t pongsSent;        // PONGs enviados
    uint32_t messagesReceived; // Mensagens recebidas
    uint32_t messagesLost;     // Mensagens perdidas
    uint32_t errors;           // Erros de comunicação
    
    /**
     * @brief Construtor padrão
     */
    TrustedSlave() {
        memset(macAddress, 0, 6);
        deviceName = "Unknown";
        deviceType = "Unknown";
        status = SlaveStatus::UNKNOWN;
        lastSeen = 0;
        firstSeen = 0;
        rssi = -100;
        numRelays = 0;
        operational = false;
        uptime = 0;
        freeHeap = 0;
        messageCount = 0;
        lastPingId = 0;
        lastPongId = 0;
        waitingForPong = false;
        pongTimeout = 0;
        pingsReceived = 0;
        pongsSent = 0;
        messagesReceived = 0;
        messagesLost = 0;
        errors = 0;
    }
    
    /**
     * @brief Construtor com MAC
     */
    TrustedSlave(const uint8_t* mac) {
        memcpy(macAddress, mac, 6);
        deviceName = "Slave-" + ESPNowController::macToString(mac).substring(12);
        deviceType = "RelayBox";
        status = SlaveStatus::DISCOVERED;
        lastSeen = millis();
        firstSeen = millis();
        rssi = -50;
        numRelays = 8;
        operational = true;
        uptime = 0;
        freeHeap = 0;
        messageCount = 0;
        lastPingId = 0;
        lastPongId = 0;
        waitingForPong = false;
        pongTimeout = 0;
        pingsReceived = 0;
        pongsSent = 0;
        messagesReceived = 0;
        messagesLost = 0;
        errors = 0;
    }
    
    /**
     * @brief Verifica se o Slave está online
     */
    bool isOnline() const {
        return status == SlaveStatus::ONLINE || status == SlaveStatus::HANDSHAKE_OK;
    }
    
    /**
     * @brief Verifica se o Slave está offline há muito tempo
     */
    bool isOfflineTimeout(unsigned long timeoutMs = 60000) const {
        return (millis() - lastSeen) > timeoutMs;
    }
    
    /**
     * @brief Atualiza timestamp de último contato
     */
    void updateLastSeen() {
        lastSeen = millis();
        if (firstSeen == 0) {
            firstSeen = lastSeen;
        }
    }
    
    /**
     * @brief Obtém tempo desde último contato
     */
    unsigned long getTimeSinceLastSeen() const {
        return millis() - lastSeen;
    }
    
    /**
     * @brief Obtém tempo desde primeiro contato
     */
    unsigned long getUptime() const {
        return millis() - firstSeen;
    }
};

/**
 * @brief Gerenciador Master-Slave para comunicação bidirecional ESP-NOW
 * 
 * Esta classe implementa:
 * - Recebimento de PINGs do SLAVE
 * - Lista confiável de SLAVEs
 * - Sistema de ACKs e confirmação de pacotes
 * - Handshake bidirecional
 * - Monitoramento de status online/offline
 */
class MasterSlaveManager {
public:
    /**
     * @brief Construtor
     * @param espNowController Instância do ESPNowController
     */
    MasterSlaveManager(ESPNowController* espNowController);
    
    /**
     * @brief Inicializa o gerenciador
     * @return true se inicialização foi bem sucedida
     */
    bool begin();
    
    /**
     * @brief Atualiza o sistema (chamar no loop principal)
     */
    void update();
    
    /**
     * @brief Para o sistema
     */
    void end();
    
    // ===== GERENCIAMENTO DE SLAVES =====
    
    /**
     * @brief Adiciona Slave à lista confiável
     * @param macAddress MAC address do Slave
     * @param deviceName Nome do dispositivo
     * @param deviceType Tipo do dispositivo
     * @return true se Slave foi adicionado
     */
    bool addTrustedSlave(const uint8_t* macAddress, const String& deviceName = "", const String& deviceType = "RelayBox");
    
    /**
     * @brief Remove Slave da lista confiável
     * @param macAddress MAC address do Slave
     * @return true se Slave foi removido
     */
    bool removeTrustedSlave(const uint8_t* macAddress);
    
    /**
     * @brief Obtém Slave da lista confiável
     * @param macAddress MAC address do Slave
     * @return Ponteiro para TrustedSlave ou nullptr se não encontrado
     */
    TrustedSlave* getTrustedSlave(const uint8_t* macAddress);
    
    /**
     * @brief Obtém lista de todos os Slaves confiáveis
     * @return Vector com todos os Slaves
     */
    std::vector<TrustedSlave> getAllTrustedSlaves();
    
    /**
     * @brief Obtém lista de Slaves online
     * @return Vector com Slaves online
     */
    std::vector<TrustedSlave> getOnlineSlaves();
    
    /**
     * @brief Obtém número de Slaves confiáveis
     * @return Número de Slaves
     */
    int getTrustedSlaveCount();
    
    /**
     * @brief Obtém número de Slaves online
     * @return Número de Slaves online
     */
    int getOnlineSlaveCount();
    
    // ===== COMUNICAÇÃO BIDIRECIONAL =====
    
    /**
     * @brief Envia PING para Slave específico
     * @param macAddress MAC address do Slave
     * @return true se PING foi enviado
     */
    bool sendPingToSlave(const uint8_t* macAddress);
    
    /**
     * @brief Envia PING para todos os Slaves online
     * @return Número de PINGs enviados
     */
    int sendPingToAllSlaves();
    
    /**
     * @brief Envia comando de relé para Slave específico
     * @param macAddress MAC address do Slave
     * @param relayNumber Número do relé
     * @param action Ação ("on", "off", "toggle")
     * @param duration Duração em segundos
     * @return true se comando foi enviado
     */
    bool sendRelayCommandToSlave(const uint8_t* macAddress, int relayNumber, const String& action, int duration = 0);
    
    /**
     * @brief Solicita status de todos os relés de um Slave
     * @param macAddress MAC address do Slave
     * @return true se solicitação foi enviada
     */
    bool requestSlaveStatus(const uint8_t* macAddress);
    
    /**
     * @brief Solicita informações do dispositivo Slave
     * @param macAddress MAC address do Slave
     * @return true se solicitação foi enviada
     */
    bool requestSlaveInfo(const uint8_t* macAddress);
    
    // ===== SISTEMA DE ACKs =====
    
    /**
     * @brief Envia ACK para confirmação de recebimento
     * @param macAddress MAC address do destinatário
     * @param messageId ID da mensagem confirmada
     * @return true se ACK foi enviado
     */
    bool sendAck(const uint8_t* macAddress, uint32_t messageId);
    
    /**
     * @brief Verifica se está aguardando ACK de uma mensagem
     * @param macAddress MAC address do destinatário
     * @param messageId ID da mensagem
     * @return true se está aguardando ACK
     */
    bool isWaitingForAck(const uint8_t* macAddress, uint32_t messageId);
    
    /**
     * @brief Marca mensagem como confirmada (ACK recebido)
     * @param macAddress MAC address do remetente
     * @param messageId ID da mensagem
     */
    void markMessageAcknowledged(const uint8_t* macAddress, uint32_t messageId);
    
    // ===== CALLBACKS =====
    
    /**
     * @brief Define callback para quando Slave é descoberto
     * @param callback Função a ser chamada
     */
    void setSlaveDiscoveredCallback(std::function<void(const uint8_t* macAddress, const String& deviceName, const String& deviceType)> callback);
    
    /**
     * @brief Define callback para quando Slave fica online
     * @param callback Função a ser chamada
     */
    void setSlaveOnlineCallback(std::function<void(const uint8_t* macAddress, const String& deviceName)> callback);
    
    /**
     * @brief Define callback para quando Slave fica offline
     * @param callback Função a ser chamada
     */
    void setSlaveOfflineCallback(std::function<void(const uint8_t* macAddress, const String& deviceName)> callback);
    
    /**
     * @brief Define callback para PING recebido do Slave
     * @param callback Função a ser chamada
     */
    void setPingReceivedCallback(std::function<void(const uint8_t* macAddress, uint32_t pingId)> callback);
    
    /**
     * @brief Define callback para PONG recebido do Slave
     * @param callback Função a ser chamada
     */
    void setPongReceivedCallback(std::function<void(const uint8_t* macAddress, uint32_t pongId)> callback);
    
    /**
     * @brief Define callback para status de relé recebido do Slave
     * @param callback Função a ser chamada
     */
    void setRelayStatusCallback(std::function<void(const uint8_t* macAddress, int relayNumber, bool state, bool hasTimer, int remainingTime, const String& name)> callback);
    
    /**
     * @brief Define callback para informações de dispositivo recebidas do Slave
     * @param callback Função a ser chamada
     */
    void setDeviceInfoCallback(std::function<void(const uint8_t* macAddress, const String& deviceName, const String& deviceType, uint8_t numRelays, bool operational)> callback);
    
    /**
     * @brief Define callback para ACK recebido
     * @param callback Função a ser chamada
     */
    void setAckReceivedCallback(std::function<void(const uint8_t* macAddress, uint32_t messageId)> callback);
    
    /**
     * @brief Define callback para erro de comunicação
     * @param callback Função a ser chamada
     */
    void setErrorCallback(std::function<void(const uint8_t* macAddress, const String& error)> callback);
    
    // ===== 🔄 FASE 2: CALLBACK PARA ACK =====
    
    /**
     * @brief Define callback para ACK de comando de relay recebido
     * @param callback Função a ser chamada
     */
    void setRelayAckCallback(std::function<void(const uint8_t* macAddress, uint32_t commandId, bool success, uint8_t relayNumber, uint8_t currentState)> callback);
    
    // ===== 🔄 FASE 2: PROCESSAMENTO DE ACKs DE RELAY =====
    
    /**
     * @brief Processa ACK de comando de relay recebido do Slave (PÚBLICO para ESPNowController)
     * @param ack Estrutura do ACK
     * @param senderMac MAC do Slave que enviou o ACK
     */
    void processRelayCommandAck(const RelayCommandAck& ack, const uint8_t* senderMac);
    
    /**
     * @brief Obtém instância estática do MasterSlaveManager (PÚBLICO para ESPNowController)
     * @return Ponteiro para instância ou nullptr
     */
    static MasterSlaveManager* getInstance() { return instance; }
    
    // ===== UTILITÁRIOS =====
    
    /**
     * @brief Obtém estatísticas de comunicação
     * @return String JSON com estatísticas
     */
    String getStatsJSON();
    
    /**
     * @brief Imprime status do sistema
     */
    void printStatus();
    
    /**
     * @brief Imprime lista de Slaves confiáveis
     */
    void printTrustedSlaves();
    
    /**
     * @brief Força limpeza de Slaves offline
     */
    void cleanupOfflineSlaves();
    
    /**
     * @brief Força re-descoberta de todos os Slaves
     */
    void rediscoverSlaves();

private:
    ESPNowController* espNowController;  // Instância do ESPNowController
    bool initialized;                  // Status de inicialização
    
    // Lista de Slaves confiáveis
    std::vector<TrustedSlave> trustedSlaves;
    
    // Contadores de mensagens pendentes de ACK
    struct PendingAck {
        uint8_t macAddress[6];
        uint32_t messageId;
        unsigned long timestamp;
        int retryCount;
    };
    std::vector<PendingAck> pendingAcks;
    
    // ===== 🔄 FASE 1: SISTEMA DE RETRY AUTOMÁTICO =====
    /**
     * @brief Estrutura para comandos pendentes de retry
     * Quando um comando falha ao enviar, ele é guardado aqui para retentar
     */
    struct PendingRelayCommand {
        uint8_t targetMac[6];        // MAC do Slave destino
        int relayNumber;             // Número do relé (0-7)
        String action;               // Ação: "on", "off", "toggle"
        int duration;                // Duração em segundos
        unsigned long timestamp;     // Quando foi criado o comando
        unsigned long nextRetry;     // Quando fazer próximo retry
        uint8_t retryCount;          // Quantas vezes já tentou
        uint32_t commandId;          // ID único do comando
        bool waitingForAck;          // Se está aguardando confirmação
    };
    std::vector<PendingRelayCommand> pendingRelayCommands;
    
    // Configurações de retry
    static constexpr uint8_t MAX_RELAY_RETRIES = 3;      // Máximo de tentativas
    static constexpr unsigned long RETRY_INTERVAL = 2000; // Intervalo base entre retries (ms)
    
    uint32_t commandIdCounter;  // Contador para IDs únicos de comandos
    
    // Estatísticas gerais
    uint32_t totalPingsReceived;
    uint32_t totalPongsSent;
    uint32_t totalAcksSent;
    uint32_t totalAcksReceived;
    uint32_t totalErrors;
    
    // Callbacks
    std::function<void(const uint8_t* macAddress, const String& deviceName, const String& deviceType)> slaveDiscoveredCallback = nullptr;
    std::function<void(const uint8_t* macAddress, const String& deviceName)> slaveOnlineCallback = nullptr;
    std::function<void(const uint8_t* macAddress, const String& deviceName)> slaveOfflineCallback = nullptr;
    std::function<void(const uint8_t* macAddress, uint32_t pingId)> pingReceivedCallback = nullptr;
    std::function<void(const uint8_t* macAddress, uint32_t pongId)> pongReceivedCallback = nullptr;
    std::function<void(const uint8_t* macAddress, int relayNumber, bool state, bool hasTimer, int remainingTime, const String& name)> relayStatusCallback = nullptr;
    std::function<void(const uint8_t* macAddress, const String& deviceName, const String& deviceType, uint8_t numRelays, bool operational)> deviceInfoCallback = nullptr;
    std::function<void(const uint8_t* macAddress, uint32_t messageId)> ackReceivedCallback = nullptr;
    std::function<void(const uint8_t* macAddress, const String& error)> errorCallback = nullptr;
    std::function<void(const uint8_t* macAddress, uint32_t commandId, bool success, uint8_t relayNumber, uint8_t currentState)> relayAckCallback = nullptr;  // 🔄 FASE 2
    
    // ===== MÉTODOS PRIVADOS =====
    
    /**
     * @brief Processa PING recebido do Slave
     * @param senderMac MAC do Slave
     * @param pingId ID do PING
     */
    void processPingReceived(const uint8_t* senderMac, uint32_t pingId);
    
    /**
     * @brief Processa PONG recebido do Slave
     * @param senderMac MAC do Slave
     * @param pongId ID do PONG
     */
    void processPongReceived(const uint8_t* senderMac, uint32_t pongId);
    
    /**
     * @brief Processa ACK recebido do Slave
     * @param senderMac MAC do Slave
     * @param messageId ID da mensagem confirmada
     */
    void processAckReceived(const uint8_t* senderMac, uint32_t messageId);
    
    /**
     * @brief Processa informações de dispositivo recebidas do Slave
     * @param senderMac MAC do Slave
     * @param deviceName Nome do dispositivo
     * @param deviceType Tipo do dispositivo
     * @param numRelays Número de relés
     * @param operational Status operacional
     */
    void processDeviceInfoReceived(const uint8_t* senderMac, const String& deviceName, const String& deviceType, uint8_t numRelays, bool operational, uint8_t wifiChannel);
    
    /**
     * @brief Processa status de relé recebido do Slave
     * @param senderMac MAC do Slave
     * @param relayNumber Número do relé
     * @param state Estado atual
     * @param hasTimer Tem timer ativo
     * @param remainingTime Tempo restante
     * @param name Nome do relé
     */
    void processRelayStatusReceived(const uint8_t* senderMac, int relayNumber, bool state, bool hasTimer, int remainingTime, const String& name);
    
    /**
     * @brief Atualiza informações de um Slave confiável
     * @param macAddress MAC do Slave
     * @param deviceName Nome do dispositivo
     * @param deviceType Tipo do dispositivo
     * @param status Novo status
     */
    void updateTrustedSlave(const uint8_t* macAddress, const String& deviceName = "", const String& deviceType = "", SlaveStatus status = SlaveStatus::UNKNOWN);
    
    /**
     * @brief Verifica e atualiza status de Slaves offline
     */
    void checkSlaveStatus();
    
    /**
     * @brief Limpa ACKs pendentes expirados
     */
    void cleanupExpiredAcks();
    
    /**
     * @brief Reenvia ACKs pendentes se necessário
     */
    void resendPendingAcks();
    
    // ===== 🔄 FASE 1: MÉTODOS DE RETRY =====
    
    /**
     * @brief Processa fila de comandos pendentes de retry
     * Chamado automaticamente no update()
     */
    void processRetryQueue();
    
    /**
     * @brief Adiciona comando à fila de retry
     * @param targetMac MAC do Slave destino
     * @param relayNumber Número do relé
     * @param action Ação a executar
     * @param duration Duração em segundos
     * @param commandId ID único do comando
     */
    void addToRetryQueue(const uint8_t* targetMac, int relayNumber, const String& action, int duration, uint32_t commandId);
    
    /**
     * @brief Remove comando da fila de retry quando confirmado
     * @param commandId ID do comando a remover
     */
    void removeFromRetryQueue(uint32_t commandId);
    
    /**
     * @brief Gera ID único para comandos
     * @return ID único
     */
    uint32_t generateCommandId();
    
    /**
     * @brief Callbacks estáticos para ESPNowController
     */
    static void onPingReceivedStatic(const uint8_t* senderMac);
    static void onPongReceivedStatic(const uint8_t* senderMac);
    static void onDeviceInfoReceivedStatic(const uint8_t* senderMac, const String& deviceName, const String& deviceType, uint8_t numRelays, bool operational, uint8_t wifiChannel);
    static void onRelayStatusReceivedStatic(const uint8_t* senderMac, int relayNumber, bool state, bool hasTimer, int remainingTime, const String& name);
    static void onErrorReceivedStatic(const String& error);
    
    // Instância estática para callbacks
    static MasterSlaveManager* instance;
};

#endif // MASTER_SLAVE_MANAGER_H
