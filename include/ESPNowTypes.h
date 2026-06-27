#ifndef ESPNOW_TYPES_H
#define ESPNOW_TYPES_H

#include <Arduino.h>

// ===== TIPOS DE MENSAGEM (Task Slave) =====
enum TaskMessageType {
    TASK_MSG_WIFI_CREDENTIALS = 1,
    TASK_MSG_RELAY_COMMAND = 2,
    TASK_MSG_PING = 3,
    TASK_MSG_PONG = 4,
    TASK_MSG_DISCOVERY = 5,
    TASK_MSG_STATUS_REQUEST = 6,
    TASK_MSG_STATUS_RESPONSE = 7,
    TASK_MSG_HEARTBEAT = 8,
    TASK_MSG_CHANNEL_CHANGE = 9,    // Notificação de mudança de canal
    TASK_MSG_RELAY_ACK = 10          // 🔄 FASE 2: ACK de comando de relay
};

// ===== ESTRUTURAS DE DADOS (Task Slave) =====
struct TaskESPNowMessage {
    TaskMessageType type;
    uint8_t targetMac[6];     // MAC destino (FF:FF:FF:FF:FF:FF para broadcast)
    uint8_t senderMac[6];     // MAC origem
    uint32_t timestamp;       // Timestamp da mensagem
    uint8_t data[200];        // Dados da mensagem
    uint8_t dataSize;         // Tamanho dos dados
    uint8_t checksum;         // Checksum para validação
    uint8_t retryCount;       // Contador de retry
};

struct WiFiCredentials {
    char ssid[33];            // SSID (máximo 32 chars + null)
    char password[65];        // Senha (máximo 64 chars + null)
    uint8_t channel;          // Canal WiFi
    uint8_t checksum;         // Checksum
};

struct ESPNowRelayCommand {
    uint8_t relayNumber;      // Número do relé (0-7)
    char action[16];          // Ação (on, off, toggle, on_forever)
    uint32_t duration;        // Duração em segundos (0 = permanente)
    uint8_t checksum;         // Checksum
};

struct SlaveInfo {
    uint8_t mac[6];           // MAC do slave
    char name[32];             // Nome do slave
    bool online;              // Status online
    uint32_t lastSeen;        // Última comunicação
    uint8_t relayCount;       // Número de relés
    int rssi;                 // Força do sinal
};

struct MasterInfo {
    uint8_t mac[6];           // MAC do master
    bool online;              // Status online
    uint32_t lastSeen;        // Última comunicação
    int rssi;                 // Força do sinal
};

struct ChannelChangeNotification {
    uint8_t oldChannel;       // Canal anterior
    uint8_t newChannel;       // Novo canal
    uint8_t reason;           // Motivo: 1=WiFi mudou, 2=Manual, 3=Interferência
    uint32_t changeTime;      // Timestamp da mudança
    uint8_t checksum;         // Checksum
};

// ===== 🔄 FASE 2: ACK DE COMANDOS DE RELAY =====
/**
 * @brief Estrutura para confirmação (ACK) de comando de relay
 * Enviado pelo SLAVE para confirmar que recebeu e executou comando
 */
struct RelayCommandAck {
    uint32_t commandId;       // ID do comando sendo confirmado
    uint8_t relayNumber;      // Número do relé
    uint8_t success;          // 1=sucesso, 0=falha
    uint8_t currentState;     // Estado atual do relé (0=OFF, 1=ON)
    uint32_t timestamp;       // Quando foi executado
    uint8_t checksum;         // Checksum
};

// ===== 🔄 FASE 3: SINCRONIZAÇÃO DE ESTADO DE TODOS OS RELAYS =====
/**
 * @brief Estado individual de um relé
 */
struct SingleRelayState {
    uint8_t state;            // Estado: 0=OFF, 1=ON
    uint8_t hasTimer;         // 1=tem timer ativo, 0=sem timer
    uint16_t remainingTime;   // Tempo restante em segundos (0 se sem timer)
} __attribute__((packed));

/**
 * @brief Estado completo de TODOS os relays
 * Enviado pelo SLAVE após executar qualquer comando para sincronizar estado completo
 */
struct AllRelaysStatus {
    uint32_t timestamp;       // Quando foi capturado o estado
    uint8_t numRelays;        // Número total de relays (geralmente 8)
    SingleRelayState relays[8]; // Estado de cada relé (0-7)
    uint8_t checksum;         // Checksum
} __attribute__((packed));

// ===== 🎯 PERSISTÊNCIA DE ESTADOS (AUTOMAÇÃO) =====
/**
 * @brief Estado persistente de um relé (guardado em NVS)
 * Permite recuperação após reinicio ou queda de energia
 */
struct PersistentRelayState {
    uint8_t state;            // 0=OFF, 1=ON
    uint8_t hasTimer;         // 1=tem timer ativo, 0=sem timer
    uint32_t timerEndTime;    // Timestamp quando timer expira (0 se sem timer)
    uint8_t isPersistent;     // 1=estado persistente (on_forever), 0=temporário
    uint8_t padding[3];       // Padding para alinhamento
} __attribute__((packed));

/**
 * @brief Estados persistentes de TODOS os relés
 * Guardado em NVS para manter estados mesmo sem conexão com master
 * Permite manter estados mesmo sem WiFi/ESP-NOW
 */
struct PersistentRelayStateData {
    uint32_t timestamp;              // Timestamp da última atualização
    uint8_t numRelays;               // Número total de relés (geralmente 8)
    uint8_t version;                 // Versão do formato (1 = inicial)
    uint8_t padding[2];              // Padding para alinhamento
    PersistentRelayState relays[8];  // Estado de cada relé (0-7)
    uint8_t checksum;                // Checksum para validação
} __attribute__((packed));

#endif // ESPNOW_TYPES_H
