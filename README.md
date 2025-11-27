# 🚀 ESP-NOW Slave - Relay Controller

<div align="center">

[![PlatformIO](https://img.shields.io/badge/PlatformIO-ESP32-orange.svg)](https://platformio.org/)
[![ESP32](https://img.shields.io/badge/ESP32-DevKit-blue.svg)](https://www.espressif.com/en/products/socs/esp32)
[![ESP-NOW](https://img.shields.io/badge/ESP--NOW-Wireless-green.svg)](https://www.espressif.com/en/products/software/esp-now/overview)
[![License](https://img.shields.io/badge/license-MIT-brightgreen.svg)](LICENSE)

**Firmware ESP32 para controle remoto de relés via ESP-NOW (Modo Slave)**

[Documentação](#-documentação) • [Instalação](#-instalação) • [Uso](#-uso) • [API](#-api-reference) • [Troubleshooting](#-troubleshooting)

</div>

---

## 📋 Sobre o Projeto

Este é o firmware **Slave** do sistema HIDROWAVE - um sistema profissional de controle hidropônico distribuído. O Slave recebe comandos do Master via ESP-NOW e controla 8 relés através de um módulo PCF8574.

### 🎯 Características Principais

- ✅ **Comunicação ESP-NOW** sem necessidade de WiFi
- ✅ **Multi-Channel Discovery** - encontra Master automaticamente (canais 1-13)
- ✅ **SafetyWatchdog** - desliga relés se Master offline
- ✅ **8 Relés controlados** via PCF8574 (I2C)
- ✅ **Timer automático** para desligamento programado
- ✅ **Modo permanente** (on_forever) para bombas e motores
- ✅ **Auto-Recovery** com 4 níveis de recuperação
- ✅ **Cache NVS** para reconexão rápida

---

## 🏗️ Arquitetura

```
┌─────────────────────────────────────────┐
│         MASTER (ESP-HIDROWAVE)          │
│  - Envia comandos via ESP-NOW           │
│  - Gerencia múltiplos Slaves            │
└──────────────────┬──────────────────────┘
                   │ ESP-NOW
                   │
        ┌──────────┴──────────┐
        │                     │
        ▼                     ▼
┌──────────────┐      ┌──────────────┐
│ SLAVE 1      │      │ SLAVE N     │
│ (Este FW)    │      │ (Este FW)   │
│              │      │              │
│ ┌──────────┐ │      │ ┌──────────┐ │
│ │PCF8574   │ │      │ │PCF8574   │ │
│ │0x20      │ │      │ │0x20      │ │
│ └────┬─────┘ │      │ └────┬─────┘ │
│      │       │      │      │       │
│ ┌────▼─────┐ │      │ ┌────▼─────┐ │
│ │ 8 Relés  │ │      │ │ 8 Relés  │ │
│ │ 220V/10A │ │      │ │ 220V/10A │ │
│ └──────────┘ │      │ └──────────┘ │
└──────────────┘      └──────────────┘
```

---

## 🔧 Hardware Necessário

### Componentes por Slave:
- **ESP32 DevKit** ou similar
- **PCF8574** (Expansor I/O I2C) - Endereço 0x20
- **Módulo de 8 relés** compatível com PCF8574
- **Resistores pull-up** 4.7kΩ para I2C (SDA/SCL)
- **Fonte de alimentação** adequada para os relés

### Conexões I2C:
```
ESP32    →  PCF8574
GPIO 21  →  SDA
GPIO 22  →  SCL
3.3V     →  VCC
GND      →  GND
```

### Endereçamento PCF8574:
- **Endereço padrão**: 0x20
- **Configurável** via jumpers A0, A1, A2
- **Suporta múltiplos PCF8574** na mesma rede I2C

---

## 📦 Instalação

### 1. Clonar o Repositório
```bash
git clone https://github.com/yhanusleverage/ESPNOW-SLAVE-TASK.git
cd ESPNOW-SLAVE-TASK
```

### 2. Instalar Dependências
```bash
# Instalar bibliotecas via PlatformIO
pio lib install
```

### 3. Compilar
```bash
# Compilar o projeto
pio run
```

### 4. Upload para ESP32
```bash
# Fazer upload do firmware
pio run --target upload

# Monitor serial (115200 baud)
pio device monitor
```

---

## 🚀 Uso

### 🔧 Comandos Serial (Monitor)

#### Sistema
```bash
help                            # Mostrar ajuda
status                          # Status dos relés
watchdog_status                 # Status do SafetyWatchdog
discover                        # Procurar Master
```

#### Multi-Channel Discovery
```bash
mcd_discover                    # Discovery completo (canais 1-13)
mcd_rediscover                  # Re-discovery rápido
mcd_stats                       # Estatísticas
mcd_cache_clear                 # Limpar cache
mcd_try_channel 6               # Testar canal específico
```

#### Controle Local de Relés
```bash
relay 0 on 30                   # Liga relé 0 por 30s
relay 1 on_forever               # Liga relé 1 permanentemente
relay 2 off                     # Desliga relé 2
relay off_all                   # Desliga todos
on_all                          # Liga todos permanente
```

#### WiFi
```bash
wifi_status                     # Status WiFi
wifi_connect SSID senha         # Conectar manualmente
wifi_clear                      # Limpar credenciais
```

---

## 📚 API Reference

### 🔧 MultiChannelDiscovery

```cpp
// Inicialização
MultiChannelDiscovery discovery;
discovery.begin();

// Callbacks
discovery.setMasterFoundCallback([](uint8_t channel, const uint8_t* mac) {
    Serial.println("Master encontrado no canal " + String(channel));
});

// Discovery
DiscoveryResult result = discovery.discoverMaster();
if (result == DiscoveryResult::SUCCESS) {
    uint8_t channel = discovery.getCurrentChannel();
}

// Re-discovery
result = discovery.rediscoverMaster(true); // Quick scan

// Cache
discovery.saveCache();
discovery.clearCache();
```

### 🛡️ SafetyWatchdog

```cpp
// Inicialização
SafetyWatchdog watchdog;
watchdog.begin();

// Alimentar watchdog (no loop)
watchdog.feed();

// Verificar saúde do Master
watchdog.checkMasterHealth();

// Responder ao Master
watchdog.onMasterResponse();

// Verificações
if (watchdog.isSafetyMode()) {
    // Master offline - modo seguro ativo
    relayBox.turnOffAllRelays();
}
```

### 🔌 RelayCommandBox

```cpp
// Inicialização
RelayCommandBox relayBox(0x20, "RelayBox-01");
relayBox.begin();

// Controle
relayBox.setRelay(0, true);                    // Liga relé 0
relayBox.setRelayWithTimer(1, true, 30);       // Liga relé 1 por 30s
relayBox.processCommand(2, "toggle", 0);       // Alterna relé 2
relayBox.processCommand(3, "on_forever", 0);   // Liga permanente
relayBox.turnOffAllRelays();                   // Desliga todos

// Status
bool state = relayBox.getRelayState(0);
int remaining = relayBox.getRemainingTime(1);
String json = relayBox.getStatusJSON();

// Update (no loop)
relayBox.update();
```

### 📡 ESPNowController

```cpp
// Inicialização
ESPNowController controller("MyDevice", 1);
controller.begin();

// Envio de mensagens
controller.sendPing(targetMac);
controller.sendDeviceInfo(targetMac, "RelayBox", 8, true, millis(), ESP.getFreeHeap());

// Callbacks
controller.setRelayCommandCallback([](auto mac, int relay, auto action, int duration) {
    relayBox.processCommand(relay, action, duration);
});

controller.setPingCallback([](const uint8_t* mac) {
    Serial.println("PONG recebido");
});
```

---

## 🔧 Configuração

### `platformio.ini`
```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200

lib_deps =
    bblanchon/ArduinoJson @ ^6.21.5
    robtillaart/PCF8574 @ ^0.3.9
    WiFi
    Preferences

build_flags = 
    -D CORE_DEBUG_LEVEL=3
    -D SLAVE_MODE                  # Modo Slave
    -D ESPNOW_CHANNEL=1
    -D MAX_ESPNOW_PEERS=10

board_build.partitions = huge_app.csv
```

### `Config.h`
```cpp
// Configurações I2C
#define I2C_SDA 21
#define I2C_SCL 22
#define PCF8574_ADDRESS 0x20

// Configurações ESP-NOW
#define ESPNOW_CHANNEL 1
#define MAX_ESPNOW_PEERS 10

// SafetyWatchdog
#define MASTER_OFFLINE_TIMEOUT 60000    // 60 segundos
#define HEARTBEAT_INTERVAL 30000        // 30 segundos

// MultiChannelDiscovery
#define DISCOVERY_TIMEOUT_MS 1000
#define DISCOVERY_RETRIES 3
```

---

## 🐛 Troubleshooting

### ❌ Slave não encontra Master

**Sintomas:**
```
❌ Master não encontrado: TIMEOUT
💡 Usando canal padrão (1) e aguardando Master...
```

**Soluções:**
1. Verificar se Master está no modo Master (`-D MASTER_MODE`)
2. Usar `mcd_try_channel 6` para testar canais específicos
3. Limpar cache NVS: `mcd_cache_clear`
4. Verificar se WiFi do Master está ativo
5. Re-discovery manual: `mcd_rediscover`

---

### ❌ PCF8574 não encontrado

**Sintomas:**
```
❌ Erro: PCF8574 não encontrado no endereço 0x20
```

**Soluções:**
1. Verificar conexões I2C (SDA/SCL)
2. Verificar alimentação do PCF8574 (3.3V)
3. Adicionar resistores pull-up 4.7kΩ
4. Verificar jumpers A0, A1, A2 do PCF8574

---

### ⚠️ SafetyMode ativado

**Sintomas:**
```
🚨 MODO SEGURO ATIVO - Relés desligados
```

**Explicação:**
- Slave não recebe heartbeat do Master há mais de 60 segundos
- Sistema de segurança desliga todos os relés automaticamente

**Soluções:**
1. Verificar conexão com Master
2. Verificar último heartbeat: `watchdog_status`
3. Forçar reconexão: `mcd_rediscover`
4. Verificar logs do Master

---

## 📖 Documentação Complementar

- **[COMO_USAR.md](COMO_USAR.md)** - Guia completo de uso
- **[COMPATIBILIDADE_MASTER_SLAVE.md](COMPATIBILIDADE_MASTER_SLAVE.md)** - Garantias de compatibilidade
- **[MODO_LISTENER_PURO.md](MODO_LISTENER_PURO.md)** - Modo listener puro

---

## 🤝 Contribuição

Contribuições são bem-vindas! Por favor:

1. Fork o projeto
2. Crie sua feature branch (`git checkout -b feature/NovaFuncionalidade`)
3. Commit suas mudanças (`git commit -m 'Adiciona nova funcionalidade'`)
4. Push para a branch (`git push origin feature/NovaFuncionalidade`)
5. Abra um Pull Request

---

## 📝 Changelog

### v2.0.0 (2025-01-29)
- ✨ MultiChannelDiscovery automático
- ✨ SafetyWatchdog para proteção
- ✨ Sistema de ACKs bidirecionais
- ✨ Cache NVS para reconexão rápida
- ✨ Auto-Recovery com 4 níveis

### v1.0.0 (Anterior)
- ✅ Controle básico de relés via ESP-NOW
- ✅ Comunicação peer-to-peer
- ✅ Interface serial para comandos

---

## 📄 Licença

Este projeto é licenciado sob a licença MIT - veja o arquivo [LICENSE](LICENSE) para detalhes.

---

## 🔗 Links Relacionados

- **Master (ESP-HIDROWAVE)**: [Repositório Principal](https://github.com/yhanusleverage/HIDROWAVE)
- **Frontend**: [HIDROWAVE Dashboard](https://github.com/yhanusleverage/HIDROWAVE/tree/main/HIDROWAVE-main)
- **Documentação Completa**: Ver repositório principal

---

## 📞 Suporte

Para dúvidas e suporte:

- 🐛 Abra uma [Issue](https://github.com/yhanusleverage/ESPNOW-SLAVE-TASK/issues)
- 📖 Consulte a [Documentação](#-documentação-complementar)
- 💬 Verifique os logs seriais para debug

---

<div align="center">

**Desenvolvido com ❤️ usando ESP32 + ESP-NOW**

[![ESP32](https://img.shields.io/badge/Hardware-ESP32-blue.svg)](https://www.espressif.com/)
[![PlatformIO](https://img.shields.io/badge/IDE-PlatformIO-orange.svg)](https://platformio.org/)
[![Made with Arduino](https://img.shields.io/badge/Made%20with-Arduino-teal.svg)](https://www.arduino.cc/)

**[⬆ Voltar ao topo](#-esp-now-slave---relay-controller)**

</div>
