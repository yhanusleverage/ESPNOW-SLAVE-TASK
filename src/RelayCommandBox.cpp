#include "RelayCommandBox.h"
#include <WiFi.h>
#include <nvs_flash.h>
#include <nvs.h>
#include "ESPNowTypes.h"  // Para PersistentRelayStateData

// Tentar incluir Config.h se disponível
#ifndef CONFIG_H
    #include "Config.h"
#endif

RelayCommandBox::RelayCommandBox(uint8_t pcf8574Address, const String& deviceName) 
    : pcf8574(nullptr), i2cAddress(pcf8574Address), deviceName(deviceName), pcfInitialized(false) {
    
    // Inicializar estados dos relés
    for (int i = 0; i < MAX_RELAYS; i++) {
        relayStates[i].isOn = false;
        relayStates[i].startTime = 0;
        relayStates[i].timerSeconds = 0;
        relayStates[i].hasTimer = false;
        relayStates[i].name = "";
    }
    
    // Inicializar nomes padrão
    initializeDefaultNames();
}

RelayCommandBox::~RelayCommandBox() {
    if (pcf8574 != nullptr) {
        delete pcf8574;
        pcf8574 = nullptr;
    }
}

bool RelayCommandBox::begin() {
    DEBUG_PRINTLN("🔌 Inicializando RelayCommandBox: " + deviceName);
    DEBUG_PRINTLN("📍 Endereço PCF8574: 0x" + String(i2cAddress, HEX));
    
    // 🎯 PASSO 1: Inicializar hardware PRIMEIRO
    // Inicializar I2C apenas se ainda não foi inicializado
    static bool wireInitialized = false;
    if (!wireInitialized) {
        Wire.begin(I2C_SDA, I2C_SCL);
        Wire.setClock(I2C_FREQUENCY);
        wireInitialized = true;
        DEBUG_PRINTLN("🔧 I2C inicializado - SDA: " + String(I2C_SDA) + ", SCL: " + String(I2C_SCL) + ", Freq: " + String(I2C_FREQUENCY));
    } else {
        DEBUG_PRINTLN("🔧 I2C já inicializado anteriormente");
    }
    
    // Usar abordagem de scan dinâmico como no teste do usuário
    DEBUG_PRINTLN("🔍 Escaneando endereços PCF8574...");
    pcfInitialized = scanAndInitializePCF8574();
    
    if (!pcfInitialized) {
        Serial.println("⚠️ PCF8574 não encontrado - Modo simulação ativado");
        Serial.println("💡 Para controle real de relés, conecte PCF8574 nos endereços 0x20-0x27");
        Serial.println("🎮 Comandos funcionarão em modo simulação");
        
        // Inicializar estados em modo simulação
        for (int i = 0; i < MAX_RELAYS; i++) {
            relayStates[i].isOn = false;
            relayStates[i].startTime = 0;
            relayStates[i].timerSeconds = 0;
            relayStates[i].hasTimer = false;
        }
        
        Serial.println("✅ RelayCommandBox inicializado (MODO SIMULAÇÃO): " + deviceName);
        Serial.println("🎯 Relés disponíveis: 0-" + String(MAX_RELAYS - 1));
        return true; // Retornar true para permitir funcionamento em simulação
    }
    
    // 🎯 PASSO 2: Agora que hardware está inicializado, carregar estados persistentes
    // IMPORTANTE: Hardware já desligou todos os relays, agora aplicamos estados persistentes
    Serial.println("💾 Carregando estados persistentes de NVS...");
    bool statesLoaded = loadPersistentStates();
    if (statesLoaded) {
        Serial.println("✅ Estados persistentes carregados e aplicados");
    } else {
        Serial.println("💡 Nenhum estado persistente encontrado - iniciando com estados padrão");
        // Hardware já desligou todos os relays no scanAndInitializePCF8574()
        // Não precisamos chamar turnOffAllRelays() novamente
    }
    
    Serial.println("✅ RelayCommandBox inicializado: " + deviceName);
    Serial.println("🎯 Relés disponíveis: 0-" + String(MAX_RELAYS - 1));
    
    return true;
}

void RelayCommandBox::update() {
    // Sempre verificar timers, mesmo em modo simulação
    checkTimers();
}

bool RelayCommandBox::setRelay(int relayNumber, bool state) {
    if (!isValidRelayNumber(relayNumber)) {
        Serial.println("❌ Número de relé inválido: " + String(relayNumber));
        return false;
    }
    
    if (!pcfInitialized) {
        Serial.println("❌ PCF8574 não inicializado");
        return false;
    }
    
    // Cancelar timer se existir
    relayStates[relayNumber].hasTimer = false;
    relayStates[relayNumber].timerSeconds = 0;
    
    // Definir novo estado
    relayStates[relayNumber].isOn = state;
    relayStates[relayNumber].startTime = millis();
    
    // Escrever no hardware
    bool success = writeToRelay(relayNumber, state);
    
    if (success) {
        String relayName = relayStates[relayNumber].name.isEmpty() ? 
                          "Relé " + String(relayNumber) : 
                          relayStates[relayNumber].name;
        
        Serial.println("🔌 " + relayName + " " + (state ? "LIGADO" : "DESLIGADO"));
        
        // 🎯 Guardar estado en NVS automáticamente
        savePersistentStates();
        
        // Chamar callback se definido
        if (stateChangeCallback) {
            stateChangeCallback(relayNumber, state, 0);
        }
    } else {
        Serial.println("❌ Erro ao controlar relé " + String(relayNumber));
    }
    
    return success;
}

bool RelayCommandBox::setRelayWithTimer(int relayNumber, bool state, int seconds) {
    if (!isValidRelayNumber(relayNumber)) {
        Serial.println("❌ Número de relé inválido: " + String(relayNumber));
        return false;
    }
    
    if (!pcfInitialized) {
        Serial.println("❌ PCF8574 não inicializado");
        return false;
    }
    
    if (seconds <= 0) {
        return setRelay(relayNumber, state);
    }
    
    // Validar duração máxima
    if (seconds > DEFAULT_MAX_DURATION) {
        Serial.println("⚠️ Duração limitada a " + String(DEFAULT_MAX_DURATION) + " segundos");
        seconds = DEFAULT_MAX_DURATION;
    }
    
    // Configurar estado com timer
    relayStates[relayNumber].isOn = state;
    relayStates[relayNumber].startTime = millis();
    relayStates[relayNumber].timerSeconds = seconds;
    relayStates[relayNumber].hasTimer = true;
    
    // Escrever no hardware
    bool success = writeToRelay(relayNumber, state);
    
    if (success) {
        String relayName = relayStates[relayNumber].name.isEmpty() ? 
                          "Relé " + String(relayNumber) : 
                          relayStates[relayNumber].name;
        
        Serial.println("⏰ " + relayName + " " + (state ? "LIGADO" : "DESLIGADO") + 
                      " por " + String(seconds) + " segundos");
        
        // 🎯 Guardar estado en NVS automáticamente
        savePersistentStates();
        
        // Chamar callback se definido
        if (stateChangeCallback) {
            stateChangeCallback(relayNumber, state, seconds);
        }
    } else {
        Serial.println("❌ Erro ao controlar relé " + String(relayNumber));
    }
    
    return success;
}

bool RelayCommandBox::toggleRelay(int relayNumber) {
    if (!isValidRelayNumber(relayNumber)) {
        return false;
    }
    
    bool currentState = getRelayState(relayNumber);
    return setRelay(relayNumber, !currentState);
}

bool RelayCommandBox::processCommand(int relayNumber, String action, int duration) {
    if (!isValidRelayNumber(relayNumber)) {
        Serial.println("❌ Comando inválido - Relé: " + String(relayNumber));
        return false;
    }
    
    action.toLowerCase();
    action.trim();
    
    // Chamar callback de comando se definido
    if (commandCallback) {
        commandCallback(relayNumber, action, duration);
    }
    
    if (action == "on") {
        if (duration > 0) {
            return setRelayWithTimer(relayNumber, true, duration);
        } else {
            // ON permanente - sem timer
            return setRelay(relayNumber, true);
        }
    }
    else if (action == "on_forever" || action == "on_permanent") {
        // ON permanente explícito - cancelar qualquer timer
        Serial.println("🔌 Ligando relé " + String(relayNumber) + " permanentemente");
        return setRelay(relayNumber, true);
    } 
    else if (action == "off") {
        return setRelay(relayNumber, false);
    } 
    else if (action == "toggle") {
        return toggleRelay(relayNumber);
    }
    // ===== COMANDOS ESPECÍFICOS PARA HIDROPONIA =====
    else if (action == "pump_cycle") {
        // Ciclo de bomba: 5min ligada, 10min desligada
        Serial.println("🌊 Iniciando ciclo de bomba (5min ON)");
        return setRelayWithTimer(relayNumber, true, 300); // 5 minutos
    }
    else if (action == "light_cycle") {
        // Ciclo de luz: 16h ligada, 8h desligada
        Serial.println("💡 Iniciando ciclo de luz (16h ON)");
        return setRelayWithTimer(relayNumber, true, 57600); // 16 horas
    }
    else if (action == "nutrient_cycle") {
        // Ciclo de nutrientes: 2min ligada, 58min desligada
        Serial.println("🧪 Iniciando ciclo de nutrientes (2min ON)");
        return setRelayWithTimer(relayNumber, true, 120); // 2 minutos
    }
    else if (action == "ventilation_cycle") {
        // Ciclo de ventilação: 15min ligada, 15min desligada
        Serial.println("🌬️ Iniciando ciclo de ventilação (15min ON)");
        return setRelayWithTimer(relayNumber, true, 900); // 15 minutos
    }
    else if (action == "emergency_off") {
        // Desligar tudo em emergência
        Serial.println("🚨 EMERGÊNCIA: Desligando relé " + String(relayNumber));
        return setRelay(relayNumber, false);
    }
    else if (action == "status") {
        // Comando de status - apenas retorna informação
        String relayName = getRelayName(relayNumber);
        bool state = getRelayState(relayNumber);
        int remaining = getRemainingTime(relayNumber);
        
        Serial.println("📊 " + relayName + ": " + (state ? "ON" : "OFF") + 
                      (remaining > 0 ? " (" + String(remaining) + "s restantes)" : ""));
        return true;
    }
    else {
        Serial.println("❌ Ação inválida: '" + action + "'");
        Serial.println("💡 Comandos disponíveis: on, off, toggle, on_forever, pump_cycle, light_cycle, nutrient_cycle, ventilation_cycle, emergency_off, status");
        return false;
    }
}

void RelayCommandBox::turnOffAllRelays() {
    Serial.println("🔄 Desligando todos os relés...");
    
    for (int i = 0; i < MAX_RELAYS; i++) {
        setRelay(i, false);
        delay(50); // Pequeno delay entre comandos
    }
    
    Serial.println("✅ Todos os relés desligados");
}

bool RelayCommandBox::getRelayState(int relayNumber) {
    if (!isValidRelayNumber(relayNumber)) {
        return false;
    }
    return relayStates[relayNumber].isOn;
}

int RelayCommandBox::getRemainingTime(int relayNumber) {
    if (!isValidRelayNumber(relayNumber) || !relayStates[relayNumber].hasTimer) {
        return 0;
    }
    
    unsigned long elapsed = (millis() - relayStates[relayNumber].startTime) / 1000;
    int remaining = relayStates[relayNumber].timerSeconds - elapsed;
    
    return remaining > 0 ? remaining : 0;
}

String RelayCommandBox::getRelayName(int relayNumber) {
    if (!isValidRelayNumber(relayNumber)) {
        return "Relé Inválido";
    }
    
    if (relayStates[relayNumber].name.isEmpty()) {
        return "Relé " + String(relayNumber);
    }
    
    return relayStates[relayNumber].name;
}

void RelayCommandBox::setRelayName(int relayNumber, const String& name) {
    if (isValidRelayNumber(relayNumber)) {
        relayStates[relayNumber].name = name;
        Serial.println("📝 Relé " + String(relayNumber) + " renomeado para: " + name);
    }
}

void RelayCommandBox::printStatus() {
    Serial.println("🔌 === STATUS " + deviceName + " ===");
    Serial.println("📍 PCF8574: 0x" + String(i2cAddress, HEX) + " (" + 
                  (pcfInitialized ? "Online" : "Offline") + ")");
    
    for (int i = 0; i < MAX_RELAYS; i++) {
        String status = "   " + getRelayName(i) + ": " + 
                       (relayStates[i].isOn ? "ON" : "OFF");
        
        if (relayStates[i].hasTimer) {
            int remaining = getRemainingTime(i);
            status += " (Timer: " + String(remaining) + "s)";
        }
        
        Serial.println(status);
    }
    Serial.println("===============================");
}

String RelayCommandBox::getStatusJSON() {
    DynamicJsonDocument doc(1024);
    
    doc["device"] = deviceName;
    doc["pcf8574_address"] = "0x" + String(i2cAddress, HEX);
    doc["operational"] = pcfInitialized;
    doc["timestamp"] = millis();
    
    JsonArray relays = doc.createNestedArray("relays");
    
    for (int i = 0; i < MAX_RELAYS; i++) {
        JsonObject relay = relays.createNestedObject();
        relay["number"] = i;
        relay["name"] = getRelayName(i);
        relay["state"] = relayStates[i].isOn;
        relay["hasTimer"] = relayStates[i].hasTimer;
        
        if (relayStates[i].hasTimer) {
            relay["remainingTime"] = getRemainingTime(i);
            relay["totalTime"] = relayStates[i].timerSeconds;
        }
    }
    
    String result;
    serializeJson(doc, result);
    return result;
}

String RelayCommandBox::getDeviceInfoJSON() {
    DynamicJsonDocument doc(512);
    
    doc["deviceName"] = deviceName;
    doc["deviceType"] = "RelayCommandBox";
    doc["numRelays"] = MAX_RELAYS;
    doc["pcf8574Address"] = "0x" + String(i2cAddress, HEX);
    doc["operational"] = pcfInitialized;
    doc["uptime"] = millis();
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["macAddress"] = WiFi.macAddress();
    
    String result;
    serializeJson(doc, result);
    return result;
}

void RelayCommandBox::setStateChangeCallback(void (*callback)(int relayNumber, bool state, int remainingTime)) {
    this->stateChangeCallback = callback;
}

void RelayCommandBox::setCommandCallback(void (*callback)(int relayNumber, String action, int duration)) {
    this->commandCallback = callback;
}

// ===== MÉTODOS PRIVADOS =====

bool RelayCommandBox::writeToRelay(int relayNumber, bool state) {
    if (!isValidRelayNumber(relayNumber)) {
        return false;
    }
    
    // Se PCF8574 não está inicializado, funcionar em modo simulação
    if (!pcfInitialized) {
        Serial.println("🎮 [SIMULAÇÃO] Relé " + String(relayNumber) + " -> " + (state ? "LIGADO" : "DESLIGADO"));
        return true; // Simular sucesso
    }
    
    try {
        // PCF8574 usa lógica: LOW = relé ligado, HIGH = relé desligado
        // Mesma lógica do teste do usuário - usando ponteiro
        pcf8574->write(relayNumber, state ? LOW : HIGH);
        
        // Pequeno delay para estabilizar
        delay(10);
        
        return true;
    } catch (...) {
        Serial.println("❌ Exceção ao escrever no relé " + String(relayNumber));
        return false;
    }
}

void RelayCommandBox::checkTimers() {
    for (int i = 0; i < MAX_RELAYS; i++) {
        if (relayStates[i].hasTimer && relayStates[i].isOn) {
            unsigned long elapsed = (millis() - relayStates[i].startTime) / 1000;
            
            if (elapsed >= relayStates[i].timerSeconds) {
                // Timer expirou - desligar relé
                String relayName = getRelayName(i);
                Serial.println("⏰ Timer do " + relayName + " expirou - desligando");
                
                relayStates[i].isOn = false;
                relayStates[i].hasTimer = false;
                relayStates[i].timerSeconds = 0;
                
                writeToRelay(i, false);
                
                // 🎯 Guardar estado en NVS cuando expira timer
                savePersistentStates();
                
                // Chamar callback se definido
                if (stateChangeCallback) {
                    stateChangeCallback(i, false, 0);
                }
            }
        }
    }
}

bool RelayCommandBox::isValidRelayNumber(int relayNumber) {
    return relayNumber >= 0 && relayNumber < MAX_RELAYS;
}

void RelayCommandBox::initializeDefaultNames() {
    // Usar nomes do Config.h
    for (int i = 0; i < MAX_RELAYS; i++) {
        relayStates[i].name = String(RELAY_NAMES[i]);
    }
    
    DEBUG_PRINTLN("✅ Nomes padrão dos relés carregados do Config.h");
}

bool RelayCommandBox::scanAndInitializePCF8574() {
    DEBUG_PRINTLN("🔍 === ESCANEANDO ENDEREÇOS I2C ===");
    DEBUG_PRINTLN("Procurando PCF8574...");
    
    // 🎯 CORREÇÃO: Timeout para evitar loop infinito
    unsigned long startTime = millis();
    const unsigned long TIMEOUT_MS = 5000; // 5 segundos máximo
    
    // Endereços comuns do PCF8574 (0x20 a 0x27)
    for (uint8_t address = 0x20; address <= 0x27; address++) {
        // Verificar timeout
        if (millis() - startTime > TIMEOUT_MS) {
            DEBUG_PRINTLN("⏱️ Timeout no escaneamento I2C");
            return false;
        }
        
        Wire.beginTransmission(address);
        uint8_t error = Wire.endTransmission();
        delay(10); // Pequeno delay entre tentativas
        
        if (error == 0) {
            DEBUG_PRINTLN("✓ Dispositivo encontrado no endereço 0x" + String(address, HEX));
            
            // 🎯 CORREÇÃO: Passar false para begin() para NÃO reinicializar I2C
            PCF8574* test_pcf = new PCF8574(address);
            if (test_pcf->begin(false)) {  // false = não reinicializar I2C
                // Limpar anterior se existir
                if (pcf8574 != nullptr) {
                    delete pcf8574;
                }
                
                pcf8574 = test_pcf;
                i2cAddress = address;
                DEBUG_PRINTLN("✓ PCF8574 confirmado no endereço 0x" + String(address, HEX));
                
                // 🎯 CRÍTICO: Desligar TODOS os relés primeiro (antes de aplicar estados persistentes)
                DEBUG_PRINTLN("🔄 Desligando todos os relés inicialmente...");
                for (int i = 0; i < MAX_RELAYS; i++) {
                    pcf8574->write(i, HIGH);  // HIGH = desligado
                    delay(10); // Pequeno delay entre comandos
                }
                
                DEBUG_PRINTLN("✅ PCF8574 inicializado com sucesso!");
                DEBUG_PRINTLN("✅ Todos os relés desligados (estados persistentes serão aplicados depois)");
                return true;
            } else {
                delete test_pcf;
                DEBUG_PRINTLN("  (Não é um PCF8574 ou falha na inicialização)");
            }
        }
    }
    
    DEBUG_PRINTLN("✗ Nenhum PCF8574 encontrado nos endereços 0x20-0x27");
    return false;
}

bool RelayCommandBox::testI2CCommunication() {
    // Teste múltiplo para garantir que o dispositivo está realmente presente
    int successCount = 0;
    int testAttempts = 3;
    
    for (int i = 0; i < testAttempts; i++) {
        Wire.beginTransmission(i2cAddress);
        byte error = Wire.endTransmission();
        
        if (error == 0) {
            successCount++;
        }
        
        delay(10); // Pequeno delay entre testes
    }
    
    DEBUG_PRINTLN("🔍 Teste I2C - Sucessos: " + String(successCount) + "/" + String(testAttempts));
    return successCount >= (testAttempts / 2); // Pelo menos metade dos testes deve passar
}

// ===== 🎯 PERSISTÊNCIA DE ESTADOS (NVS) =====

bool RelayCommandBox::savePersistentStates() {
    PersistentRelayStateData states = {};
    states.timestamp = millis();
    states.numRelays = MAX_RELAYS;
    states.version = 1;  // Versão inicial
    
    // Preencher estados de cada relé
    for (int i = 0; i < MAX_RELAYS; i++) {
        states.relays[i].state = relayStates[i].isOn ? 1 : 0;
        states.relays[i].hasTimer = relayStates[i].hasTimer ? 1 : 0;
        
        if (relayStates[i].hasTimer && relayStates[i].isOn) {
            // Calcular timestamp de expiração do timer
            states.relays[i].timerEndTime = relayStates[i].startTime + (relayStates[i].timerSeconds * 1000);
        } else {
            states.relays[i].timerEndTime = 0;
        }
        
        // Estado persistente = ON sem timer (on_forever)
        states.relays[i].isPersistent = (relayStates[i].isOn && !relayStates[i].hasTimer) ? 1 : 0;
    }
    
    // Calcular checksum
    uint8_t checksum = 0;
    uint8_t* data = (uint8_t*)&states;
    for (size_t i = 0; i < sizeof(PersistentRelayStateData) - 1; i++) {
        checksum ^= data[i];
    }
    states.checksum = checksum;
    
    // Guardar em NVS
    nvs_handle_t handle;
    esp_err_t err = nvs_open("relay_states", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        Serial.println("❌ Erro ao abrir NVS para guardar estados: " + String(esp_err_to_name(err)));
        return false;
    }
    
    // Clave NVS máximo 15 caracteres - usar "relay_states" (12 chars)
    err = nvs_set_blob(handle, "relay_states", &states, sizeof(PersistentRelayStateData));
    if (err != ESP_OK) {
        Serial.println("❌ Erro ao guardar estados em NVS: " + String(esp_err_to_name(err)));
        nvs_close(handle);
        return false;
    }
    
    err = nvs_commit(handle);
    nvs_close(handle);
    
    if (err == ESP_OK) {
        Serial.println("💾 Estados persistentes guardados em NVS");
        return true;
    } else {
        Serial.println("❌ Erro ao fazer commit em NVS: " + String(esp_err_to_name(err)));
        return false;
    }
}

bool RelayCommandBox::loadPersistentStates() {
    PersistentRelayStateData states = {};
    
    // Carregar de NVS
    nvs_handle_t handle;
    esp_err_t err = nvs_open("relay_states", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        Serial.println("💡 Nenhum estado persistente encontrado em NVS");
        return false;
    }
    
    size_t required_size = sizeof(PersistentRelayStateData);
    // Clave NVS máximo 15 caracteres - usar "relay_states" (12 chars)
    err = nvs_get_blob(handle, "relay_states", &states, &required_size);
    nvs_close(handle);
    
    if (err != ESP_OK) {
        Serial.println("💡 Nenhum estado persistente encontrado em NVS");
        return false;
    }
    
    // Validar checksum
    uint8_t calculatedChecksum = 0;
    uint8_t* data = (uint8_t*)&states;
    for (size_t i = 0; i < sizeof(PersistentRelayStateData) - 1; i++) {
        calculatedChecksum ^= data[i];
    }
    
    if (calculatedChecksum != states.checksum) {
        Serial.println("❌ Checksum inválido ao carregar estados persistentes");
        return false;
    }
    
    // Aplicar estados carregados
    return applyPersistentStates(states);
}

bool RelayCommandBox::applyPersistentStates(const PersistentRelayStateData& states) {
    Serial.println("\n🎯 ========================================");
    Serial.println("🎯 APLICANDO ESTADOS PERSISTENTES");
    Serial.println("🎯 ========================================");
    Serial.println("📦 Total de relés: " + String(states.numRelays));
    Serial.println("📅 Timestamp: " + String(states.timestamp));
    
    bool applied = false;
    
    for (int i = 0; i < MAX_RELAYS && i < states.numRelays; i++) {
        if (states.relays[i].isPersistent) {
            // 🔒 ESTADO PERSISTENTE (on_forever) - aplicar imediatamente
            Serial.println("🔒 Relé " + String(i) + ": Aplicando estado PERSISTENTE " + 
                         (states.relays[i].state ? "ON" : "OFF"));
            setRelay(i, states.relays[i].state == 1);
            applied = true;
        } 
        else if (states.relays[i].hasTimer && states.relays[i].state == 1) {
            // ⏰ TIMER ATIVO - verificar se ainda válido
            uint32_t currentTime = millis();
            if (states.relays[i].timerEndTime > currentTime) {
                uint32_t remaining = (states.relays[i].timerEndTime - currentTime) / 1000;
                Serial.println("⏰ Relé " + String(i) + ": Aplicando timer com " + String(remaining) + "s restantes");
                setRelayWithTimer(i, true, remaining);
                applied = true;
            } else {
                Serial.println("⏰ Relé " + String(i) + ": Timer expirado - desligando");
                setRelay(i, false);
            }
        }
        else if (!states.relays[i].state) {
            // OFF - garantir que está desligado
            setRelay(i, false);
        }
    }
    
    Serial.println("========================================\n");
    
    if (applied) {
        Serial.println("✅ Estados persistentes aplicados com sucesso");
    } else {
        Serial.println("💡 Nenhum estado persistente para aplicar");
    }
    
    return applied;
}

bool RelayCommandBox::getPersistentStates(PersistentRelayStateData& states) {
    states = {};
    states.timestamp = millis();
    states.numRelays = MAX_RELAYS;
    states.version = 1;
    
    // Preencher estados atuais
    for (int i = 0; i < MAX_RELAYS; i++) {
        states.relays[i].state = relayStates[i].isOn ? 1 : 0;
        states.relays[i].hasTimer = relayStates[i].hasTimer ? 1 : 0;
        
        if (relayStates[i].hasTimer && relayStates[i].isOn) {
            states.relays[i].timerEndTime = relayStates[i].startTime + (relayStates[i].timerSeconds * 1000);
        } else {
            states.relays[i].timerEndTime = 0;
        }
        
        // Estado persistente = ON sem timer (on_forever)
        states.relays[i].isPersistent = (relayStates[i].isOn && !relayStates[i].hasTimer) ? 1 : 0;
    }
    
    // Calcular checksum
    uint8_t checksum = 0;
    uint8_t* data = (uint8_t*)&states;
    for (size_t i = 0; i < sizeof(PersistentRelayStateData) - 1; i++) {
        checksum ^= data[i];
    }
    states.checksum = checksum;
    
    return true;
}
