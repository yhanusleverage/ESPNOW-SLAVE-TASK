#ifndef RELAY_CONTROLLER_H
#define RELAY_CONTROLLER_H

#include <Arduino.h>
#include <Wire.h>
#include <ArduinoJson.h>

// Incluir Config.h APÓS Arduino.h para ter acesso aos tipos
#include "Config.h"

/**
 * @brief Estrutura para armazenar estado de cada relé
 */
struct RelayState {
    bool state;           // Estado atual (true = ligado, false = desligado)
    unsigned long startTime;  // Timestamp quando foi ligado
    int duration;        // Duração em segundos (0 = permanente)
    int remainingTime;   // Tempo restante em segundos
    String name;          // Nome descritivo do relé
};

/**
 * @brief Classe para controle de múltiplos relés via PCF8574
 * Suporta até 16 relés (2 PCF8574)
 */
class RelayController {
public:
    /**
     * @brief Construtor
     * @param pcf1Address Endereço I2C do primeiro PCF8574 (padrão: 0x20)
     * @param pcf2Address Endereço I2C do segundo PCF8574 (padrão: 0x21)
     */
    RelayController(uint8_t pcf1Address = 0x20, uint8_t pcf2Address = 0x21);
    
    /**
     * @brief Inicializa o controlador de relés
     * @return true se inicialização foi bem-sucedida
     */
    bool begin();
    
    /**
     * @brief Atualiza estados dos relés (deve ser chamado no loop)
     */
    void update();
    
    /**
     * @brief Processa comando de relé
     * @param relayNumber Número do relé (0-15)
     * @param action Ação ("on", "off", "toggle")
     * @param duration Duração em segundos (0 = permanente)
     * @return true se comando foi executado com sucesso
     */
    bool processCommand(int relayNumber, const String& action, int duration = 0);
    
    /**
     * @brief Liga/desliga relé
     * @param relayNumber Número do relé (0-15)
     * @param state true para ligar, false para desligar
     * @return true se operação foi bem-sucedida
     */
    bool setRelay(int relayNumber, bool state);
    
    /**
     * @brief Liga relé com timer
     * @param relayNumber Número do relé (0-15)
     * @param state true para ligar, false para desligar
     * @param duration Duração em segundos
     * @return true se operação foi bem-sucedida
     */
    bool setRelayWithTimer(int relayNumber, bool state, int duration);
    
    /**
     * @brief Alterna estado do relé
     * @param relayNumber Número do relé (0-15)
     * @return true se operação foi bem-sucedida
     */
    bool toggleRelay(int relayNumber);
    
    /**
     * @brief Desliga todos os relés
     */
    void turnOffAllRelays();
    
    /**
     * @brief Obtém estado do relé
     * @param relayNumber Número do relé (0-15)
     * @return true se ligado, false se desligado
     */
    bool getRelayState(int relayNumber);
    
    /**
     * @brief Obtém tempo restante do relé
     * @param relayNumber Número do relé (0-15)
     * @return Tempo restante em segundos
     */
    int getRemainingTime(int relayNumber);
    
    /**
     * @brief Obtém nome do relé
     * @param relayNumber Número do relé (0-15)
     * @return Nome do relé
     */
    String getRelayName(int relayNumber);
    
    /**
     * @brief Define nome do relé
     * @param relayNumber Número do relé (0-15)
     * @param name Nome do relé
     */
    void setRelayName(int relayNumber, const String& name);
    
    /**
     * @brief Verifica se o sistema está operacional
     * @return true se operacional
     */
    bool isOperational();
    
    /**
     * @brief Define callback para mudanças de estado
     * @param callback Função callback
     */
    void setStateChangeCallback(void (*callback)(int relayNumber, bool state, int remainingTime));
    
    /**
     * @brief Obtém status JSON de todos os relés
     * @return String JSON com status
     */
    String getStatusJSON();
    
    /**
     * @brief Imprime status de todos os relés
     */
    void printStatus();

private:
    uint8_t _pcf1Address;
    uint8_t _pcf2Address;
    RelayState _relayStates[16];  // 16 relés
    bool _initialized;
    bool _pcf1Connected;
    bool _pcf2Connected;
    void (*_stateChangeCallback)(int relayNumber, bool state, int remainingTime);
    
    /**
     * @brief Atualiza estado de um relé específico
     * @param relayNumber Número do relé
     */
    void updateRelayState(int relayNumber);
    
    /**
     * @brief Verifica se número do relé é válido
     * @param relayNumber Número do relé
     * @return true se válido
     */
    bool isValidRelay(int relayNumber);
    
    /**
     * @brief Obtém endereço I2C do PCF8574 para o relé
     * @param relayNumber Número do relé
     * @return Endereço I2C
     */
    uint8_t getPCFAddress(int relayNumber);
    
    /**
     * @brief Obtém pino do PCF8574 para o relé
     * @param relayNumber Número do relé
     * @return Pino (0-7)
     */
    uint8_t getPCFPin(int relayNumber);
    
    /**
     * @brief Inicializa nomes padrão dos relés
     */
    void initializeDefaultNames();
};

#endif // RELAY_CONTROLLER_H
