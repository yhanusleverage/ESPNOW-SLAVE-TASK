/**
 * @file MultiChannelDiscovery.cpp
 * @brief Implementação do Discovery Automático Multi-Canal
 */

#include "MultiChannelDiscovery.h"

// ===== CONSTRUTOR / DESTRUTOR =====

MultiChannelDiscovery::MultiChannelDiscovery() 
    : initialized(false), masterChannelLocked(false), currentChannel(1), listenChannel(1),
      masterFound(false), abortFlag(false), lastCallbackTime(0), lastCallbackChannel(0),
      masterFoundCallback(nullptr), progressCallback(nullptr),
      sendBroadcastDelegate(nullptr), channelSyncDelegate(nullptr), restoreChannelDelegate(nullptr) {
    
    memset(masterMac, 0, 6);
}

MultiChannelDiscovery::~MultiChannelDiscovery() {
    end();
}

// ===== INICIALIZAÇÃO =====

bool MultiChannelDiscovery::begin() {
    if (initialized) {
        Serial.println("⚠️ MultiChannelDiscovery: Já inicializado");
        return true;
    }
    
    Serial.println("\n🔍 === INICIALIZANDO MULTI-CHANNEL DISCOVERY ===");
    Serial.println("   (lógica de scan — ESP-NOW gerido por ESPNowController)");
    Serial.println("================================================");
    
    // Carregar cache NVS (sem esp_now_init — único dono é ESPNowController)
    if (MCD_CACHE_ENABLED) {
        loadCache();
        listenChannel = cache.lastChannel > 0 ? cache.lastChannel : 1;
        currentChannel = listenChannel;
        Serial.println("✅ Cache carregado: Canal " + String(cache.lastChannel));
    } else {
        listenChannel = 1;
        currentChannel = 1;
    }
    
    if (stats.totalAttempts == 0) {
        resetStats();
    }
    
    initialized = true;
    Serial.println("✅ MultiChannelDiscovery inicializado (sem esp_now_init)");
    Serial.println("================================================\n");
    
    return true;
}

void MultiChannelDiscovery::end() {
    if (!initialized) return;
    
    if (MCD_CACHE_ENABLED) {
        saveCache();
    }
    
    initialized = false;
    Serial.println("🔍 MultiChannelDiscovery finalizado (stack ESP-NOW intacto)");
}

// ===== DISCOVERY PRINCIPAL =====

DiscoveryResult MultiChannelDiscovery::discoverMaster() {
    if (!initialized) {
        Serial.println("❌ Discovery: Sistema não inicializado");
        return DiscoveryResult::ERROR_ESP_NOW;
    }

    if (masterChannelLocked) {
        Serial.println("🔒 Scan bloqueado — canal master conhecido");
        return DiscoveryResult::BLOCKED;
    }
    
    Serial.println("\n🔍 === INICIANDO DISCOVERY MULTI-CANAL ===");
    Serial.println("==========================================");
    
    unsigned long startTime = millis();
    masterFound = false;
    abortFlag = false;
    lastCallbackTime = 0;
    lastCallbackChannel = 0;
    stats.totalAttempts++;

    // ===== FASE PASSIVA: escuta no canal NVS (30-60s) =====
    uint8_t passiveChannel = (cache.lastChannel >= MCD_MIN_CHANNEL && cache.lastChannel <= MCD_MAX_CHANNEL)
        ? cache.lastChannel : listenChannel;
    if (passiveChannel < MCD_MIN_CHANNEL) passiveChannel = 1;

    Serial.println("👂 Fase passiva: canal " + String(passiveChannel) +
                   " por " + String(MCD_PASSIVE_LISTEN_MS / 1000) + "s");
    if (passiveListen(passiveChannel, MCD_PASSIVE_LISTEN_MS)) {
        unsigned long elapsedTime = millis() - startTime;
        updateStats(true, elapsedTime, currentChannel);
        return DiscoveryResult::SUCCESS;
    }
    
    // ===== FASE 0: CANAL DO CACHE =====
    if (MCD_CACHE_ENABLED && cache.lastChannel > 0 && cache.successRate > 50) {
        Serial.println("📦 Fase 0: Tentando canal do cache");
        Serial.print("   Canal " + String(cache.lastChannel) + " (taxa: " + String(cache.successRate) + "%): ");
        
        if (tryChannel(cache.lastChannel, MCD_TIMEOUT_PER_CHANNEL)) {
            Serial.println("✅ MASTER ENCONTRADO!");
            unsigned long elapsedTime = millis() - startTime;
            updateStats(true, elapsedTime, cache.lastChannel);
            return DiscoveryResult::SUCCESS;
        }
        Serial.println("⚪ Sem resposta");
    }
    
    // ===== FASE 1: CANAIS PRIORITÁRIOS =====
    Serial.println("\n📡 Fase 1: Canais prioritários (1, 6, 9, 11)");
    
    for (uint8_t i = 0; i < MCD_PRIORITY_COUNT; i++) {
        if (abortFlag) {
            Serial.println("⚠️ Discovery abortado");
            return DiscoveryResult::ABORTED;
        }
        
        uint8_t channel = MCD_PRIORITY_CHANNELS[i];
        
        // Pular se já testado no cache
        if (MCD_CACHE_ENABLED && channel == cache.lastChannel) {
            continue;
        }
        
        Serial.print("   Canal " + String(channel) + ": ");
        
        // Callback de progresso
        if (progressCallback) {
            progressCallback(channel, MCD_MAX_CHANNEL);
        }
        
        if (tryChannel(channel, MCD_TIMEOUT_PER_CHANNEL)) {
            Serial.println("✅ MASTER ENCONTRADO!");
            unsigned long elapsedTime = millis() - startTime;
            updateStats(true, elapsedTime, channel);
            
            // Salvar no cache
            cache.lastChannel = channel;
            cache.usageCount++;
            cache.successRate = min(100, cache.successRate + 10);
            saveCache();
            
            return DiscoveryResult::SUCCESS;
        }
        
        Serial.println("⚪ Sem resposta");
    }
    
    // ===== FASE 2: VARREDURA COMPLETA =====
    Serial.println("\n📡 Fase 2: Varredura completa (2-5, 7-8, 10, 12-13)");
    
    for (uint8_t channel = MCD_MIN_CHANNEL; channel <= MCD_MAX_CHANNEL; channel++) {
        if (abortFlag) {
            Serial.println("⚠️ Discovery abortado");
            return DiscoveryResult::ABORTED;
        }
        
        // Pular canais já testados
        bool skip = false;
        if (MCD_CACHE_ENABLED && channel == cache.lastChannel) skip = true;
        for (uint8_t i = 0; i < MCD_PRIORITY_COUNT; i++) {
            if (channel == MCD_PRIORITY_CHANNELS[i]) skip = true;
        }
        if (skip) continue;
        
        Serial.print("   Canal " + String(channel) + ": ");
        
        // Callback de progresso
        if (progressCallback) {
            progressCallback(channel, MCD_MAX_CHANNEL);
        }
        
        if (tryChannel(channel, MCD_TIMEOUT_PER_CHANNEL)) {
            Serial.println("✅ MASTER ENCONTRADO!");
            unsigned long elapsedTime = millis() - startTime;
            updateStats(true, elapsedTime, channel);
            
            // Salvar no cache
            cache.lastChannel = channel;
            cache.usageCount++;
            cache.successRate = min(100, cache.successRate + 5);
            saveCache();
            
            return DiscoveryResult::SUCCESS;
        }
        
        Serial.println("⚪ Sem resposta");
    }
    
    // ===== RESULTADO: NÃO ENCONTRADO =====
    Serial.println("\n❌ === MASTER NÃO ENCONTRADO ===");
    Serial.println("⚠️ Possíveis causas:");
    Serial.println("   - MASTER não está ligado");
    Serial.println("   - MASTER fora de alcance ESP-NOW (>100m)");
    Serial.println("   - Interferência no sinal 2.4GHz");
    Serial.println("   - MASTER não enviou resposta");
    Serial.println("=====================================\n");
    
    unsigned long elapsedTime = millis() - startTime;
    updateStats(false, elapsedTime, 0);
    
    // Degradar cache se falhou
    if (MCD_CACHE_ENABLED && cache.successRate > 0) {
        cache.successRate = max(0, (int)cache.successRate - 20);
        saveCache();
    }
    
    restoreAfterFailedScan();
    
    return DiscoveryResult::TIMEOUT;
}

bool MultiChannelDiscovery::tryChannel(uint8_t channel, uint32_t timeout) {
    if (!initialized) return false;
    
    // Configurar canal
    if (!setChannel(channel)) {
        return false;
    }
    
    currentChannel = channel;
    
    // ✅ OTIMIZAÇÃO: Dividir timeout entre tentativas para não demorar muito
    uint32_t timeoutPerAttempt = timeout / MCD_MAX_RETRY_ATTEMPTS;
    if (timeoutPerAttempt < 150) timeoutPerAttempt = 150; // Mínimo 150ms por tentativa
    
    // Tentar múltiplas vezes se necessário
    for (uint8_t attempt = 0; attempt < MCD_MAX_RETRY_ATTEMPTS; attempt++) {
        // ✅ Enviar 2 broadcasts seguidos para aumentar chance de recepção
        sendDiscoveryBroadcast();
        delay(20);
        sendDiscoveryBroadcast();
        
        // Aguardar resposta
        if (waitForMasterResponse(timeoutPerAttempt)) {
            return true;
        }
        
        // Pequeno delay entre tentativas
        if (attempt < MCD_MAX_RETRY_ATTEMPTS - 1) {
            delay(50); // ✅ Reduzido de 100ms para 50ms
        }
    }
    
    return false;
}

DiscoveryResult MultiChannelDiscovery::rediscoverMaster(bool quickScan) {
    Serial.println("\n🔄 === RE-DISCOVERY ===");

    if (masterChannelLocked) {
        Serial.println("🔒 Modo bloqueado: escuta passiva + canal cache apenas");
        uint8_t ch = cache.lastChannel >= MCD_MIN_CHANNEL ? cache.lastChannel : listenChannel;
        if (passiveListen(ch, MCD_PASSIVE_LISTEN_MS / 2)) {
            return DiscoveryResult::SUCCESS;
        }
        if (tryChannel(ch, MCD_TIMEOUT_PER_CHANNEL)) {
            Serial.println("✅ RECONECTADO no canal cache!");
            return DiscoveryResult::SUCCESS;
        }
        restoreAfterFailedScan();
        return DiscoveryResult::TIMEOUT;
    }
    
    if (quickScan) {
        Serial.println("⚡ Modo rápido: Canal anterior + prioritários");
        
        // Tentar canal anterior primeiro
        if (cache.lastChannel > 0) {
            Serial.print("   Canal " + String(cache.lastChannel) + " (anterior): ");
            if (tryChannel(cache.lastChannel, MCD_TIMEOUT_PER_CHANNEL)) {
                Serial.println("✅ RECONECTADO!");
                return DiscoveryResult::SUCCESS;
            }
            Serial.println("⚪ Sem resposta");
        }
        
        // Tentar canais prioritários
        for (uint8_t i = 0; i < MCD_PRIORITY_COUNT; i++) {
            uint8_t channel = MCD_PRIORITY_CHANNELS[i];
            if (channel == cache.lastChannel) continue;
            
            Serial.print("   Canal " + String(channel) + ": ");
            if (tryChannel(channel, MCD_TIMEOUT_PER_CHANNEL)) {
                Serial.println("✅ RECONECTADO!");
                cache.lastChannel = channel;
                saveCache();
                return DiscoveryResult::SUCCESS;
            }
            Serial.println("⚪ Sem resposta");
        }
        
        Serial.println("⚠️ Quick scan falhou - tentando varredura completa...");
    }
    
    return discoverMaster();
}

void MultiChannelDiscovery::setSendBroadcastDelegate(McdSendBroadcastFn fn) {
    sendBroadcastDelegate = fn;
}

void MultiChannelDiscovery::setChannelSyncDelegate(McdChannelSyncFn fn) {
    channelSyncDelegate = fn;
}

void MultiChannelDiscovery::setRestoreChannelDelegate(McdRestoreChannelFn fn) {
    restoreChannelDelegate = fn;
}

void MultiChannelDiscovery::lockMasterChannel(bool locked) {
    masterChannelLocked = locked;
    if (locked) {
        Serial.println("🔒 Canal master bloqueado — scan multi-canal desativado");
    }
}

bool MultiChannelDiscovery::passiveListen(uint8_t channel, uint32_t timeoutMs) {
    if (!setChannel(channel)) return false;
    listenChannel = channel;
    masterFound = false;

    Serial.println("👂 Escuta passiva no canal " + String(channel) +
                   " (" + String(timeoutMs / 1000) + "s)");

    unsigned long start = millis();
    unsigned long lastBroadcast = 0;

    while (millis() - start < timeoutMs) {
        if (abortFlag) return false;
        if (masterFound) return true;

        if (sendBroadcastDelegate && (millis() - lastBroadcast > 5000)) {
            sendBroadcastDelegate();
            lastBroadcast = millis();
        }
        delay(50);
    }
    return masterFound;
}

void MultiChannelDiscovery::restoreAfterFailedScan() {
    uint8_t restoreCh = (cache.lastChannel >= MCD_MIN_CHANNEL && cache.lastChannel <= MCD_MAX_CHANNEL)
        ? cache.lastChannel : listenChannel;
    if (restoreCh < MCD_MIN_CHANNEL) restoreCh = 1;

    Serial.println("🔧 Restaurando canal " + String(restoreCh) + " após scan falho");

    if (restoreChannelDelegate) {
        if (restoreChannelDelegate(restoreCh)) {
            Serial.println("✅ Canal e peer broadcast restaurados");
        } else {
            Serial.println("⚠️ Falha ao restaurar canal/peer via delegate");
        }
    } else if (channelSyncDelegate) {
        channelSyncDelegate(restoreCh);
    } else {
        esp_wifi_set_channel(restoreCh, WIFI_SECOND_CHAN_NONE);
    }
    currentChannel = restoreCh;

    if (sendBroadcastDelegate) {
        sendBroadcastDelegate();
    }
}

// ===== CALLBACKS =====

void MultiChannelDiscovery::setMasterFoundCallback(void (*callback)(uint8_t channel, const uint8_t* masterMac)) {
    masterFoundCallback = callback;
}

void MultiChannelDiscovery::setProgressCallback(void (*callback)(uint8_t channel, uint8_t totalChannels)) {
    progressCallback = callback;
}

// ===== GETTERS =====

bool MultiChannelDiscovery::getMasterMac(uint8_t* mac) const {
    if (!masterFound) return false;
    memcpy(mac, masterMac, 6);
    return true;
}

// ===== CONTROLE =====

void MultiChannelDiscovery::resetStats() {
    stats = DiscoveryStats();
    Serial.println("📊 Estatísticas resetadas");
}

void MultiChannelDiscovery::clearCache() {
    cache = ChannelCache();
    if (MCD_CACHE_ENABLED) {
        // ✅ SEGUIR PATRÓN DEL PROYECTO: Verificar retorno de begin()
        if (prefs.begin(MCD_NVS_NAMESPACE, false)) {
            prefs.clear();
            prefs.end();
        }
    }
    Serial.println("🗑️ Cache limpo");
}

void MultiChannelDiscovery::saveCache() {
    saveCacheInternal();
}

void MultiChannelDiscovery::printCache() const {
    Serial.println("\n📦 ========================================");
    Serial.println("📦 CACHE NVS - MULTI-CHANNEL DISCOVERY");
    Serial.println("📦 ========================================");
    
    if (!MCD_CACHE_ENABLED) {
        Serial.println("⚠️ Cache NVS está DESABILITADO");
        Serial.println("========================================\n");
        return;
    }
    
    // Tentar ler do NVS diretamente
    Preferences prefs;
    bool cacheExists = false;
    
    if (prefs.begin(MCD_NVS_NAMESPACE, true)) { // read-only
        uint8_t cachedChannel = prefs.getUChar("channel", 0);
        uint32_t cachedLastSuccess = prefs.getUInt("last_success", 0);
        uint32_t cachedUsageCount = prefs.getUInt("usage_count", 0);
        uint8_t cachedSuccessRate = prefs.getUChar("success_rate", 0);
        
        prefs.end();
        
        if (cachedChannel > 0 && cachedChannel <= 13) {
            cacheExists = true;
            
            Serial.println("📶 Canal em cache: " + String(cachedChannel));
            Serial.println("📊 Taxa de sucesso: " + String(cachedSuccessRate) + "%");
            Serial.println("🔢 Vezes usado: " + String(cachedUsageCount));
            
            if (cachedLastSuccess > 0) {
                unsigned long currentTime = millis();
                unsigned long timeSinceSuccess;
                
                // Proteção contra overflow de millis()
                if (currentTime >= cachedLastSuccess) {
                    timeSinceSuccess = currentTime - cachedLastSuccess;
                } else {
                    // Overflow ocorreu
                    timeSinceSuccess = (ULONG_MAX - cachedLastSuccess) + currentTime;
                }
                
                unsigned long secondsAgo = timeSinceSuccess / 1000;
                unsigned long minutesAgo = secondsAgo / 60;
                unsigned long hoursAgo = minutesAgo / 60;
                
                Serial.print("⏰ Último sucesso: ");
                if (hoursAgo > 0) {
                    Serial.println(String(hoursAgo) + " hora(s) atrás");
                } else if (minutesAgo > 0) {
                    Serial.println(String(minutesAgo) + " minuto(s) atrás");
                } else {
                    Serial.println(String(secondsAgo) + " segundo(s) atrás");
                }
                
                Serial.println("   Timestamp: " + String(cachedLastSuccess));
            } else {
                Serial.println("⏰ Último sucesso: Nunca");
            }
        }
    }
    
    // Mostrar também o cache em memória (pode ser diferente do NVS)
    Serial.println("\n💾 Cache em memória:");
    Serial.println("   📶 Canal: " + String(cache.lastChannel > 0 ? String(cache.lastChannel) : "Nenhum"));
    Serial.println("   📊 Taxa de sucesso: " + String(cache.successRate) + "%");
    Serial.println("   🔢 Vezes usado: " + String(cache.usageCount));
    
    if (cache.lastSuccess > 0) {
        unsigned long currentTime = millis();
        unsigned long timeSinceSuccess;
        
        // Proteção contra overflow
        if (currentTime >= cache.lastSuccess) {
            timeSinceSuccess = currentTime - cache.lastSuccess;
        } else {
            timeSinceSuccess = (ULONG_MAX - cache.lastSuccess) + currentTime;
        }
        
        unsigned long secondsAgo = timeSinceSuccess / 1000;
        unsigned long minutesAgo = secondsAgo / 60;
        unsigned long hoursAgo = minutesAgo / 60;
        
        Serial.print("   ⏰ Último sucesso: ");
        if (hoursAgo > 0) {
            Serial.println(String(hoursAgo) + " hora(s) atrás");
        } else if (minutesAgo > 0) {
            Serial.println(String(minutesAgo) + " minuto(s) atrás");
        } else {
            Serial.println(String(secondsAgo) + " segundo(s) atrás");
        }
    } else {
        Serial.println("   ⏰ Último sucesso: Nunca");
    }
    
    if (!cacheExists && cache.lastChannel == 0) {
        Serial.println("\n⚠️ Cache vazio - Nenhum canal salvo ainda");
        Serial.println("💡 Execute 'mcd_discover' para popular o cache");
    }
    
    Serial.println("========================================\n");
}

// ===== UTILITÁRIOS =====

String MultiChannelDiscovery::resultToString(DiscoveryResult result) {
    switch (result) {
        case DiscoveryResult::SUCCESS: return "SUCCESS";
        case DiscoveryResult::TIMEOUT: return "TIMEOUT";
        case DiscoveryResult::ERROR_ESP_NOW: return "ERROR_ESP_NOW";
        case DiscoveryResult::ERROR_WIFI: return "ERROR_WIFI";
        case DiscoveryResult::ABORTED: return "ABORTED";
        case DiscoveryResult::BLOCKED: return "BLOCKED";
        default: return "UNKNOWN";
    }
}

void MultiChannelDiscovery::printStats() const {
    Serial.println("\n📊 === ESTATÍSTICAS DISCOVERY ===");
    Serial.println("Total de tentativas: " + String(stats.totalAttempts));
    Serial.println("Sucessos: " + String(stats.successCount));
    Serial.println("Falhas: " + String(stats.failureCount));
    
    if (stats.totalAttempts > 0) {
        float successRate = (stats.successCount * 100.0) / stats.totalAttempts;
        Serial.println("Taxa de sucesso: " + String(successRate, 1) + "%");
    }
    
    Serial.println("Tempo médio: " + String(stats.averageTimeMs) + "ms");
    Serial.println("Último canal: " + String(stats.lastChannelFound));
    Serial.println("==================================\n");
}

String MultiChannelDiscovery::getStatsJSON() const {
    String json = "{";
    json += "\"total_attempts\":" + String(stats.totalAttempts) + ",";
    json += "\"success_count\":" + String(stats.successCount) + ",";
    json += "\"failure_count\":" + String(stats.failureCount) + ",";
    json += "\"average_time_ms\":" + String(stats.averageTimeMs) + ",";
    json += "\"last_channel\":" + String(stats.lastChannelFound) + ",";
    json += "\"cache_channel\":" + String(cache.lastChannel) + ",";
    json += "\"cache_success_rate\":" + String(cache.successRate);
    json += "}";
    return json;
}

// ===== MÉTODOS PRIVADOS =====

bool MultiChannelDiscovery::loadCache() {
    if (!MCD_CACHE_ENABLED) return false;
    
    // ✅ SEGUIR PATRÓN DEL PROYECTO: Verificar retorno de begin()
    if (!prefs.begin(MCD_NVS_NAMESPACE, true)) { // read-only
        // Namespace no existe - primera ejecución, esto es normal
        return false;
    }
    
    cache.lastChannel = prefs.getUChar("channel", 1);
    cache.lastSuccess = prefs.getUInt("last_success", 0);
    cache.usageCount = prefs.getUInt("usage_count", 0);
    cache.successRate = prefs.getUChar("success_rate", 0);
    
    prefs.end();
    
    return (cache.lastChannel > 0 && cache.lastChannel <= 13);
}

bool MultiChannelDiscovery::saveCacheInternal() {
    if (!MCD_CACHE_ENABLED) return false;
    
    // ✅ SEGUIR PATRÓN DEL PROYECTO: Verificar retorno de begin()
    if (!prefs.begin(MCD_NVS_NAMESPACE, false)) { // read-write
        // Namespace no existe o error - esto puede pasar si NVS no se inicializó correctamente
        return false;
    }
    
    prefs.putUChar("channel", cache.lastChannel);
    prefs.putUInt("last_success", millis());
    prefs.putUInt("usage_count", cache.usageCount);
    prefs.putUChar("success_rate", cache.successRate);
    
    prefs.end();
    
    return true;
}

bool MultiChannelDiscovery::setChannel(uint8_t channel) {
    if (channel < MCD_MIN_CHANNEL || channel > MCD_MAX_CHANNEL) {
        Serial.println("❌ Canal inválido: " + String(channel));
        return false;
    }

    if (channelSyncDelegate) {
        if (!channelSyncDelegate(channel)) {
            Serial.println("❌ Erro ao sincronizar canal " + String(channel) + " via delegate");
            return false;
        }
    } else {
        esp_err_t err = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
        if (err != ESP_OK) {
            Serial.println("❌ Erro ao configurar canal " + String(channel) + ": " + String(err));
            return false;
        }
        delay(50);
    }

    currentChannel = channel;
    return true;
}

bool MultiChannelDiscovery::sendDiscoveryBroadcast() {
    if (sendBroadcastDelegate) {
        return sendBroadcastDelegate();
    }

    Serial.println("⚠️ sendDiscoveryBroadcast: delegate não configurado (ESPNowController)");
    return false;
}

bool MultiChannelDiscovery::waitForMasterResponse(uint32_t timeout) {
    unsigned long start = millis();
    masterFound = false;
    
    while (millis() - start < timeout) {
        if (masterFound) {
            return true;
        }
        delay(10);
    }
    
    return false;
}

void MultiChannelDiscovery::handleReceivedMessage(const uint8_t* mac, const uint8_t* data, int len) {
    if (len != sizeof(ESPNowMessage)) {
        return; // Mensagem com tamanho incorreto
    }
    
    ESPNowMessage* msg = (ESPNowMessage*)data;
    
    // ✅ Verificar se é resposta ao discovery (tipos válidos expandidos)
    if (msg->type == MessageType::DEVICE_INFO || 
        msg->type == MessageType::PONG ||
        msg->type == MessageType::ACK ||
        msg->type == MessageType::HANDSHAKE_REQUEST ||   // ✅ MASTER envia isso!
        msg->type == MessageType::HANDSHAKE_RESPONSE) {   // ✅ Resposta ao handshake
        
        // 🚨 CORREÇÃO CRÍTICA: Obter canal REAL do MASTER, não do Slave!
        uint8_t canalReal = 0;
        
        // Método 1: Se é DEVICE_INFO, extrair canal do DeviceInfoData
        if (msg->type == MessageType::DEVICE_INFO && msg->dataSize >= sizeof(DeviceInfoData)) {
            DeviceInfoData infoData;
            memcpy(&infoData, msg->data, sizeof(DeviceInfoData));
            if (infoData.wifiChannel > 0 && infoData.wifiChannel <= 13) {
                canalReal = infoData.wifiChannel;
                Serial.println("📥 Canal obtido do DeviceInfoData: " + String(canalReal));
            }
        }
        
        // Método 2: Se peer já existe, obter canal do peer
        if (canalReal == 0) {
            esp_now_peer_info_t peerInfo;
            if (esp_now_get_peer(mac, &peerInfo) == ESP_OK) {
                if (peerInfo.channel > 0 && peerInfo.channel <= 13) {
                    canalReal = peerInfo.channel;
                    Serial.println("📥 Canal obtido do peer existente: " + String(canalReal));
                }
            }
        }
        
        // Método 3: Fallback - usar canal atual do Slave (pode ser incorreto!)
        if (canalReal == 0) {
            wifi_second_chan_t secondChan;
            esp_wifi_get_channel(&canalReal, &secondChan);
            Serial.println("⚠️ ATENÇÃO: Usando canal do Slave como fallback: " + String(canalReal));
            Serial.println("   Isso pode estar INCORRETO se Slave está escaneando!");
        }
        
        Serial.println("\n🎯 === MASTER ENCONTRADO! ===");
        String macBuf = String(mac[0], HEX) + ":" + String(mac[1], HEX) + ":" +
                        String(mac[2], HEX) + ":" + String(mac[3], HEX) + ":" +
                        String(mac[4], HEX) + ":" + String(mac[5], HEX);
        macBuf.toUpperCase();
        Serial.println("📍 MAC: " + macBuf);
        Serial.println("📶 Canal REAL do Master: " + String(canalReal));
        Serial.println("📨 Tipo de mensagem: 0x" + String((uint8_t)msg->type, HEX));
        
        // Master encontrado!
        masterFound = true;
        memcpy(masterMac, mac, 6);
        
        // 🚨 CRÍTICO: Atualizar currentChannel para o canal REAL
        currentChannel = canalReal;
        
        // 🚨 CRÍTICO: Salvar no cache IMEDIATAMENTE
        cache.lastChannel = canalReal;
        cache.lastSuccess = millis();
        cache.usageCount++;
        cache.successRate = min(100, cache.successRate + 20);
        saveCache();
        
        Serial.println("💾 Canal salvo no cache: " + String(canalReal));
        Serial.println("✅ Discovery completo!");
        Serial.println("================================\n");
        
        // 🚨 PROTEÇÃO: Callback com throttle (evitar chamadas excessivas)
        // Só chamar callback se:
        // 1. Passou tempo suficiente desde última chamada (throttle)
        // 2. OU canal mudou (importante notificar mudança de canal)
        unsigned long now = millis();
        bool shouldCallCallback = false;
        
        if (masterFoundCallback) {
            if (now - lastCallbackTime >= CALLBACK_THROTTLE_MS) {
                // Passou tempo suficiente - pode chamar
                shouldCallCallback = true;
            } else if (canalReal != lastCallbackChannel) {
                // Canal mudou - importante notificar mesmo com throttle
                shouldCallCallback = true;
            }
            
            if (shouldCallCallback) {
                lastCallbackTime = now;
                lastCallbackChannel = canalReal;
                masterFoundCallback(canalReal, mac);
            } else {
                // Throttle ativo - pular callback mas manter logs
                Serial.println("⏱️ Callback throttled (última chamada há " + 
                             String(now - lastCallbackTime) + "ms)");
            }
        }
    }
}

void MultiChannelDiscovery::updateStats(bool success, uint32_t timeMs, uint8_t channelFound) {
    if (success) {
        stats.successCount++;
        stats.lastChannelFound = channelFound;
    } else {
        stats.failureCount++;
    }
    
    // Atualizar tempo médio
    if (stats.averageTimeMs == 0) {
        stats.averageTimeMs = timeMs;
    } else {
        stats.averageTimeMs = (stats.averageTimeMs + timeMs) / 2;
    }
    
    stats.lastAttemptTime = millis();
}

