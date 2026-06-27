/**
 * @file MultiChannelDiscovery.h
 * @brief Discovery Automático Multi-Canal para ESP-NOW
 * @version 1.0.0
 * @date 2025-10-29
 * 
 * @details
 * Sistema profissional de discovery automático que:
 * - Varre canais WiFi 1-13 procurando Master
 * - Prioriza canais comuns (1, 6, 11)
 * - Salva último canal em NVS (cache)
 * - Re-discovery automático se perder conexão
 * - Tempo típico: 300-1500ms
 * 
 * CRISP-DM Implementation:
 * - Business Understanding: Sistema deve funcionar em qualquer canal
 * - Data Understanding: ESP-NOW + WiFi compartilham hardware de rádio
 * - Data Preparation: Estruturas para cache e estado
 * - Modeling: Algoritmo de varredura otimizado
 * - Evaluation: Métricas de sucesso e performance
 * - Deployment: Integração transparente com código existente
 * 
 * @author ESP-HIDROWAVE Team
 * @copyright MIT License
 */

#ifndef MULTI_CHANNEL_DISCOVERY_H
#define MULTI_CHANNEL_DISCOVERY_H

#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <Preferences.h>
#include "ESPNowController.h"

// ===== CONFIGURAÇÕES =====
#define MCD_MIN_CHANNEL 1                    // Canal mínimo (mundial)
#define MCD_MAX_CHANNEL 13                   // Canal máximo (Europa/Ásia)
#define MCD_TIMEOUT_PER_CHANNEL 800          // ✅ Timeout por canal aumentado para 800ms
#define MCD_MAX_RETRY_ATTEMPTS 4             // ✅ 4 tentativas por canal (era 3)
#define MCD_PASSIVE_LISTEN_MS 45000          // Escuta passiva alinhada com SLAVE_REACHABLE_MS (45s)
#define MCD_CACHE_ENABLED true               // Usar cache NVS
#define MCD_NVS_NAMESPACE "mcd_cache"        // Namespace NVS
#define MCD_DEBUG_ENABLED true               // Logs detalhados

// ===== PRIORIDADE DE CANAIS =====
// ✅ Canais WiFi mais comuns + canal 9 (comum para WiFi 2.4GHz)
static const uint8_t MCD_PRIORITY_CHANNELS[] = {1, 6, 9, 11};
static const uint8_t MCD_PRIORITY_COUNT = 4;

/**
 * @brief Estrutura para cache de canal
 */
struct ChannelCache {
    uint8_t lastChannel;          // Último canal que funcionou
    uint32_t lastSuccess;         // Timestamp da última conexão bem-sucedida
    uint32_t usageCount;          // Quantas vezes este canal foi usado
    uint8_t successRate;          // Taxa de sucesso (0-100%)
    
    ChannelCache() : lastChannel(1), lastSuccess(0), usageCount(0), successRate(0) {}
};

/**
 * @brief Estrutura para estatísticas de discovery
 */
struct DiscoveryStats {
    uint32_t totalAttempts;       // Total de tentativas de discovery
    uint32_t successCount;        // Sucessos
    uint32_t failureCount;        // Falhas
    uint32_t averageTimeMs;       // Tempo médio de discovery (ms)
    uint32_t lastAttemptTime;     // Timestamp da última tentativa
    uint8_t lastChannelFound;     // Último canal onde Master foi encontrado
    
    DiscoveryStats() : totalAttempts(0), successCount(0), failureCount(0), 
                      averageTimeMs(0), lastAttemptTime(0), lastChannelFound(0) {}
};

/**
 * @brief Resultado do discovery
 */
enum class DiscoveryResult {
    SUCCESS,              // Master encontrado
    TIMEOUT,              // Timeout - Master não respondeu
    ERROR_ESP_NOW,        // Erro ao inicializar ESP-NOW
    ERROR_WIFI,           // Erro ao configurar WiFi/canal
    ABORTED,              // Discovery cancelado pelo usuário
    BLOCKED               // Scan bloqueado (canal master conhecido)
};

/** Delegates para ESPNowController (único dono do stack esp_now) */
typedef bool (*McdSendBroadcastFn)();
typedef bool (*McdChannelSyncFn)(uint8_t channel);
typedef bool (*McdRestoreChannelFn)(uint8_t channel);

/**
 * @brief Classe principal de Discovery Multi-Canal
 * 
 * @example
 * ```cpp
 * MultiChannelDiscovery discovery;
 * 
 * void setup() {
 *     if (discovery.begin()) {
 *         DiscoveryResult result = discovery.discoverMaster();
 *         
 *         if (result == DiscoveryResult::SUCCESS) {
 *             uint8_t channel = discovery.getCurrentChannel();
 *             Serial.printf("Master found on channel %d\n", channel);
 *         }
 *     }
 * }
 * ```
 */
class MultiChannelDiscovery {
public:
    /**
     * @brief Construtor
     */
    MultiChannelDiscovery();
    
    /**
     * @brief Destrutor
     */
    ~MultiChannelDiscovery();
    
    // ===== INICIALIZAÇÃO =====
    
    /**
     * @brief Inicializa sistema de discovery
     * @return true se inicialização bem-sucedida
     */
    bool begin();
    
    /**
     * @brief Para sistema de discovery
     */
    void end();
    
    // ===== DISCOVERY PRINCIPAL =====
    
    /**
     * @brief Executa discovery completo em todos os canais
     * 
     * Processo:
     * 1. Tenta canal do cache (se disponível)
     * 2. Tenta canais prioritários (1, 6, 11)
     * 3. Varre todos os outros canais (2-5, 7-10, 12-13)
     * 4. Salva canal encontrado no cache
     * 
     * @return DiscoveryResult com resultado da operação
     */
    DiscoveryResult discoverMaster();
    
    /**
     * @brief Tenta discovery em canal específico
     * @param channel Canal a testar (1-13)
     * @param timeout Timeout em ms (padrão: MCD_TIMEOUT_PER_CHANNEL)
     * @return true se Master respondeu neste canal
     */
    bool tryChannel(uint8_t channel, uint32_t timeout = MCD_TIMEOUT_PER_CHANNEL);
    
    /**
     * @brief Re-discovery se conexão for perdida
     * @param quickScan Se true, tenta apenas canal anterior e prioritários
     * @return DiscoveryResult com resultado
     */
    DiscoveryResult rediscoverMaster(bool quickScan = true);
    
    // ===== CALLBACKS =====
    
    /**
     * @brief Define callback para mensagem recebida do Master
     * @param callback Função a ser chamada quando Master responder
     */
    void setMasterFoundCallback(void (*callback)(uint8_t channel, const uint8_t* masterMac));
    
    /**
     * @brief Define callback para progresso do discovery
     * @param callback Função chamada a cada canal testado
     */
    void setProgressCallback(void (*callback)(uint8_t channel, uint8_t totalChannels));

    /**
     * @brief Delegates para envio/sincronização via ESPNowController (sem esp_now_init aqui)
     */
    void setSendBroadcastDelegate(McdSendBroadcastFn fn);
    void setChannelSyncDelegate(McdChannelSyncFn fn);
    void setRestoreChannelDelegate(McdRestoreChannelFn fn);

    /**
     * @brief Bloqueia scan multi-canal após canal master conhecido
     */
    void lockMasterChannel(bool locked);
    bool isMasterChannelLocked() const { return masterChannelLocked; }

    /**
     * @brief Escuta passiva no canal NVS antes de scan ativo
     */
    bool passiveListen(uint8_t channel, uint32_t timeoutMs = MCD_PASSIVE_LISTEN_MS);
    
    // ===== GETTERS =====
    
    /**
     * @brief Obtém canal atual
     * @return Canal ESP-NOW atual (1-13)
     */
    uint8_t getCurrentChannel() const { return currentChannel; }
    
    /**
     * @brief Verifica se Master foi encontrado
     * @return true se Master está conectado
     */
    bool hasMaster() const { return masterFound; }
    
    /**
     * @brief Obtém MAC do Master
     * @param mac Array para armazenar MAC (6 bytes)
     * @return true se Master conhecido
     */
    bool getMasterMac(uint8_t* mac) const;
    
    /**
     * @brief Obtém estatísticas de discovery
     * @return Estrutura com estatísticas
     */
    DiscoveryStats getStats() const { return stats; }
    
    /**
     * @brief Obtém cache de canal
     * @return Estrutura com dados do cache
     */
    ChannelCache getCache() const { return cache; }
    
    // ===== CONTROLE =====
    
    /**
     * @brief Aborta discovery em andamento
     */
    void abortDiscovery() { abortFlag = true; }
    
    /**
     * @brief Reseta estatísticas
     */
    void resetStats();
    
    /**
     * @brief Limpa cache NVS
     */
    void clearCache();
    
    /**
     * @brief Força salvar cache atual
     */
    void saveCache();
    
    /**
     * @brief Mostra conteúdo do cache NVS
     */
    void printCache() const;
    
    // ===== UTILITÁRIOS =====
    
    /**
     * @brief Converte DiscoveryResult para string
     * @param result Resultado do discovery
     * @return String descritiva
     */
    static String resultToString(DiscoveryResult result);
    
    /**
     * @brief Imprime estatísticas no Serial
     */
    void printStats() const;
    
    /**
     * @brief Obtém JSON com estatísticas
     * @return String JSON
     */
    String getStatsJSON() const;
    
    /**
     * @brief Processa mensagem recebida (callback ESP-NOW)
     * @param mac MAC do remetente
     * @param data Dados recebidos
     * @param len Tamanho dos dados
     */
    void handleReceivedMessage(const uint8_t* mac, const uint8_t* data, int len);

private:
    // ===== VARIÁVEIS PRIVADAS =====
    
    bool initialized;                 // Sistema inicializado
    bool masterChannelLocked;         // Scan bloqueado após sync
    uint8_t currentChannel;           // Canal atual
    uint8_t listenChannel;            // Canal de escuta passiva
    bool masterFound;                 // Master encontrado
    uint8_t masterMac[6];            // MAC do Master
    ChannelCache cache;               // Cache do canal
    DiscoveryStats stats;             // Estatísticas
    Preferences prefs;                // Acesso NVS
    bool abortFlag;                   // Flag para abortar
    
    // 🚨 PROTEÇÃO: Throttle para callback (evitar chamadas excessivas)
    unsigned long lastCallbackTime;   // Última vez que callback foi chamado
    uint8_t lastCallbackChannel;      // Último canal do callback
    static const unsigned long CALLBACK_THROTTLE_MS = 2000; // Máximo 1 callback a cada 2 segundos
    
    // Callbacks
    void (*masterFoundCallback)(uint8_t channel, const uint8_t* masterMac);
    void (*progressCallback)(uint8_t channel, uint8_t totalChannels);
    McdSendBroadcastFn sendBroadcastDelegate;
    McdChannelSyncFn channelSyncDelegate;
    McdRestoreChannelFn restoreChannelDelegate;
    
    // ===== MÉTODOS PRIVADOS =====
    
    /**
     * @brief Carrega cache do NVS
     * @return true se cache válido foi carregado
     */
    bool loadCache();
    
    /**
     * @brief Salva cache no NVS
     * @return true se salvo com sucesso
     */
    bool saveCacheInternal();
    
    /**
     * @brief Configura canal WiFi/ESP-NOW
     * @param channel Canal a configurar (1-13)
     * @return true se configurado com sucesso
     */
    bool setChannel(uint8_t channel);
    
    /**
     * @brief Envia discovery broadcast
     * @return true se enviado
     */
    bool sendDiscoveryBroadcast();
    
    /**
     * @brief Aguarda resposta do Master
     * @param timeout Timeout em ms
     * @return true se Master respondeu
     */
    bool waitForMasterResponse(uint32_t timeout);
    
    /**
     * @brief Atualiza estatísticas
     * @param success Se discovery teve sucesso
     * @param timeMs Tempo gasto (ms)
     * @param channelFound Canal encontrado (0 se não encontrou)
     */
    void updateStats(bool success, uint32_t timeMs, uint8_t channelFound);

    void restoreAfterFailedScan();
};

#endif // MULTI_CHANNEL_DISCOVERY_H

