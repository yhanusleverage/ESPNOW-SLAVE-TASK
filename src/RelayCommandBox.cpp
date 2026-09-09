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
        relayStates[i].inCycle = false;
        relayStates[i].cyclePhaseOn = true;
        relayStates[i].cycleOnSec = 0;
        relayStates[i].cycleOffSec = 0;
        relayStates[i].name = "";
        relayStates[i].config = RELAY_CONFIGS[i];
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
        Serial.println("⚠️ PCF8574 não encontrado — hardware OFFLINE");
        Serial.println("💡 Comandos ON/OFF falharão (ACK fail). Verifique I2C SDA=21 SCL=22, 0x20-0x27");
        Serial.println("🚫 Modo simulação NÃO finge sucesso no hardware");
        
        // Inicializar estados em modo simulação
        for (int i = 0; i < MAX_RELAYS; i++) {
            relayStates[i].isOn = false;
            relayStates[i].startTime = 0;
            relayStates[i].timerSeconds = 0;
            relayStates[i].hasTimer = false;
        }
        
        Serial.println("⚠️ RelayCommandBox ONLINE sem PCF — firmware segue, relés NÃO atuam");
        Serial.println("🎯 Relés lógicos: 0-" + String(MAX_RELAYS - 1) + " (PCF offline)");
        return true;
    }
    
#if RELAY_SAFE_BOOT_ALWAYS_OFF
    turnOffAllRelays();
    Serial.println("🛡️ Boot seguro: relés OFF — NVS NÃO restaura ON");
#else
    Serial.println("💾 Boot: restaurando ON/OFF desde NVS...");
    bool statesLoaded = loadPersistentStates();
    if (statesLoaded) {
        Serial.println("✅ Relés restaurados (mesmo estado que antes do reboot)");
    } else {
        Serial.println("💡 Sem NVS de relés — todos OFF");
        turnOffAllRelays();
    }
#endif
    
    Serial.println("✅ RelayCommandBox inicializado: " + deviceName);
    Serial.println("🎯 Relés disponíveis: 0-" + String(MAX_RELAYS - 1));
    
    return true;
}

void RelayCommandBox::update() {
    // Só timers/ciclos do comando — sem maxDuration fantasma em ON permanente
    checkTimers();
}

uint32_t RelayCommandBox::getMaxDuration(int relayNumber) const {
    (void)relayNumber;
    return 0;  // sem limite inventado
}

void RelayCommandBox::clearSchedule(int relayNumber) {
    if (!isValidRelayNumber(relayNumber)) return;
    relayStates[relayNumber].hasTimer = false;
    relayStates[relayNumber].timerSeconds = 0;
    relayStates[relayNumber].inCycle = false;
    relayStates[relayNumber].cyclePhaseOn = true;
    relayStates[relayNumber].cycleOnSec = 0;
    relayStates[relayNumber].cycleOffSec = 0;
}

bool RelayCommandBox::commitRelayHardware(int relayNumber, bool on) {
    if (!isValidRelayNumber(relayNumber)) return false;
    if (!pcfInitialized || pcf8574 == nullptr) {
        Serial.println("❌ PCF8574 não inicializado");
        return false;
    }
    if (!writeToRelay(relayNumber, on)) {
        return false;
    }
    relayStates[relayNumber].isOn = on;
    relayStates[relayNumber].startTime = millis();
    return true;
}

bool RelayCommandBox::enforceMaxDurationOnRelay(int relayNumber) {
    (void)relayNumber;
    return false;  // duration==0 é permanente; não inventar OFF
}

bool RelayCommandBox::setRelay(int relayNumber, bool state) {
    if (!isValidRelayNumber(relayNumber)) {
        Serial.println("❌ Número de relé inválido: " + String(relayNumber));
        return false;
    }

    if (state && safetyModeBlocked) {
        Serial.println("🚨 SafetyMode ATIVO — relé " + String(relayNumber) + " bloqueado");
        return false;
    }
    
    if (!pcfInitialized) {
        Serial.println("❌ PCF8574 não inicializado");
        return false;
    }

    clearSchedule(relayNumber);
    
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

    if (state && safetyModeBlocked) {
        Serial.println("🚨 SafetyMode ATIVO — relé " + String(relayNumber) + " bloqueado");
        return false;
    }
    
    if (!pcfInitialized) {
        Serial.println("❌ PCF8574 não inicializado");
        return false;
    }
    
    if (seconds <= 0) {
        return setRelay(relayNumber, state);
    }

    clearSchedule(relayNumber);

    // Timer = valor exacto del comando (sin techo 3600/86400)
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

bool RelayCommandBox::startCycle(int relayNumber, uint32_t onSec, uint32_t offSec) {
    if (!isValidRelayNumber(relayNumber)) {
        Serial.println("❌ Número de relé inválido: " + String(relayNumber));
        return false;
    }
    if (safetyModeBlocked) {
        Serial.println("🚨 SafetyMode ATIVO — cycle bloqueado");
        return false;
    }
    if (onSec < 1 || offSec < 1) {
        Serial.println("❌ Cycle requer onSec e offSec >= 1");
        return false;
    }

    clearSchedule(relayNumber);
    relayStates[relayNumber].inCycle = true;
    relayStates[relayNumber].cyclePhaseOn = true;
    relayStates[relayNumber].cycleOnSec = onSec;
    relayStates[relayNumber].cycleOffSec = offSec;
    relayStates[relayNumber].hasTimer = true;
    relayStates[relayNumber].timerSeconds = (int)onSec;

    if (!commitRelayHardware(relayNumber, true)) {
        clearSchedule(relayNumber);
        return false;
    }

    Serial.println("🔁 Relé " + String(relayNumber) + " CYCLE ON=" + String(onSec) +
                   "s OFF=" + String(offSec) + "s");
    savePersistentStates();
    if (stateChangeCallback) {
        stateChangeCallback(relayNumber, true, (int)onSec);
    }
    return true;
}

bool RelayCommandBox::stopCycle(int relayNumber) {
    if (!isValidRelayNumber(relayNumber)) {
        return false;
    }
    return setRelay(relayNumber, false);
}

bool RelayCommandBox::toggleRelay(int relayNumber) {
    if (!isValidRelayNumber(relayNumber)) {
        return false;
    }
    
    bool currentState = getRelayState(relayNumber);
    return setRelay(relayNumber, !currentState);
}

bool RelayCommandBox::processCommand(int relayNumber, String action, int duration, int extra) {
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
    
    if (action == "on" || action == "timed_on") {
        if (duration > 0) {
            return setRelayWithTimer(relayNumber, true, duration);
        } else {
            return setRelay(relayNumber, true);
        }
    }
    else if (action == "on_forever" || action == "on_permanent") {
        // ON permanente explícito - cancelar qualquer timer
        Serial.println("🔌 Ligando relé " + String(relayNumber) + " permanentemente");
        return setRelay(relayNumber, true);
    } 
    else if (action == "off" || action == "cycle_stop") {
        return setRelay(relayNumber, false);
    } 
    else if (action == "toggle") {
        return toggleRelay(relayNumber);
    }
    // ===== COMANDOS ESPECÍFICOS PARA HIDROPONIA =====
    else if (action == "cycle") {
        uint32_t offSec = extra > 0 ? (uint32_t)extra : (uint32_t)duration;
        if (duration < 1) duration = 1;
        if (offSec < 1) offSec = duration;
        return startCycle(relayNumber, (uint32_t)duration, offSec);
    }
    else if (action == "pump_cycle") {
        Serial.println("🌊 Ciclo de bomba (5min ON / 10min OFF)");
        return startCycle(relayNumber, 300, 600);
    }
    else if (action == "light_cycle") {
        Serial.println("💡 Ciclo de luz (16h ON / 8h OFF)");
        return startCycle(relayNumber, 57600, 28800);
    }
    else if (action == "nutrient_cycle") {
        Serial.println("🧪 Ciclo de nutrientes (2min ON / 58min OFF)");
        return startCycle(relayNumber, 120, 3480);
    }
    else if (action == "ventilation_cycle") {
        Serial.println("🌬️ Ciclo de ventilação (15min ON / 15min OFF)");
        return startCycle(relayNumber, 900, 900);
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
        Serial.println("💡 Comandos: on, off, toggle, timed_on, cycle, cycle_stop, on_forever, ...");
        return false;
    }
}

uint8_t RelayCommandBox::getRelayMask() const {
    uint8_t mask = 0;
    for (int i = 0; i < MAX_RELAYS; i++) {
        if (relayStates[i].isOn) {
            mask |= (uint8_t)(1u << i);
        }
    }
    return mask;
}

bool RelayCommandBox::applyRelayMask(uint8_t mask, uint16_t durationSec) {
    Serial.printf("[PROC] SET_RELAY_MASK 0x%02X dur=%u\n", mask, (unsigned)durationSec);

    if (safetyModeBlocked && mask != 0) {
        Serial.println("🚨 SafetyMode — máscara ON rejeitada, relés OFF");
        turnOffAllRelays();
        return false;
    }

    if (!pcfInitialized || pcf8574 == nullptr) {
        Serial.println("❌ PCF8574 offline — SET_RELAY_MASK fail");
        return false;
    }

    pcf8574->write8((uint8_t)(~mask));

    for (int i = 0; i < MAX_RELAYS; i++) {
        const bool on = (mask & (1u << i)) != 0;
        relayStates[i].inCycle = false;
        relayStates[i].cycleOnSec = 0;
        relayStates[i].cycleOffSec = 0;
        relayStates[i].cyclePhaseOn = true;
        relayStates[i].isOn = on;
        relayStates[i].startTime = millis();
        if (on && durationSec > 0) {
            relayStates[i].hasTimer = true;
            relayStates[i].timerSeconds = (int)durationSec;
        } else {
            relayStates[i].hasTimer = false;
            relayStates[i].timerSeconds = 0;
        }
    }

    savePersistentStates();
    Serial.printf("[PROC] PCF write8(~0x%02X)=0x%02X actual=0x%02X\n",
                  mask, (unsigned)((uint8_t)~mask), getRelayMask());
    return true;
}

void RelayCommandBox::turnOffAllRelays() {
    Serial.println("🔄 Desligando todos os relés...");

    if (pcfInitialized && pcf8574 != nullptr) {
        pcf8574->write8(0xFF);  // OFF atômico (ativo em LOW)
    }

    for (int i = 0; i < MAX_RELAYS; i++) {
        relayStates[i].isOn = false;
        relayStates[i].hasTimer = false;
        relayStates[i].timerSeconds = 0;
        relayStates[i].startTime = 0;
        relayStates[i].inCycle = false;
        relayStates[i].cyclePhaseOn = true;
        relayStates[i].cycleOnSec = 0;
        relayStates[i].cycleOffSec = 0;
    }

    savePersistentStates();
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
        
        if (relayStates[i].inCycle) {
            status += " (cycle " + String(relayStates[i].cyclePhaseOn ? "ON" : "OFF") +
                      " " + String(getRemainingTime(i)) + "s)";
        } else if (relayStates[i].hasTimer) {
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
        relay["inCycle"] = relayStates[i].inCycle;
        
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
    
    if (!pcfInitialized || pcf8574 == nullptr) {
        Serial.println("❌ writeToRelay: PCF8574 offline — sem ACK de sucesso");
        return false;
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
        if (relayStates[i].inCycle) {
            uint32_t phaseSec = relayStates[i].cyclePhaseOn
                ? relayStates[i].cycleOnSec
                : relayStates[i].cycleOffSec;
            if (phaseSec < 1) continue;
            unsigned long elapsed = (millis() - relayStates[i].startTime) / 1000UL;
            if (elapsed >= phaseSec) {
                relayStates[i].cyclePhaseOn = !relayStates[i].cyclePhaseOn;
                bool nextOn = relayStates[i].cyclePhaseOn;
                uint32_t nextSec = nextOn ? relayStates[i].cycleOnSec : relayStates[i].cycleOffSec;
                relayStates[i].hasTimer = true;
                relayStates[i].timerSeconds = (int)nextSec;
                if (commitRelayHardware(i, nextOn)) {
                    Serial.println("🔁 Relé " + String(i) + " cycle → " +
                               String(nextOn ? "ON" : "OFF") + " " + String(nextSec) + "s");
                    savePersistentStates();
                    if (stateChangeCallback) {
                        stateChangeCallback(i, nextOn, (int)nextSec);
                    }
                }
            }
            continue;
        }

        if (relayStates[i].hasTimer && relayStates[i].isOn) {
            unsigned long elapsed = (millis() - relayStates[i].startTime) / 1000;
            
            if (elapsed >= (unsigned long)relayStates[i].timerSeconds) {
                String relayName = getRelayName(i);
                Serial.println("⏰ Timer do " + relayName + " expirou - desligando");
                
                relayStates[i].isOn = false;
                relayStates[i].hasTimer = false;
                relayStates[i].timerSeconds = 0;
                
                writeToRelay(i, false);
                savePersistentStates();
                
                if (stateChangeCallback) {
                    stateChangeCallback(i, false, 0);
                }
            }
        }
    }
}

bool RelayCommandBox::isValidRelayNumber(int relayNumber) const {
    return relayNumber >= 0 && relayNumber < MAX_RELAYS;
}

void RelayCommandBox::initializeDefaultNames() {
    for (int i = 0; i < MAX_RELAYS; i++) {
        relayStates[i].name = String(RELAY_NAMES[i]);
        relayStates[i].config = RELAY_CONFIGS[i];
    }
    
    DEBUG_PRINTLN("✅ Relés identificados por índice (Relé 0–7)");
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
                
                // OFF atômico: 0xFF = todos HIGH (relé ativo em LOW = desligado)
                // Evita 8 writes com delay que poderiam deixar pins flutuantes/glitch
                DEBUG_PRINTLN("🔄 Forçando porta PCF8574 = 0xFF (todos OFF)...");
                pcf8574->write8(0xFF);
                for (int i = 0; i < MAX_RELAYS; i++) {
                    relayStates[i].isOn = false;
                    relayStates[i].hasTimer = false;
                    relayStates[i].timerSeconds = 0;
                    relayStates[i].startTime = 0;
                }
                
                DEBUG_PRINTLN("✅ PCF8574 inicializado — todos os relés OFF");
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
    states.version = 2;
    
    for (int i = 0; i < MAX_RELAYS; i++) {
        states.relays[i].state = relayStates[i].isOn ? 1 : 0;
        states.relays[i].hasTimer = relayStates[i].hasTimer ? 1 : 0;
        states.relays[i].inCycle = relayStates[i].inCycle ? 1 : 0;
        states.relays[i].cyclePhaseOn = relayStates[i].cyclePhaseOn ? 1 : 0;
        states.relays[i].cycleOnSec = (uint16_t)constrain(relayStates[i].cycleOnSec, 0, 65535);
        states.relays[i].cycleOffSec = (uint16_t)constrain(relayStates[i].cycleOffSec, 0, 65535);
        
        if (relayStates[i].hasTimer || relayStates[i].inCycle) {
            unsigned long elapsed = (millis() - relayStates[i].startTime) / 1000UL;
            uint32_t remaining = 0;
            if ((unsigned long)relayStates[i].timerSeconds > elapsed) {
                remaining = relayStates[i].timerSeconds - elapsed;
            }
            states.relays[i].timerEndTime = remaining;
        } else {
            states.relays[i].timerEndTime = 0;
        }
        
        states.relays[i].isPersistent = (relayStates[i].isOn && !relayStates[i].hasTimer &&
                                         !relayStates[i].inCycle) ? 1 : 0;
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
    Serial.println("📅 Timestamp: " + String(states.timestamp) + " v" + String(states.version));
    
    bool applied = false;
    
    for (int i = 0; i < MAX_RELAYS && i < states.numRelays; i++) {
        if (states.relays[i].inCycle && states.relays[i].cycleOnSec > 0 &&
            states.relays[i].cycleOffSec > 0) {
            uint32_t onSec = states.relays[i].cycleOnSec;
            uint32_t offSec = states.relays[i].cycleOffSec;
            bool phaseOn = states.relays[i].cyclePhaseOn != 0;
            uint32_t phaseSec = phaseOn ? onSec : offSec;
            uint32_t remaining = states.relays[i].timerEndTime;
            if (remaining == 0 || remaining > phaseSec) remaining = phaseSec;

            clearSchedule(i);
            relayStates[i].inCycle = true;
            relayStates[i].cyclePhaseOn = phaseOn;
            relayStates[i].cycleOnSec = onSec;
            relayStates[i].cycleOffSec = offSec;
            relayStates[i].hasTimer = true;
            relayStates[i].timerSeconds = (int)phaseSec;
            if (commitRelayHardware(i, phaseOn)) {
                unsigned long already = (phaseSec > remaining) ? (phaseSec - remaining) : 0;
                relayStates[i].startTime = millis() - (already * 1000UL);
                Serial.println("🔁 Relé " + String(i) + ": cycle restaurado " +
                               String(phaseOn ? "ON" : "OFF") + " " + String(remaining) + "s");
                applied = true;
            }
        } else if (states.relays[i].hasTimer) {
            uint32_t remaining = states.relays[i].timerEndTime;
            if (remaining > 0 && states.relays[i].state == 1) {
                Serial.println("⏰ Relé " + String(i) + ": timer " + String(remaining) + "s restantes");
                setRelayWithTimer(i, true, remaining);
                applied = true;
            } else {
                commitRelayHardware(i, false);
            }
        } else if (states.relays[i].isPersistent || states.relays[i].state == 1) {
            Serial.println("🔌 Relé " + String(i) + ": restaurando ON");
            clearSchedule(i);
            if (commitRelayHardware(i, true)) {
                applied = true;
            }
        } else {
            clearSchedule(i);
            commitRelayHardware(i, false);
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
    states.version = 2;
    
    for (int i = 0; i < MAX_RELAYS; i++) {
        states.relays[i].state = relayStates[i].isOn ? 1 : 0;
        states.relays[i].hasTimer = relayStates[i].hasTimer ? 1 : 0;
        states.relays[i].inCycle = relayStates[i].inCycle ? 1 : 0;
        states.relays[i].cyclePhaseOn = relayStates[i].cyclePhaseOn ? 1 : 0;
        states.relays[i].cycleOnSec = (uint16_t)relayStates[i].cycleOnSec;
        states.relays[i].cycleOffSec = (uint16_t)relayStates[i].cycleOffSec;
        states.relays[i].timerEndTime = (uint32_t)getRemainingTime(i);
        states.relays[i].isPersistent = (relayStates[i].isOn && !relayStates[i].hasTimer &&
                                         !relayStates[i].inCycle) ? 1 : 0;
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
