#ifndef PREFERENCES_MANAGER_H
#define PREFERENCES_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include "Config.h"

/**
 * @brief Gerenciador simplificado de preferências para projeto ESPNOW-CARGA
 * 
 * Versão simplificada focada em funcionalidades ESP-NOW essenciais.
 */
class PreferencesManager {
public:
    // ===== INICIALIZAÇÃO =====
    
    /**
     * @brief Inicializa o sistema de preferências
     * @return true se inicialização foi bem-sucedida
     */
    static bool begin();
    
    /**
     * @brief Finaliza o sistema de preferências
     */
    static void end();
    
    // ===== CREDENCIAIS WIFI =====
    
    /**
     * @brief Salva credenciais WiFi
     * @param ssid Nome da rede WiFi
     * @param password Senha da rede WiFi
     * @param channel Canal WiFi
     * @return true se salvamento foi bem-sucedido
     */
    static bool saveWiFiCredentials(const String& ssid, const String& password, uint8_t channel);
    
    /**
     * @brief Carrega credenciais WiFi
     * @param ssid Referência para armazenar SSID
     * @param password Referência para armazenar senha
     * @param channel Referência para armazenar canal
     * @return true se carregamento foi bem-sucedido
     */
    static bool loadWiFiCredentials(String& ssid, String& password, uint8_t& channel);
    
    /**
     * @brief Remove credenciais WiFi
     * @return true se remoção foi bem-sucedida
     */
    static bool clearWiFiCredentials();
    
    // ===== CONFIGURAÇÕES DE RELÉS =====
    
    /**
     * @brief Salva configuração de um relé
     * @param relayNumber Número do relé (0-7)
     * @param name Nome do relé
     * @param enabled Se o relé está habilitado
     * @return true se salvamento foi bem-sucedido
     */
    static bool saveRelayConfig(int relayNumber, const String& name, bool enabled);
    
    /**
     * @brief Carrega configuração de um relé
     * @param relayNumber Número do relé (0-7)
     * @param name Referência para armazenar nome
     * @param enabled Referência para armazenar status
     * @return true se carregamento foi bem-sucedido
     */
    static bool loadRelayConfig(int relayNumber, String& name, bool& enabled);
    
    // ===== CONFIGURAÇÕES DE DISPOSITIVO =====
    
    /**
     * @brief Salva configurações do dispositivo
     * @param deviceName Nome do dispositivo
     * @param deviceType Tipo do dispositivo
     * @param deviceMode Modo do dispositivo (Master/Slave/Standalone)
     * @return true se salvamento foi bem-sucedido
     */
    static bool saveDeviceConfig(const String& deviceName, const String& deviceType, const String& deviceMode);
    
    /**
     * @brief Carrega configurações do dispositivo
     * @param deviceName Referência para armazenar nome
     * @param deviceType Referência para armazenar tipo
     * @param deviceMode Referência para armazenar modo
     * @return true se carregamento foi bem-sucedido
     */
    static bool loadDeviceConfig(String& deviceName, String& deviceType, String& deviceMode);
    
    // ===== CONFIGURAÇÕES GERAIS =====
    
    /**
     * @brief Salva configuração geral
     * @param key Chave da configuração
     * @param value Valor da configuração
     * @return true se salvamento foi bem-sucedido
     */
    static bool saveConfig(const String& key, const String& value);
    
    /**
     * @brief Carrega configuração geral
     * @param key Chave da configuração
     * @param value Referência para armazenar valor
     * @return true se carregamento foi bem-sucedido
     */
    static bool loadConfig(const String& key, String& value);
    
    /**
     * @brief Salva configuração numérica
     * @param key Chave da configuração
     * @param value Valor numérico
     * @return true se salvamento foi bem-sucedido
     */
    static bool saveConfigInt(const String& key, int32_t value);
    
    /**
     * @brief Carrega configuração numérica
     * @param key Chave da configuração
     * @param value Referência para armazenar valor
     * @return true se carregamento foi bem-sucedido
     */
    static bool loadConfigInt(const String& key, int32_t& value);
    
    // ===== UTILITÁRIOS =====
    
    /**
     * @brief Verifica se uma configuração existe
     * @param key Chave da configuração
     * @return true se configuração existe
     */
    static bool configExists(const String& key);
    
    /**
     * @brief Remove uma configuração
     * @param key Chave da configuração
     * @return true se remoção foi bem-sucedida
     */
    static bool removeConfig(const String& key);
    
    /**
     * @brief Obtém estatísticas do armazenamento
     * @return String com estatísticas
     */
    static String getStats();
    
    /**
     * @brief Limpa todas as configurações
     * @return true se limpeza foi bem-sucedida
     */
    static bool clearAll();
    
private:
    static Preferences preferences;
    static bool initialized;
    
    /**
     * @brief Garante que o sistema está inicializado
     * @return true se inicializado
     */
    static bool ensureInitialized();
    
    /**
     * @brief Valida chave de configuração
     * @param key Chave a validar
     * @return true se chave é válida
     */
    static bool validateKey(const String& key);
    
    /**
     * @brief Valida valor de configuração
     * @param value Valor a validar
     * @return true se valor é válido
     */
    static bool validateValue(const String& value);
};

#endif // PREFERENCES_MANAGER_H
