#include "MasterSlaveManager.h"

// Instância estática para callbacks
MasterSlaveManager* MasterSlaveManager::instance = nullptr;

MasterSlaveManager::MasterSlaveManager(ESPNowController* espNowController) 
    : espNowController(espNowController), initialized(false),
      totalPingsReceived(0), totalPongsSent(0), totalAcksSent(0), 
      totalAcksReceived(0), totalErrors(0), commandIdCounter(0) {  // ⭐ Inicializar contador
    instance = this;
}

bool MasterSlaveManager::begin() {
    if (!espNowController || !espNowController->isInitialized()) {
        Serial.println("❌ ESPNowController não inicializado");
        return false;
    }
    
    Serial.println("\n🎯 ==========================================");
    Serial.println("🎯 INICIALIZANDO MASTER-SLAVE MANAGER");
    Serial.println("🎯 ==========================================");
    Serial.println("📡 Modo: MASTER (Receptor de PINGs)");
    Serial.println("🔧 Comunicação: BIDIRECIONAL");
    Serial.println("📋 Lista confiável: ATIVADA");
    Serial.println("✅ Sistema de ACKs: ATIVADO");
    Serial.println("==========================================");
    
    // Configurar callbacks do ESPNowController
    Serial.println("\n🔌 Configurando callbacks ESP-NOW...");
    
    // Callback para PING recebido
    espNowController->setPingCallback(onPingReceivedStatic);
    
    // Callback para informações de dispositivo
    espNowController->setDeviceInfoCallback(onDeviceInfoReceivedStatic);
    
    // Callback para status de relé
    espNowController->setRelayStatusCallback(onRelayStatusReceivedStatic);
    
    // Callback para erros
    espNowController->setErrorCallback(onErrorReceivedStatic);
    
    Serial.println("✅ Callbacks configurados");
    
    // Registrar callback para PONG (usar callback customizado)
    // O ESPNowController já responde automaticamente ao PING com PONG
    // Mas vamos interceptar para nossa lógica
    
    initialized = true;
    
    Serial.println("\n📊 === INFORMAÇÕES DO MASTER ===");
    Serial.println("🆔 Nome: ESP-NOW-MASTER");
    Serial.println("🆔 Tipo: MasterController");
    Serial.println("🆔 MAC: " + espNowController->getLocalMacString());
    Serial.println("📶 Canal: " + String(espNowController->isInitialized() ? "Ativo" : "Inativo"));
    Serial.println("👥 Slaves confiáveis: 0");
    Serial.println("💾 Heap Livre: " + String(ESP.getFreeHeap()) + " bytes");
    Serial.println("================================");
    
    Serial.println("\n✅ Master-Slave Manager inicializado com sucesso!");
    Serial.println("🎯 Aguardando PINGs dos Slaves...");
    Serial.println("📡 Comunicação bidirecional ativa!");
    Serial.println("==========================================\n");
    
    return true;
}

void MasterSlaveManager::update() {
    if (!initialized) return;
    
    // Verificar status dos Slaves periodicamente
    static unsigned long lastStatusCheck = 0;
    if (millis() - lastStatusCheck > 5000) {  // A cada 5 segundos
        checkSlaveStatus();
        lastStatusCheck = millis();
    }
    
    // Limpar ACKs expirados
    static unsigned long lastAckCleanup = 0;
    if (millis() - lastAckCleanup > 1000) {  // A cada 1 segundo
        cleanupExpiredAcks();
        lastAckCleanup = millis();
    }
    
    // Reenviar ACKs pendentes se necessário
    static unsigned long lastAckResend = 0;
    if (millis() - lastAckResend > 2000) {  // A cada 2 segundos
        resendPendingAcks();
        lastAckResend = millis();
    }
    
    // 🔄 FASE 1: Processar fila de retry de comandos
    processRetryQueue();
}

void MasterSlaveManager::end() {
    if (initialized) {
        Serial.println("📡 Master-Slave Manager finalizado");
        initialized = false;
    }
}

bool MasterSlaveManager::addTrustedSlave(const uint8_t* macAddress, const String& deviceName, const String& deviceType) {
    if (!initialized) return false;
    
    // Verificar se Slave já existe
    for (auto& slave : trustedSlaves) {
        if (memcmp(slave.macAddress, macAddress, 6) == 0) {
            Serial.println("⚠️ Slave já existe na lista confiável: " + ESPNowController::macToString(macAddress));
            return true;
        }
    }
    
    // Adicionar novo Slave
    TrustedSlave newSlave(macAddress);
    if (!deviceName.isEmpty()) {
        newSlave.deviceName = deviceName;
    }
    if (!deviceType.isEmpty()) {
        newSlave.deviceType = deviceType;
    }
    
    trustedSlaves.push_back(newSlave);
    
    Serial.println("\n🎉 ========================================");
    Serial.println("🎉 SLAVE ADICIONADO À LISTA CONFIÁVEL!");
    Serial.println("🎉 ========================================");
    Serial.println("📥 MAC: " + ESPNowController::macToString(macAddress));
    Serial.println("📝 Nome: " + newSlave.deviceName);
    Serial.println("🏷️ Tipo: " + newSlave.deviceType);
    Serial.println("📊 Status: " + String((int)newSlave.status));
    Serial.println("⏰ Primeiro contato: " + String(newSlave.firstSeen / 1000) + "s");
    Serial.println("========================================\n");
    
    // Chamar callback se definido
    if (slaveDiscoveredCallback) {
        slaveDiscoveredCallback(macAddress, newSlave.deviceName, newSlave.deviceType);
    }
    
    return true;
}

bool MasterSlaveManager::removeTrustedSlave(const uint8_t* macAddress) {
    if (!initialized) return false;
    
    for (auto it = trustedSlaves.begin(); it != trustedSlaves.end(); ++it) {
        if (memcmp(it->macAddress, macAddress, 6) == 0) {
            String deviceName = it->deviceName;
            trustedSlaves.erase(it);
            
            Serial.println("🗑️ Slave removido da lista confiável: " + ESPNowController::macToString(macAddress) + " (" + deviceName + ")");
            return true;
        }
    }
    
    return false;
}

TrustedSlave* MasterSlaveManager::getTrustedSlave(const uint8_t* macAddress) {
    for (auto& slave : trustedSlaves) {
        if (memcmp(slave.macAddress, macAddress, 6) == 0) {
            return &slave;
        }
    }
    return nullptr;
}

std::vector<TrustedSlave> MasterSlaveManager::getAllTrustedSlaves() {
    return trustedSlaves;
}

std::vector<TrustedSlave> MasterSlaveManager::getOnlineSlaves() {
    std::vector<TrustedSlave> onlineSlaves;
    for (const auto& slave : trustedSlaves) {
        if (slave.isOnline()) {
            onlineSlaves.push_back(slave);
        }
    }
    return onlineSlaves;
}

int MasterSlaveManager::getTrustedSlaveCount() {
    return trustedSlaves.size();
}

int MasterSlaveManager::getOnlineSlaveCount() {
    int count = 0;
    for (const auto& slave : trustedSlaves) {
        if (slave.isOnline()) {
            count++;
        }
    }
    return count;
}

bool MasterSlaveManager::sendPingToSlave(const uint8_t* macAddress) {
    if (!initialized || !espNowController) return false;
    
    TrustedSlave* slave = getTrustedSlave(macAddress);
    if (!slave) {
        Serial.println("❌ Slave não encontrado na lista confiável: " + ESPNowController::macToString(macAddress));
        return false;
    }
    
    bool success = espNowController->sendPing(macAddress);
    if (success) {
        slave->lastPongId++;
        Serial.println("🏓 PING enviado para Slave: " + ESPNowController::macToString(macAddress));
    } else {
        slave->messagesLost++;
        Serial.println("❌ Falha ao enviar PING para Slave: " + ESPNowController::macToString(macAddress));
    }
    
    return success;
}

int MasterSlaveManager::sendPingToAllSlaves() {
    if (!initialized) return 0;
    
    int pingsSent = 0;
    for (const auto& slave : trustedSlaves) {
        if (slave.isOnline()) {
            if (sendPingToSlave(slave.macAddress)) {
                pingsSent++;
            }
        }
    }
    
    Serial.println("📡 PINGs enviados para " + String(pingsSent) + " Slaves online");
    return pingsSent;
}

bool MasterSlaveManager::sendRelayCommandToSlave(const uint8_t* macAddress, int relayNumber, const String& action, int duration) {
    if (!initialized || !espNowController) return false;
    
    TrustedSlave* slave = getTrustedSlave(macAddress);
    if (!slave) {
        Serial.println("❌ Slave não encontrado na lista confiável: " + ESPNowController::macToString(macAddress));
        return false;
    }
    
    // 🔄 FASE 1: Gerar ID único para este comando
    uint32_t commandId = generateCommandId();
    
    Serial.println("\n📤 ========================================");
    Serial.println("📤 ENVIANDO COMANDO DE RELÉ");
    Serial.println("📤 ========================================");
    Serial.println("🆔 Command ID: " + String(commandId));
    Serial.println("📡 Destino: " + ESPNowController::macToString(macAddress));
    Serial.println("🔌 Relé: " + String(relayNumber));
    Serial.println("⚡ Ação: " + action);
    if (duration > 0) {
        Serial.println("⏱️ Duração: " + String(duration) + "s");
    }
    
    // Tentar enviar o comando
    bool success = espNowController->sendRelayCommand(macAddress, relayNumber, action, duration);
    
    if (success) {
        Serial.println("✅ Comando enviado com sucesso!");
        Serial.println("========================================\n");
    } else {
        Serial.println("❌ Falha ao enviar comando!");
        Serial.println("🔄 Adicionando à fila de retry...");
        Serial.println("========================================\n");
        
        slave->messagesLost++;
        
        // 🔄 FASE 1: Adicionar à fila de retry automático
        addToRetryQueue(macAddress, relayNumber, action, duration, commandId);
    }
    
    return success;
}

bool MasterSlaveManager::requestSlaveStatus(const uint8_t* macAddress) {
    if (!initialized || !espNowController) return false;
    
    // Enviar comando de status para todos os relés
    bool success = true;
    TrustedSlave* slave = getTrustedSlave(macAddress);
    if (slave) {
        for (int i = 0; i < slave->numRelays; i++) {
            if (!espNowController->sendRelayCommand(macAddress, i, "status", 0)) {
                success = false;
            }
        }
    }
    
    if (success) {
        Serial.println("📊 Solicitação de status enviada para Slave: " + ESPNowController::macToString(macAddress));
    }
    
    return success;
}

bool MasterSlaveManager::requestSlaveInfo(const uint8_t* macAddress) {
    if (!initialized || !espNowController) return false;
    
    // Enviar solicitação de informações do dispositivo
    bool success = espNowController->sendDeviceInfo(macAddress, "Master", 0, true, millis(), ESP.getFreeHeap());
    
    if (success) {
        Serial.println("📋 Solicitação de informações enviada para Slave: " + ESPNowController::macToString(macAddress));
    }
    
    return success;
}

bool MasterSlaveManager::sendAck(const uint8_t* macAddress, uint32_t messageId) {
    if (!initialized || !espNowController) return false;
    
    // Criar mensagem ACK
    ESPNowMessage ackMessage = {};
    ackMessage.type = MessageType::ACK;
    espNowController->getLocalMac(ackMessage.senderId);
    memcpy(ackMessage.targetId, macAddress, 6);
    ackMessage.messageId = ++totalAcksSent;
    ackMessage.timestamp = millis();
    
    // Incluir ID da mensagem confirmada nos dados
    ackMessage.dataSize = sizeof(uint32_t);
    memcpy(ackMessage.data, &messageId, sizeof(uint32_t));
    ackMessage.checksum = espNowController->calculateChecksum(ackMessage);
    
    bool success = espNowController->sendMessage(ackMessage, macAddress);
    if (success) {
        Serial.println("✅ ACK enviado para " + ESPNowController::macToString(macAddress) + 
                      " (MsgID: " + String(messageId) + ")");
    }
    
    return success;
}

bool MasterSlaveManager::isWaitingForAck(const uint8_t* macAddress, uint32_t messageId) {
    for (const auto& pending : pendingAcks) {
        if (memcmp(pending.macAddress, macAddress, 6) == 0 && pending.messageId == messageId) {
            return true;
        }
    }
    return false;
}

void MasterSlaveManager::markMessageAcknowledged(const uint8_t* macAddress, uint32_t messageId) {
    for (auto it = pendingAcks.begin(); it != pendingAcks.end(); ++it) {
        if (memcmp(it->macAddress, macAddress, 6) == 0 && it->messageId == messageId) {
            pendingAcks.erase(it);
            Serial.println("✅ Mensagem confirmada: " + ESPNowController::macToString(macAddress) + 
                          " (MsgID: " + String(messageId) + ")");
            break;
        }
    }
}

// ===== CALLBACKS =====

void MasterSlaveManager::setSlaveDiscoveredCallback(std::function<void(const uint8_t* macAddress, const String& deviceName, const String& deviceType)> callback) {
    this->slaveDiscoveredCallback = callback;
}

void MasterSlaveManager::setSlaveOnlineCallback(std::function<void(const uint8_t* macAddress, const String& deviceName)> callback) {
    this->slaveOnlineCallback = callback;
}

void MasterSlaveManager::setSlaveOfflineCallback(std::function<void(const uint8_t* macAddress, const String& deviceName)> callback) {
    this->slaveOfflineCallback = callback;
}

void MasterSlaveManager::setPingReceivedCallback(std::function<void(const uint8_t* macAddress, uint32_t pingId)> callback) {
    this->pingReceivedCallback = callback;
}

void MasterSlaveManager::setPongReceivedCallback(std::function<void(const uint8_t* macAddress, uint32_t pongId)> callback) {
    this->pongReceivedCallback = callback;
}

void MasterSlaveManager::setRelayStatusCallback(std::function<void(const uint8_t* macAddress, int relayNumber, bool state, bool hasTimer, int remainingTime, const String& name)> callback) {
    this->relayStatusCallback = callback;
}

void MasterSlaveManager::setDeviceInfoCallback(std::function<void(const uint8_t* macAddress, const String& deviceName, const String& deviceType, uint8_t numRelays, bool operational)> callback) {
    this->deviceInfoCallback = callback;
}

void MasterSlaveManager::setAckReceivedCallback(std::function<void(const uint8_t* macAddress, uint32_t messageId)> callback) {
    this->ackReceivedCallback = callback;
}

void MasterSlaveManager::setErrorCallback(std::function<void(const uint8_t* macAddress, const String& error)> callback) {
    this->errorCallback = callback;
}

// 🔄 FASE 2: Callback para ACK de relay
void MasterSlaveManager::setRelayAckCallback(std::function<void(const uint8_t* macAddress, uint32_t commandId, bool success, uint8_t relayNumber, uint8_t currentState)> callback) {
    this->relayAckCallback = callback;
}

// ===== MÉTODOS PRIVADOS =====

void MasterSlaveManager::processPingReceived(const uint8_t* senderMac, uint32_t pingId) {
    Serial.println("\n🏓 ========================================");
    Serial.println("🏓 PING RECEBIDO DO SLAVE!");
    Serial.println("🏓 ========================================");
    Serial.println("📥 De: " + ESPNowController::macToString(senderMac));
    Serial.println("🆔 Ping ID: " + String(pingId));
    Serial.println("⏰ Timestamp: " + String(millis() / 1000) + "s");
    
    // Verificar se Slave está na lista confiável
    TrustedSlave* slave = getTrustedSlave(senderMac);
    if (!slave) {
        Serial.println("\n🆕 SLAVE DESCONHECIDO - ADICIONANDO À LISTA CONFIÁVEL");
        addTrustedSlave(senderMac, "Slave-" + ESPNowController::macToString(senderMac).substring(12), "RelayBox");
        slave = getTrustedSlave(senderMac);
    }
    
    if (slave) {
        // Atualizar informações do Slave
        slave->updateLastSeen();
        slave->lastPingId = pingId;
        slave->pingsReceived++;
        slave->messagesReceived++;
        
        // Atualizar status
        if (slave->status == SlaveStatus::UNKNOWN || slave->status == SlaveStatus::DISCOVERED) {
            slave->status = SlaveStatus::PING_RECEIVED;
            Serial.println("📊 Status atualizado: PING_RECEIVED");
            
            // Chamar callback de descoberta se for primeira vez
            if (slaveDiscoveredCallback && slave->status == SlaveStatus::PING_RECEIVED) {
                slaveDiscoveredCallback(senderMac, slave->deviceName, slave->deviceType);
            }
        }
        
        // Responder automaticamente com PONG
        Serial.println("\n🏓 Enviando PONG de resposta...");
        bool pongSent = espNowController->sendPing(senderMac);  // ESPNowController já responde PING com PONG
        
        if (pongSent) {
            slave->pongsSent++;
            slave->lastPongId++;
            Serial.println("✅ PONG enviado com sucesso!");
            Serial.println("🔗 Conectividade confirmada");
            
            // Atualizar status para ONLINE se ainda não estiver
            if (slave->status == SlaveStatus::PING_RECEIVED) {
                slave->status = SlaveStatus::ONLINE;
                Serial.println("📊 Status atualizado: ONLINE");
                
                // Chamar callback de online
                if (slaveOnlineCallback) {
                    slaveOnlineCallback(senderMac, slave->deviceName);
                }
            }
        } else {
            slave->messagesLost++;
            Serial.println("❌ Falha ao enviar PONG");
        }
        
        Serial.println("📊 Estatísticas do Slave:");
        Serial.println("   PINGs recebidos: " + String(slave->pingsReceived));
        Serial.println("   PONGs enviados: " + String(slave->pongsSent));
        Serial.println("   Mensagens recebidas: " + String(slave->messagesReceived));
        Serial.println("   Mensagens perdidas: " + String(slave->messagesLost));
        Serial.println("   Último contato: " + String(slave->getTimeSinceLastSeen() / 1000) + "s atrás");
    }
    
    // Chamar callback se definido
    if (pingReceivedCallback) {
        pingReceivedCallback(senderMac, pingId);
    }
    
    Serial.println("========================================\n");
}

void MasterSlaveManager::processPongReceived(const uint8_t* senderMac, uint32_t pongId) {
    Serial.println("🏓 PONG recebido de: " + ESPNowController::macToString(senderMac) + " (ID: " + String(pongId) + ")");
    
    TrustedSlave* slave = getTrustedSlave(senderMac);
    if (slave) {
        slave->updateLastSeen();
        slave->messagesReceived++;
        
        // Chamar callback se definido
        if (pongReceivedCallback) {
            pongReceivedCallback(senderMac, pongId);
        }
    }
}

void MasterSlaveManager::processAckReceived(const uint8_t* senderMac, uint32_t messageId) {
    Serial.println("✅ ACK recebido de: " + ESPNowController::macToString(senderMac) + " (MsgID: " + String(messageId) + ")");
    
    // Marcar mensagem como confirmada
    markMessageAcknowledged(senderMac, messageId);
    totalAcksReceived++;
    
    // Chamar callback se definido
    if (ackReceivedCallback) {
        ackReceivedCallback(senderMac, messageId);
    }
}

void MasterSlaveManager::processDeviceInfoReceived(const uint8_t* senderMac, const String& deviceName, const String& deviceType, uint8_t numRelays, bool operational, uint8_t wifiChannel) {
    Serial.println("\n📋 ========================================");
    Serial.println("📋 INFORMAÇÕES DO SLAVE RECEBIDAS!");
    Serial.println("📋 ========================================");
    Serial.println("📥 De: " + ESPNowController::macToString(senderMac));
    Serial.println("📝 Nome: " + deviceName);
    Serial.println("🏷️ Tipo: " + deviceType);
    Serial.println("🔌 Relés: " + String(numRelays));
    Serial.println("✅ Operacional: " + String(operational ? "Sim" : "Não"));
    Serial.println("📶 Canal WiFi: " + String(wifiChannel) + " (recebido no DeviceInfo)");
    
    // ⭐ CORREÇÃO CRÍTICA: Adicionar Slave como peer ESP-NOW se não existir
    if (espNowController && !espNowController->peerExists(senderMac)) {
        Serial.println("\n🔗 === ADICIONANDO SLAVE COMO PEER ESP-NOW ===");
        Serial.println("   MAC: " + ESPNowController::macToString(senderMac));
        Serial.println("   Nome: " + deviceName);
        
        if (espNowController->addPeer(senderMac, deviceName)) {
            Serial.println("✅ Slave registrado como peer ESP-NOW!");
            Serial.println("📡 Comunicação bidirecional Master ↔ Slave ativa!");
            Serial.println("🎯 Agora posso responder aos PINGs do Slave");
            Serial.println("==============================================\n");
        } else {
            Serial.println("❌ Falha ao adicionar Slave como peer ESP-NOW");
            Serial.println("⚠️ Comunicação do Slave → Master será limitada");
            Serial.println("==============================================\n");
        }
    } else if (espNowController && espNowController->peerExists(senderMac)) {
        Serial.println("✅ Slave já está registrado como peer ESP-NOW\n");
    }
    
    // Atualizar informações do Slave
    updateTrustedSlave(senderMac, deviceName, deviceType, SlaveStatus::ONLINE);
    
    TrustedSlave* slave = getTrustedSlave(senderMac);
    if (slave) {
        slave->numRelays = numRelays;
        slave->operational = operational;
        slave->messagesReceived++;
        
        Serial.println("📊 Informações atualizadas na lista confiável");
    }
    
    // Chamar callback se definido
    if (deviceInfoCallback) {
        deviceInfoCallback(senderMac, deviceName, deviceType, numRelays, operational);
    }
    
    Serial.println("========================================\n");
}

void MasterSlaveManager::processRelayStatusReceived(const uint8_t* senderMac, int relayNumber, bool state, bool hasTimer, int remainingTime, const String& name) {
    Serial.println("📊 Status de relé recebido de " + ESPNowController::macToString(senderMac) + 
                  ": " + name + " -> " + (state ? "ON" : "OFF"));
    
    TrustedSlave* slave = getTrustedSlave(senderMac);
    if (slave) {
        slave->updateLastSeen();
        slave->messagesReceived++;
    }
    
    // Chamar callback se definido
    if (relayStatusCallback) {
        relayStatusCallback(senderMac, relayNumber, state, hasTimer, remainingTime, name);
    }
}

void MasterSlaveManager::updateTrustedSlave(const uint8_t* macAddress, const String& deviceName, const String& deviceType, SlaveStatus status) {
    TrustedSlave* slave = getTrustedSlave(macAddress);
    if (slave) {
        slave->updateLastSeen();
        
        if (!deviceName.isEmpty()) {
            slave->deviceName = deviceName;
        }
        if (!deviceType.isEmpty()) {
            slave->deviceType = deviceType;
        }
        if (status != SlaveStatus::UNKNOWN) {
            slave->status = status;
        }
    }
}

void MasterSlaveManager::checkSlaveStatus() {
    unsigned long currentTime = millis();
    const unsigned long offlineTimeout = 30000;  // 30 segundos
    
    for (auto& slave : trustedSlaves) {
        if (slave.isOfflineTimeout(offlineTimeout)) {
            if (slave.status != SlaveStatus::OFFLINE) {
                slave.status = SlaveStatus::OFFLINE;
                Serial.println("🔴 Slave offline: " + ESPNowController::macToString(slave.macAddress) + 
                              " (" + slave.deviceName + ")");
                
                // Chamar callback se definido
                if (slaveOfflineCallback) {
                    slaveOfflineCallback(slave.macAddress, slave.deviceName);
                }
            }
        }
    }
}

void MasterSlaveManager::cleanupExpiredAcks() {
    unsigned long currentTime = millis();
    const unsigned long ackTimeout = 5000;  // 5 segundos
    
    for (auto it = pendingAcks.begin(); it != pendingAcks.end();) {
        if (currentTime - it->timestamp > ackTimeout) {
            Serial.println("⏰ ACK expirado: " + ESPNowController::macToString(it->macAddress) + 
                          " (MsgID: " + String(it->messageId) + ")");
            it = pendingAcks.erase(it);
        } else {
            ++it;
        }
    }
}

void MasterSlaveManager::resendPendingAcks() {
    unsigned long currentTime = millis();
    const unsigned long resendInterval = 2000;  // 2 segundos
    
    for (auto& pending : pendingAcks) {
        if (currentTime - pending.timestamp > resendInterval && pending.retryCount < 3) {
            Serial.println("🔄 Reenviando ACK: " + ESPNowController::macToString(pending.macAddress) + 
                          " (MsgID: " + String(pending.messageId) + ")");
            
            if (sendAck(pending.macAddress, pending.messageId)) {
                pending.timestamp = currentTime;
                pending.retryCount++;
            }
        }
    }
}

// ===== CALLBACKS ESTÁTICOS =====

void MasterSlaveManager::onPingReceivedStatic(const uint8_t* senderMac) {
    if (instance) {
        // O ESPNowController já responde automaticamente ao PING com PONG
        // Aqui processamos nossa lógica adicional
        instance->processPingReceived(senderMac, millis());  // Usar timestamp como ID
    }
}

void MasterSlaveManager::onPongReceivedStatic(const uint8_t* senderMac) {
    if (instance) {
        instance->processPongReceived(senderMac, millis());
    }
}

void MasterSlaveManager::onDeviceInfoReceivedStatic(const uint8_t* senderMac, const String& deviceName, const String& deviceType, uint8_t numRelays, bool operational, uint8_t wifiChannel) {
    if (instance) {
        instance->processDeviceInfoReceived(senderMac, deviceName, deviceType, numRelays, operational, wifiChannel);
    }
}

void MasterSlaveManager::onRelayStatusReceivedStatic(const uint8_t* senderMac, int relayNumber, bool state, bool hasTimer, int remainingTime, const String& name) {
    if (instance) {
        instance->processRelayStatusReceived(senderMac, relayNumber, state, hasTimer, remainingTime, name);
    }
}

void MasterSlaveManager::onErrorReceivedStatic(const String& error) {
    if (instance) {
        instance->totalErrors++;
        Serial.println("❌ Erro ESP-NOW: " + error);
        
        if (instance->errorCallback) {
            instance->errorCallback(nullptr, error);
        }
    }
}

// ===== UTILITÁRIOS =====

String MasterSlaveManager::getStatsJSON() {
    DynamicJsonDocument doc(1024);
    
    doc["initialized"] = initialized;
    doc["totalPingsReceived"] = totalPingsReceived;
    doc["totalPongsSent"] = totalPongsSent;
    doc["totalAcksSent"] = totalAcksSent;
    doc["totalAcksReceived"] = totalAcksReceived;
    doc["totalErrors"] = totalErrors;
    doc["trustedSlavesCount"] = trustedSlaves.size();
    doc["onlineSlavesCount"] = getOnlineSlaveCount();
    doc["pendingAcksCount"] = pendingAcks.size();
    
    JsonArray slaves = doc.createNestedArray("slaves");
    for (const auto& slave : trustedSlaves) {
        JsonObject slaveObj = slaves.createNestedObject();
        slaveObj["mac"] = ESPNowController::macToString(slave.macAddress);
        slaveObj["name"] = slave.deviceName;
        slaveObj["type"] = slave.deviceType;
        slaveObj["status"] = (int)slave.status;
        slaveObj["online"] = slave.isOnline();
        slaveObj["lastSeen"] = slave.lastSeen;
        slaveObj["rssi"] = slave.rssi;
        slaveObj["numRelays"] = slave.numRelays;
        slaveObj["operational"] = slave.operational;
        slaveObj["pingsReceived"] = slave.pingsReceived;
        slaveObj["pongsSent"] = slave.pongsSent;
        slaveObj["messagesReceived"] = slave.messagesReceived;
        slaveObj["messagesLost"] = slave.messagesLost;
    }
    
    String result;
    serializeJson(doc, result);
    return result;
}

void MasterSlaveManager::printStatus() {
    Serial.println("\n🎯 === STATUS MASTER-SLAVE MANAGER ===");
    Serial.println("✅ Inicializado: " + String(initialized ? "Sim" : "Não"));
    Serial.println("📊 PINGs recebidos: " + String(totalPingsReceived));
    Serial.println("📊 PONGs enviados: " + String(totalPongsSent));
    Serial.println("📊 ACKs enviados: " + String(totalAcksSent));
    Serial.println("📊 ACKs recebidos: " + String(totalAcksReceived));
    Serial.println("📊 Erros: " + String(totalErrors));
    Serial.println("👥 Slaves confiáveis: " + String(trustedSlaves.size()));
    Serial.println("🟢 Slaves online: " + String(getOnlineSlaveCount()));
    Serial.println("⏳ ACKs pendentes: " + String(pendingAcks.size()));
    
    if (!trustedSlaves.empty()) {
        Serial.println("\n👥 === SLAVES CONFIÁVEIS ===");
        for (const auto& slave : trustedSlaves) {
            Serial.println("   " + ESPNowController::macToString(slave.macAddress) + " | " + 
                          slave.deviceName + " (" + slave.deviceType + ") | " +
                          (slave.isOnline() ? "🟢 Online" : "🔴 Offline") + 
                          " | Relés: " + String(slave.numRelays) + 
                          " | PINGs: " + String(slave.pingsReceived));
        }
    }
    
    Serial.println("=====================================\n");
}

void MasterSlaveManager::printTrustedSlaves() {
    Serial.println("\n👥 === LISTA DE SLAVES CONFIÁVEIS ===");
    
    if (trustedSlaves.empty()) {
        Serial.println("📭 Nenhum Slave confiável encontrado");
    } else {
        for (size_t i = 0; i < trustedSlaves.size(); i++) {
            const auto& slave = trustedSlaves[i];
            Serial.println("\n📋 Slave #" + String(i + 1) + ":");
            Serial.println("   MAC: " + ESPNowController::macToString(slave.macAddress));
            Serial.println("   Nome: " + slave.deviceName);
            Serial.println("   Tipo: " + slave.deviceType);
            Serial.println("   Status: " + String((int)slave.status));
            Serial.println("   Online: " + String(slave.isOnline() ? "Sim" : "Não"));
            Serial.println("   Relés: " + String(slave.numRelays));
            Serial.println("   Operacional: " + String(slave.operational ? "Sim" : "Não"));
            Serial.println("   Último contato: " + String(slave.getTimeSinceLastSeen() / 1000) + "s atrás");
            Serial.println("   PINGs recebidos: " + String(slave.pingsReceived));
            Serial.println("   PONGs enviados: " + String(slave.pongsSent));
            Serial.println("   Mensagens recebidas: " + String(slave.messagesReceived));
            Serial.println("   Mensagens perdidas: " + String(slave.messagesLost));
        }
    }
    
    Serial.println("=====================================\n");
}

void MasterSlaveManager::cleanupOfflineSlaves() {
    Serial.println("🧹 Limpando Slaves offline...");
    
    int removedCount = 0;
    for (auto it = trustedSlaves.begin(); it != trustedSlaves.end();) {
        if (it->isOfflineTimeout(120000)) {  // 2 minutos offline
            Serial.println("🗑️ Removendo Slave offline: " + ESPNowController::macToString(it->macAddress) + 
                          " (" + it->deviceName + ")");
            it = trustedSlaves.erase(it);
            removedCount++;
        } else {
            ++it;
        }
    }
    
    Serial.println("✅ " + String(removedCount) + " Slaves offline removidos");
}

void MasterSlaveManager::rediscoverSlaves() {
    Serial.println("🔍 Iniciando re-descoberta de Slaves...");
    
    // Enviar broadcast de descoberta
    if (espNowController) {
        espNowController->sendDiscoveryBroadcast();
        Serial.println("📢 Broadcast de descoberta enviado");
    }
    
    Serial.println("⏳ Aguardando respostas dos Slaves...");
}

// ===== 🔄 FASE 1: IMPLEMENTAÇÃO DO SISTEMA DE RETRY =====

uint32_t MasterSlaveManager::generateCommandId() {
    return ++commandIdCounter;
}

void MasterSlaveManager::addToRetryQueue(const uint8_t* targetMac, int relayNumber, const String& action, int duration, uint32_t commandId) {
    PendingRelayCommand cmd;
    memcpy(cmd.targetMac, targetMac, 6);
    cmd.relayNumber = relayNumber;
    cmd.action = action;
    cmd.duration = duration;
    cmd.timestamp = millis();
    cmd.nextRetry = millis() + RETRY_INTERVAL;  // Primeiro retry em 2s
    cmd.retryCount = 0;
    cmd.commandId = commandId;
    cmd.waitingForAck = false;
    
    pendingRelayCommands.push_back(cmd);
    
    Serial.println("\n📋 ========================================");
    Serial.println("📋 COMANDO ADICIONADO À FILA DE RETRY");
    Serial.println("📋 ========================================");
    Serial.println("🆔 Command ID: " + String(commandId));
    Serial.println("📡 Destino: " + ESPNowController::macToString(targetMac));
    Serial.println("🔌 Relé: " + String(relayNumber));
    Serial.println("⚡ Ação: " + action);
    Serial.println("⏱️ Próximo retry em: 2s");
    Serial.println("========================================\n");
}

void MasterSlaveManager::processRetryQueue() {
    if (pendingRelayCommands.empty()) return;
    
    unsigned long now = millis();
    
    for (auto it = pendingRelayCommands.begin(); it != pendingRelayCommands.end();) {
        // Verificar se é hora de tentar novamente
        if (now >= it->nextRetry && it->retryCount < MAX_RELAY_RETRIES) {
            Serial.println("\n🔄 ========================================");
            Serial.println("🔄 REINTENTANDO COMANDO");
            Serial.println("🔄 ========================================");
            Serial.println("🆔 Command ID: " + String(it->commandId));
            Serial.println("📡 Destino: " + ESPNowController::macToString(it->targetMac));
            Serial.println("🔌 Relé: " + String(it->relayNumber));
            Serial.println("⚡ Ação: " + it->action);
            Serial.println("🔢 Tentativa: " + String(it->retryCount + 1) + "/" + String(MAX_RELAY_RETRIES));
            
            // Tentar reenviar o comando
            bool success = espNowController->sendRelayCommand(it->targetMac, it->relayNumber, it->action, it->duration);
            
            if (success) {
                Serial.println("✅ Reintento exitoso!");
                Serial.println("⏳ Aguardando confirmação do Slave...");
                Serial.println("========================================\n");
                
                // Marcar como aguardando ACK
                it->waitingForAck = true;
                it->retryCount++;
                it->nextRetry = now + (RETRY_INTERVAL * it->retryCount);  // Backoff exponencial
                ++it;
            } else {
                Serial.println("❌ Reintento falhou!");
                it->retryCount++;
                
                if (it->retryCount >= MAX_RELAY_RETRIES) {
                    Serial.println("💔 COMANDO DESCARTADO APÓS " + String(MAX_RELAY_RETRIES) + " TENTATIVAS");
                    Serial.println("========================================\n");
                    
                    // Registrar erro
                    TrustedSlave* slave = getTrustedSlave(it->targetMac);
                    if (slave) {
                        slave->messagesLost++;
                        slave->errors++;
                    }
                    
                    it = pendingRelayCommands.erase(it);
                } else {
                    // Calcular próximo retry com backoff exponencial
                    it->nextRetry = now + (RETRY_INTERVAL * it->retryCount);
                    Serial.println("⏱️ Próximo retry em: " + String((RETRY_INTERVAL * it->retryCount) / 1000) + "s");
                    Serial.println("========================================\n");
                    ++it;
                }
            }
        }
        // Verificar timeout (se está esperando muito tempo)
        else if (it->waitingForAck && (now - it->timestamp > 30000)) {  // 30 segundos timeout
            Serial.println("\n⏰ ========================================");
            Serial.println("⏰ TIMEOUT DE COMANDO");
            Serial.println("⏰ ========================================");
            Serial.println("🆔 Command ID: " + String(it->commandId));
            Serial.println("📡 Destino: " + ESPNowController::macToString(it->targetMac));
            Serial.println("⚠️ Comando descartado por timeout");
            Serial.println("========================================\n");
            
            it = pendingRelayCommands.erase(it);
        }
        else {
            ++it;
        }
    }
}

void MasterSlaveManager::removeFromRetryQueue(uint32_t commandId) {
    for (auto it = pendingRelayCommands.begin(); it != pendingRelayCommands.end(); ++it) {
        if (it->commandId == commandId) {
            Serial.println("\n✅ ========================================");
            Serial.println("✅ COMANDO CONFIRMADO");
            Serial.println("✅ ========================================");
            Serial.println("🆔 Command ID: " + String(commandId));
            Serial.println("📡 Destino: " + ESPNowController::macToString(it->targetMac));
            Serial.println("🔌 Relé: " + String(it->relayNumber));
            Serial.println("⚡ Ação: " + it->action);
            Serial.println("🎯 Removido da fila de retry");
            Serial.println("========================================\n");
            
            pendingRelayCommands.erase(it);
            break;
        }
    }
}

// ===== 🔄 FASE 2: PROCESSAMENTO DE ACKs DE RELAY =====

void MasterSlaveManager::processRelayCommandAck(const RelayCommandAck& ack, const uint8_t* senderMac) {
    Serial.println("\n🎊 ========================================");
    Serial.println("🎊 ACK DE COMANDO RECEBIDO!");
    Serial.println("🎊 ========================================");
    Serial.println("📥 De: " + ESPNowController::macToString(senderMac));
    Serial.println("🆔 Command ID: " + String(ack.commandId));
    Serial.println("🔌 Relé: " + String(ack.relayNumber));
    Serial.println("✅ Success: " + String(ack.success ? "Sim" : "Não"));
    Serial.println("💡 Estado atual: " + String(ack.currentState ? "ON" : "OFF"));
    Serial.println("⏰ Timestamp: " + String(ack.timestamp));
    
    // Atualizar estatísticas do Slave
    TrustedSlave* slave = getTrustedSlave(senderMac);
    if (slave) {
        slave->updateLastSeen();
        slave->messagesReceived++;
        
        if (ack.success) {
            Serial.println("📊 Slave executou comando com sucesso!");
        } else {
            Serial.println("⚠️ Slave reportou falha na execução");
            slave->errors++;
        }
    }
    
    // 🎯 CRITICAL: Remover comando da fila de retry
    if (ack.success) {
        Serial.println("\n🔄 Removendo comando da fila de retry...");
        removeFromRetryQueue(ack.commandId);
        Serial.println("✅ Comando removido - retry cancelado!");
    } else {
        Serial.println("\n⚠️ Comando falhou no Slave");
        Serial.println("🔄 Sistema de retry continuará tentando...");
    }
    
    // Chamar callback se definido
    if (relayAckCallback) {
        relayAckCallback(senderMac, ack.commandId, ack.success, ack.relayNumber, ack.currentState);
    }
    
    Serial.println("========================================\n");
}
