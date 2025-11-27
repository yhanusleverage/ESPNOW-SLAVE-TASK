#ifndef ESPNOW_CONTROLLER_H
#define ESPNOW_CONTROLLER_H

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <ArduinoJson.h>
#include <vector>
#include <functional>

// Incluir configurações se disponível
#ifndef CONFIG_H
    #include "Config.h"
#endif

// Incluir WiFiCredentialsManager para estrutura WiFiCredentialsData
#include "WiFiCredentialsManager.h"

/**
 * @brief Tipos de mensagem ESP-NOW
 */
enum class MessageType : uint8_t {
    RELAY_COMMAND = 0x01,       // Comando de relé
    RELAY_STATUS = 0x02,        // Status de relé
    DEVICE_INFO = 0x03,         // Informações do dispositivo
    PING = 0x04,                // Ping/Pong para testar conectividade
    PONG = 0x05,                // Resposta ao ping
    BROADCAST = 0x06,           // Mensagem broadcast
    ACK = 0x07,                 // Confirmação de recebimento
    ERROR = 0x08,               // Mensagem de erro
    WIFI_CREDENTIALS = 0x09,    // Credenciais WiFi
    HANDSHAKE_REQUEST = 0x0A,   // Solicitação de handshake
    HANDSHAKE_RESPONSE = 0x0B,  // Resposta ao handshake
    CONNECTIVITY_CHECK = 0x0C,  // Verificação de conectividade
    CONNECTIVITY_REPORT = 0x0D, // Relatório de conectividade
    ALL_RELAYS_STATUS = 0x0E    // 🔄 FASE 3: Estado de todos os relays
};

/**
 * @brief Estrutura base para mensagens ESP-NOW
 */
struct ESPNowMessage {
    MessageType type;           // Tipo da mensagem
    uint8_t senderId[6];       // MAC do remetente
    uint8_t targetId[6];       // MAC do destinatário (FF:FF:FF:FF:FF:FF para broadcast)
    uint32_t messageId;        // ID único da mensagem
    uint32_t timestamp;        // Timestamp da mensagem
    uint8_t dataSize;          // Tamanho dos dados
    uint8_t data[200];         // Dados da mensagem (máximo ESP-NOW: 250 bytes total)
    uint8_t checksum;          // Checksum simples para validação
} __attribute__((packed));

/**
 * @brief Estrutura para comando de relé
 */
struct RelayCommandData {
    int relayNumber;           // Número do relé (0-7)
    bool state;               // Estado desejado
    int duration;             // Duração em segundos (0 = sem timer)
    char action[12];          // "on", "off", "toggle", "status"
} __attribute__((packed));

/**
 * @brief Estrutura para status de relé
 */
struct RelayStatusData {
    int relayNumber;          // Número do relé
    bool state;              // Estado atual
    bool hasTimer;           // Tem timer ativo
    int remainingTime;       // Tempo restante em segundos
    char name[32];           // Nome do relé
} __attribute__((packed));

/**
 * @brief Estrutura para informações do dispositivo
 */
struct DeviceInfoData {
    char deviceName[32];     // Nome do dispositivo
    char deviceType[16];     // Tipo do dispositivo
    uint8_t numRelays;       // Número de relés
    bool operational;        // Status operacional
    uint32_t uptime;         // Uptime em milissegundos
    uint32_t freeHeap;       // Memória livre
    uint8_t wifiChannel;     // ✅ Canal WiFi do dispositivo
    uint8_t padding[3];      // Alinhamento (total: 64 bytes)
} __attribute__((packed));

/**
 * @brief Estrutura para handshake bidirecional
 */
struct HandshakeData {
    uint32_t sessionId;         // ID único da sessão
    uint32_t timestamp;         // Timestamp do handshake
    uint8_t deviceType;        // 0=Master, 1=Slave
    char deviceName[32];       // Nome do dispositivo
    uint8_t protocolVersion;   // Versão do protocolo
    bool wifiConnected;        // Status WiFi
    uint8_t validationCode;    // Código de validação
} __attribute__((packed));

/**
 * @brief Estrutura para relatório de conectividade
 */
struct ConnectivityReportData {
    uint32_t sessionId;         // ID da sessão
    uint32_t timestamp;         // Timestamp do relatório
    bool wifiConnected;         // Status WiFi
    int32_t wifiRSSI;          // Força do sinal WiFi
    uint8_t wifiChannel;        // Canal WiFi
    uint32_t uptime;            // Tempo de funcionamento
    uint32_t freeHeap;          // Memória livre
    uint8_t messageCount;       // Contador de mensagens
    bool operational;           // Status operacional
} __attribute__((packed));

/**
 * @brief Informações de peer (dispositivo conectado)
 */
struct PeerInfo {
    uint8_t macAddress[6];   // MAC address do peer
    String deviceName;       // Nome do dispositivo
    String deviceType;       // Tipo do dispositivo
    bool online;            // Status online/offline
    unsigned long lastSeen; // Último contato
    int rssi;               // Força do sinal
};

/**
 * @brief Classe para controle de comunicação ESP-NOW
 */
class ESPNowController {
public:
    // 🔄 FASE 2: Permitir acesso de MasterSlaveManager a métodos internos
    friend class MasterSlaveManager;
    
    /**
     * @brief Construtor
     * @param deviceName Nome do dispositivo local
     * @param channel Canal WiFi a usar (1-14)
     */
    ESPNowController(const String& deviceName = "ESP32Device", int channel = 1);
    
    /**
     * @brief Inicializa ESP-NOW
     * @return true se inicialização foi bem sucedida
     */
    bool begin();
    
    /**
     * @brief Atualiza sistema (chamar no loop principal)
     */
    void update();
    
    /**
     * @brief Para o sistema ESP-NOW
     */
    void end();
    
    // ===== ENVIO DE MENSAGENS =====
    
    /**
     * @brief Envia comando de relé para dispositivo específico
     * @param targetMac MAC address do dispositivo alvo
     * @param relayNumber Número do relé
     * @param action Ação ("on", "off", "toggle", "status")
     * @param duration Duração em segundos (0 = sem timer)
     * @return true se mensagem foi enviada
     */
    bool sendRelayCommand(const uint8_t* targetMac, int relayNumber, const String& action, int duration = 0);
    
    /**
     * @brief Envia status de relé
     * @param targetMac MAC address do destinatário (nullptr para broadcast)
     * @param relayNumber Número do relé
     * @param state Estado atual
     * @param hasTimer Tem timer ativo
     * @param remainingTime Tempo restante
     * @param name Nome do relé
     * @return true se mensagem foi enviada
     */
    bool sendRelayStatus(const uint8_t* targetMac, int relayNumber, bool state, bool hasTimer, int remainingTime, const String& name);
    
    /**
     * @brief Envia informações do dispositivo
     * @param targetMac MAC address do destinatário (nullptr para broadcast)
     * @param deviceType Tipo do dispositivo
     * @param numRelays Número de relés
     * @param operational Status operacional
     * @param uptime Uptime
     * @param freeHeap Memória livre
     * @return true se mensagem foi enviada
     */
    bool sendDeviceInfo(const uint8_t* targetMac, const String& deviceType, uint8_t numRelays, bool operational, uint32_t uptime, uint32_t freeHeap);
    
    /**
     * @brief Envia ping para dispositivo
     * @param targetMac MAC address do dispositivo
     * @return true se ping foi enviado
     */
    bool sendPing(const uint8_t* targetMac);
    
    /**
     * @brief Envia mensagem broadcast para descoberta de dispositivos
     * @return true se broadcast foi enviado
     */
    bool sendDiscoveryBroadcast();
    
    /**
     * @brief Envia credenciais WiFi em broadcast para todos os dispositivos
     * @param ssid SSID da rede WiFi
     * @param password Senha da rede WiFi
     * @param channel Canal WiFi (opcional, 0 = usar canal atual)
     * @return true se credenciais foram enviadas
     */
    bool sendWiFiCredentialsBroadcast(const String& ssid, const String& password, uint8_t channel = 0);
    
    // ===== GERENCIAMENTO DE PEERS =====
    
    /**
     * @brief Adiciona peer manualmente
     * @param macAddress MAC address do peer
     * @param deviceName Nome do dispositivo (opcional)
     * @return true se peer foi adicionado
     */
    bool addPeer(const uint8_t* macAddress, const String& deviceName = "");
    
    /**
     * @brief Adiciona peer com canal específico (CORRETO!)
     * @param macAddress MAC address do peer
     * @param peerChannel Canal WiFi do peer (1-13)
     * @param deviceName Nome do dispositivo (opcional)
     * @return true se peer foi adicionado
     */
    bool addPeerWithChannel(const uint8_t* macAddress, uint8_t peerChannel, const String& deviceName = "");
    
    /**
     * @brief Adiciona peer com verificação de canal (método seguro)
     * Garante que o peer seja adicionado no canal correto
     * @param macAddress MAC address do peer
     * @param deviceName Nome do dispositivo (opcional)
     * @return true se peer foi adicionado
     */
    bool addPeerSafe(const uint8_t* macAddress, const String& deviceName = "");
    
    /**
     * @brief Remove peer
     * @param macAddress MAC address do peer
     * @return true se peer foi removido
     */
    bool removePeer(const uint8_t* macAddress);
    
    /**
     * @brief Obtém lista de peers
     * @return Vector com informações dos peers
     */
    std::vector<PeerInfo> getPeerList();
    
    /**
     * @brief Verifica se peer existe
     * @param macAddress MAC address do peer
     * @return true se peer existe
     */
    bool peerExists(const uint8_t* macAddress);
    
    /**
     * @brief Obtém número de peers conectados
     * @return Número de peers
     */
    int getPeerCount();
    
    // ===== CALLBACKS =====
    
    /**
     * @brief Define callback para comandos de relé recebidos
     * @param callback Função a ser chamada
     */
    void setRelayCommandCallback(std::function<void(const uint8_t* senderMac, int relayNumber, const String& action, int duration)> callback);
    
    /**
     * @brief Define callback para status de relé recebido
     * @param callback Função a ser chamada
     */
    void setRelayStatusCallback(std::function<void(const uint8_t* senderMac, int relayNumber, bool state, bool hasTimer, int remainingTime, const String& name)> callback);
    
    /**
     * @brief Define callback para informações de dispositivo recebidas
     * @param callback Função a ser chamada
     */
    void setDeviceInfoCallback(std::function<void(const uint8_t* senderMac, const String& deviceName, const String& deviceType, uint8_t numRelays, bool operational, uint8_t wifiChannel)> callback);
    
    /**
     * @brief Define callback para ping recebido
     * @param callback Função a ser chamada
     */
    void setPingCallback(void (*callback)(const uint8_t* senderMac));
    
    /**
     * @brief Define callback para credenciais WiFi recebidas
     * @param callback Função a ser chamada
     */
    void setWiFiCredentialsCallback(void (*callback)(const String& ssid, const String& password, uint8_t channel));
    
    /**
     * @brief Define callback para handshake recebido
     * @param callback Função a ser chamada
     */
    void setHandshakeCallback(void (*callback)(const uint8_t* senderMac, uint32_t sessionId, const String& deviceName, bool wifiConnected));
    
    /**
     * @brief Define callback para relatório de conectividade recebido
     * @param callback Função a ser chamada
     */
    void setConnectivityReportCallback(void (*callback)(const uint8_t* senderMac, uint32_t sessionId, bool wifiConnected, int32_t rssi, uint32_t uptime));
    
    /**
     * @brief Define callback para solicitação de verificação de conectividade
     * @param callback Função a ser chamada
     */
    void setConnectivityCheckCallback(void (*callback)(const uint8_t* senderMac));
    
    /**
     * @brief Define callback para mensagens de erro
     * @param callback Função a ser chamada
     */
    void setErrorCallback(void (*callback)(const String& error));
    
    // ===== UTILITÁRIOS =====
    
    /**
     * @brief Converte MAC address para string
     * @param mac Array de 6 bytes com MAC
     * @return String formatada (XX:XX:XX:XX:XX:XX)
     */
    static String macToString(const uint8_t* mac);
    
    /**
     * @brief Converte string para MAC address
     * @param macStr String no formato XX:XX:XX:XX:XX:XX
     * @param mac Array de 6 bytes para armazenar resultado
     * @return true se conversão foi bem sucedida
     */
    static bool stringToMac(const String& macStr, uint8_t* mac);
    
    /**
     * @brief Obtém MAC address local
     * @param mac Array de 6 bytes para armazenar MAC local
     */
    void getLocalMac(uint8_t* mac);
    
    /**
     * @brief Obtém MAC address local como string
     * @return String com MAC local
     */
    String getLocalMacString();
    
    /**
     * @brief Verifica se sistema está inicializado
     * @return true se ESP-NOW está funcionando
     */
    bool isInitialized() { return initialized; }
    
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
     * @brief Envia mensagem ESP-NOW customizada (uso avançado)
     * @param message Estrutura da mensagem
     * @param targetMac MAC de destino (nullptr para broadcast)
     * @return true se mensagem foi enviada
     */
    bool sendMessage(const ESPNowMessage& message, const uint8_t* targetMac);
    
    // ===== MÉTODOS DE VALIDAÇÃO E HANDSHAKE =====
    
    /**
     * @brief Valida credenciais WiFi recebidas (verifica checksum)
     * @param credentials Estrutura com credenciais
     * @return true se credenciais são válidas
     */
    bool validateWiFiCredentials(const WiFiCredentialsData& credentials);
    
    /**
     * @brief Inicia handshake bidirecional com dispositivo
     * @param targetMac MAC address do dispositivo alvo
     * @return true se handshake foi iniciado
     */
    bool initiateHandshake(const uint8_t* targetMac);
    
    /**
     * @brief Responde a handshake recebido
     * @param targetMac MAC address do dispositivo que solicitou
     * @param sessionId ID da sessão recebida
     * @return true se resposta foi enviada
     */
    bool respondToHandshake(const uint8_t* targetMac, uint32_t sessionId);
    
    /**
     * @brief Envia relatório de conectividade
     * @param targetMac MAC address do destinatário (nullptr para broadcast)
     * @param sessionId ID da sessão
     * @return true se relatório foi enviado
     */
    bool sendConnectivityReport(const uint8_t* targetMac, uint32_t sessionId);
    
    /**
     * @brief Solicita verificação de conectividade
     * @param targetMac MAC address do dispositivo alvo
     * @return true se solicitação foi enviada
     */
    bool requestConnectivityCheck(const uint8_t* targetMac);
    
    /**
     * @brief Valida handshake recebido
     * @param handshake Estrutura do handshake
     * @return true se handshake é válido
     */
    bool validateHandshake(const HandshakeData& handshake);
    
    /**
     * @brief Gera ID único de sessão
     * @return ID de sessão único
     */
    uint32_t generateSessionId();
    
    /**
     * @brief Calcula checksum da mensagem (método público para uso externo)
     * @param message Mensagem para calcular checksum
     * @return Valor do checksum
     */
    uint8_t calculateChecksum(const ESPNowMessage& message);

private:
    String deviceName;                    // Nome do dispositivo local
    int wifiChannel;                     // Canal WiFi
    bool initialized;                    // Status de inicialização
    uint32_t messageCounter;            // Contador de mensagens enviadas
    
    // Estatísticas
    uint32_t messagesSent;
    uint32_t messagesReceived;
    uint32_t messagesLost;
    uint32_t lastMessageId;
    
    // Lista de peers conhecidos
    std::vector<PeerInfo> knownPeers;
    
    // Callbacks
    std::function<void(const uint8_t* senderMac, int relayNumber, const String& action, int duration)> relayCommandCallback = nullptr;
    std::function<void(const uint8_t* senderMac, int relayNumber, bool state, bool hasTimer, int remainingTime, const String& name)> relayStatusCallback = nullptr;
    std::function<void(const uint8_t* senderMac, const String& deviceName, const String& deviceType, uint8_t numRelays, bool operational, uint8_t wifiChannel)> deviceInfoCallback = nullptr;
    void (*pingCallback)(const uint8_t* senderMac) = nullptr;
    void (*errorCallback)(const String& error) = nullptr;
    void (*wifiCredentialsCallback)(const String& ssid, const String& password, uint8_t channel) = nullptr;
    void (*handshakeCallback)(const uint8_t* senderMac, uint32_t sessionId, const String& deviceName, bool wifiConnected) = nullptr;
    void (*connectivityReportCallback)(const uint8_t* senderMac, uint32_t sessionId, bool wifiConnected, int32_t rssi, uint32_t uptime) = nullptr;
    void (*connectivityCheckCallback)(const uint8_t* senderMac) = nullptr;
    
    // ===== MÉTODOS PRIVADOS =====
    
    /**
     * @brief Processa mensagem recebida
     * @param message Mensagem recebida
     * @param senderMac MAC do remetente
     */
    void processReceivedMessage(const ESPNowMessage& message, const uint8_t* senderMac);
    
    /**
     * @brief Valida mensagem recebida
     * @param message Mensagem para validar
     * @return true se mensagem é válida
     */
    bool validateMessage(const ESPNowMessage& message);
    
    /**
     * @brief Atualiza informações do peer
     * @param macAddress MAC do peer
     * @param deviceName Nome do dispositivo
     * @param deviceType Tipo do dispositivo
     */
    void updatePeerInfo(const uint8_t* macAddress, const String& deviceName, const String& deviceType);
    
    /**
     * @brief Remove peers offline (não vistos há muito tempo)
     */
    void cleanupOfflinePeers();
    
    /**
     * @brief Gera código de validação genérico (usado para handshakes)
     * @param text1 Primeiro texto para XOR
     * @param text2 Segundo texto para XOR
     * @param value Valor numérico para XOR
     * @return Código de validação
     */
    uint8_t generateValidationCode(const String& text1, const String& text2, uint32_t value);
    
    /**
     * @brief Callback estático para recebimento de mensagens ESP-NOW
     */
    static void onDataReceived(const uint8_t* mac, const uint8_t* incomingData, int len);
    
    /**
     * @brief Callback estático para confirmação de envio ESP-NOW
     */
    static void onDataSent(const uint8_t* mac_addr, esp_now_send_status_t status);
    
    // Instância estática para callbacks
    static ESPNowController* instance;
};

#endif // ESPNOW_CONTROLLER_H
