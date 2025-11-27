#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ===== CONFIGURAÇÕES DE DEBUG =====
#define SERIAL_DEBUG_ENABLED true

// ===== CONFIGURAÇÕES DE PERSISTÊNCIA =====
#define PREFERENCES_NAMESPACE "espnow_relays"
#define CONFIG_VERSION 1

// ===== MACROS DE DEBUG =====
#ifndef DEBUG_PRINT
#define DEBUG_PRINT(x) if(SERIAL_DEBUG_ENABLED) Serial.print(x)
#endif
#ifndef DEBUG_PRINTLN
#define DEBUG_PRINTLN(x) if(SERIAL_DEBUG_ENABLED) Serial.println(x)
#endif
#ifndef DEBUG_PRINTF
#define DEBUG_PRINTF(format, ...) if(SERIAL_DEBUG_ENABLED) Serial.printf(format, ##__VA_ARGS__)
#endif

// ===== CONFIGURAÇÕES GERAIS =====
#define SYSTEM_VERSION "2.1"
// DEVICE_ID será generado automáticamente usando MAC address
// Ver función generateDeviceID() en main.cpp
#define FIRMWARE_VERSION "2.1.0"

// ===== CONFIGURAÇÕES DA API =====
// Banco de Dados (Supabase) - APENAS SUPABASE
#define SUPABASE_URL "https://mbrwdpqndasborhosewl.supabase.co"
#define SUPABASE_ANON_KEY "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6Im1icndkcHFuZGFzYm9yaG9zZXdsIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NDgxNDI3MzEsImV4cCI6MjA2MzcxODczMX0.ouRWHqrXv0Umk8SfbyGJoc-TA2vPaGDoC_OS-auj1-A"

// Tabelas do banco de dados
#define SUPABASE_ENVIRONMENT_TABLE "environment_data"
#define SUPABASE_HYDRO_TABLE "hydro_measurements"
#define SUPABASE_RELAY_TABLE "relay_commands"
#define SUPABASE_STATUS_TABLE "device_status"

// Configurações de API
#define API_RETRY_ATTEMPTS 3UL
#define SUPABASE_TIMEOUT_MS 10000
#define COMMAND_POLL_INTERVAL_MS 5000  // Verificar comandos a cada 5 segundos

// Headers HTTP para Supabase
#define SUPABASE_CONTENT_TYPE "application/json"
#define SUPABASE_PREFER "return=minimal"

// ===== LIMITES DO SISTEMA =====
#define MAX_RELAYS 8   // Sistema Master ESP-NOW com 8 relés
#define MAX_SENSORS 8
#define MAX_RETRY_ATTEMPTS 3

// ===== CONFIGURAÇÕES ESP-NOW UNIFICADAS =====
#define ESPNOW_CHANNEL 1                    // Canal WiFi (1-14)
#define MAX_ESPNOW_PEERS 10                 // Máximo de peers ESP-NOW
#define MESSAGE_TIMEOUT_MS 300000            // Timeout para mensagens (5 minutos)
#define PEER_OFFLINE_TIMEOUT 60000          // Timeout para considerar peer offline (60s)
#define DISCOVERY_INTERVAL_MS 30000         // Intervalo de descoberta (30 segundos)
#define STATUS_BROADCAST_INTERVAL 30000    // Intervalo de broadcast de status (30s)

// ===== CONFIGURAÇÕES DE ROBUSTEZ ESP-NOW =====
#define MAX_RETRY_ATTEMPTS 3                // Máximo de tentativas de retry
#define RETRY_DELAY_MS 1000                 // Delay entre tentativas (1 segundo)
#define RETRY_BACKOFF_MULTIPLIER 2          // Multiplicador de backoff
#define CRITICAL_COMMAND_TIMEOUT 5000       // Timeout para comandos críticos (5s)
#define SIGNAL_QUALITY_THRESHOLD -70        // Threshold de qualidade de sinal (dBm)
#define PACKET_LOSS_THRESHOLD 0.1          // Threshold de perda de pacotes (10%)
#define HEALTH_CHECK_INTERVAL 10000         // Intervalo de verificação de saúde (10s)
#define RECOVERY_ATTEMPTS 3                 // Tentativas de recuperação

// ===== CONFIGURAÇÕES DE AUTOMAÇÃO ESP-NOW =====
#define AUTO_DISCOVERY_ENABLED true          // Habilitar descoberta automática
#define DISCOVERY_RETRY_INTERVAL 5000        // Intervalo entre tentativas (5s)
#define MAX_DISCOVERY_ATTEMPTS 10          // Máximo de tentativas
#define PEER_AUTO_RECONNECT true            // Reconexão automática
#define RECONNECT_INTERVAL 30000           // Intervalo de reconexão (30s)
#define TARGET_DEVICE_TYPE "RelayCommandBox" // Tipo de dispositivo alvo
#define CONNECTION_TIMEOUT 60000           // Timeout de conexão (60s)

// ===== PINOS DO HARDWARE =====
// Sensores
#define DHT_PIN 15                     // Sensor DHT22
#define DHT_TYPE DHT22                 // Tipo do sensor DHT
#define PH_PIN 35                      // Sensor de pH
#define TDS_PIN 34                     // Sensor TDS (Analógico)
#define TDS_RX_PIN 36                  // Sensor TDS (RX) - CORRIGIDO: pino ADC válido
#define TDS_TX_PIN 17                  // Sensor TDS (TX)
#define TEMP_PIN 4                     // Sensor de temperatura DS18B20
#define TANK_LOW_PIN 32                // Sensor de nível baixo
#define TANK_HIGH_PIN 33               // Sensor de nível alto
#define WATER_TEMP_PIN 25              // Sensor de temperatura da água
#define WATER_LEVEL_NPN_PIN 32         // Sensor de nível NPN
#define WATER_LEVEL_PNP_PIN 33         // Sensor de nível PNP

// I2C - Barramento compartilhado
#define I2C_SDA 21
#define I2C_SCL 22
#define I2C_FREQUENCY 100000  // 100kHz - frequência padrão para PCF8574

// Status LED
#define STATUS_LED_PIN 2               // LED de status (built-in)

// ===== ENDEREÇOS I2C =====
#define PCF8574_ADDR_1 0x20           // Primeiro PCF8574
#define PCF8574_ADDR_2 0x24           // Segundo PCF8574 (se usado)
#define LCD_ADDR 0x27                 // Display LCD

// ===== INTERVALOS DE TEMPO (em milissegundos) =====
#define SENSOR_READ_INTERVAL_MS 30000     // 30 segundos
#define RELAY_CHECK_INTERVAL_MS 5000      // 5 segundos
#define STATUS_PRINT_INTERVAL_MS 60000    // 1 minuto
#define WIFI_RETRY_INTERVAL_MS 10000      // 10 segundos
#define API_RETRY_INTERVAL_MS 5000        // 5 segundos
#define SUPABASE_STATUS_INTERVAL_MS 30000 // 30 segundos

// ===== CONFIGURAÇÕES DE REDE =====
// 🔓 Configurações de AP definidas em WiFiManager.h
// Evitando redefinição para prevenir conflitos

// ⚠️ Para ambientes que exigem segurança extra:
// Altere diretamente em WiFiManager.h: #define AP_PASSWORD "hidro123"

// ===== LIMITES DE SENSORES =====
#define MIN_PH 0.0
#define MAX_PH 14.0
#define MIN_TDS 0.0
#define MAX_TDS 5000.0
#define MIN_TEMP 0.0
#define MAX_TEMP 50.0
#define MIN_HUMIDITY 0.0
#define MAX_HUMIDITY 100.0

// ===== CONFIGURAÇÕES DOS SENSORES =====
// TDS
#define TDS_VREF 5.0
#define TDS_CALIBRATION_FACTOR 1.0

// pH
#define PH_VREF 3.3
#define PH_CALIBRATION_FACTOR 1.0
#define PH_CAL_7 2.56   // Voltagem para pH 7 (~2.5V)
#define PH_CAL_4 3.3    // Voltagem para pH 4 (~3.3V)
#define PH_CAL_10 2.05  // Voltagem para pH 10 (~2.0V)
#define PH_SAMPLES 10   // Número de amostras para média
#define PH_SAMPLE_INTERVAL 10  // Intervalo entre amostras (ms)

// ===== CONFIGURAÇÕES DE RELÉS =====
#define MAX_RELAYS 8   // Sistema Master ESP-NOW com 8 relés

// Mapeamento de relés para pinos PCF8574 (permite pular pinos defeituosos)
// Formato: {pcf_chip, pin_number, enabled}
// pcf_chip: 0 = PCF1 (0x20), 1 = PCF2 (0x24)
// pin_number: 0-7 para cada PCF
// enabled: true = funcional, false = pino defeituoso (pular)
struct RelayPinMap {
    uint8_t pcf_chip;    // 0 ou 1
    uint8_t pin_number;  // 0-7
    bool enabled;        // true se o pino funciona
};

// Mapeamento flexível - pode ser modificado se houver pinos defeituosos
static const RelayPinMap RELAY_PIN_MAPPING[MAX_RELAYS] = {
    // Relés 0-7 no PCF1 (0x20) - Sistema Master ESP-NOW
    {0, 0, true},   // Relé 0 -> PCF1 P0
    {0, 1, true},   // Relé 1 -> PCF1 P1  
    {0, 2, true},   // Relé 2 -> PCF1 P2
    {0, 3, true},   // Relé 3 -> PCF1 P3
    {0, 4, true},   // Relé 4 -> PCF1 P4
    {0, 5, true},   // Relé 5 -> PCF1 P5
    {0, 6, true},   // Relé 6 -> PCF1 P6
    {0, 7, true}    // Relé 7 -> PCF1 P7
};

// Exemplo de como marcar pinos defeituosos:
// Se o pino P3 do PCF1 estiver quebrado, mude para:
// {0, 3, false},  // Relé 3 -> PCF1 P3 (DEFEITUOSO)

// ===== CONFIGURAÇÕES DA API E BANCO DE DADOS =====

#endif // CONFIG_H 