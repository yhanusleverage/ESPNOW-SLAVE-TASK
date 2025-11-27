#ifndef ESPNOW_BRIDGE_H
#define ESPNOW_BRIDGE_H

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <vector>
#include "RelayCommandBox.h"
#include "Config.h"
#include "WiFiCredentialsManager.h"

// Incluir ESPNowController do ESPNOW-CARGA
#include "ESPNowController.h"

// Usar estruturas do ESPNowController para compatibilidade
// (As estruturas já estão definidas no ESPNowController.h)

/**
 * @brief Informações de dispositivo remoto
 */
struct RemoteDevice {
    uint8_t mac[6];
    String name;
    String deviceType;
    bool online;
    unsigned long lastSeen;
    int rssi;
    uint8_t numRelays;
    bool operational;
};

/**
 * @brief Ponte ESP-NOW para ESP-HIDROWAVE
 * Permite comunicação com dispositivos ESPNOW-CARGA
 * 
 * FASE 2: Adaptado para usar ESPNowController como base
 * Mantém compatibilidade com sistema existente
 */
class ESPNowBridge {
public:
    /**
     * @brief Construtor
     * @param relayController Referência ao RelayCommandBox local
     * @param channel Canal WiFi/ESP-NOW (padrão: 1)
     */
    ESPNowBridge(RelayCommandBox* relayController, int channel = 1);
    
    /**
     * @brief Inicializa sistema ESP-NOW
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
    
    // ===== ENVIO DE COMANDOS =====
    
    /**
     * @brief Envia comando de relé para dispositivo remoto
     * @param targetMac MAC do dispositivo alvo
     * @param relayNumber Número do relé (0-7)
     * @param action Ação ("on", "off", "toggle")
     * @param duration Duração em segundos (0 = sem timer)
     * @return true se comando foi enviado
     */
    bool sendRelayCommand(const uint8_t* targetMac, int relayNumber, const String& action, int duration = 0);
    
    /**
     * @brief Envia ping para dispositivo
     * @param targetMac MAC do dispositivo
     * @return true se ping foi enviado
     */
    bool sendPing(const uint8_t* targetMac);
    
    /**
     * @brief Envia broadcast de descoberta
     * @return true se broadcast foi enviado
     */
    bool sendDiscoveryBroadcast();
    
    /**
     * @brief Envia credenciais WiFi em broadcast para todos os dispositivos
     * @param ssid SSID da rede WiFi
     * @param password Senha da rede WiFi
     * @return true se credenciais foram enviadas
     */
    bool sendWiFiCredentialsBroadcast(const String& ssid, const String& password);
    
    /**
     * @brief Envia dados de sensores para dispositivos remotos
     * @param sensorData JSON com dados dos sensores
     * @return true se dados foram enviados
     */
    bool broadcastSensorData(const String& sensorData);
    
    // ===== GERENCIAMENTO DE DISPOSITIVOS =====
    
    /**
     * @brief Adiciona dispositivo remoto conhecido
     * @param mac MAC address do dispositivo
     * @param name Nome do dispositivo
     * @return true se adicionado com sucesso
     */
    bool addRemoteDevice(const uint8_t* mac, const String& name = "");
    
    /**
     * @brief Remove dispositivo remoto
     * @param mac MAC address do dispositivo
     * @return true se removido
     */
    bool removeRemoteDevice(const uint8_t* mac);
    
    /**
     * @brief Obtém lista de dispositivos remotos
     * @return Vector com dispositivos conhecidos
     */
    std::vector<RemoteDevice> getRemoteDevices();
    
    /**
     * @brief Verifica se dispositivo está online
     * @param mac MAC address do dispositivo
     * @return true se dispositivo está online
     */
    bool isDeviceOnline(const uint8_t* mac);
    
    /**
     * @brief Obtém número de dispositivos online
     * @return Contagem de dispositivos online
     */
    int getOnlineDeviceCount();
    
    // ===== CALLBACKS =====
    
    /**
     * @brief Define callback para status de relé remoto recebido
     * @param callback Função a ser chamada
     */
    void setRemoteRelayStatusCallback(void (*callback)(const uint8_t* mac, int relay, bool state, int remainingTime, const String& name));
    
    /**
     * @brief Define callback para descoberta de dispositivo
     * @param callback Função a ser chamada
     */
    void setDeviceDiscoveryCallback(void (*callback)(const uint8_t* mac, const String& name, const String& type, bool operational));
    
    /**
     * @brief Define callback para erro de comunicação
     * @param callback Função a ser chamada
     */
    void setErrorCallback(void (*callback)(const String& error));
    
    /**
     * @brief Define callback para ping/pong recebido
     * @param callback Função a ser chamada quando receber resposta de ping
     */
    void setPingCallback(void (*callback)(const uint8_t* senderMac));
    
    /**
     * @brief Define callback para QUALQUER mensagem recebida
     * @param callback Função a ser chamada com (mac, data, len) de qualquer mensagem
     * 
     * Este callback é útil para sistemas de discovery automático (MultiChannelDiscovery)
     * que precisam ser notificados sobre TODAS as mensagens recebidas, independente do tipo.
     */
    void setMessageReceivedCallback(void (*callback)(const uint8_t* mac, const uint8_t* data, int len));
    
    // ===== UTILITÁRIOS =====
    
    /**
     * @brief Converte MAC para string
     * @param mac Array de 6 bytes
     * @return String formatada XX:XX:XX:XX:XX:XX
     */
    static String macToString(const uint8_t* mac);
    
    /**
     * @brief Converte string para MAC
     * @param macStr String no formato XX:XX:XX:XX:XX:XX
     * @param mac Array de 6 bytes para resultado
     * @return true se conversão foi bem sucedida
     */
    static bool stringToMac(const String& macStr, uint8_t* mac);
    
    /**
     * @brief Obtém MAC local como string
     * @return String com MAC local
     */
    String getLocalMacString();
    
    /**
     * @brief Obtém instância do ESPNowController
     * @return Ponteiro para ESPNowController
     */
    ESPNowController* getESPNowController();
    
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
     * @brief Verifica se sistema está inicializado
     * @return true se ESP-NOW está funcionando
     */
    bool isInitialized() { return espNowController && espNowController->isInitialized(); }

    // ===== NOVOS MÉTODOS FASE 2 =====
    
    /**
     * @brief Obtém lista de peers do ESPNowController
     * @return Vector com informações dos peers
     */
    std::vector<PeerInfo> getPeerList();
    
    /**
     * @brief Obtém estatísticas detalhadas do ESPNowController
     * @return String JSON com estatísticas completas
     */
    String getDetailedStatsJSON();
    
    /**
     * @brief Força descoberta de dispositivos
     * @return true se broadcast foi enviado
     */
    bool forceDiscovery();
    
    /**
     * @brief Inicia handshake bidirecional com dispositivo
     * @param targetMac MAC address do dispositivo alvo
     * @return true se handshake foi iniciado
     */
    bool initiateHandshake(const uint8_t* targetMac);
    
    /**
     * @brief Solicita verificação de conectividade
     * @param targetMac MAC address do dispositivo alvo
     * @return true se solicitação foi enviada
     */
    bool requestConnectivityCheck(const uint8_t* targetMac);
    
    /**
     * @brief Envia relatório de conectividade
     * @param targetMac MAC address do destinatário (nullptr para broadcast)
     * @param sessionId ID da sessão
     * @return true se relatório foi enviado
     */
    bool sendConnectivityReport(const uint8_t* targetMac, uint32_t sessionId);
    
    /**
     * @brief Gera ID único de sessão
     * @return ID de sessão único
     */
    uint32_t generateSessionId();
    
    /**
     * @brief Ativa escuta em múltiplos canais para receber mensagens
     * Útil quando Master e Slave estão em canais diferentes
     */
    void enableMultiChannelListening();

private:
    RelayCommandBox* localRelayController;
    int wifiChannel;
    bool initialized;
    uint32_t messageCounter;
    
    // Estatísticas
    uint32_t messagesSent;
    uint32_t messagesReceived;
    uint32_t messagesLost;
    
    // Lista de dispositivos remotos (compatibilidade)
    std::vector<RemoteDevice> remoteDevices;
    
    // ESPNowController integrado (FASE 2)
    ESPNowController* espNowController;
    
    // Callbacks
    void (*remoteRelayStatusCallback)(const uint8_t* mac, int relay, bool state, int remainingTime, const String& name) = nullptr;
    void (*deviceDiscoveryCallback)(const uint8_t* mac, const String& name, const String& type, bool operational) = nullptr;
    void (*errorCallback)(const String& error) = nullptr;
    void (*messageReceivedCallback)(const uint8_t* mac, const uint8_t* data, int len) = nullptr;
    
    // ===== MÉTODOS PRIVADOS =====
    
    /**
     * @brief Envia mensagem ESP-NOW (compatibilidade)
     * @param message Estrutura da mensagem
     * @param targetMac MAC de destino
     * @return true se enviado
     */
    bool sendMessage(const ESPNowMessage& message, const uint8_t* targetMac);
    
    /**
     * @brief Processa mensagem recebida (compatibilidade)
     * @param message Mensagem recebida
     * @param senderMac MAC do remetente
     */
    void processReceivedMessage(const ESPNowMessage& message, const uint8_t* senderMac);
    
    /**
     * @brief Calcula checksum da mensagem
     * @param message Mensagem para calcular
     * @return Valor do checksum
     */
    uint8_t calculateChecksum(const ESPNowMessage& message);
    
    /**
     * @brief Valida mensagem recebida
     * @param message Mensagem para validar
     * @return true se válida
     */
    bool validateMessage(const ESPNowMessage& message);
    
    /**
     * @brief Atualiza informações do dispositivo remoto
     * @param mac MAC do dispositivo
     * @param name Nome do dispositivo
     * @param deviceType Tipo do dispositivo
     * @param operational Status operacional
     */
    void updateRemoteDevice(const uint8_t* mac, const String& name, const String& deviceType, bool operational);
    
    /**
     * @brief Remove dispositivos offline
     */
    void cleanupOfflineDevices();
    
    // ===== CALLBACKS DO ESPNOWCONTROLLER =====
    
    /**
     * @brief Callback para comandos de relé recebidos via ESPNowController
     */
    void onRelayCommandReceived(const uint8_t* senderMac, int relayNumber, const String& action, int duration);
    
    /**
     * @brief Callback para status de relé recebido via ESPNowController
     */
    void onRelayStatusReceived(const uint8_t* senderMac, int relayNumber, bool state, bool hasTimer, int remainingTime, const String& name);
    
    /**
     * @brief Callback para informações de dispositivo recebidas via ESPNowController
     */
    void onDeviceInfoReceived(const uint8_t* senderMac, const String& deviceName, const String& deviceType, uint8_t numRelays, bool operational, uint8_t wifiChannel);
    
    /**
     * @brief Callback para ping recebido via ESPNowController
     */
    void onPingReceived(const uint8_t* senderMac);
    
    /**
     * @brief Callback para credenciais WiFi recebidas via ESPNowController
     */
    void onWiFiCredentialsReceived(const String& ssid, const String& password, uint8_t channel);
    
    /**
     * @brief Callback para erro do ESPNowController
     */
    void onErrorReceived(const String& error);
    
    /**
     * @brief Callback estático para recebimento de dados (compatibilidade)
     */
    static void onDataReceived(const uint8_t* mac, const uint8_t* incomingData, int len);
    
    /**
     * @brief Callback estático para confirmação de envio (compatibilidade)
     */
    static void onDataSent(const uint8_t* mac_addr, esp_now_send_status_t status);
    
    // ===== CALLBACKS ESTÁTICOS PARA ESPNOWCONTROLLER =====
    
    /**
     * @brief Callback estático para comando de relé recebido
     */
    static void onRelayCommandReceivedStatic(const uint8_t* senderMac, int relayNumber, const String& action, int duration);
    
    /**
     * @brief Callback estático para status de relé recebido
     */
    static void onRelayStatusReceivedStatic(const uint8_t* senderMac, int relayNumber, bool state, bool hasTimer, int remainingTime, const String& name);
    
    /**
     * @brief Callback estático para informações de dispositivo recebidas
     */
    static void onDeviceInfoReceivedStatic(const uint8_t* senderMac, const String& deviceName, const String& deviceType, uint8_t numRelays, bool operational, uint8_t wifiChannel);
    
    /**
     * @brief Callback estático para ping recebido
     */
    static void onPingReceivedStatic(const uint8_t* senderMac);
    
    /**
     * @brief Callback estático para credenciais WiFi recebidas
     */
    static void onWiFiCredentialsReceivedStatic(const String& ssid, const String& password, uint8_t channel);
    
    /**
     * @brief Callback estático para erro recebido
     */
    static void onErrorReceivedStatic(const String& error);
    
    // Instância estática para callbacks
    static ESPNowBridge* instance;
};

#endif // ESPNOW_BRIDGE_H