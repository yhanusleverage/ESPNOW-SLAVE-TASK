#include "ESPNowController.h"

// Incluir configurações se disponível
#ifndef CONFIG_H
    #include "Config.h"
#endif

// Incluir WiFiCredentialsManager para estrutura WiFiCredentials
#include "WiFiCredentialsManager.h"

// 🔄 FASE 2: Incluir tipos e manager para processar ACKs
#include "ESPNowTypes.h"

// Forward declaration para evitar dependência circular
class MasterSlaveManager;

// Definir macros de debug se não estiverem definidas
#ifndef DEBUG_PRINTLN
    #define DEBUG_PRINTLN(x) Serial.println(x)
#endif

#ifndef DEBUG_PRINTF
    #define DEBUG_PRINTF(x, ...) Serial.printf(x, __VA_ARGS__)
#endif

// 🔄 FASE 2: Include do manager APÓS definições para evitar circular dependency
#include "MasterSlaveManager.h"

// Instância estática para callbacks
ESPNowController* ESPNowController::instance = nullptr;

ESPNowController::ESPNowController(const String& deviceName, int channel) 
    : deviceName(deviceName), wifiChannel(channel), initialized(false), messageCounter(0),
      messagesSent(0), messagesReceived(0), messagesLost(0), lastMessageId(0) {
    instance = this;
}

bool ESPNowController::begin() {
    DEBUG_PRINTLN("📡 Inicializando ESP-NOW Controller: " + deviceName);
    
    // Configurar WiFi em modo Station
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    
    // Configurar canal WiFi
    esp_wifi_set_channel(wifiChannel, WIFI_SECOND_CHAN_NONE);
    
    Serial.println("📶 WiFi configurado - Canal: " + String(wifiChannel));
    Serial.println("🆔 MAC Local: " + getLocalMacString());
    
    // Inicializar ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("❌ Erro ao inicializar ESP-NOW");
        return false;
    }
    
    Serial.println("✅ ESP-NOW inicializado");
    
    // Registrar callbacks
    esp_now_register_recv_cb(onDataReceived);
    esp_now_register_send_cb(onDataSent);
    
    // Adicionar peer broadcast para descoberta
    esp_now_peer_info_t peerInfo = {};
    memset(peerInfo.peer_addr, 0xFF, 6); // Broadcast address
    peerInfo.channel = wifiChannel;
    peerInfo.encrypt = false;
    peerInfo.ifidx = WIFI_IF_STA;  // ⭐ CRÍTICO: Definir interface
    
    esp_err_t addResult = esp_now_add_peer(&peerInfo);
    
    // ✅ CORREÇÃO BUG #2: Aceitar se peer já existe (MultiChannelDiscovery pode ter criado)
    if (addResult == ESP_ERR_ESPNOW_EXIST) {
        Serial.println("ℹ️  Peer broadcast já existe (provavelmente do MultiChannelDiscovery)");
        Serial.println("✅ Continuando normalmente - OK");
    } else if (addResult != ESP_OK) {
        Serial.println("⚠️ Aviso: Não foi possível adicionar peer broadcast");
        Serial.println("   Código de erro: " + String(addResult) + " (0x" + String(addResult, HEX) + ")");
        Serial.println("   Canal tentado: " + String(wifiChannel));
        Serial.println("   💡 Tentando remover e readicionar...");
        
        // Tentar remover primeiro (caso já exista)
        esp_now_del_peer(peerInfo.peer_addr);
        
        // Tentar adicionar novamente
        addResult = esp_now_add_peer(&peerInfo);
        if (addResult == ESP_OK) {
            Serial.println("   ✅ Peer broadcast adicionado na segunda tentativa");
        } else if (addResult == ESP_ERR_ESPNOW_EXIST) {
            Serial.println("   ℹ️  Peer já existe - OK");
        } else {
            Serial.println("   ❌ Falha persistente: " + String(addResult));
        }
    } else {
        Serial.println("✅ Peer broadcast adicionado com sucesso");
    }
    
    initialized = true;
    Serial.println("✅ ESP-NOW Controller inicializado: " + deviceName);
    Serial.println("🎯 Canal: " + String(wifiChannel) + " | MAC: " + getLocalMacString());
    
    // Enviar broadcast de descoberta
    sendDiscoveryBroadcast();
    
    return true;
}

void ESPNowController::update() {
    if (!initialized) return;
    
    // Limpar peers offline periodicamente
    static unsigned long lastCleanup = 0;
    if (millis() - lastCleanup > PEER_OFFLINE_TIMEOUT) {
        cleanupOfflinePeers();
        lastCleanup = millis();
    }
    
    // ✅ CORREÇÃO BUG #5: Discovery periódico (a cada 30s)
    static unsigned long lastDiscovery = 0;
    if (millis() - lastDiscovery > 30000) {  // 30 segundos
        Serial.println("🔍 Auto-discovery: Procurando novos dispositivos...");
        sendDiscoveryBroadcast();
        lastDiscovery = millis();
    }
}

void ESPNowController::end() {
    if (initialized) {
        esp_now_deinit();
        initialized = false;
        Serial.println("📡 ESP-NOW Controller finalizado");
    }
}

bool ESPNowController::sendRelayCommand(const uint8_t* targetMac, int relayNumber, const String& action, int duration) {
    if (!initialized) {
        Serial.println("❌ ESP-NOW não inicializado");
        return false;
    }
    
    ESPNowMessage message = {};
    message.type = MessageType::RELAY_COMMAND;
    getLocalMac(message.senderId);
    memcpy(message.targetId, targetMac, 6);
    message.messageId = ++messageCounter;
    message.timestamp = millis();
    
    RelayCommandData cmdData = {};
    cmdData.relayNumber = relayNumber;
    cmdData.state = (action == "on");
    cmdData.duration = duration;
    strncpy(cmdData.action, action.c_str(), sizeof(cmdData.action) - 1);
    
    message.dataSize = sizeof(RelayCommandData);
    memcpy(message.data, &cmdData, sizeof(RelayCommandData));
    message.checksum = calculateChecksum(message);
    
    bool success = sendMessage(message, targetMac);
    
    if (success) {
        Serial.println("📤 Comando enviado: Relé " + String(relayNumber) + " -> " + action + 
                      " para " + macToString(targetMac));
    }
    
    return success;
}

bool ESPNowController::sendRelayStatus(const uint8_t* targetMac, int relayNumber, bool state, bool hasTimer, int remainingTime, const String& name) {
    if (!initialized) return false;
    
    ESPNowMessage message = {};
    message.type = MessageType::RELAY_STATUS;
    getLocalMac(message.senderId);
    
    if (targetMac) {
        memcpy(message.targetId, targetMac, 6);
    } else {
        memset(message.targetId, 0xFF, 6); // Broadcast
    }
    
    message.messageId = ++messageCounter;
    message.timestamp = millis();
    
    RelayStatusData statusData = {};
    statusData.relayNumber = relayNumber;
    statusData.state = state;
    statusData.hasTimer = hasTimer;
    statusData.remainingTime = remainingTime;
    strncpy(statusData.name, name.c_str(), sizeof(statusData.name) - 1);
    
    message.dataSize = sizeof(RelayStatusData);
    memcpy(message.data, &statusData, sizeof(RelayStatusData));
    message.checksum = calculateChecksum(message);
    
    return sendMessage(message, targetMac);
}

bool ESPNowController::sendDeviceInfo(const uint8_t* targetMac, const String& deviceType, uint8_t numRelays, bool operational, uint32_t uptime, uint32_t freeHeap) {
    if (!initialized) return false;
    
    ESPNowMessage message = {};
    message.type = MessageType::DEVICE_INFO;
    getLocalMac(message.senderId);
    
    if (targetMac) {
        memcpy(message.targetId, targetMac, 6);
    } else {
        memset(message.targetId, 0xFF, 6); // Broadcast
    }
    
    message.messageId = ++messageCounter;
    message.timestamp = millis();
    
    DeviceInfoData infoData = {};
    strncpy(infoData.deviceName, deviceName.c_str(), sizeof(infoData.deviceName) - 1);
    strncpy(infoData.deviceType, deviceType.c_str(), sizeof(infoData.deviceType) - 1);
    infoData.numRelays = numRelays;
    infoData.operational = operational;
    infoData.uptime = uptime;
    infoData.freeHeap = freeHeap;
    infoData.wifiChannel = wifiChannel;  // ✅ Incluir canal WiFi do dispositivo!
    memset(infoData.padding, 0, sizeof(infoData.padding));  // Zerar padding
    
    message.dataSize = sizeof(DeviceInfoData);
    memcpy(message.data, &infoData, sizeof(DeviceInfoData));
    message.checksum = calculateChecksum(message);
    
    return sendMessage(message, targetMac);
}

bool ESPNowController::sendPing(const uint8_t* targetMac) {
    if (!initialized) return false;
    
    ESPNowMessage message = {};
    message.type = MessageType::PING;
    getLocalMac(message.senderId);
    memcpy(message.targetId, targetMac, 6);
    message.messageId = ++messageCounter;
    message.timestamp = millis();
    message.dataSize = 0;
    message.checksum = calculateChecksum(message);
    
    bool success = sendMessage(message, targetMac);
    
    if (success) {
        Serial.println("🏓 Ping enviado para: " + macToString(targetMac));
    }
    
    return success;
}

bool ESPNowController::sendDiscoveryBroadcast() {
    if (!initialized) return false;
    
    Serial.println("📢 Enviando broadcast de descoberta...");
    
    uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    return sendDeviceInfo(nullptr, "RelayCommandBox", 8, true, millis(), ESP.getFreeHeap());
}

bool ESPNowController::sendWiFiCredentialsBroadcast(const String& ssid, const String& password, uint8_t channel) {
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
    
    // Copiar SSID
    strncpy(creds.ssid, ssid.c_str(), sizeof(creds.ssid) - 1);
    creds.ssid[sizeof(creds.ssid) - 1] = '\0';
    
    // Copiar senha
    strncpy(creds.password, password.c_str(), sizeof(creds.password) - 1);
    creds.password[sizeof(creds.password) - 1] = '\0';
    
    // Obter canal WiFi
    if (channel > 0) {
        creds.channel = channel;  // Usar canal fornecido
        Serial.println("📶 Usando canal fornecido: " + String(channel));
    } else {
        // Obter canal atual do WiFi
        wifi_second_chan_t secondChan;
        esp_wifi_get_channel(&creds.channel, &secondChan);
        Serial.println("📶 Usando canal atual: " + String(creds.channel));
    }
    
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
    getLocalMac(message.senderId);
    
    // Configurar target (broadcast)
    uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    memcpy(message.targetId, broadcastMac, 6);
    
    // Configurar dados
    message.dataSize = sizeof(WiFiCredentialsData);
    memcpy(message.data, &creds, sizeof(WiFiCredentialsData));
    
    // Enviar mensagem
    bool success = sendMessage(message, broadcastMac);
    
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

bool ESPNowController::addPeer(const uint8_t* macAddress, const String& deviceName) {
    if (!initialized) return false;
    
    // ⚠️ DEPRECADO: Este método usa o canal LOCAL (pode estar errado!)
    // Use addPeerWithChannel() para especificar o canal do peer
    
    // Verificar se peer já existe
    if (peerExists(macAddress)) {
        Serial.println("⚠️ Peer já existe: " + macToString(macAddress));
        return true;
    }
    
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, macAddress, 6);
    peerInfo.channel = wifiChannel;  // ⚠️ Canal LOCAL (pode ser incorreto!)
    peerInfo.encrypt = false;
    peerInfo.ifidx = WIFI_IF_STA;
    
    esp_err_t result = esp_now_add_peer(&peerInfo);
    
    if (result == ESP_OK) {
        // Adicionar à lista de peers conhecidos
        PeerInfo newPeer;
        memcpy(newPeer.macAddress, macAddress, 6);
        newPeer.deviceName = deviceName.isEmpty() ? "Unknown" : deviceName;
        newPeer.deviceType = "Unknown";
        newPeer.online = true;
        newPeer.lastSeen = millis();
        newPeer.rssi = -50;
        
        knownPeers.push_back(newPeer);
        
        Serial.println("✅ Peer adicionado: " + macToString(macAddress) + 
                      (deviceName.isEmpty() ? "" : " (" + deviceName + ")"));
        return true;
    } else {
        Serial.println("❌ Erro ao adicionar peer: " + macToString(macAddress) + 
                      " (Código: " + String(result) + ")");
        return false;
    }
}

bool ESPNowController::addPeerWithChannel(const uint8_t* macAddress, uint8_t peerChannel, const String& deviceName) {
    if (!initialized) {
        Serial.println("❌ ESP-NOW não inicializado");
        return false;
    }
    
    Serial.println("\n🔧 === ADD PEER WITH CHANNEL ===");
    Serial.println("📍 MAC: " + macToString(macAddress));
    Serial.println("📶 Canal do Peer: " + String(peerChannel));
    Serial.println("📝 Nome: " + (deviceName.isEmpty() ? "Unknown" : deviceName));
    
    // Validar canal
    if (peerChannel < 1 || peerChannel > 13) {
        Serial.println("❌ Canal inválido: " + String(peerChannel));
        return false;
    }
    
    // Se peer já existe, remover para atualizar
    if (esp_now_is_peer_exist(macAddress)) {
        Serial.println("⚠️ Peer já existe - removendo para atualizar...");
        esp_now_del_peer(macAddress);
        
        // Remover da lista local também
        for (auto it = knownPeers.begin(); it != knownPeers.end(); ++it) {
            if (memcmp(it->macAddress, macAddress, 6) == 0) {
                knownPeers.erase(it);
                break;
            }
        }
    }
    
    // Configurar peer com CANAL CORRETO
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, macAddress, 6);
    peerInfo.channel = peerChannel;  // ✅ Canal do PEER, não local!
    peerInfo.encrypt = false;
    peerInfo.ifidx = WIFI_IF_STA;
    
    Serial.println("🔗 Adicionando peer no canal " + String(peerChannel) + "...");
    
    // Adicionar peer
    esp_err_t result = esp_now_add_peer(&peerInfo);
    
    if (result == ESP_OK) {
        // Adicionar à lista de peers conhecidos
        PeerInfo newPeer;
        memcpy(newPeer.macAddress, macAddress, 6);
        newPeer.deviceName = deviceName.isEmpty() ? "Unknown" : deviceName;
        newPeer.deviceType = "Unknown";
        newPeer.online = true;
        newPeer.lastSeen = millis();
        newPeer.rssi = -50;
        
        knownPeers.push_back(newPeer);
        
        Serial.println("✅ Peer adicionado com sucesso no canal " + String(peerChannel) + "!");
        Serial.println("==================================\n");
        return true;
        
    } else {
        Serial.println("❌ ERRO ao adicionar peer!");
        Serial.println("💡 Código de erro: " + String(result) + " (0x" + String(result, HEX) + ")");
        
        // Detalhar erros comuns
        if (result == ESP_ERR_ESPNOW_EXIST) {
            Serial.println("⚠️ Erro: Peer já existe (0x306B)");
        } else if (result == ESP_ERR_ESPNOW_FULL) {
            Serial.println("⚠️ Erro: Lista de peers cheia");
        } else if (result == ESP_ERR_ESPNOW_ARG) {
            Serial.println("⚠️ Erro: Argumento inválido");
        } else if (result == 0x306A) {
            Serial.println("⚠️ Erro: ESP_ERR_ESPNOW_CHAN - Canal incorreto!");
            Serial.println("💡 Verifique se dispositivos estão no mesmo canal");
        }
        
        Serial.println("==================================\n");
        return false;
    }
}

bool ESPNowController::addPeerSafe(const uint8_t* macAddress, const String& deviceName) {
    if (!initialized) {
        Serial.println("❌ ESP-NOW não inicializado");
        return false;
    }
    
    Serial.println("\n🔧 === ADD PEER SAFE ===");
    Serial.println("📍 MAC: " + macToString(macAddress));
    Serial.println("📝 Nome: " + (deviceName.isEmpty() ? "Unknown" : deviceName));
    
    // 1. Verificar canal WiFi atual
    uint8_t currentChannel;
    wifi_second_chan_t secondChan;
    esp_wifi_get_channel(&currentChannel, &secondChan);
    
    Serial.println("📶 Canal WiFi atual: " + String(currentChannel));
    Serial.println("📶 Canal configurado: " + String(wifiChannel));
    
    // 2. Se peer já existe, remover para forçar atualização
    if (esp_now_is_peer_exist(macAddress)) {
        Serial.println("⚠️ Peer já existe - removendo para atualizar...");
        esp_now_del_peer(macAddress);
        
        // Remover da lista local também
        for (auto it = knownPeers.begin(); it != knownPeers.end(); ++it) {
            if (memcmp(it->macAddress, macAddress, 6) == 0) {
                knownPeers.erase(it);
                break;
            }
        }
    }
    
    // 3. Configurar informações do peer com CANAL CORRETO
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, macAddress, 6);
    peerInfo.channel = currentChannel;  // ✅ Usar canal WiFi atual!
    peerInfo.encrypt = false;
    peerInfo.ifidx = WIFI_IF_STA;
    
    Serial.println("🔗 Adicionando peer no canal " + String(peerInfo.channel) + "...");
    
    // 4. Adicionar peer
    esp_err_t result = esp_now_add_peer(&peerInfo);
    
    if (result == ESP_OK) {
        // Adicionar à lista de peers conhecidos
        PeerInfo newPeer;
        memcpy(newPeer.macAddress, macAddress, 6);
        newPeer.deviceName = deviceName.isEmpty() ? "Unknown" : deviceName;
        newPeer.deviceType = "Unknown";
        newPeer.online = true;
        newPeer.lastSeen = millis();
        newPeer.rssi = -50;
        
        knownPeers.push_back(newPeer);
        
        Serial.println("✅ Peer adicionado com sucesso!");
        Serial.println("========================\n");
        return true;
        
    } else {
        Serial.println("❌ ERRO ao adicionar peer!");
        Serial.println("💡 Código de erro: 0x" + String(result, HEX));
        
        // Detalhar erros comuns
        if (result == ESP_ERR_ESPNOW_EXIST) {
            Serial.println("⚠️ Erro: Peer já existe (ESP_ERR_ESPNOW_EXIST)");
        } else if (result == ESP_ERR_ESPNOW_FULL) {
            Serial.println("⚠️ Erro: Lista de peers cheia (ESP_ERR_ESPNOW_FULL)");
        } else if (result == ESP_ERR_ESPNOW_ARG) {
            Serial.println("⚠️ Erro: Argumento inválido (ESP_ERR_ESPNOW_ARG)");
        }
        
        Serial.println("========================\n");
        return false;
    }
}

bool ESPNowController::removePeer(const uint8_t* macAddress) {
    if (!initialized) return false;
    
    esp_err_t result = esp_now_del_peer(macAddress);
    
    if (result == ESP_OK) {
        // Remover da lista de peers conhecidos
        for (auto it = knownPeers.begin(); it != knownPeers.end(); ++it) {
            if (memcmp(it->macAddress, macAddress, 6) == 0) {
                knownPeers.erase(it);
                break;
            }
        }
        
        Serial.println("✅ Peer removido: " + macToString(macAddress));
        return true;
    } else {
        Serial.println("❌ Erro ao remover peer: " + macToString(macAddress));
        return false;
    }
}

std::vector<PeerInfo> ESPNowController::getPeerList() {
    return knownPeers;
}

bool ESPNowController::peerExists(const uint8_t* macAddress) {
    return esp_now_is_peer_exist(macAddress);
}

int ESPNowController::getPeerCount() {
    esp_now_peer_num_t peerNum;
    esp_now_get_peer_num(&peerNum);
    return peerNum.total_num;
}

void ESPNowController::setRelayCommandCallback(std::function<void(const uint8_t* senderMac, int relayNumber, const String& action, int duration)> callback) {
    this->relayCommandCallback = callback;
}

void ESPNowController::setRelayStatusCallback(std::function<void(const uint8_t* senderMac, int relayNumber, bool state, bool hasTimer, int remainingTime, const String& name)> callback) {
    this->relayStatusCallback = callback;
}

void ESPNowController::setDeviceInfoCallback(std::function<void(const uint8_t* senderMac, const String& deviceName, const String& deviceType, uint8_t numRelays, bool operational, uint8_t wifiChannel)> callback) {
    this->deviceInfoCallback = callback;
}

void ESPNowController::setPingCallback(void (*callback)(const uint8_t* senderMac)) {
    this->pingCallback = callback;
}

void ESPNowController::setWiFiCredentialsCallback(void (*callback)(const String& ssid, const String& password, uint8_t channel)) {
    this->wifiCredentialsCallback = callback;
}

void ESPNowController::setErrorCallback(void (*callback)(const String& error)) {
    this->errorCallback = callback;
}

String ESPNowController::macToString(const uint8_t* mac) {
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X", 
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(macStr);
}

bool ESPNowController::stringToMac(const String& macStr, uint8_t* mac) {
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

void ESPNowController::getLocalMac(uint8_t* mac) {
    WiFi.macAddress(mac);
}

String ESPNowController::getLocalMacString() {
    uint8_t mac[6];
    getLocalMac(mac);
    return macToString(mac);
}

String ESPNowController::getStatsJSON() {
    DynamicJsonDocument doc(512);
    
    doc["deviceName"] = deviceName;
    doc["initialized"] = initialized;
    doc["channel"] = wifiChannel;
    doc["localMac"] = getLocalMacString();
    doc["messagesSent"] = messagesSent;
    doc["messagesReceived"] = messagesReceived;
    doc["messagesLost"] = messagesLost;
    doc["peerCount"] = getPeerCount();
    doc["knownPeersCount"] = knownPeers.size();
    
    JsonArray peers = doc.createNestedArray("peers");
    for (const auto& peer : knownPeers) {
        JsonObject peerObj = peers.createNestedObject();
        peerObj["mac"] = macToString(peer.macAddress);
        peerObj["name"] = peer.deviceName;
        peerObj["type"] = peer.deviceType;
        peerObj["online"] = peer.online;
        peerObj["lastSeen"] = peer.lastSeen;
        peerObj["rssi"] = peer.rssi;
    }
    
    String result;
    serializeJson(doc, result);
    return result;
}

void ESPNowController::printStatus() {
    Serial.println("📡 === STATUS ESP-NOW ===");
    Serial.println("🆔 Dispositivo: " + deviceName);
    Serial.println("📶 Canal: " + String(wifiChannel));
    Serial.println("🆔 MAC Local: " + getLocalMacString());
    Serial.println("✅ Inicializado: " + String(initialized ? "Sim" : "Não"));
    Serial.println("📊 Mensagens enviadas: " + String(messagesSent));
    Serial.println("📊 Mensagens recebidas: " + String(messagesReceived));
    Serial.println("📊 Mensagens perdidas: " + String(messagesLost));
    Serial.println("👥 Peers conectados: " + String(getPeerCount()));
    Serial.println("👥 Peers conhecidos: " + String(knownPeers.size()));
    
    if (!knownPeers.empty()) {
        Serial.println("\n👥 === PEERS CONHECIDOS ===");
        for (const auto& peer : knownPeers) {
            Serial.println("   " + macToString(peer.macAddress) + " | " + 
                          peer.deviceName + " (" + peer.deviceType + ") | " +
                          (peer.online ? "Online" : "Offline") + " | RSSI: " + String(peer.rssi));
        }
    }
    
    Serial.println("=========================");
}

// ===== MÉTODOS PRIVADOS =====

bool ESPNowController::sendMessage(const ESPNowMessage& message, const uint8_t* targetMac) {
    if (!initialized) return false;
    
    uint8_t* sendMac = nullptr;
    bool isBroadcast = false;
    
    if (targetMac) {
        sendMac = const_cast<uint8_t*>(targetMac);
        // Verificar se é broadcast
        isBroadcast = (targetMac[0] == 0xFF && targetMac[1] == 0xFF && 
                      targetMac[2] == 0xFF && targetMac[3] == 0xFF && 
                      targetMac[4] == 0xFF && targetMac[5] == 0xFF);
    } else {
        static uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        sendMac = broadcastMac;
        isBroadcast = true;
    }
    
    // ⭐ CORREÇÃO CRÍTICA: Adicionar peer automaticamente se for UNICAST!
    if (!isBroadcast && !peerExists(sendMac)) {
        Serial.println("🔗 Peer não registrado, adicionando automaticamente: " + macToString(sendMac));
        
        // Adicionar peer com informações básicas
        esp_now_peer_info_t peerInfo = {};
        memcpy(peerInfo.peer_addr, sendMac, 6);
        peerInfo.channel = wifiChannel;
        peerInfo.encrypt = false;
        peerInfo.ifidx = WIFI_IF_STA;
        
        esp_err_t addResult = esp_now_add_peer(&peerInfo);
        
        if (addResult == ESP_OK) {
            Serial.println("✅ Peer adicionado automaticamente!");
            
            // Atualizar lista local
            PeerInfo newPeer;
            memcpy(newPeer.macAddress, sendMac, 6);
            newPeer.deviceName = "Auto-" + macToString(sendMac).substring(12);
            newPeer.deviceType = "Unknown";
            newPeer.online = true;
            newPeer.lastSeen = millis();
            newPeer.rssi = -50;
            knownPeers.push_back(newPeer);
        } else {
            Serial.println("❌ Falha ao adicionar peer automaticamente: " + String(addResult));
            Serial.println("⚠️ Mensagem pode não ser entregue!");
            // Continua tentando enviar mesmo assim
        }
    }
    
    // Enviar mensagem
    esp_err_t result = esp_now_send(sendMac, (uint8_t*)&message, sizeof(ESPNowMessage));
    
    if (result == ESP_OK) {
        messagesSent++;
        return true;
    } else {
        messagesLost++;
        Serial.println("❌ Erro ao enviar mensagem: " + String(result));
        Serial.println("💡 Código de erro: 0x" + String(result, HEX));
        if (result == ESP_ERR_ESPNOW_NOT_FOUND) {
            Serial.println("⚠️ Peer não encontrado - Tentando adicionar...");
        }
        return false;
    }
}

void ESPNowController::processReceivedMessage(const ESPNowMessage& message, const uint8_t* senderMac) {
    if (!validateMessage(message)) {
        Serial.println("❌ Mensagem inválida recebida de: " + macToString(senderMac));
        return;
    }
    
    // 🔧 AUTO-ADD PEER: Adicionar remetente automaticamente usando método seguro
    bool peerExists = esp_now_is_peer_exist(senderMac);
    if (!peerExists) {
        Serial.println("🔗 Auto-adicionando peer (método seguro): " + macToString(senderMac));
        addPeerSafe(senderMac, "Auto-" + macToString(senderMac).substring(12));
    }
    
    // Atualizar informações do peer
    updatePeerInfo(senderMac, "", "");
    
    switch (message.type) {
        case MessageType::RELAY_COMMAND: {
            if (relayCommandCallback && message.dataSize >= sizeof(RelayCommandData)) {
                RelayCommandData cmdData;
                memcpy(&cmdData, message.data, sizeof(RelayCommandData));
                
                Serial.println("📥 Comando recebido de " + macToString(senderMac) + 
                              ": Relé " + String(cmdData.relayNumber) + " -> " + String(cmdData.action));
                
                relayCommandCallback(senderMac, cmdData.relayNumber, String(cmdData.action), cmdData.duration);
            }
            break;
        }
        
        case MessageType::RELAY_STATUS: {
            if (relayStatusCallback && message.dataSize >= sizeof(RelayStatusData)) {
                RelayStatusData statusData;
                memcpy(&statusData, message.data, sizeof(RelayStatusData));
                
                Serial.println("📥 Status recebido de " + macToString(senderMac) + 
                              ": " + String(statusData.name) + " -> " + (statusData.state ? "ON" : "OFF"));
                
                relayStatusCallback(senderMac, statusData.relayNumber, statusData.state, 
                                  statusData.hasTimer, statusData.remainingTime, String(statusData.name));
            }
            break;
        }
        
        case MessageType::DEVICE_INFO: {
            if (deviceInfoCallback && message.dataSize >= sizeof(DeviceInfoData)) {
                DeviceInfoData infoData;
                memcpy(&infoData, message.data, sizeof(DeviceInfoData));
                
                Serial.println("📥 Info recebida de " + macToString(senderMac) + 
                              ": " + String(infoData.deviceName) + " (" + String(infoData.deviceType) + ")");
                Serial.println("📶 Canal do dispositivo remoto: " + String(infoData.wifiChannel));
                
                // ✅ SINCRONIZAÇÃO DE CANAL - CRÍTICO!
                if (infoData.wifiChannel > 0 && infoData.wifiChannel != wifiChannel) {
                    Serial.println("\n🔧 === SINCRONIZAÇÃO DE CANAL DETECTADA ===");
                    Serial.println("⚠️ Dispositivo remoto está no canal " + String(infoData.wifiChannel));
                    Serial.println("⚠️ Nós estamos no canal " + String(wifiChannel));
                    Serial.println("🔄 Atualizando para canal " + String(infoData.wifiChannel) + "...");
                    
                    // Atualizar canal local
                    esp_wifi_set_channel(infoData.wifiChannel, WIFI_SECOND_CHAN_NONE);
                    wifiChannel = infoData.wifiChannel;
                    delay(100); // Aguardar estabilização
                    
                    Serial.println("✅ Canal local atualizado para " + String(wifiChannel));
                    Serial.println("=======================================\n");
                }
                
                // ✅ ADICIONAR PEER COM CANAL CORRETO (novo método!)
                if (infoData.wifiChannel > 0) {
                    addPeerWithChannel(senderMac, infoData.wifiChannel, String(infoData.deviceName));
                } else {
                    // Fallback: se não tem canal no DeviceInfo, usar canal local
                    Serial.println("⚠️ DeviceInfo sem canal, usando canal local");
                    addPeerWithChannel(senderMac, wifiChannel, String(infoData.deviceName));
                }
                
                // Atualizar informações detalhadas do peer
                updatePeerInfo(senderMac, String(infoData.deviceName), String(infoData.deviceType));
                
                deviceInfoCallback(senderMac, String(infoData.deviceName), String(infoData.deviceType), 
                                 infoData.numRelays, infoData.operational, infoData.wifiChannel);
            }
            break;
        }
        
        case MessageType::PING: {
            Serial.println("🏓 Ping recebido de: " + macToString(senderMac));
            
            // Responder com PONG
            ESPNowMessage pongMsg = {};
            pongMsg.type = MessageType::PONG;
            getLocalMac(pongMsg.senderId);
            memcpy(pongMsg.targetId, senderMac, 6);
            pongMsg.messageId = ++messageCounter;
            pongMsg.timestamp = millis();
            pongMsg.dataSize = 0;
            pongMsg.checksum = calculateChecksum(pongMsg);
            
            sendMessage(pongMsg, senderMac);
            
            if (pingCallback) {
                pingCallback(senderMac);
            }
            break;
        }
        
        case MessageType::PONG: {
            Serial.println("🏓 Pong recebido de: " + macToString(senderMac));
            break;
        }
        
        case MessageType::BROADCAST: {
            Serial.println("📢 Broadcast recebido de: " + macToString(senderMac));
            break;
        }
        
        case MessageType::ERROR: {
            Serial.println("\n❌ ========================================");
            Serial.println("❌ MENSAGEM DE ERRO RECEBIDA");
            Serial.println("❌ ========================================");
            Serial.println("📨 De: " + macToString(senderMac));
            
            if (message.dataSize > 0 && message.dataSize < 200) {
                String errorMsg = String((char*)message.data);
                errorMsg = errorMsg.substring(0, message.dataSize);
                Serial.println("💬 Mensagem: " + errorMsg);
            }
            
            // Chamar callback de erro se definido
            if (errorCallback) {
                String errorMsg = String((char*)message.data);
                errorCallback(errorMsg.substring(0, message.dataSize));
            }
            
            Serial.println("========================================\n");
            break;
        }
        
        case MessageType::WIFI_CREDENTIALS: {
            if (wifiCredentialsCallback && message.dataSize >= sizeof(WiFiCredentialsData)) {
                WiFiCredentialsData creds;
                memcpy(&creds, message.data, sizeof(WiFiCredentialsData));
                
                Serial.println("📶 Credenciais WiFi recebidas de: " + macToString(senderMac));
                
                // Validar credenciais usando checksum
                if (validateWiFiCredentials(creds)) {
                    Serial.println("✅ Credenciais validadas com sucesso!");
                    Serial.println("   SSID: " + String(creds.ssid));
                    Serial.println("   Canal: " + String(creds.channel));
                    Serial.println("   Checksum: 0x" + String(creds.checksum, HEX));
                    
                    // Chamar callback para processar credenciais
                    wifiCredentialsCallback(String(creds.ssid), String(creds.password), creds.channel);
                } else {
                    Serial.println("❌ Credenciais WiFi inválidas (checksum falhou)");
                }
            }
            break;
        }
        
        case MessageType::HANDSHAKE_REQUEST: {
            if (message.dataSize >= sizeof(HandshakeData)) {
                HandshakeData handshake;
                memcpy(&handshake, message.data, sizeof(HandshakeData));
                
                if (validateHandshake(handshake)) {
                    Serial.println("🤝 Handshake recebido de: " + macToString(senderMac));
                    Serial.println("   Sessão: " + String(handshake.sessionId));
                    Serial.println("   Dispositivo: " + String(handshake.deviceName));
                    Serial.println("   WiFi: " + String(handshake.wifiConnected ? "Conectado" : "Desconectado"));
                    
                    // Responder ao handshake
                    respondToHandshake(senderMac, handshake.sessionId);
                    
                    // Chamar callback se definido
                    if (handshakeCallback) {
                        handshakeCallback(senderMac, handshake.sessionId, String(handshake.deviceName), handshake.wifiConnected);
                    }
                } else {
                    Serial.println("❌ Handshake inválido recebido de: " + macToString(senderMac));
                }
            }
            break;
        }
        
        case MessageType::HANDSHAKE_RESPONSE: {
            if (message.dataSize >= sizeof(HandshakeData)) {
                HandshakeData handshake;
                memcpy(&handshake, message.data, sizeof(HandshakeData));
                
                if (validateHandshake(handshake)) {
                    Serial.println("🤝 Resposta de handshake recebida de: " + macToString(senderMac));
                    Serial.println("   Sessão: " + String(handshake.sessionId));
                    Serial.println("   Dispositivo: " + String(handshake.deviceName));
                    Serial.println("   WiFi: " + String(handshake.wifiConnected ? "Conectado" : "Desconectado"));
                    
                    // Chamar callback se definido
                    if (handshakeCallback) {
                        handshakeCallback(senderMac, handshake.sessionId, String(handshake.deviceName), handshake.wifiConnected);
                    }
                } else {
                    Serial.println("❌ Resposta de handshake inválida de: " + macToString(senderMac));
                }
            }
            break;
        }
        
        case MessageType::CONNECTIVITY_CHECK: {
            Serial.println("🔍 Solicitação de verificação de conectividade de: " + macToString(senderMac));
            
            // Responder com relatório de conectividade
            uint32_t sessionId = generateSessionId();
            sendConnectivityReport(senderMac, sessionId);
            
            // Chamar callback se definido
            if (connectivityCheckCallback) {
                connectivityCheckCallback(senderMac);
            }
            break;
        }
        
        case MessageType::CONNECTIVITY_REPORT: {
            if (message.dataSize >= sizeof(ConnectivityReportData)) {
                ConnectivityReportData report;
                memcpy(&report, message.data, sizeof(ConnectivityReportData));
                
                Serial.println("📊 Relatório de conectividade recebido de: " + macToString(senderMac));
                Serial.println("   Sessão: " + String(report.sessionId));
                Serial.println("   WiFi: " + String(report.wifiConnected ? "Conectado" : "Desconectado"));
                Serial.println("   RSSI: " + String(report.wifiRSSI) + " dBm");
                Serial.println("   Canal: " + String(report.wifiChannel));
                Serial.println("   Uptime: " + String(report.uptime / 1000) + "s");
                Serial.println("   Heap: " + String(report.freeHeap) + " bytes");
                Serial.println("   Mensagens: " + String(report.messageCount));
                Serial.println("   Operacional: " + String(report.operational ? "Sim" : "Não"));
                
                // Chamar callback se definido
                if (connectivityReportCallback) {
                    connectivityReportCallback(senderMac, report.sessionId, report.wifiConnected, report.wifiRSSI, report.uptime);
                }
            }
            break;
        }
        
        default:
            Serial.println("❓ Tipo de mensagem desconhecido: " + String((int)message.type));
            break;
    }
}

uint8_t ESPNowController::calculateChecksum(const ESPNowMessage& message) {
    uint8_t checksum = 0;
    const uint8_t* data = (const uint8_t*)&message;
    
    // Calcular checksum de todos os campos exceto o próprio checksum
    for (size_t i = 0; i < sizeof(ESPNowMessage) - 1; i++) {
        checksum ^= data[i];
    }
    
    return checksum;
}

bool ESPNowController::validateMessage(const ESPNowMessage& message) {
    // Verificar checksum
    ESPNowMessage tempMsg = message;
    tempMsg.checksum = 0;
    uint8_t calculatedChecksum = calculateChecksum(tempMsg);
    
    if (calculatedChecksum != message.checksum) {
        Serial.println("❌ Checksum inválido");
        return false;
    }
    
    // Verificar tamanho dos dados
    if (message.dataSize > sizeof(message.data)) {
        Serial.println("❌ Tamanho de dados inválido");
        return false;
    }
    
    // Verificar se não é uma mensagem muito antiga
    unsigned long currentTime = millis();
    if (currentTime > message.timestamp && (currentTime - message.timestamp) > MESSAGE_TIMEOUT_MS) {
        DEBUG_PRINTLN("❌ Mensagem muito antiga");
        return false;
    }
    
    return true;
}

void ESPNowController::updatePeerInfo(const uint8_t* macAddress, const String& deviceName, const String& deviceType) {
    // Procurar peer existente
    for (auto& peer : knownPeers) {
        if (memcmp(peer.macAddress, macAddress, 6) == 0) {
            peer.online = true;
            peer.lastSeen = millis();
            
            if (!deviceName.isEmpty()) {
                peer.deviceName = deviceName;
            }
            if (!deviceType.isEmpty()) {
                peer.deviceType = deviceType;
            }
            return;
        }
    }
    
    // Se não encontrou, adicionar novo peer (FALLBACK - não deveria acontecer!)
    if (!peerExists(macAddress)) {
        Serial.println("⚠️ updatePeerInfo: Peer não existe! Adicionando como fallback...");
        Serial.println("💡 Isso não deveria acontecer se addPeerWithChannel() foi chamado corretamente");
        
        // ⚠️ FALLBACK: Adicionar com canal atual (pode estar errado!)
        uint8_t currentChannel;
        wifi_second_chan_t secondChan;
        esp_wifi_get_channel(&currentChannel, &secondChan);
        
        addPeerWithChannel(macAddress, currentChannel, deviceName);
    }
}

void ESPNowController::cleanupOfflinePeers() {
    unsigned long currentTime = millis();
    
    for (auto& peer : knownPeers) {
        if (currentTime - peer.lastSeen > PEER_OFFLINE_TIMEOUT) {
            peer.online = false;
        }
    }
}

// ===== CALLBACKS ESTÁTICOS =====

void ESPNowController::onDataReceived(const uint8_t* mac, const uint8_t* incomingData, int len) {
    if (!instance) return;
    
    // 🔄 FASE 2: RECONHECER TaskESPNowMessage (do SLAVE) para ACKs
    if (len == sizeof(TaskESPNowMessage)) {
        TaskESPNowMessage taskMsg;
        memcpy(&taskMsg, incomingData, sizeof(TaskESPNowMessage));
        
        // Verificar se é um ACK de relay
        if (taskMsg.type == TASK_MSG_RELAY_ACK) {
            Serial.println("\n🎊 === TaskESPNowMessage ACK DETECTADO ===");
            Serial.println("📥 De: " + macToString(mac));
            Serial.println("📦 Tamanho: " + String(len) + " bytes");
            Serial.println("🆔 Tipo: TASK_MSG_RELAY_ACK");
            
            // Extrair dados do ACK
            if (taskMsg.dataSize >= sizeof(RelayCommandAck)) {
                RelayCommandAck ack;
                memcpy(&ack, taskMsg.data, sizeof(RelayCommandAck));
                
                // Validar checksum
                uint8_t expectedChecksum = 0;
                for (size_t i = 0; i < sizeof(RelayCommandAck) - 1; i++) {
                    expectedChecksum ^= ((uint8_t*)&ack)[i];
                }
                
                if (ack.checksum == expectedChecksum) {
                    Serial.println("✅ Checksum válido");
                    Serial.println("🆔 Command ID: " + String(ack.commandId));
                    Serial.println("🔌 Relé: " + String(ack.relayNumber));
                    Serial.println("✅ Success: " + String(ack.success ? "Sim" : "Não"));
                    Serial.println("💡 Estado: " + String(ack.currentState ? "ON" : "OFF"));
                    Serial.println("==========================================\n");
                    
                    // Notificar MasterSlaveManager se houver instância
                    if (MasterSlaveManager::getInstance()) {
                        MasterSlaveManager::getInstance()->processRelayCommandAck(ack, mac);
                    }
                } else {
                    Serial.println("❌ Checksum inválido!");
                    Serial.println("==========================================\n");
                }
            }
            return; // Já processado, não tentar como ESPNowMessage
        }
    }
    
    // Aceitar pequenas diferenças de alinhamento (±4 bytes) para ESPNowMessage legacy
    int expectedSize = sizeof(ESPNowMessage);
    int sizeDiff = abs(len - expectedSize);
    
    if (sizeDiff > 4) {
        Serial.println("❌ Tamanho de mensagem inválido: " + String(len));
        Serial.println("💡 Esperado: ~" + String(expectedSize) + " bytes (±4 para alinhamento)");
        Serial.println("💡 Ou: " + String(sizeof(TaskESPNowMessage)) + " bytes (TaskESPNowMessage)");
        return;
    }
    
    // Copiar mensagem com segurança
    ESPNowMessage message;
    memset(&message, 0, sizeof(ESPNowMessage));
    int copySize = min(len, (int)sizeof(ESPNowMessage));
    memcpy(&message, incomingData, copySize);
    
    instance->messagesReceived++;
    instance->processReceivedMessage(message, mac);
}

void ESPNowController::onDataSent(const uint8_t* mac_addr, esp_now_send_status_t status) {
    if (!instance) return;
    
    if (status != ESP_NOW_SEND_SUCCESS) {
        instance->messagesLost++;
        Serial.println("❌ Falha ao enviar para: " + macToString(mac_addr));
    }
}

// ===== MÉTODOS DE VALIDAÇÃO BIDIRECIONAL =====

bool ESPNowController::validateWiFiCredentials(const WiFiCredentialsData& credentials) {
    // Validação SIMPLIFICADA (apenas checksum e dados básicos)
    
    // Verificar SSID não vazio
    if (strlen(credentials.ssid) == 0 || strlen(credentials.ssid) > 32) {
        Serial.println("❌ SSID inválido (vazio ou muito longo)");
        return false;
    }
    
    // Verificar canal válido (1-13)
    if (credentials.channel < 1 || credentials.channel > 13) {
        Serial.println("❌ Canal inválido: " + String(credentials.channel) + " (deve ser 1-13)");
        return false;
    }
    
    // Verificar checksum
    if (!credentials.isValid()) {
        Serial.println("❌ Checksum inválido das credenciais WiFi");
        return false;
    }
    
    Serial.println("✅ Credenciais WiFi validadas com sucesso");
    Serial.println("   SSID: " + String(credentials.ssid));
    Serial.println("   Canal: " + String(credentials.channel));
    return true;
}

// Mantido para uso em handshakes (não usado para credenciais WiFi)
uint8_t ESPNowController::generateValidationCode(const String& text1, const String& text2, uint32_t value) {
    // Gerar código de validação genérico (usado para handshakes)
    // NÃO usado para credenciais WiFi (que usam checksum próprio)
    uint8_t code = 0;
    
    // XOR com texto 1
    for (size_t i = 0; i < text1.length(); i++) {
        code ^= text1.charAt(i);
    }
    
    // XOR com texto 2
    for (size_t i = 0; i < text2.length(); i++) {
        code ^= text2.charAt(i);
    }
    
    // XOR com valor (bytes individuais)
    code ^= (value & 0xFF);
    code ^= ((value >> 8) & 0xFF);
    code ^= ((value >> 16) & 0xFF);
    code ^= ((value >> 24) & 0xFF);
    
    // Adicionar constante para evitar código zero
    code ^= 0xAA;
    
    return code;
}

bool ESPNowController::initiateHandshake(const uint8_t* targetMac) {
    if (!initialized) return false;
    
    ESPNowMessage message = {};
    message.type = MessageType::HANDSHAKE_REQUEST;
    getLocalMac(message.senderId);
    memcpy(message.targetId, targetMac, 6);
    message.messageId = ++messageCounter;
    message.timestamp = millis();
    
    HandshakeData handshake = {};
    handshake.sessionId = generateSessionId();
    handshake.timestamp = message.timestamp;
    handshake.deviceType = 1; // Slave
    strncpy(handshake.deviceName, deviceName.c_str(), sizeof(handshake.deviceName) - 1);
    handshake.protocolVersion = 1;
    handshake.wifiConnected = WiFi.isConnected();
    handshake.validationCode = generateValidationCode(deviceName, String(handshake.sessionId), handshake.timestamp);
    
    handshake.deviceName[sizeof(handshake.deviceName) - 1] = '\0';
    
    message.dataSize = sizeof(HandshakeData);
    memcpy(message.data, &handshake, sizeof(HandshakeData));
    message.checksum = calculateChecksum(message);
    
    Serial.println("🤝 Iniciando handshake bidirecional com " + macToString(targetMac));
    Serial.println("   Sessão: " + String(handshake.sessionId));
    Serial.println("   Dispositivo: " + deviceName);
    Serial.println("   WiFi: " + String(handshake.wifiConnected ? "Conectado" : "Desconectado"));
    
    return sendMessage(message, targetMac);
}

bool ESPNowController::respondToHandshake(const uint8_t* targetMac, uint32_t sessionId) {
    if (!initialized) return false;
    
    ESPNowMessage message = {};
    message.type = MessageType::HANDSHAKE_RESPONSE;
    getLocalMac(message.senderId);
    memcpy(message.targetId, targetMac, 6);
    message.messageId = ++messageCounter;
    message.timestamp = millis();
    
    HandshakeData handshake = {};
    handshake.sessionId = sessionId; // Usar mesmo ID da sessão
    handshake.timestamp = message.timestamp;
    handshake.deviceType = 1; // Slave
    strncpy(handshake.deviceName, deviceName.c_str(), sizeof(handshake.deviceName) - 1);
    handshake.protocolVersion = 1;
    handshake.wifiConnected = WiFi.isConnected();
    handshake.validationCode = generateValidationCode(deviceName, String(sessionId), handshake.timestamp);
    
    handshake.deviceName[sizeof(handshake.deviceName) - 1] = '\0';
    
    message.dataSize = sizeof(HandshakeData);
    memcpy(message.data, &handshake, sizeof(HandshakeData));
    message.checksum = calculateChecksum(message);
    
    Serial.println("🤝 Respondendo handshake para " + macToString(targetMac));
    Serial.println("   Sessão: " + String(sessionId));
    Serial.println("   Dispositivo: " + deviceName);
    Serial.println("   WiFi: " + String(handshake.wifiConnected ? "Conectado" : "Desconectado"));
    
    return sendMessage(message, targetMac);
}

bool ESPNowController::sendConnectivityReport(const uint8_t* targetMac, uint32_t sessionId) {
    if (!initialized) return false;
    
    ESPNowMessage message = {};
    message.type = MessageType::CONNECTIVITY_REPORT;
    getLocalMac(message.senderId);
    if (targetMac) {
        memcpy(message.targetId, targetMac, 6);
    } else {
        memset(message.targetId, 0xFF, 6); // Broadcast
    }
    message.messageId = ++messageCounter;
    message.timestamp = millis();
    
    ConnectivityReportData report = {};
    report.sessionId = sessionId;
    report.timestamp = message.timestamp;
    report.wifiConnected = WiFi.isConnected();
    report.wifiRSSI = WiFi.RSSI();
    report.wifiChannel = WiFi.channel();
    report.uptime = millis();
    report.freeHeap = ESP.getFreeHeap();
    report.messageCount = messagesSent + messagesReceived;
    report.operational = initialized && (ESP.getFreeHeap() > 10000);
    
    message.dataSize = sizeof(ConnectivityReportData);
    memcpy(message.data, &report, sizeof(ConnectivityReportData));
    message.checksum = calculateChecksum(message);
    
    Serial.println("📊 Enviando relatório de conectividade");
    Serial.println("   Sessão: " + String(sessionId));
    Serial.println("   WiFi: " + String(report.wifiConnected ? "Conectado" : "Desconectado"));
    Serial.println("   RSSI: " + String(report.wifiRSSI) + " dBm");
    Serial.println("   Canal: " + String(report.wifiChannel));
    Serial.println("   Heap: " + String(report.freeHeap) + " bytes");
    
    return sendMessage(message, targetMac);
}

bool ESPNowController::requestConnectivityCheck(const uint8_t* targetMac) {
    if (!initialized) return false;
    
    ESPNowMessage message = {};
    message.type = MessageType::CONNECTIVITY_CHECK;
    getLocalMac(message.senderId);
    memcpy(message.targetId, targetMac, 6);
    message.messageId = ++messageCounter;
    message.timestamp = millis();
    message.dataSize = 0;
    message.checksum = calculateChecksum(message);
    
    Serial.println("🔍 Solicitando verificação de conectividade de " + macToString(targetMac));
    
    return sendMessage(message, targetMac);
}

bool ESPNowController::validateHandshake(const HandshakeData& handshake) {
    // Verificar se não é muito antigo (30 segundos)
    unsigned long currentTime = millis();
    if (currentTime > handshake.timestamp && (currentTime - handshake.timestamp) > 30000) {
        Serial.println("❌ Handshake muito antigo");
        return false;
    }
    
    // Verificar versão do protocolo
    if (handshake.protocolVersion != 1) {
        Serial.println("❌ Versão de protocolo incompatível: " + String(handshake.protocolVersion));
        return false;
    }
    
    // Verificar código de validação
    String deviceNameStr = String(handshake.deviceName);
    String sessionStr = String(handshake.sessionId);
    uint8_t expectedCode = generateValidationCode(deviceNameStr, sessionStr, handshake.timestamp);
    
    if (handshake.validationCode != expectedCode) {
        Serial.println("❌ Código de validação do handshake inválido");
        Serial.println("   Esperado: " + String(expectedCode));
        Serial.println("   Recebido: " + String(handshake.validationCode));
        return false;
    }
    
    Serial.println("✅ Handshake validado com sucesso");
    return true;
}

uint32_t ESPNowController::generateSessionId() {
    // Gerar ID único baseado em timestamp e MAC
    uint32_t sessionId = millis();
    uint8_t mac[6];
    getLocalMac(mac);
    
    // XOR com MAC para tornar único
    for (int i = 0; i < 6; i++) {
        sessionId ^= (mac[i] << (i % 4) * 8);
    }
    
    return sessionId;
}