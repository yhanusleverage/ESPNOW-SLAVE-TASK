#include "PreferencesManager.h"

// Instância estática
Preferences PreferencesManager::preferences;
bool PreferencesManager::initialized = false;

bool PreferencesManager::begin() {
    if (initialized) {
        return true;
    }
    
    DEBUG_PRINTLN("🔧 Inicializando PreferencesManager (ESPNOW-CARGA)...");
    
    // Usar Preferences (mais simples e confiável)
    if (preferences.begin(PREFERENCES_NAMESPACE, false)) {
        DEBUG_PRINTLN("✅ Preferences inicializado com sucesso");
    } else {
        DEBUG_PRINTLN("❌ Falha ao inicializar Preferences");
        return false;
    }
    
    initialized = true;
    DEBUG_PRINTLN("✅ PreferencesManager inicializado");
    return true;
}

void PreferencesManager::end() {
    if (initialized) {
        preferences.end();
        initialized = false;
        DEBUG_PRINTLN("📁 PreferencesManager finalizado");
    }
}

bool PreferencesManager::ensureInitialized() {
    if (!initialized) {
        return begin();
    }
    return true;
}

bool PreferencesManager::validateKey(const String& key) {
    return key.length() > 0 && key.length() <= 15;  // Limite NVS
}

bool PreferencesManager::validateValue(const String& value) {
    return value.length() <= 4000;  // Limite razoável para valores
}

// ===== CREDENCIAIS WIFI =====

bool PreferencesManager::saveWiFiCredentials(const String& ssid, const String& password, uint8_t channel) {
    if (!ensureInitialized()) return false;
    
    if (!validateKey("wifi_ssid") || !validateKey("wifi_pass") || !validateKey("wifi_chan")) {
        return false;
    }
    
    if (!validateValue(ssid) || !validateValue(password)) {
        return false;
    }
    
    // Usar Preferences (mais simples)
    bool success = true;
    success &= preferences.putString("wifi_ssid", ssid);
    success &= preferences.putString("wifi_pass", password);
    success &= preferences.putUChar("wifi_chan", channel);
    return success;
}

bool PreferencesManager::loadWiFiCredentials(String& ssid, String& password, uint8_t& channel) {
    if (!ensureInitialized()) return false;
    
    // Usar Preferences (mais simples)
    ssid = preferences.getString("wifi_ssid", "");
    password = preferences.getString("wifi_pass", "");
    channel = preferences.getUChar("wifi_chan", 1);
    return !ssid.isEmpty() && !password.isEmpty();
}

bool PreferencesManager::clearWiFiCredentials() {
    if (!ensureInitialized()) return false;
    
    bool success = true;
    success &= preferences.remove("wifi_ssid");
    success &= preferences.remove("wifi_pass");
    success &= preferences.remove("wifi_chan");
    return success;
}

// ===== CONFIGURAÇÕES DE RELÉS =====

bool PreferencesManager::saveRelayConfig(int relayNumber, const String& name, bool enabled) {
    if (!ensureInitialized()) return false;
    if (relayNumber < 0 || relayNumber >= 8) return false;  // ESPNOW-CARGA tem 8 relés
    
    char keyName[32], keyEnabled[32];
    snprintf(keyName, sizeof(keyName), "relay_%d_name", relayNumber);
    snprintf(keyEnabled, sizeof(keyEnabled), "relay_%d_enabled", relayNumber);
    
    bool success = true;
    success &= preferences.putString(keyName, name);
    success &= preferences.putBool(keyEnabled, enabled);
    return success;
}

bool PreferencesManager::loadRelayConfig(int relayNumber, String& name, bool& enabled) {
    if (!ensureInitialized()) return false;
    if (relayNumber < 0 || relayNumber >= 8) return false;  // ESPNOW-CARGA tem 8 relés
    
    char keyName[32], keyEnabled[32];
    snprintf(keyName, sizeof(keyName), "relay_%d_name", relayNumber);
    snprintf(keyEnabled, sizeof(keyEnabled), "relay_%d_enabled", relayNumber);
    
    name = preferences.getString(keyName, "Relé " + String(relayNumber + 1));
    enabled = preferences.getBool(keyEnabled, true);
    return true;
}

// ===== CONFIGURAÇÕES DE DISPOSITIVO =====

bool PreferencesManager::saveDeviceConfig(const String& deviceName, const String& deviceType, const String& deviceMode) {
    if (!ensureInitialized()) return false;
    
    bool success = true;
    success &= preferences.putString("device_name", deviceName);
    success &= preferences.putString("device_type", deviceType);
    success &= preferences.putString("device_mode", deviceMode);
    return success;
}

bool PreferencesManager::loadDeviceConfig(String& deviceName, String& deviceType, String& deviceMode) {
    if (!ensureInitialized()) return false;
    
    deviceName = preferences.getString("device_name", "ESP-NOW-SLAVE");
    deviceType = preferences.getString("device_type", "RelayController");
    deviceMode = preferences.getString("device_mode", "Slave");
    return true;
}

// ===== CONFIGURAÇÕES GERAIS =====

bool PreferencesManager::saveConfig(const String& key, const String& value) {
    if (!ensureInitialized()) return false;
    if (!validateKey(key) || !validateValue(value)) return false;
    
    return preferences.putString(key.c_str(), value);
}

bool PreferencesManager::loadConfig(const String& key, String& value) {
    if (!ensureInitialized()) return false;
    if (!validateKey(key)) return false;
    
    value = preferences.getString(key.c_str(), "");
    return !value.isEmpty();
}

bool PreferencesManager::saveConfigInt(const String& key, int32_t value) {
    if (!ensureInitialized()) return false;
    if (!validateKey(key)) return false;
    
    return preferences.putInt(key.c_str(), value);
}

bool PreferencesManager::loadConfigInt(const String& key, int32_t& value) {
    if (!ensureInitialized()) return false;
    if (!validateKey(key)) return false;
    
    value = preferences.getInt(key.c_str(), 0);
    return true;
}

// ===== UTILITÁRIOS =====

bool PreferencesManager::configExists(const String& key) {
    if (!ensureInitialized()) return false;
    if (!validateKey(key)) return false;
    
    return preferences.isKey(key.c_str());
}

bool PreferencesManager::removeConfig(const String& key) {
    if (!ensureInitialized()) return false;
    if (!validateKey(key)) return false;
    
    return preferences.remove(key.c_str());
}

String PreferencesManager::getStats() {
    if (!ensureInitialized()) return "PreferencesManager não inicializado";
    
    String stats = "📊 === ESTATÍSTICAS STORAGE (ESPNOW-CARGA) ===\n";
    stats += "🔧 Método: Preferences\n";
    stats += "✅ Status: Inicializado\n";
    stats += "💾 Namespace: " + String(PREFERENCES_NAMESPACE) + "\n";
    
    return stats;
}

bool PreferencesManager::clearAll() {
    if (!ensureInitialized()) return false;
    
    preferences.clear();
    return true;
}
