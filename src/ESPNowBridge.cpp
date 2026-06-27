#include "ESPNowBridge.h"

// Instância estática para callbacks
ESPNowBridge* ESPNowBridge::instance = nullptr;

ESPNowBridge::ESPNowBridge(RelayCommandBox* relayController, int channel) 
    : localRelayController(relayController), wifiChannel(channel), initialized(false), 
      messageCounter(0), messagesSent(0), messagesReceived(0), messagesLost(0) {
    instance = this;
    
    // FASE 2: Criar instância do ESPNowController
    // 🔧 Usar nome identificador único para Slave
    espNowController = new ESPNowController("ESP-NOW-SLAVE", channel);
}

bool ESPNowBridge::begin() {
    Serial.println("\n🚀 ==========================================");
    Serial.println("🚀 INICIALIZANDO ESP-NOW BRIDGE (SLAVE)");
    Serial.println("🚀 ==========================================");
    Serial.println("📡 Versão: FASE 2 - Com Discovery Automático");
    Serial.println("🔧 Modo: SLAVE (RelayBox)");
    
    // FASE 2: Inicializar ESPNowController
    Serial.println("\n⚙️  Inicializando ESPNowController...");
    if (!espNowController->begin()) {
        Serial.println("❌ Erro ao inicializar ESPNowController");
        Serial.println("💡 Verifique se o WiFi está configurado corretamente");
        return false;
    }
    Serial.println("✅ ESPNowController inicializado");
    
    // Configurar callbacks do ESPNowController
    Serial.println("\n🔌 Configurando callbacks...");
    espNowController->setRelayCommandCallback(ESPNowBridge::onRelayCommandReceivedStatic);
    espNowController->setRelayStatusCallback(ESPNowBridge::onRelayStatusReceivedStatic);
    espNowController->setDeviceInfoCallback(ESPNowBridge::onDeviceInfoReceivedStatic);
    espNowController->setPingCallback(ESPNowBridge::onPingReceivedStatic);
    espNowController->setWiFiCredentialsCallback(ESPNowBridge::onWiFiCredentialsReceivedStatic);
    espNowController->setErrorCallback(ESPNowBridge::onErrorReceivedStatic);
    Serial.println("✅ Callbacks configurados");
    
    // Registrar callbacks de compatibilidade
    Serial.println("\n🔧 Registrando callbacks ESP-NOW nativos...");
    esp_now_register_recv_cb(onDataReceived);
    esp_now_register_send_cb(onDataSent);
    Serial.println("✅ Callbacks nativos registrados");
    
    initialized = true;
    
    Serial.println("\n📊 === INFORMAÇÕES DO DISPOSITIVO ===");
    Serial.println("🆔 Nome: ESP-NOW-SLAVE");
    Serial.println("🆔 Tipo: RelayBox");
    Serial.println("🆔 MAC: " + getLocalMacString());
    Serial.println("📶 Canal: " + String(wifiChannel));
    Serial.println("🔌 Relés: 8");
    Serial.println("💾 Heap Livre: " + String(ESP.getFreeHeap()) + " bytes");
    Serial.println("================================");
    
    // Enviar broadcast de descoberta
    Serial.println("\n📢 Enviando broadcast de descoberta...");
    Serial.println("🔍 Procurando Master na rede ESP-NOW...");
    if (sendDiscoveryBroadcast()) {
        Serial.println("✅ Broadcast enviado");
        Serial.println("⏳ Aguardando resposta do Master...");
    } else {
        Serial.println("⚠️ Falha ao enviar broadcast");
    }
    
    Serial.println("\n✅ ESP-NOW Bridge inicializado com sucesso!");
    Serial.println("🎯 Aguardando comandos do Master...");
    Serial.println("==========================================\n");
    
    return true;
}

void ESPNowBridge::update() {
    if (!initialized) return;
    
    // FASE 2: Atualizar ESPNowController
    espNowController->update();
    
    // Limpar dispositivos offline periodicamente (compatibilidade)
    static unsigned long lastCleanup = 0;
    if (millis() - lastCleanup > 60000) {  // A cada 1 minuto
        cleanupOfflineDevices();
        lastCleanup = millis();
    }
    
    // Ping dispositivos conhecidos periodicamente (compatibilidade)
    static unsigned long lastPing = 0;
    if (millis() - lastPing > 30000) {  // A cada 30 segundos
        for (const auto& device : remoteDevices) {
            if (device.online) {
                sendPing(device.mac);
            }
        }
        lastPing = millis();
    }
}

void ESPNowBridge::end() {
    if (initialized) {
        // FASE 2: Finalizar ESPNowController
        if (espNowController) {
            espNowController->end();
        }
        
        // Manter compatibilidade
        esp_now_deinit();
        initialized = false;
        Serial.println("📡 ESP-NOW Bridge finalizado");
    }
}

bool ESPNowBridge::sendRelayCommand(const uint8_t* targetMac, int relayNumber, const String& action, int duration) {
    if (!initialized) {
        Serial.println("❌ ESP-NOW não inicializado");
        return false;
    }
    
    // FASE 2: Usar ESPNowController
    bool success = espNowController->sendRelayCommand(targetMac, relayNumber, action, duration);
    
    if (success) {
        Serial.printf("📤 Comando enviado para %s: Relé %d -> %s", 
                     macToString(targetMac).c_str(), relayNumber, action.c_str());
        if (duration > 0) {
            Serial.printf(" (%ds)", duration);
        }
        Serial.println();
    }
    
    return success;
}

bool ESPNowBridge::sendPing(const uint8_t* targetMac) {
    if (!initialized) return false;
    
    // FASE 2: Usar ESPNowController
    return espNowController->sendPing(targetMac);
}

bool ESPNowBridge::sendDiscoveryBroadcast() {
    if (!initialized) return false;
    
    Serial.println("📢 Enviando broadcast de descoberta ESP-NOW...");
    
    // FASE 2: Usar ESPNowController
    return espNowController->sendDiscoveryBroadcast();
}

bool ESPNowBridge::syncRadioChannel(uint8_t channel) {
    if (!initialized || !espNowController) return false;
    return espNowController->syncRadioChannel(channel);
}

bool ESPNowBridge::ensureBroadcastPeer() {
    if (!initialized || !espNowController) return false;
    return espNowController->ensureBroadcastPeer();
}

bool ESPNowBridge::sendWiFiCredentialsBroadcast(const String& ssid, const String& password) {
    if (!initialized) return false;
    
    Serial.println("\n📢 === ENVIANDO CREDENCIAIS WiFi EM BROADCAST ===");
    
    // Validar SSID
    if (ssid.length() == 0 || ssid.length() > 32) {
        Serial.println("❌ SSID inválido (deve ter 1-32 caracteres)");
        return false;
    }
    
    // Validar senha
    if (password.length() > 63) {
        Serial.println("❌ Senha inválida (máximo 63 caracteres)");
        return false;
    }
    
    // Criar estrutura de credenciais
    WiFiCredentialsData creds;
    
    // Copiar SSID (ssid tem 33 bytes no WiFiCredentialsManager)
    strncpy(creds.ssid, ssid.c_str(), 32);
    creds.ssid[32] = '\0';
    
    // Copiar senha (password tem 64 bytes)
    strncpy(creds.password, password.c_str(), 63);
    creds.password[63] = '\0';
    
    // Obter canal atual do WiFi
    wifi_second_chan_t secondChan;
    esp_wifi_get_channel(&creds.channel, &secondChan);
    
    // Calcular checksum
    creds.calculateChecksum();
    
    // Debug
    Serial.println("📤 Dados a enviar:");
    Serial.println("   SSID: " + ssid);
    Serial.print("   Senha: ");
    for (size_t i = 0; i < password.length(); i++) Serial.print("*");
    Serial.println();
    Serial.println("   Canal: " + String(creds.channel));
    Serial.println("   Tamanho: " + String(sizeof(creds)) + " bytes");
    Serial.println("   Checksum: 0x" + String(creds.checksum, HEX));
    Serial.println("   Alcance: TODOS os dispositivos");
    
    // Validar antes de enviar
    if (!creds.isValid()) {
        Serial.println("❌ Erro: Checksum inválido antes de enviar!");
        return false;
    }
    
    // Criar mensagem ESP-NOW
    ESPNowMessage message = {};
    message.type = MessageType::WIFI_CREDENTIALS;
    
    // Configurar sender (local)
    uint8_t localMac[6];
    WiFi.macAddress(localMac);
    memcpy(message.senderId, localMac, 6);
    
    // Configurar target (broadcast)
    uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    memcpy(message.targetId, broadcastMac, 6);
    
    // Configurar dados
    message.dataSize = sizeof(WiFiCredentialsData);
    memcpy(message.data, &creds, sizeof(WiFiCredentialsData));
    
    // Enviar mensagem via ESPNowController
    bool success = espNowController->sendMessage(message, broadcastMac);
    
    if (success) {
        Serial.println("✅ Credenciais enviadas em broadcast com sucesso!");
        Serial.println("📡 Todos os dispositivos no alcance receberão");
        Serial.println("⏳ Aguarde os dispositivos conectarem (10-20 segundos)...");
        Serial.println("================================================\n");
    } else {
        Serial.println("❌ Falha ao enviar credenciais via ESP-NOW");
    }
    
    return success;
}

bool ESPNowBridge::broadcastSensorData(const String& sensorData) {
    if (!initialized) return false;
    
    // Manter compatibilidade com sistema existente
    ESPNowMessage message = {};
    message.type = MessageType::BROADCAST;
    WiFi.macAddress(message.senderId);
    memset(message.targetId, 0xFF, 6);  // Broadcast
    message.messageId = ++messageCounter;
    message.timestamp = millis();
    
    message.dataSize = min(sensorData.length(), sizeof(message.data));
    memcpy(message.data, sensorData.c_str(), message.dataSize);
    message.checksum = calculateChecksum(message);
    
    uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    return sendMessage(message, broadcastMac);
}

bool ESPNowBridge::addRemoteDevice(const uint8_t* mac, const String& name) {
    // FASE 2: Usar ESPNowController (com canal atual)
    uint8_t currentChannel;
    wifi_second_chan_t secondChan;
    esp_wifi_get_channel(&currentChannel, &secondChan);
    
    Serial.println("📍 addRemoteDevice: Adicionando no canal " + String(currentChannel));
    bool success = espNowController->addPeerWithChannel(mac, currentChannel, name);
    
    if (success) {
        // Manter compatibilidade com lista local
        for (auto& device : remoteDevices) {
            if (memcmp(device.mac, mac, 6) == 0) {
                device.online = true;
                device.lastSeen = millis();
                if (!name.isEmpty()) device.name = name;
                return true;
            }
        }
        
        // Adicionar novo dispositivo
        RemoteDevice newDevice;
        memcpy(newDevice.mac, mac, 6);
        newDevice.name = name.isEmpty() ? "Dispositivo-" + macToString(mac).substring(12) : name;
        newDevice.deviceType = "Unknown";
        newDevice.online = true;
        newDevice.lastSeen = millis();
        newDevice.rssi = -50;
        newDevice.numRelays = 8;
        newDevice.operational = true;
        
        remoteDevices.push_back(newDevice);
        
        Serial.println("✅ Dispositivo remoto adicionado: " + macToString(mac));
    }
    
    return success;
}

bool ESPNowBridge::removeRemoteDevice(const uint8_t* mac) {
    // FASE 2: Usar ESPNowController
    bool success = espNowController->removePeer(mac);
    
    if (success) {
        // Remover da lista local
        for (auto it = remoteDevices.begin(); it != remoteDevices.end(); ++it) {
            if (memcmp(it->mac, mac, 6) == 0) {
                remoteDevices.erase(it);
                break;
            }
        }
        
        Serial.println("✅ Dispositivo remoto removido: " + macToString(mac));
    }
    
    return success;
}

std::vector<RemoteDevice> ESPNowBridge::getRemoteDevices() {
    return remoteDevices;
}

bool ESPNowBridge::isDeviceOnline(const uint8_t* mac) {
    for (const auto& device : remoteDevices) {
        if (memcmp(device.mac, mac, 6) == 0) {
            return device.online;
        }
    }
    return false;
}

int ESPNowBridge::getOnlineDeviceCount() {
    int count = 0;
    for (const auto& device : remoteDevices) {
        if (device.online) count++;
    }
    return count;
}

// ===== NOVOS MÉTODOS FASE 2 =====

std::vector<PeerInfo> ESPNowBridge::getPeerList() {
    if (espNowController) {
        return espNowController->getPeerList();
    }
    return std::vector<PeerInfo>();
}

String ESPNowBridge::getDetailedStatsJSON() {
    if (espNowController) {
        return espNowController->getStatsJSON();
    }
    return "{}";
}

bool ESPNowBridge::forceDiscovery() {
    if (espNowController) {
        return espNowController->sendDiscoveryBroadcast();
    }
    return false;
}

bool ESPNowBridge::initiateHandshake(const uint8_t* targetMac) {
    if (!initialized || !espNowController) {
        Serial.println("❌ ESP-NOW não inicializado");
        return false;
    }
    
    bool success = espNowController->initiateHandshake(targetMac);
    if (success) {
        Serial.println("🤝 Handshake iniciado com: " + macToString(targetMac));
    } else {
        Serial.println("❌ Falha ao iniciar handshake");
    }
    
    return success;
}

bool ESPNowBridge::requestConnectivityCheck(const uint8_t* targetMac) {
    if (!initialized || !espNowController) {
        Serial.println("❌ ESP-NOW não inicializado");
        return false;
    }
    
    bool success = espNowController->requestConnectivityCheck(targetMac);
    if (success) {
        Serial.println("🔍 Solicitação de conectividade enviada para: " + macToString(targetMac));
    } else {
        Serial.println("❌ Falha ao solicitar verificação de conectividade");
    }
    
    return success;
}

bool ESPNowBridge::sendConnectivityReport(const uint8_t* targetMac, uint32_t sessionId) {
    if (!initialized || !espNowController) {
        Serial.println("❌ ESP-NOW não inicializado");
        return false;
    }
    
    bool success = espNowController->sendConnectivityReport(targetMac, sessionId);
    if (success) {
        String targetStr = targetMac ? macToString(targetMac) : "BROADCAST";
        Serial.println("📊 Relatório de conectividade enviado para: " + targetStr);
    } else {
        Serial.println("❌ Falha ao enviar relatório de conectividade");
    }
    
    return success;
}

uint32_t ESPNowBridge::generateSessionId() {
    if (!espNowController) {
        Serial.println("❌ ESPNowController não disponível");
        return 0;
    }
    
    return espNowController->generateSessionId();
}

void ESPNowBridge::enableMultiChannelListening() {
    Serial.println("🔧 === ATIVANDO MULTI-CHANNEL LISTENING ===");
    
    if (!espNowController) {
        Serial.println("❌ ESPNowController não inicializado");
        return;
    }
    
    // Configurar ESP-NOW para escutar em todos os canais 2.4GHz (1-13)
    Serial.println("📡 Configurando ESP-NOW para escutar em canais 1-13...");
    
    // Em vez de modo promiscuous, vamos configurar ESP-NOW para escutar em canal 0 (todos)
    // Isso permite receber mensagens de qualquer canal
    esp_wifi_set_channel(0, WIFI_SECOND_CHAN_NONE);
    
    Serial.println("✅ Multi-channel listening configurado");
    Serial.println("📡 ESP-NOW agora escuta em todos os canais 2.4GHz");
    Serial.println("==========================================");
}

// ===== CALLBACKS =====

void ESPNowBridge::setRemoteRelayStatusCallback(void (*callback)(const uint8_t* mac, int relay, bool state, int remainingTime, const String& name)) {
    this->remoteRelayStatusCallback = callback;
}

void ESPNowBridge::setDeviceDiscoveryCallback(void (*callback)(const uint8_t* mac, const String& name, const String& type, bool operational)) {
    this->deviceDiscoveryCallback = callback;
}

void ESPNowBridge::setErrorCallback(void (*callback)(const String& error)) {
    this->errorCallback = callback;
}

void ESPNowBridge::setPingCallback(void (*callback)(const uint8_t* senderMac)) {
    if (espNowController) {
        espNowController->setPingCallback(callback);
    }
}

void ESPNowBridge::setMessageReceivedCallback(void (*callback)(const uint8_t* mac, const uint8_t* data, int len)) {
    this->messageReceivedCallback = callback;
}

// ===== CALLBACKS DO ESPNOWCONTROLLER =====

void ESPNowBridge::onRelayCommandReceived(const uint8_t* senderMac, int relayNumber, const String& action, int duration) {
    if (!instance) return;
    
    Serial.println("\n🎯 ==========================================");
    Serial.println("🎯 COMANDO DE RELÉ RECEBIDO DO MASTER!");
    Serial.println("🎯 ==========================================");
    Serial.println("📥 De: " + macToString(senderMac));
    Serial.println("🔌 Relé: " + String(relayNumber));
    Serial.println("⚡ Ação: " + action);
    if (duration > 0) {
        Serial.println("⏱️  Duração: " + String(duration) + " segundos");
    } else {
        Serial.println("⏱️  Duração: Permanente");
    }
    Serial.println("==========================================");
    
    // Processar comando no RelayController local
    if (instance->localRelayController) {
        Serial.println("\n⚡ Executando comando no RelayController local...");
        
        #ifdef SLAVE_MODE
        // No modo Slave, executar o comando recebido
        if (action == "on" || action == "ON") {
            if (duration > 0) {
                instance->localRelayController->setRelayWithTimer(relayNumber, true, duration);
                Serial.println("✅ Relé " + String(relayNumber) + " LIGADO por " + String(duration) + " segundos");
                Serial.println("⏲️  Timer ativo - Desligará automaticamente");
            } else {
                instance->localRelayController->setRelay(relayNumber, true);
                Serial.println("✅ Relé " + String(relayNumber) + " LIGADO permanentemente");
                Serial.println("⚠️  Sem timer - Ficará ligado até comando OFF");
            }
        } else if (action == "off" || action == "OFF") {
            instance->localRelayController->setRelay(relayNumber, false);
            Serial.println("✅ Relé " + String(relayNumber) + " DESLIGADO");
        } else if (action == "toggle" || action == "TOGGLE") {
            instance->localRelayController->toggleRelay(relayNumber);
            Serial.println("✅ Relé " + String(relayNumber) + " ALTERNADO");
        } else {
            Serial.println("❌ Ação desconhecida: " + action);
            Serial.println("💡 Ações válidas: on, off, toggle");
        }
        #else
        Serial.println("⚠️ Modo SLAVE não está ativado (#define SLAVE_MODE)");
        Serial.println("💡 Comando recebido mas não será executado");
        #endif
    } else {
        Serial.println("❌ RelayController local não disponível!");
        Serial.println("⚠️ Comando não pode ser executado");
    }
    
    Serial.println("==========================================\n");
}

void ESPNowBridge::onRelayStatusReceived(const uint8_t* senderMac, int relayNumber, bool state, bool hasTimer, int remainingTime, const String& name) {
    if (!instance) return;
    Serial.printf("📥 Status remoto de %s: %s -> %s", 
                 macToString(senderMac).c_str(),
                 name.c_str(), 
                 state ? "ON" : "OFF");
    
    if (hasTimer) {
        Serial.printf(" (%ds restantes)", remainingTime);
    }
    Serial.println();
    
    // Chamar callback de compatibilidade
    if (instance->remoteRelayStatusCallback) {
        instance->remoteRelayStatusCallback(senderMac, relayNumber, state, remainingTime, name);
    }
}

void ESPNowBridge::onDeviceInfoReceived(const uint8_t* senderMac, const String& deviceName, const String& deviceType, uint8_t numRelays, bool operational, uint8_t wifiChannel) {
    if (!instance) return;
    
    Serial.println("\n🎉 ========================================");
    Serial.println("🎉 DEVICE INFO RECEBIDO (SLAVE)");
    Serial.println("🎉 ========================================");
    Serial.println("📥 Master descoberto: " + deviceName + " (" + deviceType + ")");
    Serial.println("📡 MAC Master: " + macToString(senderMac));
    Serial.println("🔌 Relés Master: " + String(numRelays));
    Serial.println("✅ Operacional: " + String(operational ? "Sim" : "Não"));
    Serial.println("📶 Canal WiFi Master: " + String(wifiChannel) + " (recebido no DeviceInfo)");
    
    // ✅ PEER JÁ FOI ADICIONADO: ESPNowController já chamou addPeerWithChannel()
    // quando processou a mensagem DEVICE_INFO com o canal correto!
    if (instance->espNowController) {
        // Verificar se Master foi registrado corretamente
        if (instance->espNowController->peerExists(senderMac)) {
            Serial.println("\n✅ Master já está registrado como peer (adicionado por ESPNowController)");
            Serial.println("📡 Comunicação bidirecional ATIVA!");
        } else {
            Serial.println("\n⚠️ ATENÇÃO: Master não foi registrado automaticamente");
            Serial.println("💡 ESPNowController deveria ter chamado addPeerWithChannel()");
        }
        
        // ⭐ RESPONDER AUTOMATICAMENTE com nosso DeviceInfo (Discovery Response)
        Serial.println("\n📤 Enviando nosso DeviceInfo de volta para Master...");
        Serial.println("   Nome: ESP-NOW-SLAVE");
        Serial.println("   Tipo: RelayBox");
        Serial.println("   Relés: 8");
        Serial.println("   Status: Operacional");
        Serial.println("   Uptime: " + String(millis() / 1000) + "s");
        Serial.println("   Heap Livre: " + String(ESP.getFreeHeap()) + " bytes");
        
        bool sent = instance->espNowController->sendDeviceInfo(
            senderMac,           // Para o Master que nos descobriu
            "RelayBox",          // Tipo do dispositivo
            8,                   // 8 relés disponíveis
            true,                // Operacional
            millis(),            // Uptime atual
            ESP.getFreeHeap()    // Memória livre
        );
        
        if (sent) {
            Serial.println("\n✅ DeviceInfo enviado com sucesso!");
            Serial.println("🎯 Master deve adicionar este Slave à lista knownSlaves");
            Serial.println("🔗 Comunicação bidirecional estabelecida!");
        } else {
            Serial.println("\n❌ Falha ao enviar DeviceInfo");
            Serial.println("⚠️ Master pode não reconhecer este Slave");
            Serial.println("💡 Tentando re-enviar...");
            
            // Tentar novamente após 100ms
            delay(100);
            sent = instance->espNowController->sendDeviceInfo(
                senderMac, "RelayBox", 8, true, millis(), ESP.getFreeHeap()
            );
            
            if (sent) {
                Serial.println("✅ DeviceInfo re-enviado com sucesso!");
            } else {
                Serial.println("❌ Falha definitiva no envio");
            }
        }
    } else {
        Serial.println("❌ ESPNowController não disponível!");
    }
    
    // Atualizar lista local de dispositivos remotos
    instance->updateRemoteDevice(senderMac, deviceName, deviceType, operational);
    
    // Chamar callback de compatibilidade (se definido)
    if (instance->deviceDiscoveryCallback) {
        instance->deviceDiscoveryCallback(senderMac, deviceName, deviceType, operational);
    }
    
    Serial.println("========================================\n");
}

void ESPNowBridge::onPingReceived(const uint8_t* senderMac) {
    if (!instance) return;
    
    Serial.println("\n🏓 ========================================");
    Serial.println("🏓 PING RECEBIDO");
    Serial.println("🏓 ========================================");
    Serial.println("📥 De: " + macToString(senderMac));
    Serial.println("⏰ Timestamp: " + String(millis() / 1000) + "s");
    
    // ⭐ Registrar sender como peer se não existir (usando canal atual)
    bool isNewPeer = false;
    if (instance->espNowController) {
        if (!instance->espNowController->peerExists(senderMac)) {
            Serial.println("\n🔗 Peer não registrado, adicionando...");
            isNewPeer = true;
            
            // ✅ CORREÇÃO: Usar canal atual (já que recebemos mensagem, estamos sincronizados)
            uint8_t currentChannel;
            wifi_second_chan_t secondChan;
            esp_wifi_get_channel(&currentChannel, &secondChan);
            
            Serial.println("📶 Adicionando peer no canal atual: " + String(currentChannel));
            
            if (instance->espNowController->addPeerWithChannel(senderMac, currentChannel, "Master")) {
                Serial.println("✅ Peer registrado ao receber ping (canal " + String(currentChannel) + ")");
            } else {
                Serial.println("❌ Falha ao registrar peer");
            }
        } else {
            Serial.println("✅ Peer já registrado");
        }
        
        // 🚨 CRÍTICO: Enviar DeviceInfo automaticamente quando recebe PING do master
        // Isso garante que o master registre o slave mesmo se não enviar DeviceInfo primeiro
        if (isNewPeer) {
            Serial.println("\n📤 [AUTO] Enviando DeviceInfo para Master (primeira vez)...");
            Serial.println("   💡 Master precisa registrar este Slave em trustedSlaves");
            
            bool sent = instance->espNowController->sendDeviceInfo(
                senderMac,           // Para o Master que enviou PING
                "RelayBox",          // Tipo do dispositivo
                8,                   // 8 relés disponíveis
                true,                // Operacional
                millis(),            // Uptime atual
                ESP.getFreeHeap()    // Memória livre
            );
            
            if (sent) {
                Serial.println("✅ DeviceInfo enviado automaticamente!");
                Serial.println("🎯 Master deve registrar este Slave agora");
            } else {
                Serial.println("❌ Falha ao enviar DeviceInfo automaticamente");
                Serial.println("💡 Tentando novamente em 200ms...");
                delay(200);
                sent = instance->espNowController->sendDeviceInfo(
                    senderMac, "RelayBox", 8, true, millis(), ESP.getFreeHeap()
                );
                if (sent) {
                    Serial.println("✅ DeviceInfo re-enviado com sucesso!");
                }
            }
        }
        
        // Responder automaticamente com PONG
        Serial.println("\n🏓 Enviando PONG para: " + macToString(senderMac));
        if (instance->espNowController->sendPing(senderMac)) {
            Serial.println("✅ PONG enviado com sucesso!");
            Serial.println("🔗 Conectividade confirmada");
        } else {
            Serial.println("❌ Falha ao enviar PONG");
        }
    } else {
        Serial.println("❌ ESPNowController não disponível");
    }
    
    Serial.println("========================================\n");
}

void ESPNowBridge::onWiFiCredentialsReceived(const String& ssid, const String& password, uint8_t channel) {
    Serial.println("\n🔔 === CREDENCIAIS WiFi RECEBIDAS ===");
    Serial.println("📡 SSID: " + ssid);
    Serial.println("📶 Canal Master: " + String(channel));
    
    if (!instance) {
        Serial.println("❌ ERRO: Instância estática é nullptr!");
        return;
    }
    
    // ⭐ CRÍTICO: Mudar para canal do Master IMEDIATAMENTE
    Serial.println("\n🔧 SINCRONIZANDO CANAL COM MASTER...");
    Serial.println("   Canal anterior: " + String(instance->wifiChannel));
    Serial.println("   Canal novo (Master): " + String(channel));
    
    // Parar scan de canais (definir flag global)
    extern bool channelSyncCompleted;
    channelSyncCompleted = true;
    Serial.println("   ✅ Scan de canais desativado");
    
    // Mudar para canal do Master
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    instance->wifiChannel = channel;
    Serial.println("   ✅ Canal ESP-NOW sincronizado: " + String(channel));
    
    // Conectar ao WiFi
    Serial.println("\n🔌 Conectando ao WiFi...");
    Serial.println("   SSID: " + ssid);
    
    // Conectar ao WiFi
    WiFi.begin(ssid.c_str(), password.c_str());
    
    // Aguardar conexão (máximo 30 segundos)
    unsigned long startTime = millis();
    int dots = 0;
    
    while (WiFi.status() != WL_CONNECTED && (millis() - startTime < 30000)) {
        delay(500);
        Serial.print(".");
        dots++;
        if (dots >= 60) {
            Serial.println();
            dots = 0;
        }
    }
    
    if (dots > 0) Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("✅ Conectado ao WiFi com sucesso!");
        Serial.println("🌐 IP: " + WiFi.localIP().toString());
        Serial.println("📶 SSID: " + WiFi.SSID());
        
        // Obter canal real do WiFi conectado
        uint8_t connectedChannel = WiFi.channel();
        Serial.println("📡 Canal conectado: " + String(connectedChannel));
        
        // ⭐ CRÍTICO: Sincronizar canal ESP-NOW com WiFi
        if (instance) {
            instance->wifiChannel = connectedChannel;
            Serial.println("🔧 Canal ESP-NOW atualizado: " + String(instance->wifiChannel));
            Serial.println("✅ Sincronização de canal completa!");
        }
        
        // ✅ CORREÇÃO: Salvar credenciais no namespace correεια 
        Preferences prefs;
        if (prefs.begin("wifi_creds", false)) {  // Mesmo namespace do WiFiCredentialsManager
            prefs.putString("ssid", ssid);
            prefs.putString("password", password);
            prefs.putUChar("channel", connectedChannel);  // Salvar canal real conectado
            prefs.end();
            Serial.println("💾 Credenciais salvas para reconexão automática");
        } else {
            Serial.println("⚠️ Não foi possível salvar credenciais (NVS indisponAgain)");
        }
        
        // ⭐ CRÍTICO: Chatter ESP-NOW no canal correto após conexão WiFi
        if (instance && instance->espNowController) {
            Serial.println("\n🔄 Reiniciando ESP-NOW no canal sincronizado...");
            
            // Finalizar ESP-NOW atual
            instance->espNowController->end();
            delay(100);
            
            // Reconfigurar canal WiFi
            esp_wifi_set_channel(connectedChannel, WIFI_SECOND_CHAN_NONE);
            Serial.println("   Canal WiFi reconfigurado: " + String(connectedChannel));
            
            // Reinicializar ESP-NOW no canal correto
            if (instance->espNowController->begin()) {
                Serial.println("✅ ESP-NOW reiniciado no canal " + String(connectedChannel));
                Serial.println("🎯 MASTER e SLAVE agora estão sincronizados!");
            } else {
                Serial.println("❌ Falha ao reiniciar ESP-NOW");
            }
        }
        
        Serial.println("==========================================\n");
    } else {
        Serial.println("❌ Falha ao conectar ao WiFi");
        Serial.println("💡 Verifique se as credenciais estão corretas");
        Serial.println("💡 Verifique se a rede está no alcance");
        Serial.println("==========================================\n");
    }
}

void ESPNowBridge::onErrorReceived(const String& error) {
    if (!instance) return;
    Serial.println("❌ Erro ESP-NOW: " + error);
    
    if (instance->errorCallback) {
        instance->errorCallback(error);
    }
}

// ===== UTILITÁRIOS =====

String ESPNowBridge::macToString(const uint8_t* mac) {
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(macStr);
}

bool ESPNowBridge::stringToMac(const String& macStr, uint8_t* mac) {
    if (macStr.length() != 17) return false;
    
    int values[6];
    if (sscanf(macStr.c_str(), "%x:%x:%x:%x:%x:%x", 
               &values[0], &values[1], &values[2], 
               &values[3], &values[4], &values[5]) != 6) {
        return false;
    }
    
    for (int i = 0; i < 6; i++) {
        mac[i] = (uint8_t)values[i];
    }
    
    return true;
}

String ESPNowBridge::getLocalMacString() {
    if (espNowController) {
        return espNowController->getLocalMacString();
    }
    
    uint8_t mac[6];
    WiFi.macAddress(mac);
    return macToString(mac);
}

ESPNowController* ESPNowBridge::getESPNowController() {
    return espNowController;
}

String ESPNowBridge::getStatsJSON() {
    DynamicJsonDocument doc(512);
    
    doc["initialized"] = initialized;
    doc["channel"] = wifiChannel;
    doc["localMac"] = getLocalMacString();
    doc["messagesSent"] = messagesSent;
    doc["messagesReceived"] = messagesReceived;
    doc["messagesLost"] = messagesLost;
    doc["remoteDevices"] = remoteDevices.size();
    doc["onlineDevices"] = getOnlineDeviceCount();
    
    // FASE 2: Adicionar estatísticas do ESPNowController
    if (espNowController) {
        doc["espNowController"] = true;
        doc["peerCount"] = espNowController->getPeerCount();
    }
    
    String result;
    serializeJson(doc, result);
    return result;
}

void ESPNowBridge::printStatus() {
    Serial.println("\n📡 === STATUS ESP-NOW BRIDGE (FASE 2) ===");
    Serial.println("✅ Inicializado: " + String(initialized ? "Sim" : "Não"));
    Serial.println("📶 Canal: " + String(wifiChannel));
    Serial.println("🆔 MAC Local: " + getLocalMacString());
    Serial.println("📊 Mensagens enviadas: " + String(messagesSent));
    Serial.println("📊 Mensagens recebidas: " + String(messagesReceived));
    Serial.println("📊 Mensagens perdidas: " + String(messagesLost));
    Serial.println("👥 Dispositivos remotos: " + String(remoteDevices.size()));
    Serial.println("🟢 Dispositivos online: " + String(getOnlineDeviceCount()));
    
    // FASE 2: Mostrar estatísticas do ESPNowController
    if (espNowController) {
        Serial.println("🔧 ESPNowController: Ativo");
        Serial.println("👥 Peers ESP-NOW: " + String(espNowController->getPeerCount()));
    }
    
    if (!remoteDevices.empty()) {
        Serial.println("\n👥 === DISPOSITIVOS REMOTOS ===");
        for (const auto& device : remoteDevices) {
            Serial.println("   " + macToString(device.mac) + " | " + 
                          device.name + " (" + device.deviceType + ") | " +
                          (device.online ? "🟢 Online" : "🔴 Offline") + 
                          " | Relés: " + String(device.numRelays));
        }
    }
    
    Serial.println("================================\n");
}

// ===== MÉTODOS PRIVADOS (COMPATIBILIDADE) =====

bool ESPNowBridge::sendMessage(const ESPNowMessage& message, const uint8_t* targetMac) {
    if (!initialized) return false;
    
    esp_err_t result = esp_now_send(targetMac, (uint8_t*)&message, sizeof(ESPNowMessage));
    
    if (result == ESP_OK) {
        messagesSent++;
        return true;
    } else {
        messagesLost++;
        Serial.println("❌ Erro ao enviar mensagem ESP-NOW: " + String(result));
        return false;
    }
}

void ESPNowBridge::processReceivedMessage(const ESPNowMessage& message, const uint8_t* senderMac) {
    if (!validateMessage(message)) {
        Serial.println("❌ Mensagem ESP-NOW inválida de: " + macToString(senderMac));
        return;
    }
    
    // Atualizar dispositivo remoto
    updateRemoteDevice(senderMac, "", "", true);
    
    switch (message.type) {
        case MessageType::RELAY_COMMAND: {
            // 🔌 COMANDO DE RELÉ recebido do MASTER!
            if (message.dataSize >= sizeof(RelayCommandData)) {
                RelayCommandData cmdData;
                memcpy(&cmdData, message.data, sizeof(RelayCommandData));
                
                Serial.println("\n🔌 ========================================");
                Serial.println("🔌 COMANDO DE RELÉ RECEBIDO DO MASTER!");
                Serial.println("🔌 ========================================");
                Serial.println("📥 De: " + macToString(senderMac));
                Serial.println("🔌 Relé: " + String(cmdData.relayNumber));
                Serial.println("🎯 Ação: " + String(cmdData.action));
                Serial.println("⏱️ Duração: " + String(cmdData.duration) + "s");
                Serial.println("========================================\n");
                
                // 🔄 Delegar para o ESPNowController processar
                // O ESPNowController tem o callback conectado ao RelayBox
                if (espNowController) {
                    Serial.println("🔄 Delegando para ESPNowController processar...");
                    // Criar uma cópia da mensagem e passar para o ESPNowController
                    ESPNowMessage msgCopy = message;
                    // O processReceivedMessage do ESPNowController é privado, 
                    // mas podemos usar os métodos públicos via callback estático
                    // que foi registrado no esp_now_register_recv_cb
                    
                    // Alternativa: chamar diretamente o método do RelayBox
                    #ifdef SLAVE_MODE
                    extern RelayCommandBox relayBox;
                    
                    String action = String(cmdData.action);
                    Serial.println("⚡ Executando comando no RelayBox...");
                    
                    if (action == "on") {
                        if (cmdData.duration > 0) {
                            relayBox.setRelayWithTimer(cmdData.relayNumber, true, cmdData.duration);
                        } else {
                            relayBox.setRelay(cmdData.relayNumber, true);
                        }
                        Serial.println("✅ Relé " + String(cmdData.relayNumber) + " LIGADO");
                    } else if (action == "off") {
                        relayBox.setRelay(cmdData.relayNumber, false);
                        Serial.println("✅ Relé " + String(cmdData.relayNumber) + " DESLIGADO");
                    } else if (action == "toggle") {
                        relayBox.toggleRelay(cmdData.relayNumber);
                        Serial.println("✅ Relé " + String(cmdData.relayNumber) + " ALTERNADO");
                    }
                    
                    // 🔄 FASE 3: Enviar estado de TODOS os relays de volta ao MASTER
                    Serial.println("\n📤 Enviando estado de TODOS os relays ao MASTER...");
                    
                    // Incluir ESPNowTypes.h para AllRelaysStatus
                    #include "ESPNowTypes.h"
                    
                    // Coletar estado de todos os relays
                    AllRelaysStatus allStatus;
                    allStatus.timestamp = millis();
                    allStatus.numRelays = 8;
                    
                    for (int i = 0; i < 8; i++) {
                        allStatus.relays[i].state = relayBox.getRelayState(i) ? 1 : 0;
                        allStatus.relays[i].hasTimer = 0; // TODO: Implementar detecção de timer ativo
                        allStatus.relays[i].remainingTime = 0; // TODO: Implementar obtenção de tempo restante
                    }
                    
                    // Calcular checksum simples
                    allStatus.checksum = 0;
                    uint8_t* ptr = (uint8_t*)&allStatus;
                    for (size_t i = 0; i < sizeof(AllRelaysStatus) - 1; i++) {
                        allStatus.checksum ^= ptr[i];
                    }
                    
                    // Criar mensagem ESP-NOW
                    ESPNowMessage statusMsg = {};
                    statusMsg.type = MessageType::ALL_RELAYS_STATUS;
                    WiFi.macAddress(statusMsg.senderId);
                    memcpy(statusMsg.targetId, senderMac, 6);
                    statusMsg.messageId = ++messageCounter;
                    statusMsg.timestamp = millis();
                    statusMsg.dataSize = sizeof(AllRelaysStatus);
                    memcpy(statusMsg.data, &allStatus, sizeof(AllRelaysStatus));
                    statusMsg.checksum = calculateChecksum(statusMsg);
                    
                    // Enviar estado completo
                    if (sendMessage(statusMsg, senderMac)) {
                        Serial.println("✅ Estado completo de todos os relays enviado!");
                        Serial.println("📊 " + String(allStatus.numRelays) + " relays sincronizados");
                    } else {
                        Serial.println("❌ Falha ao enviar estado dos relays");
                    }
                    #endif
                }
            }
            break;
        }
        
        case MessageType::RELAY_STATUS: {
            if (remoteRelayStatusCallback && message.dataSize >= sizeof(RelayStatusData)) {
                RelayStatusData statusData;
                memcpy(&statusData, message.data, sizeof(RelayStatusData));
                
                Serial.printf("📥 Status remoto de %s: %s -> %s", 
                             macToString(senderMac).c_str(),
                             statusData.name, 
                             statusData.state ? "ON" : "OFF");
                
                if (statusData.hasTimer) {
                    Serial.printf(" (%ds restantes)", statusData.remainingTime);
                }
                Serial.println();
                
                remoteRelayStatusCallback(senderMac, statusData.relayNumber, statusData.state, 
                                        statusData.remainingTime, String(statusData.name));
            }
            break;
        }
        
        case MessageType::DEVICE_INFO: {
            if (deviceDiscoveryCallback) {
                // Parse JSON dos dados do dispositivo
                DynamicJsonDocument doc(256);
                deserializeJson(doc, (char*)message.data, message.dataSize);
                
                String name = doc["name"] | "Unknown";
                String type = doc["type"] | "Unknown";
                bool operational = doc["operational"] | false;
                
                Serial.println("📥 Dispositivo descoberto: " + name + " (" + type + ") de " + macToString(senderMac));
                
                updateRemoteDevice(senderMac, name, type, operational);
                deviceDiscoveryCallback(senderMac, name, type, operational);
            }
            break;
        }
        
        case MessageType::PING: {
            Serial.println("🏓 Ping recebido de: " + macToString(senderMac));
            
            // Responder com PONG
            ESPNowMessage pongMsg = {};
            pongMsg.type = MessageType::PONG;
            WiFi.macAddress(pongMsg.senderId);
            memcpy(pongMsg.targetId, senderMac, 6);
            pongMsg.messageId = ++messageCounter;
            pongMsg.timestamp = millis();
            pongMsg.dataSize = 0;
            pongMsg.checksum = calculateChecksum(pongMsg);
            
            sendMessage(pongMsg, senderMac);
            break;
        }
        
        case MessageType::PONG: {
            Serial.println("🏓 Pong recebido de: " + macToString(senderMac));
            break;
        }
        
        case MessageType::WIFI_CREDENTIALS: {
            #ifdef SLAVE_MODE
            Serial.println("\n📶 ========================================");
            Serial.println("📶 CREDENCIAIS WiFi RECEBIDAS DO MASTER!");
            Serial.println("📶 De: " + macToString(senderMac));
            
            if (message.dataSize >= sizeof(WiFiCredentialsData)) {
                WiFiCredentialsData creds;
                memcpy(&creds, message.data, sizeof(WiFiCredentialsData));
                
                Serial.println("📶 SSID: " + String(creds.ssid));
                Serial.println("📶 Canal: " + String(creds.channel));
                Serial.println("📶 ========================================\n");
                
                // Processar credenciais (conectar ao WiFi)
                extern WiFiCredentialsManager wifiManager;
                if (wifiManager.connectToWiFi(String(creds.ssid), String(creds.password))) {
                    Serial.println("✅ Conectado ao WiFi via credenciais ESP-NOW!");
                    // Salvar credenciais para reconexão automática
                    wifiManager.saveCredentials(String(creds.ssid), String(creds.password), creds.channel);
                } else {
                    Serial.println("❌ Falha ao conectar com credenciais recebidas");
                }
            } else {
                Serial.println("❌ Tamanho de credenciais inválido: " + String(message.dataSize));
            }
            #endif
            break;
        }
        
        case MessageType::ERROR: {
            Serial.println("\n❌ ========================================");
            Serial.println("❌ MENSAGEM DE ERRO RECEBIDA");
            Serial.println("❌ De: " + macToString(senderMac));
            if (message.dataSize > 0) {
                String errorMsg = String((char*)message.data);
                Serial.println("📄 Mensagem: " + errorMsg);
            }
            Serial.println("❌ ========================================\n");
            break;
        }
        
        case MessageType::HANDSHAKE_REQUEST: {
            Serial.println("🤝 Handshake Request recebido de: " + macToString(senderMac));
            // Responder com HANDSHAKE_RESPONSE
            ESPNowMessage response = {};
            response.type = MessageType::HANDSHAKE_RESPONSE;
            WiFi.macAddress(response.senderId);
            memcpy(response.targetId, senderMac, 6);
            response.messageId = ++messageCounter;
            response.timestamp = millis();
            response.dataSize = 0;
            response.checksum = calculateChecksum(response);
            sendMessage(response, senderMac);
            Serial.println("✅ Handshake Response enviado!");
            break;
        }
        
        case MessageType::HANDSHAKE_RESPONSE: {
            Serial.println("🤝 Handshake Response recebido de: " + macToString(senderMac));
            break;
        }
        
        case MessageType::CONNECTIVITY_CHECK: {
            Serial.println("🔍 Connectivity Check recebido de: " + macToString(senderMac));
            break;
        }
        
        case MessageType::CONNECTIVITY_REPORT: {
            Serial.println("📊 Connectivity Report recebido de: " + macToString(senderMac));
            break;
        }
        
        default:
            Serial.println("❓ Tipo de mensagem ESP-NOW desconhecido: " + String((int)message.type));
            break;
    }
}

uint8_t ESPNowBridge::calculateChecksum(const ESPNowMessage& message) {
    uint8_t checksum = 0;
    const uint8_t* data = (const uint8_t*)&message;
    
    for (size_t i = 0; i < sizeof(ESPNowMessage) - 1; i++) {
        checksum ^= data[i];
    }
    
    return checksum;
}

bool ESPNowBridge::validateMessage(const ESPNowMessage& message) {
    // Verificar checksum
    ESPNowMessage tempMsg = message;
    tempMsg.checksum = 0;
    uint8_t calculatedChecksum = calculateChecksum(tempMsg);
    
    if (calculatedChecksum != message.checksum) {
        Serial.println("❌ Checksum inválido");
        Serial.println("   Esperado: " + String(calculatedChecksum));
        Serial.println("   Recebido: " + String(message.checksum));
        return false;
    }
    
    // Verificar tamanho dos dados
    if (message.dataSize > sizeof(message.data)) {
        Serial.println("❌ Tamanho de dados inválido: " + String(message.dataSize));
        return false;
    }
    
    // NOTA: Validação de timestamp desabilitada
    // Master e Slave podem ter millis() diferentes (reiniciados em momentos diferentes)
    // Timestamp é usado apenas para referência, não para validação
    // Se precisar validar, use um sistema de sincronização de tempo (NTP)
    
    // ✅ Verificar tipo de mensagem válido (atualizado para incluir novos tipos)
    if (message.type > MessageType::CONNECTIVITY_REPORT) {  // 0x0D = último tipo válido
        Serial.println("❌ Tipo de mensagem inválido: " + String((int)message.type));
        Serial.println("💡 Tipos válidos: 0x01 a 0x0D");
        Serial.println("💡 Tipo recebido: 0x" + String((int)message.type, HEX));
        return false;
    }
    
    return true;
}

void ESPNowBridge::updateRemoteDevice(const uint8_t* mac, const String& name, const String& deviceType, bool operational) {
    for (auto& device : remoteDevices) {
        if (memcmp(device.mac, mac, 6) == 0) {
            device.online = true;
            device.lastSeen = millis();
            device.operational = operational;
            
            if (!name.isEmpty()) device.name = name;
            if (!deviceType.isEmpty()) device.deviceType = deviceType;
            return;
        }
    }
    
    // Se não encontrou, adicionar automaticamente
    addRemoteDevice(mac, name);
}

void ESPNowBridge::cleanupOfflineDevices() {
    unsigned long currentTime = millis();
    const unsigned long offlineTimeout = 120000;  // 2 minutos
    
    for (auto& device : remoteDevices) {
        if (currentTime - device.lastSeen > offlineTimeout) {
            if (device.online) {
                Serial.println("🔴 Dispositivo offline: " + macToString(device.mac));
                device.online = false;
            }
        }
    }
}

// ===== CALLBACKS ESTÁTICOS (COMPATIBILIDADE) =====

void ESPNowBridge::onDataReceived(const uint8_t* mac, const uint8_t* incomingData, int len) {
    if (!instance) return;
    
    // 🚨 CRÍTICO: Notificar callback genérico PRIMEIRO (para MultiChannelDiscovery)
    if (instance->messageReceivedCallback) {
        instance->messageReceivedCallback(mac, incomingData, len);
    }
    
    // LOG: Mensagem recebida
    Serial.println("\n📨 ========================================");
    Serial.println("📨 MENSAGEM ESP-NOW RECEBIDA!");
    Serial.println("📨 De: " + macToString(mac));
    Serial.println("📨 Tamanho: " + String(len) + " bytes");
    
    // Verificar se é uma mensagem de credenciais WiFi (nova estrutura)
    #ifdef SLAVE_MODE
    if (len == sizeof(WiFiCredentialsData)) {  // Tamanho de WiFiCredentialsData
        Serial.println("📨 Tipo: CREDENCIAIS WiFi");
        Serial.println("📨 ========================================\n");
        
        // Chamar função externa para processar credenciais
        extern void processWiFiCredentials(const uint8_t* data, int len);
        processWiFiCredentials(incomingData, len);
        return;
    }
    #endif
    
    // Mensagem ESP-NOW normal (aceitar pequenas diferenças de alinhamento)
    int expectedSize = sizeof(ESPNowMessage);
    int sizeDiff = abs(len - expectedSize);
    
    if (sizeDiff > 4) {  // Permitir diferença de até 4 bytes (alinhamento)
        Serial.println("📨 Tipo: DESCONHECIDO/INVÁLIDO");
        Serial.println("📨 ========================================\n");
        Serial.println("❌ Tamanho de mensagem ESP-NOW inválido: " + String(len));
        Serial.println("💡 Esperado: " + String(expectedSize) + " bytes");
        Serial.println("💡 Diferença: " + String(sizeDiff) + " bytes (MUITA diferença!)");
        return;
    } else if (sizeDiff > 0) {
        // Apenas log de debug - diferença pequena é OK
        // Serial.println("⚠️ Diferença de tamanho: " + String(sizeDiff) + " bytes (alinhamento OK)");
    }
    
    // Copiar mensagem (usar o menor tamanho para evitar buffer overflow)
    ESPNowMessage message;
    memset(&message, 0, sizeof(ESPNowMessage));
    int copySize = min(len, (int)sizeof(ESPNowMessage));
    memcpy(&message, incomingData, copySize);
    
    // LOG: Tipo de mensagem
    String msgType = "DESCONHECIDO";
    switch (message.type) {
        case MessageType::RELAY_COMMAND: msgType = "COMANDO DE RELÉ"; break;
        case MessageType::RELAY_STATUS: msgType = "STATUS DE RELÉ"; break;
        case MessageType::DEVICE_INFO: msgType = "INFO DO DISPOSITIVO"; break;
        case MessageType::PING: msgType = "PING"; break;
        case MessageType::PONG: msgType = "PONG"; break;
        case MessageType::BROADCAST: msgType = "BROADCAST"; break;
        case MessageType::ACK: msgType = "ACK (CONFIRMAÇÃO)"; break;
        case MessageType::ERROR: msgType = "ERRO"; break;
        case MessageType::WIFI_CREDENTIALS: msgType = "CREDENCIAIS WiFi"; break;
        case MessageType::HANDSHAKE_REQUEST: msgType = "HANDSHAKE REQUEST"; break;
        case MessageType::HANDSHAKE_RESPONSE: msgType = "HANDSHAKE RESPONSE"; break;
        case MessageType::CONNECTIVITY_CHECK: msgType = "CONNECTIVITY CHECK"; break;
        case MessageType::CONNECTIVITY_REPORT: msgType = "CONNECTIVITY REPORT"; break;
        default: msgType = "DESCONHECIDO (0x" + String((int)message.type, HEX) + ")"; break;
    }
    Serial.println("📨 Tipo: " + msgType);
    Serial.println("📨 ========================================\n");
    
    instance->messagesReceived++;
    instance->processReceivedMessage(message, mac);
}

void ESPNowBridge::onDataSent(const uint8_t* mac_addr, esp_now_send_status_t status) {
    if (!instance) return;
    
    if (status != ESP_NOW_SEND_SUCCESS) {
        instance->messagesLost++;
        Serial.println("❌ Falha ao enviar para: " + macToString(mac_addr));
    }
}

// ===== CALLBACKS ESTÁTICOS PARA ESPNOWCONTROLLER =====

void ESPNowBridge::onRelayCommandReceivedStatic(const uint8_t* senderMac, int relayNumber, const String& action, int duration) {
    if (instance) {
        instance->onRelayCommandReceived(senderMac, relayNumber, action, duration);
    }
}

void ESPNowBridge::onRelayStatusReceivedStatic(const uint8_t* senderMac, int relayNumber, bool state, bool hasTimer, int remainingTime, const String& name) {
    if (instance) {
        instance->onRelayStatusReceived(senderMac, relayNumber, state, hasTimer, remainingTime, name);
    }
}

void ESPNowBridge::onDeviceInfoReceivedStatic(const uint8_t* senderMac, const String& deviceName, const String& deviceType, uint8_t numRelays, bool operational, uint8_t wifiChannel) {
    if (instance) {
        instance->onDeviceInfoReceived(senderMac, deviceName, deviceType, numRelays, operational, wifiChannel);
    }
}

void ESPNowBridge::onPingReceivedStatic(const uint8_t* senderMac) {
    if (instance) {
        instance->onPingReceived(senderMac);
    }
}

void ESPNowBridge::onWiFiCredentialsReceivedStatic(const String& ssid, const String& password, uint8_t channel) {
    Serial.println("\n🔔 === CALLBACK ESTÁTICO CHAMADO ===");
    Serial.println("📡 WiFi Credentials Static Callback");
    Serial.println("   SSID: " + ssid);
    Serial.println("   Canal: " + String(channel));
    Serial.println("   Instance: " + String(instance ? "OK" : "NULL"));
    
    if (instance) {
        instance->onWiFiCredentialsReceived(ssid, password, channel);
    } else {
        Serial.println("❌ ERRO: Instância estática é nullptr no callback estático!");
    }
}

void ESPNowBridge::onErrorReceivedStatic(const String& error) {
    if (instance) {
        instance->onErrorReceived(error);
    }
}