#ifndef SAVE_MANAGER_H
#define SAVE_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include <vector>
#include <ArduinoJson.h>
#include "Config.h"
#include "ESPNowController.h"  // Para PeerInfo

/**
 * @brief Gerenciador de configurações persistentes
 * Usa Preferences para salvar/carregar configurações
 */
class SaveManager {
public:
    /**
     * @brief Construtor
     */
    SaveManager();
    
    /**
     * @brief Inicializa o sistema de persistência
     * @return true se inicialização foi bem sucedida
     */
    bool begin();
    
    /**
     * @brief Finaliza o sistema de persistência
     */
    void end();
    
    // ===== CONFIGURAÇÕES DE RELÉS =====
    
    /**
     * @brief Salva nome de um relé
     * @param relayNumber Número do relé (0-7)
     * @param name Nome do relé
     * @return true se salvou com sucesso
     */
    bool saveRelayName(int relayNumber, const String& name);
    
    /**
     * @brief Carrega nome de um relé
     * @param relayNumber Número do relé (0-7)
     * @return Nome do relé (vazio se não encontrado)
     */
    String loadRelayName(int relayNumber);
    
    /**
     * @brief Salva configurações de todos os relés
     * @param relayNames Array com nomes dos relés
     * @return true se salvou com sucesso
     */
    bool saveAllRelayNames(const String relayNames[8]);
    
    /**
     * @brief Carrega configurações de todos os relés
     * @param relayNames Array para armazenar nomes dos relés
     * @return true se carregou com sucesso
     */
    bool loadAllRelayNames(String relayNames[8]);
    
    // ===== CONFIGURAÇÕES DE PEERS =====
    
    /**
     * @brief Salva lista de peers conhecidos
     * @param peers Vector com informações dos peers
     * @return true se salvou com sucesso
     */
    bool saveKnownPeers(const std::vector<PeerInfo>& peers);
    
    /**
     * @brief Carrega lista de peers conhecidos
     * @param peers Vector para armazenar informações dos peers
     * @return true se carregou com sucesso
     */
    bool loadKnownPeers(std::vector<PeerInfo>& peers);
    
    /**
     * @brief Adiciona um peer à lista persistente
     * @param macAddress MAC address do peer
     * @param deviceName Nome do dispositivo
     * @param deviceType Tipo do dispositivo
     * @return true se adicionou com sucesso
     */
    bool addPeer(const uint8_t* macAddress, const String& deviceName, const String& deviceType);
    
    /**
     * @brief Remove um peer da lista persistente
     * @param macAddress MAC address do peer
     * @return true se removeu com sucesso
     */
    bool removePeer(const uint8_t* macAddress);
    
    // ===== CONFIGURAÇÕES DO SISTEMA =====
    
    /**
     * @brief Salva configurações do sistema
     * @param deviceName Nome do dispositivo
     * @param channel Canal WiFi
     * @param numRelays Número de relés
     * @return true se salvou com sucesso
     */
    bool saveSystemConfig(const String& deviceName, int channel, int numRelays);
    
    /**
     * @brief Carrega configurações do sistema
     * @param deviceName Nome do dispositivo (retorno)
     * @param channel Canal WiFi (retorno)
     * @param numRelays Número de relés (retorno)
     * @return true se carregou com sucesso
     */
    bool loadSystemConfig(String& deviceName, int& channel, int& numRelays);
    
    // ===== UTILITÁRIOS =====
    
    /**
     * @brief Limpa todas as configurações
     * @return true se limpou com sucesso
     */
    bool clearAll();
    
    /**
     * @brief Verifica se há configurações salvas
     * @return true se há configurações
     */
    bool hasConfig();
    
    /**
     * @brief Obtém estatísticas de uso
     * @return String JSON com estatísticas
     */
    String getStats();

private:
    Preferences preferences;
    bool initialized;
    
    /**
     * @brief Converte MAC address para string para armazenamento
     * @param mac MAC address
     * @return String formatada
     */
    String macToString(const uint8_t* mac);
    
    /**
     * @brief Converte string para MAC address
     * @param macStr String formatada
     * @param mac MAC address (retorno)
     * @return true se conversão foi bem sucedida
     */
    bool stringToMac(const String& macStr, uint8_t* mac);
};

#endif // SAVE_MANAGER_H
