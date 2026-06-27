#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include <nvs_flash.h>
#include "ESPNowBridge.h"
#include "RelayCommandBox.h"
#include "Config.h"
#include "WiFiCredentialsManager.h"
#include "SafetyWatchdog.h"
#include "AutoCommunicationManager.h"  // 🧠 PILAR INTELIGENTE
#include "MultiChannelDiscovery.h"  // 🔍 DISCOVERY AUTOMÁTICO MULTI-CANAL

// ✅ CORREÇÃO BUG #4: Include para MasterSlaveManager
#ifdef MASTER_MODE
    #include "MasterSlaveManager.h"  // 🎯 SISTEMA INTELIGENTE MASTER
#endif

// ===== CONFIGURAÇÃO DO SISTEMA =====
// Modo definido em platformio.ini via build_flags
// Para alternar entre MASTER e SLAVE, edite platformio.ini (linha 56)

// #define MASTER_MODE    // Modo Master (comentado, use build_flags)
// #define SLAVE_MODE     // Modo Slave (comentado, use build_flags)

// ===== VARIÁVEIS GLOBAIS =====


#ifdef MASTER_MODE
    // Instância do ESP-NOW Master
    ESPNowController master("MasterController", 1);
    
    // ✅ CORREÇÃO BUG #4: Instância do MasterSlaveManager
    MasterSlaveManager* masterManager = nullptr;
    
    // Lista de slaves conhecidos
    std::vector<PeerInfo> knownSlaves;
    
    // Protótipos de função para Master
void setupCallbacks();
    void handleSerialCommands();
    void handleRelayCommand(const String& command);
    void handleBroadcastCommand(const String& command);
    void controlRelay(const String& slaveName, int relayNumber, const String& action, int duration);
    void controlAllRelays(int relayNumber, const String& action, int duration);
    void discoverSlaves();
    void addSlaveToList(const uint8_t* macAddress, const String& deviceName, const String& deviceType, uint8_t numRelays);
    uint8_t* findSlaveMac(const String& slaveName);
    void printSlavesList();
    void printSystemStatus();
    void monitorSlaves();
    void checkOfflineSlaves();
    void printHelp();

#elif defined(SLAVE_MODE)
    // Instâncias principais para Slave
    RelayCommandBox relayBox(0x20, "ESP-NOW-SLAVE");
    ESPNowBridge* espNowBridge = nullptr;  // Será inicializado após detectar canal
    
    // Gerenciadores
    WiFiCredentialsManager wifiManager;
    SafetyWatchdog watchdog;  // Sistema de segurança
    
    // 🔍 DISCOVERY AUTOMÁTICO MULTI-CANAL
    MultiChannelDiscovery* multiChannelDiscovery = nullptr;
    uint8_t masterChannel = 1;  // Canal do Master (detectado via discovery)
    
    // 🧠 SISTEMA INTELIGENTE DE COMUNICAÇÃO
    AutoCommunicationManager* autoComm = nullptr;
    
    // Sistema de monitoramento automático (mantido para compatibilidade)
    unsigned long lastMasterCheck = 0;
    unsigned long lastReconnectionAttempt = 0;
    unsigned long lastSignalCheck = 0;
    int failedPingCount = 0;
    int maxFailedPings = 3;
    bool masterConnected = false;
    bool channelSyncCompleted = false;  // Flag para indicar se canal já foi sincronizado
    
    // Protótipos de função para Slave
    uint8_t detectMasterChannel();
    void processWiFiCredentials(const uint8_t* data, int len);
    void handleSerialCommands();
    void printHelp();
    void monitorMasterConnection();
    void attemptMasterReconnection();
    void checkSignalQuality();
    void implementFallbackStrategy();
    void automaticSlaveDecisions();
    void checkSlaveCommunicationIntegrity();
    void sendAutomaticStatusReport();
    void scanChannelsForMaster();
    
    // 🔍 NOVOS PROTÓTIPOS PARA MULTI-CHANNEL DISCOVERY
    void onMasterFound(uint8_t channel, const uint8_t* masterMac);
    void onDiscoveryProgress(uint8_t channel, uint8_t totalChannels);
    void performRediscoveryIfNeeded();
    void linkMcdToEspNowBridge();

    /** Alinhado com master SLAVE_REACHABLE_MS (45s) */
    static constexpr unsigned long SLAVE_REDISCOVERY_INITIAL_MS = 45000;

    static bool mcdSendBroadcastDelegate();
    static bool mcdChannelSyncDelegate(uint8_t ch);
    static bool mcdRestoreChannelDelegate(uint8_t ch);

#else
    #error "Defina MASTER_MODE ou SLAVE_MODE no início do arquivo"
#endif

// ===== FUNÇÃO PARA INICIALIZAR NVS =====

/**
 * @brief Inicializa NVS e cria namespaces necessários
 * Elimina o erro "nvs_open failed: NOT_FOUND"
 */
void initializeNVS() {
    Serial.println("💾 Inicializando NVS (Non-Volatile Storage)...");
    
    // Inicializar NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        Serial.println("⚠️ NVS precisa ser apagado e reinicializado...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    
    if (err != ESP_OK) {
        Serial.println("❌ Erro ao inicializar NVS: " + String(esp_err_to_name(err)));
        return;
    }
    
    Serial.println("✅ NVS inicializado com sucesso");
    
    // Criar namespaces necessários (isso evita o erro NOT_FOUND)
    Preferences prefs;
    
    // 1. Namespace para WiFi credentials
    if (prefs.begin("wifi_creds", false)) {
        Serial.println("✅ Namespace 'wifi_creds' criado/verificado");
        prefs.end();
    }
    
    // 2. Namespace para configurações do sistema
    if (prefs.begin(PREFERENCES_NAMESPACE, false)) {
        Serial.println("✅ Namespace '" + String(PREFERENCES_NAMESPACE) + "' criado/verificado");
        prefs.end();
    }
    
    // 3. Namespace para MultiChannelDiscovery cache
    if (prefs.begin("mcd_cache", false)) {
        Serial.println("✅ Namespace 'mcd_cache' criado/verificado");
        prefs.end();
    }
    
    Serial.println("💾 NVS pronto para uso!\n");
}

// ===== SETUP PRINCIPAL =====

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    // Inicializar NVS ANTES de tudo
    initializeNVS();
    
#ifdef MASTER_MODE
    Serial.println("🎯 Iniciando ESP-NOW Master Controller");
    Serial.println("=====================================");
    
    // Inicializar ESP-NOW
    if (!master.begin()) {
        Serial.println("❌ Erro ao inicializar ESP-NOW Master");
        return;
    }
    Serial.println("✅ ESP-NOW Master inicializado");
    
    // Configurar callbacks
    setupCallbacks();
    
    // ✅ CORREÇÃO BUG #4: Inicializar MasterSlaveManager
    Serial.println("\n🎯 Inicializando MasterSlaveManager...");
    masterManager = new MasterSlaveManager(&master);
    if (masterManager->begin()) {
        Serial.println("✅ MasterSlaveManager inicializado");
        Serial.println("   ✓ Sistema de Retry: ATIVO");
        Serial.println("   ✓ Sistema de ACK: ATIVO");
        Serial.println("   ✓ Lista Confiável: ATIVA");
    } else {
        Serial.println("❌ Erro ao inicializar MasterSlaveManager");
    }
    
    Serial.println("\n🎯 Master Controller pronto!");
    Serial.println("📡 MAC Master: " + master.getLocalMacString());
    
    // 🔍 DISCOVERY AUTOMÁTICO INICIAL - Sistema Profissional
    Serial.println("\n🔍 === DISCOVERY AUTOMÁTICO INICIAL ===");
    Serial.println("📡 Procurando slaves na rede ESP-NOW...");
    Serial.println("⏳ Aguarde 5 segundos...");
    
    // Fazer 3 broadcasts espaçados para garantir descoberta
    for (int i = 0; i < 3; i++) {
        master.sendDiscoveryBroadcast();
        delay(1500); // 1.5s entre broadcasts
        master.update(); // Processar respostas
    }
    
    Serial.println("✅ Discovery inicial concluído");
    Serial.println("📋 Slaves descobertos: " + String(masterManager->getTrustedSlaveCount()));
    
    if (masterManager->getTrustedSlaveCount() > 0) {
        Serial.println("\n🎉 === SLAVES ENCONTRADOS ===");
        masterManager->printTrustedSlaves();
    } else {
        Serial.println("\n⚠️ Nenhum slave encontrado no discovery inicial");
        Serial.println("💡 Use 'discover' para procurar novamente");
    }
    
    Serial.println("\n💡 Digite 'help' para ver comandos disponíveis");
    Serial.println("📝 Aguardando comandos...");

#elif defined(SLAVE_MODE)
    Serial.println("🚀 Iniciando ESP-NOW Slave (Multi-Channel Discovery)");
    Serial.println("===================================================");
    
    // 1. Cache NVS (sem esp_now — ESPNowController é o único dono)
    Serial.println("\n📡 FASE 1: Cache de canal NVS...");
    multiChannelDiscovery = new MultiChannelDiscovery();
    
    if (multiChannelDiscovery->begin()) {
        multiChannelDiscovery->setMasterFoundCallback(onMasterFound);
        multiChannelDiscovery->setProgressCallback(onDiscoveryProgress);
        
        ChannelCache cache = multiChannelDiscovery->getCache();
        if (cache.lastChannel > 0 && cache.lastChannel <= 13) {
            masterChannel = cache.lastChannel;
            Serial.println("📦 Canal carregado do cache: " + String(masterChannel));
        } else {
            masterChannel = 1;
            Serial.println("📶 Canal padrão: " + String(masterChannel));
        }
    } else {
        Serial.println("❌ Erro ao inicializar MultiChannelDiscovery");
        masterChannel = 1;
    }
    
    masterConnected = false;
    channelSyncCompleted = false;
    
    // 2. RelayCommandBox
    Serial.println("\n🔌 FASE 2: Inicializando RelayCommandBox...");
    if (!relayBox.begin()) {
        Serial.println("⚠️ Aviso: PCF8574 não encontrado - Modo simulação ativado");
    } else {
        Serial.println("✅ RelayCommandBox inicializado");
    }
    
    // 3. ESPNowBridge — único dono de esp_now_init
    Serial.println("\n📡 FASE 3: Inicializando ESP-NOW Bridge...");
    espNowBridge = new ESPNowBridge(&relayBox, masterChannel);
    if (!espNowBridge->begin()) {
        Serial.println("❌ Erro: Falha ao inicializar ESPNowBridge");
        return;
    }
    Serial.println("✅ ESPNowBridge inicializado");
    
    linkMcdToEspNowBridge();
    
    if (multiChannelDiscovery) {
        espNowBridge->setMessageReceivedCallback([](const uint8_t* mac, const uint8_t* data, int len) {
            if (multiChannelDiscovery) {
                multiChannelDiscovery->handleReceivedMessage(mac, data, len);
            }
        });
        Serial.println("✅ MCD vinculado ao ESPNowController (sem segundo esp_now_init)");
    }
    
    espNowBridge->syncRadioChannel(masterChannel);
    
    // 4. SafetyWatchdog
    Serial.println("\n🛡️ FASE 4: Inicializando SafetyWatchdog...");
    watchdog.begin();
    
    // Configurar callback para recebimento de PONG do Master
    espNowBridge->setPingCallback([](const uint8_t* senderMac) {
        watchdog.onMasterResponse();
    });
    Serial.println("✅ SafetyWatchdog configurado");
    Serial.println();
    
    // 5. 🧠 INICIALIZAR SISTEMA INTELIGENTE DE COMUNICAÇÃO (Legacy)
    Serial.println("🧠 FASE 5: Ativando Sistema Inteligente (Legacy)...");
    Serial.println("==================================================");
    
    ESPNowController* controller = espNowBridge->getESPNowController();
    if (controller) {
        autoComm = new AutoCommunicationManager(controller, &wifiManager, false); // false = SLAVE
        if (autoComm->begin()) {
            Serial.println("✅ Sistema Inteligente ATIVO!");
            Serial.println("   ✓ Auto-Discovery: 30s");
            Serial.println("   ✓ Auto-Response: Imediato");
            Serial.println("   ✓ Health Check: 10s");
            Serial.println("   ✓ Auto-Recovery: 4 níveis");
        } else {
            Serial.println("⚠️ Sistema Inteligente não inicializado");
        }
    } else {
        Serial.println("❌ ERRO: ESPNowController não encontrado!");
    }
    Serial.println("==================================================\n");
    
    Serial.println("🎯 Sistema pronto para receber comandos do Master");
    Serial.println("📡 MAC Local: " + WiFi.macAddress());
    Serial.println("📶 Canal: " + String(masterChannel));
    Serial.println("🔌 Relés disponíveis: 0-7");
    Serial.println("💡 Digite 'help' para ver comandos disponíveis");
    Serial.println("📝 Aguardando comandos...");
    
    // 🔍 DEBUG: Mostrar informações de canal
    Serial.println("\n🔍 === DEBUG: INFORMAÇÕES DE CANAL ===");
    uint8_t currentChannel;
    wifi_second_chan_t secondChan;
    esp_wifi_get_channel(&currentChannel, &secondChan);
    Serial.println("📶 Canal ESP-NOW atual: " + String(currentChannel));
    Serial.println("📶 Canal WiFi atual: " + String(WiFi.channel()));
    Serial.println("📶 Canal configurado: " + String(masterChannel));
    Serial.println("=====================================\n");
#endif
}

// ===== LOOP PRINCIPAL =====

void loop() {
#ifdef MASTER_MODE
    // ✅ CORREÇÃO BUG #4: Atualizar MasterSlaveManager (CRÍTICO!)
    if (masterManager) {
        masterManager->update();        // Sistema de retry, ACKs, status
    }
    
    master.update();                    // ESPNowController (cleanup peers)
    handleSerialCommands();             // Comandos do usuário
    monitorSlaves();                    // Ping + check offline
    
    // 🔍 REDISCOVERY PERIÓDICO AUTOMÁTICO - Sistema Profissional
    static unsigned long lastAutoDiscovery = 0;
    const unsigned long AUTO_DISCOVERY_INTERVAL = 120000; // 2 minutos
    
    if (millis() - lastAutoDiscovery > AUTO_DISCOVERY_INTERVAL) {
        Serial.println("\n🔍 [AUTO] Rediscovery periódico iniciado...");
        master.sendDiscoveryBroadcast();
        lastAutoDiscovery = millis();
        
        // Exibir estatísticas
        Serial.printf("📊 Slaves online: %d / %d\n", 
                     masterManager->getOnlineSlaveCount(),
                     masterManager->getTrustedSlaveCount());
    }
    
    delay(100);

#elif defined(SLAVE_MODE)
    // 0. 🔍 RE-DISCOVERY AUTOMÁTICO (se Master perdido)
    if (multiChannelDiscovery) {
        performRediscoveryIfNeeded();
    }
    
    // 0.1. 🧠 SISTEMA INTELIGENTE (prioritário)
    if (autoComm) {
        autoComm->update();  // ✨ CÉREBRO da comunicação
    }
    
    // 1. Alimentar watchdog de hardware (CRÍTICO - sempre primeiro)
    watchdog.feed();
    
    // 2. Verificar saúde do Master
    watchdog.checkMasterHealth();
    
    // 3. Enviar heartbeat se necessário
    if (watchdog.shouldSendHeartbeat()) {
        if (espNowBridge && espNowBridge->isInitialized()) {
            auto peers = espNowBridge->getPeerList();
            if (!peers.empty()) {
                espNowBridge->sendPing(peers[0].macAddress);
            }
        }
    }
    
    // 4. Verificar WiFi periodicamente
    if (watchdog.shouldCheckWiFi()) {
        if (!WiFi.isConnected()) {
            // Só tentar reconectar se tiver credenciais salvas
            // Evitar erros de NVS quando não há credenciais
            if (wifiManager.hasCredentials()) {
                Serial.println("📶 WiFi desconectado - tentando reconectar...");
                wifiManager.connectWithSavedCredentials();
            }
            // Se não tem credenciais, apenas silenciar (Master enviará)
        }
    }
    
    // 5. MODO SEGURO: Desligar todas as bombas se Master offline
    if (watchdog.isSafetyMode()) {
        static unsigned long lastSafetyWarning = 0;
        if (millis() - lastSafetyWarning > 30000) { // Aviso a cada 30s
            Serial.println("🚨 MODO SEGURO ATIVO - Relés desligados");
            relayBox.turnOffAllRelays();
            lastSafetyWarning = millis();
        }
    }
    
    // 6. Atualizar sistemas
    if (espNowBridge) {
        espNowBridge->update();
    }
    relayBox.update();
    
    // 7. Processar comandos seriais
    handleSerialCommands();
    
    delay(100);
#endif
}

// ===== FUNÇÕES AUXILIARES =====


// ===== IMPLEMENTAÇÕES PARA MASTER =====

#ifdef MASTER_MODE

// Buffer para acumular comando
static String commandBuffer = "";

void setupCallbacks() {
    // ✅ CRÍTICO: Configurar callbacks do MasterSlaveManager (recebe notificações do ESPNowController)
    if (masterManager) {
        // Callback quando um novo SLAVE é descoberto
        masterManager->setSlaveDiscoveredCallback([](const uint8_t* macAddress, const String& deviceName, const String& deviceType) {
            Serial.println("\n🎉 ========================================");
            Serial.println("🎉 NOVO SLAVE DESCOBERTO!");
            Serial.println("🎉 ========================================");
            Serial.println("📱 Nome: " + deviceName);
            Serial.println("🏷️ Tipo: " + deviceType);
            Serial.println("📡 MAC: " + ESPNowController::macToString(macAddress));
            Serial.println("========================================\n");
            
            // Adicionar à lista de slaves conhecidos
            addSlaveToList(macAddress, deviceName, deviceType, 8); // 8 relés por padrão
        });
        
        // Callback quando SLAVE fica online
        masterManager->setSlaveOnlineCallback([](const uint8_t* macAddress, const String& deviceName) {
            Serial.println("🟢 Slave ONLINE: " + deviceName + " (" + ESPNowController::macToString(macAddress) + ")");
        });
        
        // Callback quando SLAVE fica offline
        masterManager->setSlaveOfflineCallback([](const uint8_t* macAddress, const String& deviceName) {
            Serial.println("🔴 Slave OFFLINE: " + deviceName + " (" + ESPNowController::macToString(macAddress) + ")");
        });
        
        // Callback para PING recebido do SLAVE
        masterManager->setPingReceivedCallback([](const uint8_t* macAddress, uint32_t pingId) {
            Serial.println("🏓 PING recebido de: " + ESPNowController::macToString(macAddress) + " (ID: " + String(pingId) + ")");
        });
        
        // Callback para status de relé recebido do SLAVE
        masterManager->setRelayStatusCallback([](const uint8_t* macAddress, int relayNumber, bool state, bool hasTimer, int remainingTime, const String& name) {
            Serial.printf("🔌 [%s] Relé %d (%s): %s", 
                         ESPNowController::macToString(macAddress).c_str(),
                         relayNumber, 
                         name.c_str(),
                         state ? "ON" : "OFF");
            
            if (hasTimer) {
                Serial.printf(" (Timer: %ds)", remainingTime);
            }
            Serial.println();
        });
        
        Serial.println("✅ Callbacks do MasterSlaveManager configurados");
    }
    
    // Callback para informações de dispositivos (ESPNowController direto - mantido para compatibilidade)
    master.setDeviceInfoCallback([](const uint8_t* senderMac, const String& deviceName, 
        const String& deviceType, uint8_t numRelays, bool operational) {
        
        Serial.println("📱 [ESPNow] Info de dispositivo:");
        Serial.println("   Nome: " + deviceName);
        Serial.println("   Tipo: " + deviceType);
        Serial.println("   Relés: " + String(numRelays));
        Serial.println("   MAC: " + ESPNowController::macToString(senderMac));
    });
    
    // Callback para status de relés
    master.setRelayStatusCallback([](const uint8_t* senderMac, int relayNumber, 
        bool state, bool hasTimer, int remainingTime, const String& name) {
        
        Serial.printf("🔌 Status de %s: Relé %d = %s", 
                     ESPNowController::macToString(senderMac).c_str(), 
                     relayNumber, state ? "ON" : "OFF");
        
        if (hasTimer) {
            Serial.printf(" (Timer: %ds)", remainingTime);
        }
        Serial.println();
    });
    
    // Callback para ping
    master.setPingCallback([](const uint8_t* senderMac) {
        Serial.println("🏓 Pong recebido de: " + ESPNowController::macToString(senderMac));
    });
    
    // 🔄 FASE 2: Callback para ACK de comandos de relay
    master.setRelayAckCallback([](const uint8_t* senderMac, uint32_t commandId, bool success, 
        uint8_t relayNumber, uint8_t currentState) {
        
        Serial.println("\n🎊 === ACK DE RELAY RECEBIDO ===");
        Serial.println("📱 De: " + ESPNowController::macToString(senderMac));
        Serial.println("🆔 Command ID: " + String(commandId));
        Serial.println("🔌 Relé: " + String(relayNumber));
        Serial.println("✅ Success: " + String(success ? "Sim" : "Não"));
        Serial.println("💡 Estado: " + String(currentState ? "ON" : "OFF"));
        Serial.println("==================================\n");
    });
}

void handleSerialCommands() {
    while (Serial.available()) {
        char c = Serial.read();
        
        // Se for Enter, processar comando
        if (c == '\n' || c == '\r') {
            if (commandBuffer.length() > 0) {
                String command = commandBuffer;
                commandBuffer = ""; // Limpar buffer
                command.trim();
                
                Serial.println(); // Nova linha
                
                if (command == "help") {
                    printHelp();
                }
                else if (command == "discover") {
                    discoverSlaves();
                }
        else if (command == "list") {
            printSlavesList();
        }
        else if (command == "status") {
            printSystemStatus();
        }
        else if (command == "master_stats") {
            // Exibir estatísticas do MasterSlaveManager
            if (masterManager) {
                masterManager->printStatus();
            } else {
                Serial.println("❌ MasterSlaveManager não inicializado");
            }
        }
        else if (command == "master_slaves") {
            // Exibir lista confiável de slaves
            if (masterManager) {
                masterManager->printTrustedSlaves();
            } else {
                Serial.println("❌ MasterSlaveManager não inicializado");
            }
        }
        else if (command == "master_cleanup") {
            // Limpar slaves offline
            if (masterManager) {
                Serial.println("🧹 Limpando slaves offline...");
                masterManager->cleanupOfflineSlaves();
                Serial.println("✅ Limpeza concluída");
            } else {
                Serial.println("❌ MasterSlaveManager não inicializado");
            }
        }
        else if (command == "master_rediscover") {
            // Forçar re-discovery
            if (masterManager) {
                Serial.println("🔍 Forçando re-discovery...");
                masterManager->rediscoverSlaves();
                delay(5000); // Aguardar respostas
                masterManager->printTrustedSlaves();
            } else {
                Serial.println("❌ MasterSlaveManager não inicializado");
            }
        }
        else if (command.startsWith("ping ")) {
            // Comando: ping <slave>
            String slaveName = command.substring(5);
            slaveName.trim();
            uint8_t* slaveMac = findSlaveMac(slaveName);
            if (slaveMac) {
                Serial.println("🏓 Enviando ping para " + slaveName + "...");
                master.sendPing(slaveMac);
            } else {
                Serial.println("❌ Slave não encontrado: " + slaveName);
            }
        }
        else if (command == "ping") {
            // Ping em todos os slaves
            Serial.println("🏓 Enviando ping para todos os slaves...");
            for (const auto& slave : knownSlaves) {
                if (slave.online) {
                    Serial.println("   → " + slave.deviceName);
                    master.sendPing(slave.macAddress);
                    delay(50);
                }
            }
        }
        else if (command.startsWith("remove ")) {
            // Comando: remove <slave>
            String slaveName = command.substring(7);
            slaveName.trim();
            bool found = false;
            for (auto it = knownSlaves.begin(); it != knownSlaves.end(); ++it) {
                if (it->deviceName == slaveName) {
                    knownSlaves.erase(it);
                    Serial.println("✅ Slave removido: " + slaveName);
                    found = true;
                    break;
                }
            }
            if (!found) {
                Serial.println("❌ Slave não encontrado: " + slaveName);
            }
        }
        else if (command.startsWith("relay ")) {
            // Verificar se é comando especial relay off_all ou relay on_all
            if (command == "relay off_all") {
                Serial.println("🔄 Desligando todos os relés em todos os slaves...");
                for (int relayNum = 0; relayNum < 8; relayNum++) {
                    controlAllRelays(relayNum, "off", 0);
                    delay(100);
                }
                Serial.println("✅ Comando relay off_all enviado para todos os slaves");
            }
            else if (command == "relay on_all") {
                Serial.println("🔌 Ligando todos os relés permanentemente em todos os slaves...");
                for (int relayNum = 0; relayNum < 8; relayNum++) {
                    controlAllRelays(relayNum, "on_forever", 0);
                    delay(100);
                }
                Serial.println("✅ Comando relay on_all enviado para todos os slaves");
            }
            else {
                handleRelayCommand(command);
            }
        }
        else if (command.startsWith("broadcast ")) {
            handleBroadcastCommand(command);
        }
        else if (command == "on_all") {
            // Ligar todos os relés permanentemente em todos os slaves
            Serial.println("🔌 Ligando todos os relés permanentemente em todos os slaves...");
            for (int relayNum = 0; relayNum < 8; relayNum++) {
                controlAllRelays(relayNum, "on_forever", 0);
                delay(100);
            }
            Serial.println("✅ Comando on_all enviado para todos os slaves");
        }
        else if (command == "off_all") {
            // Desligar todos os relés em todos os slaves
            Serial.println("🔄 Desligando todos os relés em todos os slaves...");
            for (int relayNum = 0; relayNum < 8; relayNum++) {
                controlAllRelays(relayNum, "off", 0);
                delay(100); // Pequeno delay entre comandos
            }
            Serial.println("✅ Comando off_all enviado para todos os slaves");
        }
        else if (command.startsWith("send_wifi ")) {
            // Formato: send_wifi <ssid> <password>
            // Envia em BROADCAST para TODOS os slaves usando ESPNowBridge
            int space = command.indexOf(' ', 10);
            
            if (space > 0) {
                String ssid = command.substring(10, space);
                String password = command.substring(space + 1);
                
                // Usar ESPNowBridge para enviar credenciais
                bool success = master.sendWiFiCredentialsBroadcast(ssid, password);
                
                if (success) {
                    Serial.println("✅ Credenciais enviadas em broadcast!");
                    Serial.println("📡 Todos os slaves no alcance receberão");
                    Serial.println("⏳ Aguarde os slaves conectarem...");
                } else {
                    Serial.println("❌ Erro ao enviar credenciais");
                }
            } else {
                Serial.println("❌ Formato: send_wifi <ssid> <password>");
                Serial.println("💡 Exemplo: send_wifi MinhaRede senha123");
            }
        }
        else if (command == "send_wifi_auto") {
            // Enviar credenciais do WiFi atual em broadcast usando ESPNowController
            String ssid = WiFi.SSID();
            
            Serial.println("📢 Enviando credenciais WiFi atual em BROADCAST...");
            Serial.println("   SSID: " + ssid);
            Serial.println("⚠️ Digite a senha do WiFi:");
            
            // Aguardar senha via serial
            Serial.print("   Senha: ");
            while (!Serial.available()) {
                delay(100);
            }
            String password = Serial.readStringUntil('\n');
            password.trim();
            Serial.print("   Senha: ");
            for (size_t i = 0; i < password.length(); i++) Serial.print("*");
            Serial.println();
            
            // Obter canal atual
            wifi_second_chan_t secondChan;
            uint8_t currentChannel;
            esp_wifi_get_channel(&currentChannel, &secondChan);
            
            // Usar ESPNowController para enviar com validação
            bool success = master.sendWiFiCredentialsBroadcast(ssid, password, currentChannel);
            
            if (success) {
                Serial.println("✅ Credenciais enviadas em broadcast!");
                Serial.println("📡 Todos os slaves no alcance receberão");
                Serial.println("⏳ Aguarde os slaves conectarem...");
            } else {
                Serial.println("❌ Erro ao enviar credenciais");
            }
        }
        else if (command == "test_wifi_broadcast") {
            Serial.println("\n🧪 === TESTE DE BROADCAST WiFi ===");
            Serial.println("📡 Testando envio de credenciais WiFi...");
            
            // Testar com credenciais de exemplo
            String testSSID = "TESTE_WIFI";
            String testPassword = "senha123";
            
            Serial.println("📤 Enviando credenciais de teste:");
            Serial.println("   SSID: " + testSSID);
            Serial.println("   Senha: " + testPassword);
            
            bool success = master.sendWiFiCredentialsBroadcast(testSSID, testPassword);
            
            if (success) {
                Serial.println("✅ Teste de broadcast bem-sucedido!");
                Serial.println("💡 Se você tem slaves próximos, eles devem receber as credenciais");
            } else {
                Serial.println("❌ Teste de broadcast falhou!");
                Serial.println("💡 Verifique o diagnóstico acima");
            }
            Serial.println("=====================================\n");
        }
        else {
            Serial.println("❓ Comando desconhecido: " + command);
            Serial.println("💡 Digite 'help' para ajuda");
        }
            }
        } else {
            // Acumular caractere no buffer
            commandBuffer += c;
            Serial.print(c); // Echo
        }
    }
}

void handleRelayCommand(const String& command) {
    // Formato: relay <slave> <número> <ação> [duração]
    // Exemplo: relay ESP-NOW-SLAVE 0 on 30
    
    int firstSpace = command.indexOf(' ', 6);
    int secondSpace = command.indexOf(' ', firstSpace + 1);
    int thirdSpace = command.indexOf(' ', secondSpace + 1);
    
    if (firstSpace > 0 && secondSpace > 0) {
        String slaveName = command.substring(6, firstSpace);
        int relayNumber = command.substring(firstSpace + 1, secondSpace).toInt();
        String action;
        int duration = 0;
        
        if (thirdSpace > 0) {
            // Tem duração: relay ESP-NOW-SLAVE 0 on 30
            action = command.substring(secondSpace + 1, thirdSpace);
            duration = command.substring(thirdSpace + 1).toInt();
        } else {
            // Sem duração: relay ESP-NOW-SLAVE 0 on
            action = command.substring(secondSpace + 1);
            action.trim();
        }
        
        controlRelay(slaveName, relayNumber, action, duration);
    } else {
        Serial.println("❌ Formato: relay <slave> <número> <ação> [duração]");
        Serial.println("💡 Exemplo: relay ESP-NOW-SLAVE 0 on 30");
    }
}

void handleBroadcastCommand(const String& command) {
    // Formato: broadcast <número> <ação> [duração]
    // Exemplo: broadcast 1 off
    
    int firstSpace = command.indexOf(' ', 10);
    int secondSpace = command.indexOf(' ', firstSpace + 1);
    
    if (firstSpace > 0) {
        int relayNumber = command.substring(10, firstSpace).toInt();
        String action;
        int duration = 0;
        
        if (secondSpace > 0) {
            // Tem duração: broadcast 1 on 30
            action = command.substring(firstSpace + 1, secondSpace);
            duration = command.substring(secondSpace + 1).toInt();
        } else {
            // Sem duração: broadcast 1 on
            action = command.substring(firstSpace + 1);
            action.trim();
        }
        
        controlAllRelays(relayNumber, action, duration);
    } else {
        Serial.println("❌ Formato: broadcast <número> <ação> [duração]");
        Serial.println("💡 Exemplo: broadcast 1 off");
    }
}

void controlRelay(const String& slaveName, int relayNumber, const String& action, int duration) {
    uint8_t* slaveMac = findSlaveMac(slaveName);
    if (!slaveMac) {
        Serial.println("❌ Slave não encontrado: " + slaveName);
        return;
    }
    
    bool success = master.sendRelayCommand(slaveMac, relayNumber, action, duration);
    if (success) {
        Serial.printf("✅ Comando enviado: %s -> Relé %d %s\n", 
                     slaveName.c_str(), relayNumber, action.c_str());
    } else {
        Serial.println("❌ Falha ao enviar comando");
    }
}

void controlAllRelays(int relayNumber, const String& action, int duration) {
    Serial.println("📤 Enviando comando para todos os slaves...");
    
    for (const auto& slave : knownSlaves) {
        if (slave.online) {
            Serial.println("📤 Enviando para: " + slave.deviceName);
            master.sendRelayCommand(slave.macAddress, relayNumber, action, duration);
            delay(100); // Pequeno delay entre comandos
        }
    }
}

void discoverSlaves() {
    Serial.println("\n🔍 === DISCOVERY MANUAL ===");
    Serial.println("📡 Procurando slaves na rede...");
    
    // Fazer múltiplos broadcasts para garantir descoberta
    for (int i = 0; i < 3; i++) {
        master.sendDiscoveryBroadcast();
        Serial.printf("   Broadcast %d/3 enviado\n", i + 1);
        delay(1000);
        master.update(); // Processar respostas imediatas
    }
    
    // Aguardar mais 5 segundos processando respostas
    Serial.println("⏳ Aguardando respostas...");
    unsigned long startTime = millis();
    while (millis() - startTime < 5000) {
        master.update();
        if (masterManager) {
            masterManager->update();
        }
        delay(100);
    }
    
    Serial.println("\n✅ Discovery concluído!");
    
    // Exibir resultados usando MasterSlaveManager
    if (masterManager) {
        int totalSlaves = masterManager->getTrustedSlaveCount();
        int onlineSlaves = masterManager->getOnlineSlaveCount();
        
        Serial.printf("📊 Slaves conhecidos: %d\n", totalSlaves);
        Serial.printf("🟢 Slaves online: %d\n", onlineSlaves);
        
        if (totalSlaves > 0) {
            Serial.println();
            masterManager->printTrustedSlaves();
        } else {
            Serial.println("\n⚠️ Nenhum slave encontrado");
            Serial.println("💡 Verifique se os slaves estão ligados");
            Serial.println("💡 Verifique se estão no mesmo canal WiFi");
        }
    }
    
    // Exibir lista legada (compatibilidade)
    Serial.println("\n📋 === LISTA LEGADA (knownSlaves) ===");
    Serial.println("📋 Slaves encontrados: " + String(knownSlaves.size()));
    if (knownSlaves.size() > 0) {
        printSlavesList();
    }
}

void addSlaveToList(const uint8_t* macAddress, const String& deviceName, 
                   const String& deviceType, uint8_t numRelays) {
    // Verificar se já existe
    for (auto& slave : knownSlaves) {
        if (memcmp(slave.macAddress, macAddress, 6) == 0) {
            slave.online = true;
            slave.lastSeen = millis();
            slave.deviceName = deviceName;
            slave.deviceType = deviceType;
            return;
        }
    }
    
    // Adicionar novo slave
    PeerInfo newSlave;
    memcpy(newSlave.macAddress, macAddress, 6);
    newSlave.deviceName = deviceName;
    newSlave.deviceType = deviceType;
    newSlave.online = true;
    newSlave.lastSeen = millis();
    newSlave.rssi = -50; // Valor padrão
    
    knownSlaves.push_back(newSlave);
    Serial.println("✅ Novo slave adicionado: " + deviceName);
}

uint8_t* findSlaveMac(const String& slaveName) {
    for (auto& slave : knownSlaves) {
        if (slave.deviceName == slaveName) {
            return slave.macAddress;
        }
    }
    return nullptr;
}

void printSlavesList() {
    Serial.println("\n📋 === SLAVES CONHECIDOS ===");
    if (knownSlaves.empty()) {
        Serial.println("   ⚠️ Nenhum slave encontrado");
        Serial.println("   💡 Use 'discover' para procurar slaves");
    } else {
        Serial.printf("   Total: %d slave(s)\n\n", knownSlaves.size());
        for (const auto& slave : knownSlaves) {
            String statusIcon = slave.online ? "🟢" : "🔴";
            Serial.printf("   %s %s\n", statusIcon.c_str(), slave.deviceName.c_str());
            Serial.printf("      Tipo: %s\n", slave.deviceType.c_str());
            Serial.printf("      MAC: %s\n", ESPNowController::macToString(slave.macAddress).c_str());
            Serial.printf("      Status: %s\n", slave.online ? "Online" : "Offline");
            if (slave.online) {
                unsigned long timeSinceLastSeen = (millis() - slave.lastSeen) / 1000;
                Serial.printf("      Última comunicação: %lu segundos atrás\n", timeSinceLastSeen);
            }
            Serial.println();
        }
    }
    Serial.println("===========================");
}

void printSystemStatus() {
    Serial.println("\n📊 === STATUS DO SISTEMA ===");
    Serial.println("🎯 Master Controller");
    Serial.println("   MAC: " + master.getLocalMacString());
    Serial.println("   Canal: 1");
    Serial.println();
    
    int onlineSlaves = 0;
    for (const auto& slave : knownSlaves) {
        if (slave.online) onlineSlaves++;
    }
    
    Serial.printf("👥 Slaves: %d total (%d online, %d offline)\n", 
                  knownSlaves.size(), onlineSlaves, knownSlaves.size() - onlineSlaves);
    Serial.println();
    
    Serial.println("📊 Estatísticas:");
    Serial.println(master.getStatsJSON());
    
    Serial.println();
    Serial.printf("⏱️ Uptime: %lu segundos\n", millis() / 1000);
    Serial.printf("💾 Heap livre: %d bytes\n", ESP.getFreeHeap());
    Serial.println("===========================");
}

void monitorSlaves() {
    static unsigned long lastPing = 0;
    
    // ✅ CORREÇÃO BUG #6: PING mais frequente (15s → igual ao SLAVE)
    if (millis() - lastPing > 15000) {        // A cada 15s (era 30s)
        for (const auto& slave : knownSlaves) {
            if (slave.online) {
                master.sendPing(slave.macAddress);
            }
        }
        lastPing = millis();
    }
    
    // ✅ CORREÇÃO BUG #6: Check offline mais frequente (45s → igual ao SLAVE SafetyMode)
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck > 45000) {       // A cada 45s (era 60s)
        checkOfflineSlaves();
        lastCheck = millis();
    }
}

void checkOfflineSlaves() {
    for (auto& slave : knownSlaves) {
        // ✅ CORREÇÃO BUG #6: Timeout mais curto (45s → igual ao SLAVE SafetyMode)
        if (millis() - slave.lastSeen > 45000) {  // 45s sem resposta (era 120s)
            if (slave.online) {  // Só loga se mudou de estado
                slave.online = false;
                Serial.println("⚠️ Slave offline: " + slave.deviceName);
                Serial.println("   Última comunicação: " + String((millis() - slave.lastSeen) / 1000) + "s atrás");
            }
        }
    }
}

void printHelp() {
    Serial.println("\n🎮 === COMANDOS MASTER ESP-NOW (Sistema Profissional) ===");
    Serial.println("🔍 DESCOBERTA E GERENCIAMENTO:");
    Serial.println("   discover                        - Discovery manual de slaves");
    Serial.println("   list                            - Listar slaves conhecidos");
    Serial.println("   status                          - Status completo do sistema");
    Serial.println("   ping                            - Testar conectividade com todos os slaves");
    Serial.println("   ping <slave>                    - Testar conectividade com slave específico");
    Serial.println("   remove <slave>                  - Remover slave da lista");
    Serial.println();
    Serial.println("🎯 SISTEMA INTELIGENTE (MasterSlaveManager):");
    Serial.println("   master_stats                    - Estatísticas do MasterSlaveManager");
    Serial.println("   master_slaves                   - Lista confiável de slaves");
    Serial.println("   master_cleanup                  - Limpar slaves offline da lista");
    Serial.println("   master_rediscover               - Forçar re-discovery imediato");
    Serial.println();
    Serial.println("🤖 AUTOMAÇÃO:");
    Serial.println("   • Discovery automático ao iniciar");
    Serial.println("   • Rediscovery periódico (2 min)");
    Serial.println("   • Ping automático (15s)");
    Serial.println("   • Detecção de offline (45s)");
    Serial.println("   • Sistema de ACKs automático");
    Serial.println();
    Serial.println("📶 WIFI (Broadcast para TODOS):");
    Serial.println("   send_wifi <ssid> <password>     - Enviar WiFi em broadcast");
    Serial.println("   send_wifi_auto                  - Enviar WiFi atual em broadcast");
    Serial.println("   test_wifi_broadcast             - Testar envio de credenciais");
    Serial.println();
    Serial.println("🔌 CONTROLE DE RELÉS:");
    Serial.println("   relay <slave> <n> <ação> [duração]");
    Serial.println("   Exemplo: relay ESP-NOW-SLAVE 0 on 30");
    Serial.println();
    Serial.println("📢 CONTROLE EM LOTE:");
    Serial.println("   broadcast <n> <ação> [duração]");
    Serial.println("   Exemplo: broadcast 1 off");
    Serial.println("   relay off_all          - Desligar todos os relés em todos os slaves");
    Serial.println("   relay on_all           - Ligar todos os relés permanentemente em todos os slaves");
    Serial.println("   off_all                - Desligar todos os relés em todos os slaves");
    Serial.println("   on_all                 - Ligar todos os relés permanentemente em todos os slaves");
    Serial.println();
    Serial.println("🎯 AÇÕES DISPONÍVEIS:");
    Serial.println("   on [duração]    - Ligar relé");
    Serial.println("   on_forever     - Ligar relé permanentemente");
    Serial.println("   off            - Desligar relé");
    Serial.println("   toggle         - Alternar relé");
    Serial.println("   status         - Consultar status");
    Serial.println();
    Serial.println("📝 EXEMPLOS:");
    Serial.println("   discover                       - Procura slaves na rede");
    Serial.println("   master_stats                   - Ver estatísticas do sistema");
    Serial.println("   ping ESP-NOW-SLAVE             - Testa conexão com slave");
    Serial.println("   remove ESP-NOW-SLAVE           - Remove slave da lista");
    Serial.println("   relay ESP-NOW-SLAVE 0 on 60    - Liga relé 0 por 1 minuto");
    Serial.println("   relay ESP-NOW-SLAVE 0 on       - Liga relé 0 permanentemente");
    Serial.println("   relay ESP-NOW-SLAVE 1 off      - Desliga relé 1");
    Serial.println("   broadcast 2 toggle             - Alterna relé 2 em todos");
    Serial.println("   relay off_all                  - Desliga todos os relés em todos os slaves");
    Serial.println("   relay on_all                   - Liga todos os relés em todos os slaves");
    Serial.println("   off_all                        - Desliga todos os relés");
    Serial.println("   on_all                         - Liga todos os relés");
    Serial.println("================================\n");
}

#endif // MASTER_MODE

// ===== IMPLEMENTAÇÕES PARA SLAVE =====

#ifdef SLAVE_MODE

// Buffer para acumular comando
static String commandBuffer = "";

// Função para processar credenciais WiFi recebidas via ESP-NOW
void processWiFiCredentials(const uint8_t* data, int len) {
    Serial.println("\n📥 Credenciais WiFi recebidas via ESP-NOW!");
    
    if (len != sizeof(WiFiCredentialsData)) {
        Serial.println("❌ Tamanho de credenciais inválido");
        Serial.printf("   Esperado: %d bytes, Recebido: %d bytes\n", 
                     sizeof(WiFiCredentialsData), len);
        return;
    }
    
    WiFiCredentialsData* creds = (WiFiCredentialsData*)data;
    
    // Validar credenciais usando o novo protocolo
    ESPNowController tempController("temp", 1);
    if (!tempController.validateWiFiCredentials(*creds)) {
        Serial.println("❌ Credenciais inválidas - validação falhou");
        return;
    }
    
    Serial.println("✅ Credenciais validadas com sucesso!");
    Serial.println("   SSID: " + String(creds->ssid));
    Serial.print("   Senha: ");
    for (size_t i = 0; i < strlen(creds->password); i++) Serial.print("*");
    Serial.println();
    Serial.println("   Canal: " + String(creds->channel));
    Serial.println("   Checksum: 0x" + String(creds->checksum, HEX));
    
    // Salvar credenciais
    if (wifiManager.saveCredentials(creds->ssid, creds->password, creds->channel)) {
        Serial.println("💾 Credenciais salvas com sucesso!");
        
        // Conectar ao WiFi
        Serial.println("🔄 Conectando ao WiFi...");
        uint8_t newChannel = creds->channel;  // Canal recebido nas credenciais
        
        if (wifiManager.connectToWiFi(creds->ssid, creds->password)) {
            Serial.println("✅ WiFi conectado!");
            newChannel = WiFi.channel();  // Usar canal do WiFi conectado
            Serial.println("📶 Canal WiFi conectado: " + String(newChannel));
        } else {
            Serial.println("❌ Falha ao conectar ao WiFi");
            Serial.println("💡 Verifique se o roteador está acessível");
            Serial.println("🔄 Sincronizando canal ESP-NOW mesmo sem WiFi...");
            Serial.println("📶 Usando canal recebido: " + String(newChannel));
        }
        
        // ===== CRÍTICO: Sempre reiniciar ESP-NOW no canal correto =====
        Serial.println("🔄 Reiniciando ESP-NOW no canal " + String(newChannel) + "...");
        
        if (espNowBridge) {
            espNowBridge->end();  // Finalizar antes de deletar
            delete espNowBridge;
            espNowBridge = nullptr;
        }
        
        // Pequeno delay para garantir limpeza
        delay(500);
        
        // Criar nova instância ESP-NOW no canal correto
        espNowBridge = new ESPNowBridge(&relayBox, newChannel);
        if (espNowBridge->begin()) {
            Serial.println("✅ ESP-NOW reiniciado com sucesso!");
            Serial.println("📶 Novo canal: " + String(newChannel));
            
            // ===== FLUXO COMPLETO: DESCOBERTA E COMUNICAÇÃO BIDIRECIONAL =====
            Serial.println("\n🔗 === ESTABELECENDO COMUNICAÇÃO BIDIRECIONAL ===");
            
            // 1. Enviar broadcast de discovery inicial
            Serial.println("📢 1/3: Enviando broadcast de discovery para Master...");
            espNowBridge->sendDiscoveryBroadcast();
            delay(500);
            
            // 2. Adicionar Master como peer (MAC recebido nas credenciais)
            // Assumir que o MAC do Master é conhecido ou será recebido via discovery
            Serial.println("🔗 2/3: Adicionando Master como peer...");
            
            // TODO: Pegar MAC do Master (pode ser salvo ou recebido)
            // Por ora, vamos adicionar no próximo discovery response
            
            // 3. Reenviar discovery após sync
            Serial.println("📢 3/3: Reenviando discovery para sincronização...");
            espNowBridge->sendDiscoveryBroadcast();
            
            Serial.println("✅ Processo de sincronização iniciado!");
            Serial.println("⏳ Aguardando Master encontrar este Slave...");
            Serial.println("==========================================\n");
        } else {
            Serial.println("❌ Erro ao reiniciar ESP-NOW");
        }
    } else {
        Serial.println("❌ Erro ao salvar credenciais");
    }
}

// Função para detectar o canal do MASTER automaticamente
uint8_t detectMasterChannel() {
    Serial.println("🔍 Procurando rede WiFi do MASTER...");
    
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    
    // Scan de redes WiFi
    int n = WiFi.scanNetworks();
    Serial.printf("📡 %d rede(s) WiFi encontrada(s)\n", n);
    
    if (n == 0) {
        Serial.println("⚠️ Nenhuma rede WiFi encontrada");
        Serial.println("💡 Usando canal padrão: 1");
        return 1;
    }
    
    // Listar todas as redes encontradas
    Serial.println("\n📋 Redes disponíveis:");
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        uint8_t channel = WiFi.channel(i);
        int32_t rssi = WiFi.RSSI(i);
        
        Serial.printf("   [%d] %s - Canal %d (RSSI: %d dBm)\n", 
                     i, ssid.c_str(), channel, rssi);
    }
    Serial.println();
    
    // ⚠️ NÃO DEVE detectar automaticamente - Master enviará credenciais via ESP-NOW
    Serial.println("⚠️ Nenhuma rede do MASTER conhecida");
    Serial.println("💡 O MASTER deve enviar credenciais via ESP-NOW");
    Serial.println("📡 Aguardando broadcast de credenciais do Master...");
    
    // Retornar canal padrão - Master enviará as credenciais via ESP-NOW
    return 1;
}

void handleSerialCommands() {
    while (Serial.available()) {
        char c = Serial.read();
        
        // Se for Enter, processar comando
        if (c == '\n' || c == '\r') {
            if (commandBuffer.length() > 0) {
                String command = commandBuffer;
                commandBuffer = ""; // Limpar buffer
                command.trim();
                
                Serial.println(); // Nova linha
        
        if (command == "help") {
            printHelp();
        }
        else if (command == "status") {
            relayBox.printStatus();
        }
        else if (command == "watchdog_status") {
            watchdog.printStatus();
        }
        else if (command == "watchdog_reset") {
            watchdog.reset();
        }
        else if (command.startsWith("relay ")) {
            // Verificar se é comando especial relay off_all ou relay on_all
            if (command == "relay off_all") {
                relayBox.turnOffAllRelays();
                Serial.println("🔄 Todos os relés desligados");
            }
            else if (command == "relay on_all") {
                Serial.println("🔌 Ligando todos os relés permanentemente...");
                for (int i = 0; i < 8; i++) {
                    relayBox.processCommand(i, "on_forever", 0);
                }
                Serial.println("✅ Todos os relés ligados permanentemente");
            }
            else {
                // Comando: relay <número> <ação> [duração]
                int firstSpace = command.indexOf(' ', 6);
                
                if (firstSpace > 0) {
                    int relayNumber = command.substring(6, firstSpace).toInt();
                    int secondSpace = command.indexOf(' ', firstSpace + 1);
                    String action;
                    int duration = 0;
                    
                    if (secondSpace > 0) {
                        // Tem duração: relay 1 on 30
                        action = command.substring(firstSpace + 1, secondSpace);
                        duration = command.substring(secondSpace + 1).toInt();
                    } else {
                        // Sem duração: relay 1 on ou relay 1 on_forever
                        action = command.substring(firstSpace + 1);
                        action.trim();
                    }
                    
                    if (relayNumber >= 0 && relayNumber < 8) {
                        bool success = relayBox.processCommand(relayNumber, action, duration);
                        if (success) {
                            Serial.println("✅ Comando executado: Relé " + String(relayNumber) + " -> " + action);
                        } else {
                            Serial.println("❌ Falha ao executar comando");
                        }
                    } else {
                        Serial.println("❌ Número de relé inválido (0-7)");
                    }
                } else {
                    Serial.println("❌ Formato: relay <número> <ação> [duração] ou relay off_all / relay on_all");
                }
            }
        }
        else if (command == "off_all") {
            relayBox.turnOffAllRelays();
            Serial.println("🔄 Todos os relés desligados");
        }
        else if (command == "on_all") {
            // Ligar todos os relés permanentemente
            Serial.println("🔌 Ligando todos os relés permanentemente...");
            for (int i = 0; i < 8; i++) {
                relayBox.processCommand(i, "on_forever", 0);
            }
            Serial.println("✅ Todos os relés ligados permanentemente");
        }
        else if (command == "wifi_status") {
            wifiManager.printStatus();
        }
        else if (command == "wifi_clear") {
            wifiManager.clearCredentials();
            Serial.println("✅ Credenciais WiFi removidas");
            Serial.println("💡 Reinicie o ESP32 para aplicar");
        }
        else if (command == "discover") {
            // Descobrir MASTER na rede
            Serial.println("🔍 Procurando MASTER na rede...");
            if (espNowBridge && espNowBridge->isInitialized()) {
                bool success = espNowBridge->forceDiscovery();
                if (success) {
                    Serial.println("📡 Broadcast de descoberta enviado");
                    Serial.println("⏳ Aguardando resposta do MASTER...");
                    
                    // Aguardar respostas por 10 segundos
                    unsigned long startTime = millis();
                    while (millis() - startTime < 10000) {
                        espNowBridge->update();
                        auto peers = espNowBridge->getPeerList();
                        if (!peers.empty()) {
                            Serial.println("✅ MASTER encontrado: " + peers[0].deviceName);
                            Serial.println("📡 MAC: " + ESPNowBridge::macToString(peers[0].macAddress));
                            break;
                        }
                        delay(100);
                    }
                    
                    auto peers = espNowBridge->getPeerList();
                    if (peers.empty()) {
                        Serial.println("⚠️ Nenhum MASTER respondeu");
                        Serial.println("💡 Verifique se o MASTER está ligado e no mesmo canal");
                    }
                } else {
                    Serial.println("❌ Falha ao enviar broadcast de descoberta");
                }
            } else {
                Serial.println("❌ ESP-NOW não inicializado");
            }
        }
        else if (command == "handshake") {
            // Handshake bidirecional com MASTER
            Serial.println("🤝 Iniciando handshake bidirecional com MASTER...");
            if (espNowBridge && espNowBridge->isInitialized()) {
                // Encontrar MAC do MASTER (assumindo que é o primeiro peer conhecido)
                auto peers = espNowBridge->getPeerList();
                if (!peers.empty()) {
                    espNowBridge->initiateHandshake(peers[0].macAddress);
                    Serial.println("📤 Handshake enviado para: " + peers[0].deviceName);
                } else {
                    Serial.println("❌ Nenhum MASTER encontrado");
                    Serial.println("💡 Use 'discover' primeiro para encontrar o MASTER");
                }
            } else {
                Serial.println("❌ ESP-NOW não inicializado");
            }
        }
        else if (command == "connectivity_report") {
            // Enviar relatório de conectividade
            Serial.println("📊 Enviando relatório de conectividade...");
            if (espNowBridge && espNowBridge->isInitialized()) {
                uint32_t sessionId = espNowBridge->generateSessionId();
                espNowBridge->sendConnectivityReport(nullptr, sessionId); // Broadcast
                Serial.println("✅ Relatório de conectividade enviado");
            } else {
                Serial.println("❌ ESP-NOW não inicializado");
            }
        }
        else if (command == "request_connectivity") {
            // Solicitar verificação de conectividade do MASTER
            Serial.println("🔍 Solicitando verificação de conectividade do MASTER...");
            if (espNowBridge && espNowBridge->isInitialized()) {
                auto peers = espNowBridge->getPeerList();
                if (!peers.empty()) {
                    espNowBridge->requestConnectivityCheck(peers[0].macAddress);
                    Serial.println("📤 Solicitação enviada para: " + peers[0].deviceName);
                } else {
                    Serial.println("❌ Nenhum MASTER encontrado");
                }
            } else {
                Serial.println("❌ ESP-NOW não inicializado");
            }
        }
        else if (command == "auto_validation") {
            // Sistema automático de validação bidirecional
            Serial.println("🔄 Iniciando sistema automático de validação bidirecional...");
            Serial.println("📋 Sequência: Handshake → Relatório de Conectividade → Ping");
            
            if (espNowBridge && espNowBridge->isInitialized()) {
                auto peers = espNowBridge->getPeerList();
                if (!peers.empty()) {
                    Serial.println("\n🎯 Processando: " + peers[0].deviceName);
                    
                    // 1. Handshake
                    Serial.println("   🤝 Enviando handshake...");
                    espNowBridge->initiateHandshake(peers[0].macAddress);
                    delay(500);
                    
                    // 2. Relatório de conectividade
                    Serial.println("   📊 Enviando relatório...");
                    uint32_t sessionId = espNowBridge->generateSessionId();
                    espNowBridge->sendConnectivityReport(peers[0].macAddress, sessionId);
                    delay(500);
                    
                    // 3. Solicitar verificação
                    Serial.println("   🔍 Solicitando verificação...");
                    espNowBridge->requestConnectivityCheck(peers[0].macAddress);
                    delay(500);
                    
                    Serial.println("\n✅ Sistema automático de validação concluído!");
                    Serial.println("📊 Aguarde as respostas do MASTER...");
                } else {
                    Serial.println("❌ Nenhum MASTER encontrado");
                    Serial.println("💡 Use 'discover' primeiro para encontrar o MASTER");
                }
            } else {
                Serial.println("❌ ESP-NOW não inicializado");
            }
        }
        else if (command.startsWith("wifi_connect ")) {
            // Formato: wifi_connect <ssid> <password>
            String params = command.substring(13); // Remove "wifi_connect "
            params.trim();
            
            int space = params.indexOf(' '); // Procura primeiro espaço nos parâmetros
            if (space > 0) {
                String ssid = params.substring(0, space);
                String password = params.substring(space + 1);
                password.trim();
                
                Serial.println("📶 Conectando ao WiFi:");
                Serial.println("   SSID: " + ssid);
                Serial.println("   Senha: " + String(password.length()) + " caracteres");
                
                if (wifiManager.connectToWiFi(ssid, password)) {
                    wifiManager.saveCredentials(ssid, password, WiFi.channel());
                }
            } else {
                Serial.println("❌ Formato: wifi_connect <ssid> <password>");
                Serial.println("💡 Exemplo: wifi_connect YAGO_2.4 suasenha123");
            }
        }
        // ===== 🔍 COMANDOS MULTI-CHANNEL DISCOVERY =====
        else if (command == "mcd_discover") {
            if (multiChannelDiscovery) {
                multiChannelDiscovery->lockMasterChannel(false);
                Serial.println("🔍 Iniciando discovery multi-canal...");
                DiscoveryResult result = multiChannelDiscovery->discoverMaster();
                
                if (result == DiscoveryResult::SUCCESS) {
                    uint8_t channel = multiChannelDiscovery->getCurrentChannel();
                    Serial.println("✅ Master encontrado no canal " + String(channel));
                    
                    // Atualizar flags
                    channelSyncCompleted = true;
                    masterConnected = true;
                    failedPingCount = 0;
                    
                    // Sincronizar canal sem destruir ESP-NOW
                    if (espNowBridge) {
                        espNowBridge->syncRadioChannel(channel);
                        masterChannel = channel;
                        multiChannelDiscovery->lockMasterChannel(true);
                    }
                } else {
                    Serial.println("❌ Master não encontrado: " + 
                                 MultiChannelDiscovery::resultToString(result));
                }
            } else {
                Serial.println("❌ MultiChannelDiscovery não inicializado");
            }
        }
        else if (command == "mcd_rediscover") {
            // Re-discovery rápido
            if (multiChannelDiscovery) {
                Serial.println("🔄 Iniciando re-discovery rápido...");
                DiscoveryResult result = multiChannelDiscovery->rediscoverMaster(true);
                
                if (result == DiscoveryResult::SUCCESS) {
                    Serial.println("✅ Master reencontrado!");
                    channelSyncCompleted = true;
                    masterConnected = true;
                    failedPingCount = 0;
                } else {
                    Serial.println("❌ Re-discovery falhou: " + 
                                 MultiChannelDiscovery::resultToString(result));
                }
            } else {
                Serial.println("❌ MultiChannelDiscovery não inicializado");
            }
        }
        else if (command == "mcd_stats") {
            // Estatísticas do discovery
            if (multiChannelDiscovery) {
                multiChannelDiscovery->printStats();
                Serial.println("\n📊 JSON:");
                Serial.println(multiChannelDiscovery->getStatsJSON());
            } else {
                Serial.println("❌ MultiChannelDiscovery não inicializado");
            }
        }
        else if (command == "mcd_cache_clear") {
            // Limpar cache NVS
            if (multiChannelDiscovery) {
                multiChannelDiscovery->clearCache();
                Serial.println("✅ Cache limpo!");
            } else {
                Serial.println("❌ MultiChannelDiscovery não inicializado");
            }
        }
        else if (command == "mcd_cache_save") {
            // Forçar salvar cache
            if (multiChannelDiscovery) {
                multiChannelDiscovery->saveCache();
                Serial.println("✅ Cache salvo!");
                
                ChannelCache cache = multiChannelDiscovery->getCache();
                Serial.println("📦 Cache atual:");
                Serial.println("   Canal: " + String(cache.lastChannel));
                Serial.println("   Uso: " + String(cache.usageCount) + "x");
                Serial.println("   Taxa: " + String(cache.successRate) + "%");
            } else {
                Serial.println("❌ MultiChannelDiscovery não inicializado");
            }
        }
        else if (command == "mcd_cache_show" || command == "mcd_cache_view") {
            // Mostrar conteúdo do cache NVS
            if (multiChannelDiscovery) {
                multiChannelDiscovery->printCache();
            } else {
                Serial.println("❌ MultiChannelDiscovery não inicializado");
            }
        }
        else if (command.startsWith("mcd_try_channel ")) {
            // Testar canal específico
            uint8_t channel = command.substring(16).toInt();
            
            if (channel >= 1 && channel <= 13) {
                if (multiChannelDiscovery) {
                    Serial.println("🔍 Testando canal " + String(channel) + "...");
                    bool found = multiChannelDiscovery->tryChannel(channel, 1000);
                    
                    if (found) {
                        Serial.println("✅ Master respondeu no canal " + String(channel));
                    } else {
                        Serial.println("❌ Sem resposta no canal " + String(channel));
                    }
                } else {
                    Serial.println("❌ MultiChannelDiscovery não inicializado");
                }
            } else {
                Serial.println("❌ Canal inválido (1-13)");
                Serial.println("💡 Uso: mcd_try_channel <1-13>");
            }
        }
        else {
            Serial.println("❓ Comando desconhecido: " + command);
            Serial.println("💡 Digite 'help' para ver comandos disponíveis");
        }
            }
        } else {
            // Acumular caractere no buffer
            commandBuffer += c;
            Serial.print(c); // Echo
        }
    }
}

void printHelp() { 
    Serial.println("\n📋 === COMANDOS SLAVE ESP-NOW ===");
    Serial.println("🏗️ SISTEMA:");
    Serial.println("   help                        - Esta ajuda");
    Serial.println("   status                      - Status de todos os relés");
    Serial.println("   discover                    - Procurar MASTER na rede");
    Serial.println();
    Serial.println("🔍 MULTI-CHANNEL DISCOVERY (Novo!):");
    Serial.println("   mcd_discover                - Discovery completo (canais 1-13)");
    Serial.println("   mcd_rediscover              - Re-discovery rápido");
    Serial.println("   mcd_stats                   - Estatísticas do discovery");
    Serial.println("   mcd_cache_clear             - Limpar cache NVS");
    Serial.println("   mcd_cache_save              - Salvar cache atual");
    Serial.println("   mcd_cache_show              - Mostrar conteúdo do cache NVS");
    Serial.println("   mcd_try_channel <1-13>      - Testar canal específico");
    Serial.println();
    Serial.println("🛡️ WATCHDOG:");
    Serial.println("   watchdog_status             - Status do SafetyWatchdog");
    Serial.println("   watchdog_reset              - Resetar watchdog manualmente");
    Serial.println();
    Serial.println("🤝 VALIDAÇÃO BIDIRECIONAL:");
    Serial.println("   handshake                   - Handshake bidirecional com MASTER");
    Serial.println("   connectivity_report         - Enviar relatório de conectividade");
    Serial.println("   request_connectivity       - Solicitar verificação do MASTER");
    Serial.println("   auto_validation            - Sistema automático completo de validação");
    Serial.println();
    Serial.println("📶 WIFI:");
    Serial.println("   wifi_status                 - Status da conexão WiFi");
    Serial.println("   wifi_connect <ssid> <pass>  - Conectar ao WiFi manualmente");
    Serial.println("   wifi_clear                  - Limpar credenciais salvas");
    Serial.println();
    Serial.println("🚀 TASK DEDICADA ESP-NOW (Nova arquitetura):");
    Serial.println("   task_status                 - Status da task dedicada");
    Serial.println("   task_discover               - Discovery via task dedicada");
    Serial.println();
    Serial.println("🔌 CONTROLE DE RELÉS (0-7):");
    Serial.println("   relay <n> on [tempo]    - Ligar relé");
    Serial.println("   relay <n> on_forever    - Ligar relé permanentemente");
    Serial.println("   relay <n> off           - Desligar relé");
    Serial.println("   relay <n> toggle        - Alternar relé");
    Serial.println("   relay off_all           - Desligar todos os relés");
    Serial.println("   relay on_all            - Ligar todos os relés permanentemente");
    Serial.println("   off_all                 - Desligar todos");
    Serial.println("   on_all                  - Ligar todos os relés permanentemente");
    Serial.println();
    Serial.println("📝 EXEMPLOS:");
    Serial.println("   handshake                 - Handshake com MASTER");
    Serial.println("   auto_validation           - Validação automática completa");
    Serial.println("   connectivity_report       - Enviar relatório de status");
    Serial.println("   relay 0 on 30            - Liga relé 0 por 30s");
    Serial.println("   relay 0 on               - Liga relé 0 permanentemente");
    Serial.println("   relay 0 on_forever       - Liga relé 0 permanentemente");
    Serial.println("   relay 1 off              - Desliga relé 1");
    Serial.println("   relay 2 toggle           - Alterna relé 2");
    Serial.println("   relay off_all            - Desliga todos os relés");
    Serial.println("   relay on_all             - Liga todos os relés permanentemente");
    Serial.println("   off_all                  - Desliga todos");
    Serial.println("   on_all                   - Liga todos os relés permanentemente");
    Serial.println();
    Serial.println("🤖 MODO SLAVE:");
    Serial.println("   - Recebe comandos via ESP-NOW do Master");
    Serial.println("   - Suporta 8 relés via PCF8574");
    Serial.println("   - Interface serial para teste local");
    Serial.println("   - Validação bidirecional automática");
    Serial.println("   - Monitoramento automático de conexão");
    Serial.println("===============================\n");
}

// ===== SISTEMA DE MONITORAMENTO AUTOMÁTICO SLAVE =====

/**
 * @brief Monitora conexão com MASTER automaticamente
 */
void monitorMasterConnection() {
    unsigned long currentTime = millis();
    
    // Verificar conexão com MASTER a cada 30 segundos
    if (currentTime - lastMasterCheck > 30000) {
        Serial.println("🔍 Verificação automática de conexão com MASTER...");
        
        if (!masterConnected) {
            Serial.println("⚠️ MASTER desconectado detectado");
            
            // Tentar reconexão automática
            attemptMasterReconnection();
        } else {
            // Verificar se ainda está conectado
            if (espNowBridge && espNowBridge->isInitialized()) {
                auto peers = espNowBridge->getPeerList();
                if (peers.empty()) {
                    Serial.println("⚠️ Nenhum peer MASTER encontrado");
                    masterConnected = false;
                    attemptMasterReconnection();
                }
            }
        }
        
        lastMasterCheck = currentTime;
    }
    
    // Verificar qualidade do sinal a cada 60 segundos
    if (currentTime - lastSignalCheck > 60000) {
        checkSignalQuality();
        lastSignalCheck = currentTime;
    }
}

/**
 * @brief Tenta reconectar com MASTER
 */
void attemptMasterReconnection() {
    if (!espNowBridge) return;
    
    Serial.println("🔄 Tentativa de reconexão com MASTER...");
    
    // 1. Tentar descobrir MASTER novamente
    Serial.println("   🔍 Tentando descobrir MASTER...");
    
    // 2. Enviar handshake para testar conectividade
    if (espNowBridge && espNowBridge->isInitialized()) {
        auto peers = espNowBridge->getPeerList();
        if (!peers.empty()) {
            Serial.println("   🤝 Enviando handshake para MASTER...");
            espNowBridge->initiateHandshake(peers[0].macAddress);
            
            // Aguardar resposta por 5 segundos
            unsigned long handshakeStart = millis();
            bool receivedResponse = false;
            
            while (millis() - handshakeStart < 5000) {
                if (masterConnected) {
                    receivedResponse = true;
                    break;
                }
                delay(100);
            }
            
            if (receivedResponse) {
                Serial.println("✅ MASTER reconectado!");
                masterConnected = true;
                failedPingCount = 0;
            } else {
                Serial.println("❌ Sem resposta do MASTER");
                failedPingCount++;
                
                if (failedPingCount >= maxFailedPings) {
                    Serial.println("🚨 Máximo de tentativas atingido");
                    implementFallbackStrategy();
                }
            }
        } else {
            Serial.println("❌ Nenhum peer MASTER encontrado");
            implementFallbackStrategy();
        }
    }
    
    lastReconnectionAttempt = millis();
}

/**
 * @brief Verifica qualidade do sinal WiFi
 */
void checkSignalQuality() {
    if (!WiFi.isConnected()) {
        Serial.println("⚠️ WiFi desconectado - tentando reconectar...");
        if (wifiManager.hasCredentials()) {
            wifiManager.connectWithSavedCredentials();
        }
        return;
    }
    
    int rssi = WiFi.RSSI();
    Serial.println("📶 Verificando qualidade do sinal WiFi...");
    Serial.println("   RSSI: " + String(rssi) + " dBm");
    
    if (rssi < -80) {
        Serial.println("⚠️ Sinal WiFi fraco detectado (RSSI: " + String(rssi) + ")");
        
        if (rssi < -90) {
            Serial.println("🚨 Sinal WiFi crítico - tentando reconexão...");
            if (wifiManager.hasCredentials()) {
                wifiManager.connectWithSavedCredentials();
            }
        }
    }
}

/**
 * @brief Implementa estratégia de fallback quando comunicação falha
 */
void implementFallbackStrategy() {
    Serial.println("🔄 Implementando estratégia de fallback...");
    
    // 1. Tentar reconectar WiFi
    Serial.println("   📶 Tentando reconectar WiFi...");
    if (wifiManager.hasCredentials()) {
        if (wifiManager.connectWithSavedCredentials()) {
            Serial.println("   ✅ WiFi reconectado");
        } else {
            Serial.println("   ❌ Falha ao reconectar WiFi");
        }
    }
    
    // 2. Tentar detectar canal MASTER novamente
    Serial.println("   🔍 Tentando detectar canal MASTER...");
    uint8_t newChannel = detectMasterChannel();
    Serial.println("   📡 Canal detectado: " + String(newChannel));
    
    // 3. Reinicializar ESP-NOW se necessário
    if (espNowBridge) {
        Serial.println("   🔄 Reinicializando ESP-NOW...");
        espNowBridge->end();
        delay(1000);
        espNowBridge->begin();
    }
    
    // 4. Resetar contador de falhas
    failedPingCount = 0;
}

/**
 * @brief Sistema de decisão automática para SLAVE
 */
void automaticSlaveDecisions() {
    unsigned long currentTime = millis();
    
    // Decisão 1: Monitorar conexão com MASTER
    monitorMasterConnection();
    
    // Decisão 2: Verificar integridade da comunicação a cada 2 minutos
    static unsigned long lastIntegrityCheck = 0;
    if (currentTime - lastIntegrityCheck > 120000) {
        checkSlaveCommunicationIntegrity();
        lastIntegrityCheck = currentTime;
    }
    
    // Decisão 3: Enviar relatório de status a cada 5 minutos
    static unsigned long lastStatusReport = 0;
    if (currentTime - lastStatusReport > 300000) {
        sendAutomaticStatusReport();
        lastStatusReport = currentTime;
    }
}

/**
 * @brief Verifica integridade da comunicação do SLAVE
 */
void checkSlaveCommunicationIntegrity() {
    Serial.println("🔍 Verificação de integridade da comunicação SLAVE...");
    
    bool wifiOk = WiFi.isConnected();
    bool espNowOk = (espNowBridge != nullptr);
    bool masterOk = masterConnected;
    
    Serial.println("📊 Status da comunicação:");
    Serial.println("   WiFi: " + String(wifiOk ? "✅ Conectado" : "❌ Desconectado"));
    Serial.println("   ESP-NOW: " + String(espNowOk ? "✅ Ativo" : "❌ Inativo"));
    Serial.println("   MASTER: " + String(masterOk ? "✅ Conectado" : "❌ Desconectado"));
    
    // Decisão automática baseada na integridade
    if (!wifiOk) {
        Serial.println("🚨 WiFi desconectado - tentando reconectar...");
        if (wifiManager.hasCredentials()) {
            wifiManager.connectWithSavedCredentials();
        }
    }
    
    if (!masterOk) {
        Serial.println("🚨 MASTER desconectado - tentando reconectar...");
        attemptMasterReconnection();
    }
}

/**
 * @brief Envia relatório de status automático
 */
void sendAutomaticStatusReport() {
    if (!espNowBridge || !espNowBridge->isInitialized()) return;
    
    Serial.println("📊 Enviando relatório de status automático...");
    
    uint32_t sessionId = espNowBridge->generateSessionId();
    espNowBridge->sendConnectivityReport(nullptr, sessionId); // Broadcast
    
    Serial.println("✅ Relatório de status enviado");
}

/**
 * @brief Escaneia canais WiFi para encontrar Master
 */
void scanChannelsForMaster() {
    static uint8_t currentScanChannel = 1;
    static bool scanning = false;
    
    if (scanning) return; // Evitar múltiplos scans simultâneos
    
    scanning = true;
    
    // Escanear canal atual
    uint8_t currentChannel;
    wifi_second_chan_t secondChan;
    esp_wifi_get_channel(&currentChannel, &secondChan);
    
    if (currentChannel != currentScanChannel) {
        // Scan silencioso - sem logs
        esp_wifi_set_channel(currentScanChannel, WIFI_SECOND_CHAN_NONE);
        delay(100); // Pequeno delay para estabilizar
    }
    
    // Próximo canal
    currentScanChannel++;
    if (currentScanChannel > 13) {
        currentScanChannel = 1; // Voltar ao canal 1
    }
    
    scanning = false;
}

// ===== 🔍 IMPLEMENTAÇÕES MULTI-CHANNEL DISCOVERY =====

/**
 * @brief Callback quando Master é encontrado
 */
void onMasterFound(uint8_t channel, const uint8_t* masterMac) {
    Serial.println("\n🎯 ========================================");
    Serial.println("🎯 === CALLBACK: MASTER ENCONTRADO! ===");
    Serial.println("🎯 ========================================");
    Serial.println("📶 Canal Master: " + String(channel));
    Serial.printf("🆔 MAC Master: %02X:%02X:%02X:%02X:%02X:%02X\n",
                 masterMac[0], masterMac[1], masterMac[2], 
                 masterMac[3], masterMac[4], masterMac[5]);
    
    // 🚨 CRÍTICO: Verificar canal atual do Slave
    uint8_t canalAtual;
    wifi_second_chan_t secondChan;
    esp_wifi_get_channel(&canalAtual, &secondChan);
    
    Serial.println("\n🔍 Verificação de canal:");
    Serial.println("   Canal Slave atual: " + String(canalAtual));
    Serial.println("   Canal Master: " + String(channel));
    
    // 🚨 CRÍTICO: Se canais diferentes, sincronizar via ESPNowController (sem deinit)
    if (canalAtual != channel) {
        Serial.println("\n🔄 === SINCRONIZANDO CANAL ===");
        Serial.println("🔧 Canal " + String(canalAtual) + " → " + String(channel));
        
        if (espNowBridge && espNowBridge->syncRadioChannel(channel)) {
            Serial.println("✅ Canal sincronizado via ESPNowController");
        } else {
            esp_err_t err = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
            Serial.println(err == ESP_OK ? "✅ Canal alterado (fallback)" :
                          "❌ Erro ao alterar canal: " + String(err));
        }
        delay(100);
        esp_wifi_get_channel(&canalAtual, &secondChan);
        Serial.println("🔍 Canal após mudança: " + String(canalAtual));
    } else {
        Serial.println("✅ Já estamos no canal correto!");
    }
    
    Serial.println("========================================\n");
    
    // 🚨 CRÍTICO: Enviar DeviceInfo automaticamente quando Master é encontrado
    // Isso garante que o Master registre o Slave imediatamente
    if (espNowBridge) {
        ESPNowController* controller = espNowBridge->getESPNowController();
        if (controller) {
            Serial.println("\n📤 [AUTO] Enviando DeviceInfo para Master recém-descoberto...");
            Serial.println("   💡 Master precisa registrar este Slave em trustedSlaves");
            Serial.println("   📶 Canal: " + String(channel));
            
            bool sent = controller->sendDeviceInfo(
                masterMac,           // Para o Master recém-descoberto
                "RelayBox",          // Tipo do dispositivo
                8,                   // 8 relés disponíveis
                true,                // Operacional
                millis(),            // Uptime atual
                ESP.getFreeHeap()    // Memória livre
            );
            
            if (sent) {
                Serial.println("✅ DeviceInfo enviado automaticamente ao Master!");
                Serial.println("🎯 Master deve registrar este Slave agora");
            } else {
                Serial.println("❌ Falha ao enviar DeviceInfo automaticamente");
                Serial.println("💡 Tentando novamente em 300ms...");
                delay(300);
                sent = controller->sendDeviceInfo(
                    masterMac, "RelayBox", 8, true, millis(), ESP.getFreeHeap()
                );
                if (sent) {
                    Serial.println("✅ DeviceInfo re-enviado com sucesso!");
                } else {
                    Serial.println("❌ Falha definitiva - Master pode não registrar este Slave");
                }
            }
        }
    }
    
    // Atualizar flags
    channelSyncCompleted = true;
    masterConnected = true;
    failedPingCount = 0;
    masterChannel = channel;

    if (multiChannelDiscovery) {
        multiChannelDiscovery->lockMasterChannel(true);
    }
}

static bool mcdSendBroadcastDelegate() {
    return espNowBridge && espNowBridge->isInitialized() && espNowBridge->sendDiscoveryBroadcast();
}

static bool mcdChannelSyncDelegate(uint8_t ch) {
    return espNowBridge && espNowBridge->isInitialized() && espNowBridge->syncRadioChannel(ch);
}

static bool mcdRestoreChannelDelegate(uint8_t ch) {
    if (!espNowBridge || !espNowBridge->isInitialized()) return false;
    return espNowBridge->syncRadioChannel(ch) && espNowBridge->ensureBroadcastPeer();
}

void linkMcdToEspNowBridge() {
    if (!multiChannelDiscovery || !espNowBridge) return;
    multiChannelDiscovery->setSendBroadcastDelegate(mcdSendBroadcastDelegate);
    multiChannelDiscovery->setChannelSyncDelegate(mcdChannelSyncDelegate);
    multiChannelDiscovery->setRestoreChannelDelegate(mcdRestoreChannelDelegate);
    Serial.println("🔗 MCD delegates → ESPNowController");
}

/**
 * @brief Callback de progresso do discovery
 */
void onDiscoveryProgress(uint8_t channel, uint8_t totalChannels) {
    // Mostrar progresso apenas em modo verbose
    // Serial.print(".");
}

/**
 * @brief Executa re-discovery automático se Master perdido
 */
void performRediscoveryIfNeeded() {
    static unsigned long lastRediscoveryAttempt = 0;
    static unsigned long rediscoveryInterval = SLAVE_REDISCOVERY_INITIAL_MS;
    static unsigned long slaveBootMs = 0;
    if (slaveBootMs == 0) slaveBootMs = millis();

    // Link estável: não fazer scan
    if (channelSyncCompleted && masterConnected && !watchdog.isSafetyMode() &&
        multiChannelDiscovery && multiChannelDiscovery->isMasterChannelLocked()) {
        return;
    }
    
    bool needsRediscovery = false;
    
    // Condição 0: nunca encontrou master — escuta passiva 45s antes do primeiro scan
    if (!masterConnected && !channelSyncCompleted) {
        if (millis() - slaveBootMs < SLAVE_REDISCOVERY_INITIAL_MS) {
            return;
        }
        needsRediscovery = true;
        rediscoveryInterval = SLAVE_REDISCOVERY_INITIAL_MS;
        
        static bool firstAttempt = true;
        if (firstAttempt) {
            Serial.println("💡 Escuta passiva " + String(SLAVE_REDISCOVERY_INITIAL_MS / 1000) +
                           "s no canal NVS antes do scan...");
            firstAttempt = false;
        }
    }
    
    if (watchdog.isSafetyMode()) {
        needsRediscovery = true;
        rediscoveryInterval = 45000;
    }
    
    if (failedPingCount >= maxFailedPings) {
        needsRediscovery = true;
        rediscoveryInterval = SLAVE_REDISCOVERY_INITIAL_MS;
    }
    
    if (!masterConnected && channelSyncCompleted) {
        needsRediscovery = true;
        if (multiChannelDiscovery) {
            multiChannelDiscovery->lockMasterChannel(false);
        }
        rediscoveryInterval = SLAVE_REDISCOVERY_INITIAL_MS;
    }
    
    if (needsRediscovery && (millis() - lastRediscoveryAttempt > rediscoveryInterval)) {
        Serial.println("\n🔄 === INICIANDO RE-DISCOVERY ===");
        Serial.println("Motivo: " + String(
            (!masterConnected && !channelSyncCompleted) ? "MasterNuncaEncontrado" :
            watchdog.isSafetyMode() ? "SafetyMode" :
            failedPingCount >= maxFailedPings ? "PingsFalhados" :
            "MasterDesconectado"
        ));
        
        DiscoveryResult result = multiChannelDiscovery->rediscoverMaster(true);
        
        if (result == DiscoveryResult::SUCCESS) {
            uint8_t newChannel = multiChannelDiscovery->getCurrentChannel();
            Serial.println("✅ Master reencontrado no canal " + String(newChannel));
            
            if (espNowBridge) {
                espNowBridge->syncRadioChannel(newChannel);
                masterChannel = newChannel;
            }
            masterConnected = true;
            failedPingCount = 0;
            channelSyncCompleted = true;
            multiChannelDiscovery->lockMasterChannel(true);
            rediscoveryInterval = SLAVE_REDISCOVERY_INITIAL_MS;
            
        } else if (result != DiscoveryResult::BLOCKED) {
            Serial.println("❌ Re-discovery falhou: " + MultiChannelDiscovery::resultToString(result));
            if (espNowBridge) {
                espNowBridge->ensureBroadcastPeer();
                espNowBridge->sendDiscoveryBroadcast();
            }
            rediscoveryInterval = min((unsigned long)(rediscoveryInterval * 1.5), 300000UL);
            Serial.println("⏳ Próxima tentativa em " + String(rediscoveryInterval / 1000) + "s");
        }
        
        lastRediscoveryAttempt = millis();
        Serial.println("=================================\n");
    }
}

#endif // SLAVE_MODE