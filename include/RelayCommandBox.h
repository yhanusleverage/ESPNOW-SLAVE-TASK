#ifndef RELAY_COMMAND_BOX_H
#define RELAY_COMMAND_BOX_H

#include <Arduino.h>
#include <Wire.h>
#include <PCF8574.h>
#include <ArduinoJson.h>
#include "DataTypes.h"
#include "ESPNowTypes.h"  // Para PersistentRelayStateData

// Incluir configurações se disponível
#ifdef CONFIG_H
    // Config.h já incluído
#else
    // Definições padrão se Config.h não estiver disponível
    #ifndef NUM_RELAYS
        #define NUM_RELAYS 8
    #endif
    #ifndef MAX_RELAY_DURATION
        #define MAX_RELAY_DURATION 3600
    #endif
    #ifndef DEFAULT_RELAY_NAMES
        static const char* const FALLBACK_RELAY_NAMES[8] = {
            "Relé 0", "Relé 1", "Relé 2", "Relé 3",
            "Relé 4", "Relé 5", "Relé 6", "Relé 7"
        };
        #define DEFAULT_RELAY_NAMES FALLBACK_RELAY_NAMES
    #endif
#endif

// RelayState definido em DataTypes.h - usar definição centralizada

// RelayCommand definido em SupabaseClient.h - usar definição centralizada

/**
 * @brief Classe para controle de caixa de comandos com relés 8 canais via PCF8574
 * Baseada na implementação do projeto ESP-HIDROWAVE
 */
class RelayCommandBox {
public:
    /**
     * @brief Construtor da classe
     * @param pcf8574Address Endereço I2C do PCF8574 (padrão: 0x20)
     * @param deviceName Nome identificador do dispositivo
     */
    RelayCommandBox(uint8_t pcf8574Address = 0x20, const String& deviceName = "RelayBox");
    
    /**
     * @brief Destrutor - limpa ponteiro PCF8574
     */
    ~RelayCommandBox();
    
    /**
     * @brief Inicializa o sistema de relés e I2C
     * @return true se inicialização foi bem sucedida
     */
    bool begin();
    
    /**
     * @brief Atualiza timers e estados (chamar no loop principal)
     */
    void update();
    
    // ===== CONTROLE DE RELÉS =====
    
    /**
     * @brief Liga ou desliga um relé específico
     * @param relayNumber Número do relé (0-7)
     * @param state Estado desejado (true = ligar, false = desligar)
     * @return true se comando foi executado com sucesso
     */
    bool setRelay(int relayNumber, bool state);
    
    /**
     * @brief Liga ou desliga um relé com timer
     * @param relayNumber Número do relé (0-7)
     * @param state Estado desejado
     * @param seconds Duração em segundos
     * @return true se comando foi executado com sucesso
     */
    bool setRelayWithTimer(int relayNumber, bool state, int seconds);
    
    /**
     * @brief Alterna o estado de um relé
     * @param relayNumber Número do relé (0-7)
     * @return true se comando foi executado com sucesso
     */
    bool toggleRelay(int relayNumber);
    
    /**
     * @brief Processa comando de relé (compatível com ESP-NOW)
     * @param relayNumber Número do relé (0-7)
     * @param action Ação: "on", "off", "toggle"
     * @param duration Duração em segundos (0 = sem timer)
     * @return true se comando foi processado com sucesso
     */
    bool processCommand(int relayNumber, String action, int duration = 0);
    
    /**
     * @brief Desliga todos os relés
     */
    void turnOffAllRelays();
    
    // ===== 🎯 PERSISTÊNCIA DE ESTADOS (NVS) =====
    
    /**
     * @brief Guarda estados persistentes em NVS
     * @return true se guardado com sucesso
     */
    bool savePersistentStates();
    
    /**
     * @brief Carrega estados persistentes de NVS e aplica
     * @return true se carregado e aplicado com sucesso
     */
    bool loadPersistentStates();
    
    /**
     * @brief Aplica estados persistentes recebidos via ESP-NOW
     * @param states Estados persistentes recebidos
     * @return true se aplicado com sucesso
     */
    bool applyPersistentStates(const PersistentRelayStateData& states);
    
    /**
     * @brief Obtém estados persistentes atuais
     * @param states Estrutura para preencher com estados atuais
     * @return true se obtido com sucesso
     */
    bool getPersistentStates(PersistentRelayStateData& states);
    
    // ===== GETTERS =====
    
    /**
     * @brief Obtém o estado atual de um relé
     * @param relayNumber Número do relé (0-7)
     * @return Estado atual do relé
     */
    bool getRelayState(int relayNumber);
    
    /**
     * @brief Obtém todos os estados dos relés
     * @return Ponteiro para array de estados
     */
    RelayState* getAllStates() { return relayStates; }
    
    /**
     * @brief Obtém tempo restante do timer de um relé
     * @param relayNumber Número do relé (0-7)
     * @return Segundos restantes (0 se não tem timer)
     */
    int getRemainingTime(int relayNumber);
    
    /**
     * @brief Obtém o nome de um relé
     * @param relayNumber Número do relé (0-7)
     * @return Nome do relé
     */
    String getRelayName(int relayNumber);
    
    /**
     * @brief Define o nome de um relé
     * @param relayNumber Número do relé (0-7)
     * @param name Nome do relé
     */
    void setRelayName(int relayNumber, const String& name);
    
    // ===== STATUS E DEBUG =====
    
    /**
     * @brief Verifica se o sistema está operacional
     * @return true se PCF8574 está funcionando
     */
    bool isOperational() { return pcfInitialized; }
    
    /**
     * @brief Imprime status de todos os relés no Serial
     */
    void printStatus();
    
    /**
     * @brief Obtém status em formato JSON
     * @return String JSON com status de todos os relés
     */
    String getStatusJSON();
    
    /**
     * @brief Obtém informações do dispositivo em JSON
     * @return String JSON com informações do dispositivo
     */
    String getDeviceInfoJSON();
    
    // ===== CALLBACKS =====
    
    /**
     * @brief Define callback para mudanças de estado
     * @param callback Função a ser chamada quando um relé muda de estado
     */
    void setStateChangeCallback(void (*callback)(int relayNumber, bool state, int remainingTime));
    
    /**
     * @brief Define callback para comandos recebidos
     * @param callback Função a ser chamada quando um comando é recebido
     */
    void setCommandCallback(void (*callback)(int relayNumber, String action, int duration));

private:
    static const int RELAY_COUNT = 8;           // Número de relés do PCF8574
    static const int DEFAULT_MAX_DURATION = 3600; // Duração máxima padrão (1 hora)
    
    PCF8574* pcf8574;                        // Ponteiro para PCF8574 (como no teste do usuário)
    uint8_t i2cAddress;                       // Endereço I2C do PCF8574
    String deviceName;                        // Nome do dispositivo
    bool pcfInitialized;                      // Status de inicialização
    
    RelayState relayStates[8];       // Estados dos relés (8 relés)
    
    // Callbacks
    void (*stateChangeCallback)(int relayNumber, bool state, int remainingTime) = nullptr;
    void (*commandCallback)(int relayNumber, String action, int duration) = nullptr;
    
    // ===== MÉTODOS PRIVADOS =====
    
    /**
     * @brief Escreve estado físico no PCF8574
     * @param relayNumber Número do relé (0-7)
     * @param state Estado desejado
     * @return true se escrita foi bem sucedida
     */
    bool writeToRelay(int relayNumber, bool state);
    
    /**
     * @brief Verifica e processa timers ativos
     */
    void checkTimers();
    
    /**
     * @brief Valida número do relé
     * @param relayNumber Número do relé a validar
     * @return true se número é válido (0-7)
     */
    bool isValidRelayNumber(int relayNumber);
    
    /**
     * @brief Inicializa nomes padrão dos relés
     */
    void initializeDefaultNames();
    
    /**
     * @brief Escaneia e inicializa PCF8574 dinamicamente
     * @return true se PCF8574 foi encontrado e inicializado
     */
    bool scanAndInitializePCF8574();
    
    /**
     * @brief Testa comunicação I2C com o dispositivo
     * @return true se dispositivo responde
     */
    bool testI2CCommunication();
};

#endif // RELAY_COMMAND_BOX_H
