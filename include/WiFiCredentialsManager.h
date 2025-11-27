/**
 * @file WiFiCredentialsManager.h
 * @brief Gerenciador de credenciais WiFi para ESP-NOW
 * @version 2.0
 * @date 2025-10-26
 * 
 * Sistema UNIFICADO para enviar e receber credenciais WiFi via ESP-NOW broadcast
 * ESTRUTURA PADRONIZADA - COMPATÍVEL COM MASTER ESP-HIDROWAVE E SLAVE ESPNOW-CARGA
 */

#ifndef WIFI_CREDENTIALS_MANAGER_H
#define WIFI_CREDENTIALS_MANAGER_H

#include <Arduino.h>
#include <cstring>
#include <Preferences.h>
#include <WiFi.h>

/**
 * @brief Estrutura UNIFICADA para credenciais WiFi
 * Tamanho: 100 bytes (33 + 64 + 1 + 1 + 1 padding)
 * 
 * IMPORTANTE: Esta estrutura é IDÊNTICA em:
 * - ESP-HIDROWAVE (Master)
 * - ESPNOW-CARGA (Slave)
 * 
 * CAMPOS ESSENCIAIS (sem complexidade extra):
 * - ssid[33]: Nome da rede WiFi (32 chars + null terminator)
 * - password[64]: Senha da rede WiFi (63 chars + null terminator)
 * - channel: Canal WiFi (1-13)
 * - checksum: Validação XOR simples
 * - reserved: Padding para alinhamento
 */
struct WiFiCredentialsData {
    char ssid[33];        // SSID (máx 32 caracteres + \0)
    char password[64];    // Senha (máx 63 caracteres + \0)
    uint8_t channel;      // Canal WiFi (1-13)
    uint8_t checksum;     // Validação XOR
    uint8_t reserved;     // Padding para alinhamento de 100 bytes
    
    /**
     * @brief Construtor padrão - zera estrutura
     */
    WiFiCredentialsData() {
        memset(this, 0, sizeof(WiFiCredentialsData));
    }
    
    /**
     * @brief Calcula checksum XOR de toda estrutura (exceto o próprio checksum)
     */
    void calculateChecksum() {
        checksum = 0;
        uint8_t* ptr = (uint8_t*)this;
        // XOR de todos os bytes exceto o checksum (último byte antes de reserved)
        for (size_t i = 0; i < offsetof(WiFiCredentialsData, checksum); i++) {
            checksum ^= ptr[i];
        }
    }
    
    /**
     * @brief Valida checksum recebido
     * @return true se checksum válido
     */
    bool isValid() const {
        uint8_t calc = 0;
        const uint8_t* ptr = (const uint8_t*)this;
        for (size_t i = 0; i < offsetof(WiFiCredentialsData, checksum); i++) {
            calc ^= ptr[i];
        }
        return calc == checksum;
    }
    
    /**
     * @brief Verifica se SSID não está vazio
     * @return true se SSID válido
     */
    bool hasValidSSID() const {
        return ssid[0] != '\0' && strlen(ssid) > 0 && strlen(ssid) <= 32;
    }
    
    /**
     * @brief Verifica se canal é válido
     * @return true se canal entre 1-13
     */
    bool hasValidChannel() const {
        return channel >= 1 && channel <= 13;
    }
    
    /**
     * @brief Validação completa da estrutura
     * @return true se todos os campos são válidos
     */
    bool isFullyValid() const {
        return hasValidSSID() && hasValidChannel() && isValid();
    }
    
    /**
     * @brief Imprime informações (sem mostrar senha completa)
     */
    void printInfo() const {
        Serial.println("📶 Credenciais WiFi:");
        Serial.println("   SSID: " + String(ssid));
        Serial.print("   Senha: ");
        if (strlen(password) > 0) {
            for (size_t i = 0; i < strlen(password); i++) {
                Serial.print("*");
            }
        } else {
            Serial.print("(vazia)");
        }
        Serial.println();
        Serial.println("   Canal: " + String(channel));
        Serial.println("   Checksum: 0x" + String(checksum, HEX));
        Serial.println("   Tamanho: " + String(sizeof(WiFiCredentialsData)) + " bytes");
        Serial.println("   Válido: " + String(isFullyValid() ? "✅ Sim" : "❌ Não"));
    }
} __attribute__((packed));

/**
 * @brief Gerenciador de credenciais WiFi com NVS
 */
class WiFiCredentialsManager {
private:
    Preferences prefs;
    const char* NAMESPACE = "wifi_creds";
    
public:
    /**
     * @brief Salva credenciais na NVS
     */
    bool saveCredentials(const WiFiCredentialsData& creds) {
        // Tentar abrir namespace para escrita
        if (!prefs.begin(NAMESPACE, false)) {
            Serial.println("❌ Erro: Não foi possível abrir NVS para salvar credenciais");
            Serial.println("💡 Possíveis causas:");
            Serial.println("   - Partição NVS não encontrada");
            Serial.println("   - Memória flash corrompida");
            return false;
        }
        
        bool success = true;
        success &= prefs.putString("ssid", creds.ssid);
        success &= prefs.putString("password", creds.password);
        success &= prefs.putUChar("channel", creds.channel);
        
        prefs.end();
        
        if (success) {
            Serial.println("💾 Credenciais WiFi salvas na NVS");
            Serial.println("   SSID: " + String(creds.ssid));
            Serial.println("   Canal: " + String(creds.channel));
        } else {
            Serial.println("❌ Erro ao salvar credenciais na NVS");
        }
        
        return success;
    }
    
    /**
     * @brief Salva credenciais na NVS (sobrecarga)
     */
    bool saveCredentials(const String& ssid, const String& password, uint8_t channel = 0) {
        WiFiCredentialsData creds;
        strncpy(creds.ssid, ssid.c_str(), sizeof(creds.ssid) - 1);
        strncpy(creds.password, password.c_str(), sizeof(creds.password) - 1);
        creds.channel = channel;
        return saveCredentials(creds);
    }
    
    /**
     * @brief Carrega credenciais da NVS
     */
    bool loadCredentials(WiFiCredentialsData& creds) {
        // Tentar abrir namespace - se falhar, não há credenciais
        if (!prefs.begin(NAMESPACE, true)) {
            // NVS não inicializado ou namespace não existe - isso é NORMAL na primeira execução
            return false;
        }
        
        String ssid = prefs.getString("ssid", "");
        String password = prefs.getString("password", "");
        uint8_t channel = prefs.getUChar("channel", 0);
        
        prefs.end();
        
        if (ssid.length() == 0) {
            return false;
        }
        
        // Copiar para estrutura
        memset(&creds, 0, sizeof(creds));
        strncpy(creds.ssid, ssid.c_str(), sizeof(creds.ssid) - 1);
        strncpy(creds.password, password.c_str(), sizeof(creds.password) - 1);
        creds.channel = channel;
        
        Serial.println("📂 Credenciais WiFi carregadas da NVS");
        Serial.println("   SSID: " + ssid);
        Serial.println("   Canal salvo: " + String(channel));
        
        return true;
    }
    
    /**
     * @brief Carrega credenciais da NVS (sobrecarga)
     */
    bool loadCredentials(String& ssid, String& password, uint8_t& channel) {
        WiFiCredentialsData creds;
        if (loadCredentials(creds)) {
            ssid = String(creds.ssid);
            password = String(creds.password);
            channel = creds.channel;
            return true;
        }
        return false;
    }
    
    /**
     * @brief Verifica se existem credenciais salvas
     */
    bool hasCredentials() {
        // Tentar abrir namespace - se falhar, não há credenciais
        if (!prefs.begin(NAMESPACE, true)) {
            // NVS não inicializado ou namespace não existe
            return false;
        }
        
        // Verificar se chave ssid existe sem gerar erro
        if (!prefs.isKey("ssid")) {
            prefs.end();
            return false;
        }
        
        String ssid = prefs.getString("ssid", "");
        prefs.end();
        return ssid.length() > 0;
    }
    
    /**
     * @brief Remove credenciais salvas
     */
    void clearCredentials() {
        if (!prefs.begin(NAMESPACE, false)) {
            Serial.println("⚠️ Não foi possível abrir NVS para limpar credenciais");
            Serial.println("💡 Credenciais podem não existir (isso é normal)");
            return;
        }
        prefs.clear();
        prefs.end();
        Serial.println("🗑️ Credenciais WiFi removidas");
    }
    
    /**
     * @brief Conecta ao WiFi usando credenciais salvas
     */
    bool connectWithSavedCredentials(int maxAttempts = 20) {
        WiFiCredentialsData creds;
        
        if (!loadCredentials(creds)) {
            return false;
        }
        
        return connectToWiFi(String(creds.ssid), String(creds.password), maxAttempts);
    }
    
    /**
     * @brief Conecta ao WiFi
     */
    bool connectToWiFi(const String& ssid, const String& password, int maxAttempts = 30) {
        Serial.println("🔄 Conectando ao WiFi...");
        Serial.println("   SSID: " + ssid);
        
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid.c_str(), password.c_str());
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts) {
            delay(500);
            Serial.print(".");
            attempts++;
        }
        Serial.println();
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("✅ WiFi conectado!");
            Serial.println("🌐 IP: " + WiFi.localIP().toString());
            Serial.println("📶 Canal: " + String(WiFi.channel()));
            Serial.println("📡 RSSI: " + String(WiFi.RSSI()) + " dBm");
            return true;
        } else {
            Serial.println("❌ Falha ao conectar ao WiFi");
            Serial.println("💡 Verifique SSID e senha");
            return false;
        }
    }
    
    /**
     * @brief Obtém status da conexão WiFi
     */
    void printStatus() {
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("📶 WiFi Status:");
            Serial.println("   SSID: " + WiFi.SSID());
            Serial.println("   IP: " + WiFi.localIP().toString());
            Serial.println("   Canal: " + String(WiFi.channel()));
            Serial.println("   RSSI: " + String(WiFi.RSSI()) + " dBm");
        } else {
            Serial.println("📶 WiFi: Desconectado");
        }
    }
};

#endif // WIFI_CREDENTIALS_MANAGER_H
