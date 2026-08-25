# Contrato Slave ↔ Master (no pisar)

Documento único para el agente del **Master**. El firmware de este repo (SLAVE) ya está alineado. Si el Master usa otros opcodes o manda 8 `on` en bucle, choca con esto.

**Fecha:** 2026-08-24  
**Repo:** `ESPNOW-SLAVE-TASK-main`  
**Rol del slave:** tonto — PCF + ACK honesto + máscara. **Cero** reglas, MQTT, Auto EC/pH, owners.

---

## 1. Quién decide qué

| Concepto | Dónde | Notas |
|----------|--------|--------|
| **Desired** (qué se quiere) | **Master** `RelayCoordinator` | bits[8], owner, blocked |
| **Actual** (qué hay en hardware) | **Slave** PCF | se reporta en `ALL_RELAYS_STATUS` |
| Fotoperiodo / Auto EC / reglas | **Master** | el slave no interpreta |
| Safety local | **Slave** | SafetyMode → OFF; PCF muerto → ACK fail |
| Timer / cycle | **Slave** | `timed_on` y `cycle` locales; sobreviven reboot |
| Boot | **Slave** | restaura ON, timer restante o cycle (8 relés iguales) |
| Boot | **Slave** | restaura ON, timer restante o cycle (todos los relés, luces incluidas) |

El Master compara su desired con el 0x0E. Si no coinciden → mismatch (GND, PCF, SafetyMode). **No** asumas éxito por haber enviado.

---

## 2. Opcodes (obligatorio)

```
0x01  RELAY_COMMAND        Master → slave   un relé
0x0E  ALL_RELAYS_STATUS    slave → Master    actual 8 bits + timers
0x0F  SET_RELAY_MASK       Master → slave   máscara atómica ALL_ON/OFF
```

**Prohibido en Master:**

- `SET_RELAY_MASK = 0x10` (el slave **ya no** lo entiende; es **0x0F**)
- `on_all` = `for (0..7) sendRelayCommand` (8 RF, cola, 16–30 s)
- Tratar ACK `success=1` si no llegó; el slave **no miente**

---

## 3. ALL_ON / ALL_OFF = 1 paquete

```
Master:  SET_RELAY_MASK  mask=0xFF  durationSec=0  commandId=N
Slave:   write8(~mask)  // PCF activo LOW
         ACK  relayNumber=0xFF  success=0|1
         ALL_RELAYS_STATUS  (8 estados reales)
```

Payload `SET_RELAY_MASK` (packed, 8 bytes):

```c
struct RelayMaskCommandData {
    uint8_t  mask;         // bit i = relé i, 1 = ON
    uint8_t  pad;          // 0
    uint16_t durationSec;  // 0 = permanente (slave aplica maxDuration)
    uint32_t commandId;
};
```

PCF: activo en **LOW** → el slave hace `write8(~mask)`. El Master **solo** manda lógica 1=ON. No invertir en el Master.

Relé suelto: sigue `0x01` (un comando). No uses máscara para un bit si el resto del coordinator no está listo; o manda máscara con el resto de bits = actual conocido.

Si **blocked** (ej. R3 Auto EC): `mask_enviar = 0xFF & ~deny` (ej. `0xF7`). El slave aplica lo que reciba; **no** sabe de owners.

---

## 4. ACK honesto

`RelayCommandAck`:

- `commandId` = el del comando
- `relayNumber` = 0–7 (suelto) o **0xFF** (máscara)
- `success` = **1 solo si el PCF escribió**
- `currentState` = 0/1 de **un** relé (en máscara no uses este bit; mira 0x0E)

Si PCF offline / SafetyMode + bits ON:

```
success = 0
ALL_RELAYS refleja actual (casi todo OFF)
```

El dashboard MQTT debe pintar **actual** (0x0E), no desired.

---

## 5. Telemetría (actual)

Tras cada comando (y heartbeat **~20 s**): `ALL_RELAYS_STATUS` 0x0E.

```c
struct SingleRelayState { uint8_t state; uint8_t hasTimer; uint16_t remainingTime; };
struct AllRelaysStatus {
    uint32_t timestamp;
    uint8_t  numRelays;          // 8
    SingleRelayState relays[8]; // state, hasTimer, remainingTime  ← ya rellenados
    uint8_t  checksum;
};
```

**No alargamos el struct** (el Master viejo seguiría parseando).  
`CONNECTIVITY_REPORT.operational` = PCF vivo (no solo heap).

---

## 6. Serial de bancada (slave)

```
mask FF     → 1 write8, ACK, 8 ON
mask 00     → todo OFF
relay 3 on          → 1 comando
relay 0 on 10       → timer 10s
relay 0 cycle 5 5   → cycle local ON/OFF
relay 0 cycle_stop  → corta cycle
```

Si ves 8 líneas `setRelay` en ALL_ON, el **Master** aún manda ráfaga. El slave de máscara loguea:

```
[PROC] SET_RELAY_MASK 0xFF
[PROC] PCF write8(~0xFF)=0x00 actual=0xFF
RELAY_ACK MASK success=1 actual=0xFF
```

---

## 7. Qué el Master SÍ debe hacer (su plan)

1. `sendSetRelayMask(mac, mask, duration, commandId)` opcode **0x0F**
2. `relay on_all` / `off_all` / UI lote → **un** RF
3. `RelayCoordinator`: desired, owner, blocked; **toda** actuación pasa por ahí
4. Un in-flight por MAC (mutex ya parcheado)
5. Log: `[PROC] owner=Manual mask=0xF7 deny=0x08`
6. MQTT `relay/state` = último **0x0E**, no el comando enviado

## 8. Qué el Master NO debe tocar en el slave

- No pedir al slave que ejecute JSON/reglas
- Relé 0x01: action `on`|`off`|`timed_on`|`cycle`|`cycle_stop`; cycle usa `duration`=ON s y `cycleOffDuration`=OFF s
- Tras reboot el slave restaura ON/timer/cycle solo; el Master puede reenviar desired si quiere re-sync
- No cambiar 0x0E layout sin avisar
- No usar 0x10 para máscara

---

## 9. Test conjunto (cuando el Master tenga 0x0F)

1. Slave boot: PCF OK o ACK fail (GND)
2. `on_all` Master → **un** SET_RELAY_MASK, &lt; 2 s, heap estable
3. `off_all` → máscara 0x00
4. Relé 3 suelto sigue 0x01
5. Auto EC en R3 + ALL_ON → slave recibe máscara **sin** bit 3 (deny en Master)
6. Heartbeat 0x0E ~20 s con timers

Si ALL_ON sigue tardando 16–30 s: el Master **no** está mandando 0x0F.

---

## 10. Backlog de mejoras (slave) — para mañana

**Estado 2026-08-24:** cycle, timer, máscara `0x0F`, ACK honesto y restore NVS **ya están en código**. Si el Atlas aún dice `Ação desconhecida: timed_on`, el chip **no está flasheado** con este binario. Subir firmware **antes** de optimizar.

Este bloque es trabajo **del slave**, no del Master. No reabrir el diagnóstico GND/PCF ni el contrato de opcodes.

### Orden de implementación (no saltar)

| # | Ítem | Esfuerzo | Por qué primero |
|---|--------|----------|-----------------|
| 1 | Un solo `executeRelayCommand` | 1–2 h | Evita de nuevo `timed_on` / `cycle` en un camino y no en el otro |
| 2 | NVS: no escribir en cada flip de cycle | 1 h | Flash se desgasta; cycle 5s/5s = write continuo |
| 3 | Quitar `delay(10)` I2C | 15 min | Loop bloqueado; WDT y ESP-NOW |
| 4 | `LOG_RADIO 0` | 30 min | UART llena cola; `Falha ao enviar` |
| 5 | 0x0E al flip de cycle | 30–45 min | MQTT no se entera del OFF/ON local |
| 6 | Unificar heartbeats | 1 h | Menos radio |
| 7 | Apagar AutoComm legacy | 30 min | Dos cerebros de recovery |
| 8 | RelayController vs RelayCommandBox | **no mañana** | Deuda; no bloquea campo |

---

### 10.1 Un solo handler de relé

**Archivos:** [`src/ESPNowBridge.cpp`](src/ESPNowBridge.cpp)

**Hoy:** dos caminos:

- `processReceivedMessage` ~1110 (`RELAY_COMMAND` packed, `startCycle`, `timed_on`)
- `onRelayCommandReceived` ~500 (callback ESPNowController: antes solo `on`/`off`/`toggle`)

Ahí nació ACK `success=0` con `timed_on`.

**Cambio:** una función, p.ej. `bool executeRelayCommand(mac, relay, action, duration, cycleOff, commandId)` usada por ambos. Aliases: `on`, `timed_on`, `cycle`, `cycle_stop`, `off`, `toggle`, `status`.

**Hecho cuando:** un `timed_on` 10 s y un `cycle` 5/5 por ESP-NOW dan ACK=1 **sin** `Ação desconhecida`, da igual por qué callback entre.

**No hacer:** un tercer parser de strings.

---

### 10.2 NVS: no grabar cada flip de cycle

**Archivo:** [`src/RelayCommandBox.cpp`](src/RelayCommandBox.cpp) `checkTimers()` ~616–631.

**Hoy:** cada ON↔OFF llama `savePersistentStates()` (erase+write flash). Cycle corto = desgaste y micro-lags.

**Cambio:**

- Guardar al **startCycle** y **stopCycle** / `off` (ya ocurre vía `setRelay`).
- En el flip: **no** NVS cada vez.
- Opcional: debounce 30–60 s (`lastNvsSaveMs`) por si hay brownout a mitad de fase.
- Restaurar: basta `inCycle + cycleOnSec + cycleOffSec + cyclePhaseOn`; el segundo exacto de la fase puede empezar de 0 (aceptable).

**Hecho cuando:** `relay 0 cycle 5 5` durante 2 min no imprime `Estados persistentes guardados` en cada flip; sí al start y al `off`.

**No hacer:** quitar NVS del reboot; el restore ON/timer/cycle se queda.

---

### 10.3 Quitar `delay(10)` en I2C

**Archivo:** [`src/RelayCommandBox.cpp`](src/RelayCommandBox.cpp) `writeToRelay` ~605.

**Hoy:** cada bit PCF espera 10 ms. Máscara 8 bits o cycle rápido bloquea el loop.

**Cambio:** `delay(10)` → 0 (o `delayMicroseconds` solo si el PCF falla en bancada).

**Hecho cuando:** `mask FF` y cycle 5/5 no introducen ~80 ms extra; WDT no ladra.

**No hacer:** subir clock I2C (100 kHz está bien).

---

### 10.4 Logs de radio

**Hoy:** cada PING/PONG imprime un bloque de 10+ líneas. UART a 115200 se satura; coincide con `Falha ao enviar`.

**Cambio:** en [`include/Config.h`](include/Config.h) (o `ESPNowBridge.cpp`):

```c
#ifndef LOG_RADIO
#define LOG_RADIO 0
#endif
```

PING/PONG/discovery verboso solo si `LOG_RADIO`. Comandos relé / ACK / CYCLE / PCF offline **siempre** 1 línea.

**Hecho cuando:** en reposo el serial no es un mural de ping; un `cycle` sigue siendo visible.

---

### 10.5 Telemetría al flip de cycle

**Hoy:** cycle cambia el PCF en el chip; Master/MQTT se enteran en el heartbeat ~20 s.

**Cambio:** tras flip OK en `checkTimers`, llamar (o flag) `sendAllRelaysStatusToMaster(mac, true)` — el throttle 3 s ya existe. Si no hay peer Master, no enviar.

**Hecho cuando:** cycle 5/5 y el Master ve 0x0E ON y OFF sin esperar 20 s.

**No hacer:** alargar el struct 0x0E; `hasTimer` + `remainingTime` de la fase actual ya valen.

---

### 10.6 Unificar heartbeats

**Hoy (aprox.):**

- PING ~15 s (`SafetyWatchdog`)
- `ALL_RELAYS` ~20 s
- `CONNECTIVITY_REPORT` (menos frecuente)

**Cambio:** un “pulso de vida”: PING **o** 0x0E, no los dos cada 15 s. Dejar 0x0E cada 15–20 s (ya lleva máscara + timers). PING solo si no hubo 0x0E reciente. Connectivity: heap/PCF cada 60 s o dentro de 0x0E `operational`.

**Hecho cuando:** en idle, &lt; 2 paquetes/20 s hacia el Master.

**Nota Master:** ALL_ON = **un** `0x0F`. Ocho `on` no es bug del Atlas.

---

### 10.7 AutoComm vs SafetyWatchdog

**Hoy:** [`AutoCommunicationManager`](include/AutoCommunicationManager.h) (recovery stub) **y** [`SafetyWatchdog.h`](include/SafetyWatchdog.h).

**Cambio:** `autoComm->setHealthMonitoringEnabled(false)` ya está; mañana no llamar `update()` de recovery legacy o `#if 0` el bloque. El orquestador es el Watchdog (feed, SafetyMode, reboot 10 min).

**Hecho cuando:** un solo log de recovery; no dos máquinas de estados peleando.

---

### 10.8 Deuda (no mañana)

- Fusionar `RelayController` y `RelayCommandBox` — el slave de campo usa **RelayCommandBox**.
- `String` en hot path — no urgente.
- RTOS extra / I2C 400 kHz / JSON por radio — no.

---

### 10.9 Checklist mañana (después de flash)

1. Upload este firmware; serial: `timed_on` 10 s → ACK success=1, relé ON, OFF a los 10 s.
2. `relay 0 cycle 5 5` → logs `cycle → OFF` / `ON` en el chip **sin** comando Master extra.
3. Reboot a mitad de cycle → retoma (fase puede reiniciar el tramo).
4. Implementar 10.1 → 10.5 en ese orden.
5. Bancada 30 min cycle: heap estable, NVS no spamea, radio sin mural de ping.

### 10.10 Qué no reabrir

- GND / PCF: ya diagnosticado (tierra común).
- Privilegio de luces: no existe; 8 relés iguales.
- Opcode máscara: **0x0F**, no 0x10.
- Desired en el slave: no. Cycle local sí; reglas Auto EC no.
