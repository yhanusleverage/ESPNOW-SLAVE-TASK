# 🚀 COMO USAR - AutoCommunicationManager

## ✅ O QUE TEMOS:

```
AutoCommunicationManager.h  → CÉREBRO da comunicação
AutoCommunicationManager.cpp → Implementação
```

## 🎯 O QUE FAZ:

✅ **Auto-Discovery** → Acha Slave/Master sozinho (30s)
✅ **Auto-Heartbeat** → PING/PONG automático (15s) 
✅ **Auto-Recovery** → 4 níveis de recuperação
✅ **Auto-Channel-Sync** → Sincroniza canal automaticamente
✅ **Health Monitoring** → Score 0-100 em tempo real

---

## 📝 INTEGRAÇÃO RÁPIDA

### **MASTER (ESP-HIDROWAVE) - main.cpp:**

```cpp
#include "AutoCommunicationManager.h"

// Criar global
AutoCommunicationManager* autoComm = nullptr;

void setup() {
    // ... código existente ...
    
    // DEPOIS de inicializar ESPNowController
    autoComm = new AutoCommunicationManager(
        espNowController,  // ponteiro existente
        &wifiManager,      // ponteiro existente
        true               // true = MASTER
    );
    
    autoComm->begin();
}

void loop() {
    // ... código existente ...
    
    // ADICIONAR ISTO:
    if (autoComm) {
        autoComm->update();  // ESSENCIAL!
    }
}
```

### **SLAVE (ESPNOW-CARGA) - main.cpp:**

```cpp
#include "AutoCommunicationManager.h"

// Criar global
AutoCommunicationManager* autoComm = nullptr;

void setup() {
    // ... código existente ...
    
    // DEPOIS de inicializar ESPNowController
    autoComm = new AutoCommunicationManager(
        espNowController,  // ponteiro existente
        &wifiManager,      // ponteiro existente
        false              // false = SLAVE
    );
    
    autoComm->begin();
}

void loop() {
    // ... código existente ...
    
    // ADICIONAR ISTO:
    if (autoComm) {
        autoComm->update();  // ESSENCIAL!
    }
}
```

---

## 🔧 FUNCIONALIDADES

### **1. Discovery Automático**
```cpp
// Automático a cada 30s
// OU forçar:
autoComm->forceDiscovery();
```

### **2. Ver Status**
```cpp
autoComm->printStatus();  // Dashboard completo
```

### **3. Callbacks (Opcional)**
```cpp
autoComm->onDeviceDiscovered([](const uint8_t* mac, const String& name) {
    Serial.println("Dispositivo: " + name);
});

autoComm->onConnectionLost([](const uint8_t* mac) {
    Serial.println("Conexão perdida!");
});
```

### **4. Ativar/Desativar**
```cpp
autoComm->setAutoMode(true);   // Liga automático
autoComm->setAutoMode(false);  // Desliga automático
```

---

## 📊 ESTADOS

```
INIT → WIFI_CONNECTING → ESPNOW_INIT → WAITING_SLAVES (Master)
                                     → WAITING_CREDENTIALS (Slave)
                                     → CONNECTED → MONITORING
                                     → RECOVERY (se necessário)
```

---

## 🎯 RESULTADO

**ANTES:**
- Manual discovery
- Manual ping/pong
- Sem recovery
- Canal hardcoded

**DEPOIS:**
- ✅ Tudo automático
- ✅ Heartbeat bidirecional
- ✅ 4 níveis de recovery
- ✅ Canal sincronizado
- ✅ Health score 0-100
- ✅ Zero configuração

---

## 🚀 PRÓXIMO PASSO

1. Integrar nos main.cpp (Master e Slave)
2. Compilar
3. Upload
4. Ver logs: "🤖 SISTEMA DE COMUNICAÇÃO AUTOMÁTICA"

**PRONTO!** 🎉

