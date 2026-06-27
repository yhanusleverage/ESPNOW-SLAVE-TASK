#include "RelayController.h"

RelayController::RelayController(uint8_t pcf1Address, uint8_t pcf2Address)
    : _pcf1Address(pcf1Address), _pcf2Address(pcf2Address), _initialized(false),
      _pcf1Connected(false), _pcf2Connected(false), _stateChangeCallback(nullptr) {
    
    // Inicializar estados dos relés
    for (int i = 0; i < 16; i++) {
        _relayStates[i].state = false;
        _relayStates[i].startTime = 0;
        _relayStates[i].duration = 0;
        _relayStates[i].remainingTime = 0;
        _relayStates[i].name = "Relé " + String(i);
    }
}

bool RelayController::begin() {
    DEBUG_PRINTLN("🔌 Inicializando RelayController (16 relés)");
    
    // Inicializar I2C
    Wire.begin();
    delay(100);
    
    // Verificar conexão com PCF8574
    Wire.beginTransmission(_pcf1Address);
    _pcf1Connected = (Wire.endTransmission() == 0);
    
    Wire.beginTransmission(_pcf2Address);
    _pcf2Connected = (Wire.endTransmission() == 0);
    
    if (!_pcf1Connected && !_pcf2Connected) {
        DEBUG_PRINTLN("❌ Nenhum PCF8574 detectado");
        return false;
    }
    
    if (_pcf1Connected) {
        DEBUG_PRINTLN("✅ PCF8574 #1 detectado (0x" + String(_pcf1Address, HEX) + ")");
    }
    
    if (_pcf2Connected) {
        DEBUG_PRINTLN("✅ PCF8574 #2 detectado (0x" + String(_pcf2Address, HEX) + ")");
    }
    
    // Inicializar nomes padrão
    initializeDefaultNames();
    
    _initialized = true;
    DEBUG_PRINTLN("✅ RelayController inicializado com sucesso");
    return true;
}

void RelayController::update() {
    if (!_initialized) return;
    
    unsigned long currentTime = millis();
    
    // Atualizar estados de todos os relés
    for (int i = 0; i < 16; i++) {
        if (_relayStates[i].state && _relayStates[i].duration > 0) {
            // Calcular tempo restante
            unsigned long elapsed = (currentTime - _relayStates[i].startTime) / 1000;
            _relayStates[i].remainingTime = _relayStates[i].duration - elapsed;
            
            // Verificar se timer expirou
            if (_relayStates[i].remainingTime <= 0) {
                setRelay(i, false);
                _relayStates[i].remainingTime = 0;
            }
        }
    }
}

bool RelayController::processCommand(int relayNumber, const String& action, int duration) {
    if (!_initialized || !isValidRelay(relayNumber)) {
        DEBUG_PRINTLN("❌ Comando inválido para relé " + String(relayNumber));
        return false;
    }
    
    DEBUG_PRINTLN("🔧 Processando comando: Relé " + String(relayNumber) + " -> " + action + 
                  (duration > 0 ? " (" + String(duration) + "s)" : ""));
    
    if (action == "on") {
        return setRelayWithTimer(relayNumber, true, duration);
    } else if (action == "off") {
        return setRelay(relayNumber, false);
    } else if (action == "toggle") {
        return toggleRelay(relayNumber);
    } else {
        DEBUG_PRINTLN("❌ Ação inválida: " + action);
        return false;
    }
}

bool RelayController::setRelay(int relayNumber, bool state) {
    if (!_initialized || !isValidRelay(relayNumber)) {
        return false;
    }
    
    uint8_t pcfAddress = getPCFAddress(relayNumber);
    uint8_t pin = getPCFPin(relayNumber);
    
    // Verificar se PCF8574 está conectado
    if ((relayNumber < 8 && !_pcf1Connected) || (relayNumber >= 8 && !_pcf2Connected)) {
        DEBUG_PRINTLN("❌ PCF8574 não conectado para relé " + String(relayNumber));
        return false;
    }
    
    // Ler estado atual do PCF8574
    Wire.beginTransmission(pcfAddress);
    Wire.requestFrom((uint8_t)pcfAddress, (uint8_t)1);
    uint8_t currentState = Wire.read();
    Wire.endTransmission();
    
    // Modificar bit específico
    if (state) {
        currentState &= ~(1 << pin);  // Ligar (bit 0)
    } else {
        currentState |= (1 << pin);   // Desligar (bit 1)
    }
    
    // Escrever novo estado
    Wire.beginTransmission(pcfAddress);
    Wire.write(currentState);
    uint8_t result = Wire.endTransmission();
    
    if (result == 0) {
        _relayStates[relayNumber].state = state;
        _relayStates[relayNumber].startTime = millis();
        _relayStates[relayNumber].duration = 0;
        _relayStates[relayNumber].remainingTime = 0;
        
        // Chamar callback se definido
        if (_stateChangeCallback) {
            _stateChangeCallback(relayNumber, state, 0);
        }
        
        DEBUG_PRINTLN("✅ Relé " + String(relayNumber) + " " + (state ? "LIGADO" : "DESLIGADO"));
        return true;
    }
    
    DEBUG_PRINTLN("❌ Falha ao controlar relé " + String(relayNumber));
    return false;
}

bool RelayController::setRelayWithTimer(int relayNumber, bool state, int duration) {
    if (!setRelay(relayNumber, state)) {
        return false;
    }
    
    if (state && duration > 0) {
        _relayStates[relayNumber].duration = duration;
        _relayStates[relayNumber].remainingTime = duration;
        _relayStates[relayNumber].startTime = millis();
        
        DEBUG_PRINTLN("⏰ Timer configurado: " + String(duration) + "s para relé " + String(relayNumber));
    }
    
    return true;
}

bool RelayController::toggleRelay(int relayNumber) {
    if (!_initialized || !isValidRelay(relayNumber)) {
        return false;
    }
    
    return setRelay(relayNumber, !_relayStates[relayNumber].state);
}

void RelayController::turnOffAllRelays() {
    DEBUG_PRINTLN("🔄 Desligando todos os relés...");
    
    for (int i = 0; i < 16; i++) {
        if (_relayStates[i].state) {
            setRelay(i, false);
        }
    }
}

bool RelayController::getRelayState(int relayNumber) {
    if (!_initialized || !isValidRelay(relayNumber)) {
        return false;
    }
    
    return _relayStates[relayNumber].state;
}

int RelayController::getRemainingTime(int relayNumber) {
    if (!_initialized || !isValidRelay(relayNumber)) {
        return 0;
    }
    
    return _relayStates[relayNumber].remainingTime;
}

String RelayController::getRelayName(int relayNumber) {
    if (!_initialized || !isValidRelay(relayNumber)) {
        return "Inválido";
    }
    
    return _relayStates[relayNumber].name;
}

void RelayController::setRelayName(int relayNumber, const String& name) {
    if (_initialized && isValidRelay(relayNumber)) {
        _relayStates[relayNumber].name = name;
    }
}

bool RelayController::isOperational() {
    return _initialized && (_pcf1Connected || _pcf2Connected);
}

void RelayController::setStateChangeCallback(void (*callback)(int relayNumber, bool state, int remainingTime)) {
    _stateChangeCallback = callback;
}

String RelayController::getStatusJSON() {
    if (!_initialized) {
        return "{\"error\":\"not_initialized\"}";
    }
    
    String json = "{\"relays\":[";
    
    for (int i = 0; i < 16; i++) {
        if (i > 0) json += ",";
        json += "{";
        json += "\"number\":" + String(i) + ",";
        json += "\"state\":" + String(_relayStates[i].state ? "true" : "false") + ",";
        json += "\"name\":\"" + _relayStates[i].name + "\",";
        json += "\"remaining\":" + String(_relayStates[i].remainingTime);
        json += "}";
    }
    
    json += "]}";
    return json;
}

void RelayController::printStatus() {
    if (!_initialized) {
        Serial.println("❌ RelayController não inicializado");
        return;
    }
    
    Serial.println("\n📊 === STATUS DOS RELÉS ===");
    Serial.println("PCF8574 #1: " + String(_pcf1Connected ? "Conectado" : "Desconectado"));
    Serial.println("PCF8574 #2: " + String(_pcf2Connected ? "Conectado" : "Desconectado"));
    Serial.println();
    
    for (int i = 0; i < 16; i++) {
        String status = _relayStates[i].state ? "ON" : "OFF";
        String timer = _relayStates[i].remainingTime > 0 ? 
                     " (Timer: " + String(_relayStates[i].remainingTime) + "s)" : "";
        
        Serial.printf("Relé %2d: %-3s %-20s%s\n", 
                     i, status.c_str(), _relayStates[i].name.c_str(), timer.c_str());
    }
    Serial.println("==========================\n");
}

void RelayController::updateRelayState(int relayNumber) {
    // Implementação futura para leitura de estado real do hardware
}

bool RelayController::isValidRelay(int relayNumber) {
    return relayNumber >= 0 && relayNumber < 16;
}

uint8_t RelayController::getPCFAddress(int relayNumber) {
    return (relayNumber < 8) ? _pcf1Address : _pcf2Address;
}

uint8_t RelayController::getPCFPin(int relayNumber) {
    return relayNumber % 8;
}

void RelayController::initializeDefaultNames() {
    const char* defaultNames[16] = {
        "Bomba Principal",    // Relé 0
        "Bomba Auxiliar",    // Relé 1
        "Luzes LED 1",       // Relé 2
        "Luzes LED 2",       // Relé 3
        "Ventilador 1",      // Relé 4
        "Ventilador 2",      // Relé 5
        "Aquecedor",         // Relé 6
        "Resfriador",        // Relé 7
        "Solenoide 1",       // Relé 8
        "Solenoide 2",       // Relé 9
        "Solenoide 3",       // Relé 10
        "Solenoide 4",       // Relé 11
        "Alarme Sonoro",     // Relé 12
        "Alarme Visual",     // Relé 13
        "Reserva 1",         // Relé 14
        "Reserva 2"          // Relé 15
    };
    
    for (int i = 0; i < 16; i++) {
        _relayStates[i].name = String(defaultNames[i]);
    }
    
    DEBUG_PRINTLN("✅ Nomes padrão dos relés configurados");
}
