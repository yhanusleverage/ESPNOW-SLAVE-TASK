#ifndef DATA_TYPES_H
#define DATA_TYPES_H

#include <Arduino.h>
#include "Config.h"

/**
 * @brief Configurações para cada relé
 * Define o comportamento e limites de cada relé
 */
struct RelayConfig {
    bool autoMode;                       // Se true, permite controle automático baseado em sensores
    uint32_t maxDuration;               // Duração máxima em segundos que o relé pode ficar ligado
    bool safetyLock;                    // Se true, requer confirmação para operações críticas

    /**
     * @brief Valida as configurações do relé
     * @return true se a configuração é válida
     */
    bool isValid() const {
        // Verifica duração máxima (24 horas)
        if (maxDuration > 86400) return false;
        
        // Duração mínima (1 segundo)
        if (maxDuration < 1) return false;
        
        // Se modo automático está ativo, safetyLock deve estar ativo para relés críticos
        if (autoMode && !safetyLock && maxDuration > 3600) return false;
        
        return true;
    }

    /**
     * @brief Retorna uma mensagem de erro se a configuração for inválida
     * @return String vazia se válido, ou mensagem de erro
     */
    String getValidationError() const {
        if (maxDuration > 86400) 
            return "Duração máxima excede 24 horas";
        if (maxDuration < 1) 
            return "Duração mínima deve ser 1 segundo";
        if (autoMode && !safetyLock && maxDuration > 3600)
            return "Relés com duração > 1h precisam de trava de segurança no modo automático";
        return "";
    }
};

// Nomes dos relés para 8 relés (Sistema Master ESP-NOW)
static const char* const RELAY_NAMES[MAX_RELAYS] = {
    "Bomba Principal",
    "Luzes LED", 
    "Ventilador",
    "Aquecedor",
    "Solenoide 1",
    "Solenoide 2", 
    "Alarme",
    "Reserva"
};

// Configurações padrão dos relés para 8 relés
static const RelayConfig RELAY_CONFIGS[MAX_RELAYS] = {
    {true, 3600, true},   // Bomba Principal: 1h max, com trava
    {true, 43200, false}, // Luzes LED: 12h max, sem trava
    {true, 7200, false},  // Ventilador: 2h max, sem trava
    {true, 3600, true},   // Aquecedor: 1h max, com trava
    {true, 300, true},    // Solenoide 1: 5min max, com trava
    {true, 300, true},    // Solenoide 2: 5min max, com trava
    {true, 60, false},    // Alarme: 1min max, sem trava
    {true, 3600, true}    // Reserva: 1h max, com trava
};

// Verificações em tempo de compilação
static_assert(sizeof(RELAY_NAMES)/sizeof(RELAY_NAMES[0]) == MAX_RELAYS, 
              "RELAY_NAMES deve ter exatamente MAX_RELAYS elementos");
static_assert(sizeof(RELAY_CONFIGS)/sizeof(RELAY_CONFIGS[0]) == MAX_RELAYS, 
              "RELAY_CONFIGS deve ter exatamente MAX_RELAYS elementos");

/**
 * @brief Estrutura para armazenar dados dos sensores
 * Todos os valores são inicializados com valores seguros
 */
struct SensorData {
    float environmentTemp = 0.0;     // Temperatura ambiente em °C
    float environmentHumidity = 0.0; // Umidade ambiente em %
    float waterTemp = 0.0;          // Temperatura da água em °C
    float ph = 7.0;                 // pH da água (0-14)
    float tds = 0.0;               // Total de sólidos dissolvidos em ppm
    bool waterLevelOk = false;     // Status do nível da água
    unsigned long timestamp = 0;    // Timestamp da última leitura
    bool valid = false;            // Indica se os dados são válidos

    // Validação de dados
    bool isValid() const {
        return environmentTemp >= MIN_TEMP && environmentTemp <= MAX_TEMP &&
               environmentHumidity >= MIN_HUMIDITY && environmentHumidity <= MAX_HUMIDITY &&
               waterTemp >= MIN_TEMP && waterTemp <= MAX_TEMP &&
               ph >= MIN_PH && ph <= MAX_PH &&
               tds >= MIN_TDS && tds <= MAX_TDS;
    }
};

/**
 * @brief Estrutura para armazenar status do sistema
 * Monitora o estado geral do sistema
 */
struct SystemStatus {
    bool wifiConnected = false;           // Status da conexão WiFi
    bool apiConnected = false;            // Status da conexão com a API
    bool sensorsOk = false;               // Status geral dos sensores
    bool relaysOk = false;                // Status dos relés
    unsigned long uptime = 0;             // Tempo de execução em ms
    uint32_t freeHeap = 0;               // Memória heap livre
    int wifiRSSI = 0;                    // Força do sinal WiFi em dBm
    String lastError = "";               // Última mensagem de erro

    // Validação de status
    bool isHealthy() const {
        return wifiConnected && apiConnected && sensorsOk && relaysOk && freeHeap > 10000;
    }
};

struct RelayState {
    bool isOn = false;
    unsigned long startTime = 0;
    int timerSeconds = 0;
    bool hasTimer = false;
    String name = "";  // Nome do relé
    RelayConfig config;  // Incorporar configuração
};

#endif // DATA_TYPES_H 