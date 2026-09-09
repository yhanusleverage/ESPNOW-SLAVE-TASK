# HANDOFF — Radio calmada + NVS cuidada + quién toca qué

**Fecha:** 2026-09-09  
**Repos:** Slave `ESPNOW-SLAVE-TASK-main` + Master (otro repo)  
**Contrato opcodes / ACK:** ver también [`HANDOFF_SLAVE_MASTER.md`](HANDOFF_SLAVE_MASTER.md)  
**Objetivo:** producto fiable en campo — slave tonto, Master inteligente, enlace RF estable, flash con vida útil.

---

## 1. Principio (no negociable)

| Concepto | Dónde |
|----------|--------|
| Desired, schedules, fotoperiodo, Auto EC/pH, MQTT, owners | **Master** |
| Máscara, timer, cycle, ACK honesto, SafetyMode, NVS de relés | **Slave** |
| “Qué quiero” | Master |
| “Qué hay en hardware” | Slave → `ALL_RELAYS_STATUS` (0x0E) |

El slave **no** implementa calendarios ni reglas de cultivo.  
Sí mantiene `timed_on` / `cycle` locales (ejecución + supervivencia si cae el Master).

---

## 2. Qué significa cada optimización

### Radio calmada

Menos paquetes ESP-NOW y menos logs Serial para no saturar la cola RF ni el UART.

- Un envío a la vez (esperar `onDataSent` antes del siguiente).
- `ALL_RELAYS` tras **cambio real** (comando / flip de cycle), no spam cada 3 s.
- Un solo heartbeat/ping (15–30 s).
- Throttle de telemetría alto (15–30 s).
- Logs verbosos OFF en producción (`LOG_RADIO 0` / silenciar mural `📨`).

**Síntomas si falta:** `❌ Falha ao enviar`, `` en Serial, dispositivo “offline” con PCF OK.

### NVS cuidada

Escribir flash solo cuando importa (start/stop de cycle/timer, cambio permanente, lock de canal).  
**No** `savePersistentStates()` en cada flip ON↔OFF de un cycle corto.

**Síntomas si falta:** spam `Estados persistentes guardados em NVS` cada pocos segundos; desgaste de flash; tirones de CPU.

---

## 3. Modificar en el SLAVE (este repo)

### Prioridad alta

| # | Cambio | Archivos típicos | Hecho cuando |
|---|--------|------------------|--------------|
| 1 | Unificar handler de relé (`executeRelayCommand`) | `src/ESPNowBridge.cpp` | `timed_on`/`cycle` OK por cualquier camino |
| 2 | NVS: no write en cada flip de cycle | `src/RelayCommandBox.cpp` | cycle 5/5 sin spam NVS en serial |
| 3 | Subir `ALL_RELAYS_THROTTLE_MS` 3s → 15–30s | `include/ESPNowBridge.h` | menos `📤 ALL_RELAYS` |
| 4 | Quitar/alargar telemetría periódica redundante | `src/main.cpp` (bloque ~20s) | no doblar con poll del Master |
| 5 | Unificar pings (quitar duplicado Bridge `update` 30s) | `src/ESPNowBridge.cpp` | un solo origen de heartbeat |
| 6 | Silenciar logs link (ping, mensaje completo) | Bridge / Controller / `Config.h` | serial limpio, sin `` |
| 7 | 0x0E al flip de cycle | `RelayCommandBox` + Bridge | Master ve OFF/ON local |
| 8 | Apagar AutoComm recovery legacy | `src/main.cpp` | solo SafetyWatchdog |

### Prioridad media

| # | Cambio | Por qué |
|---|--------|---------|
| 9 | Pacing `esp_now_send` + cola | evita `Falha ao enviar` |
| 10 | Quitar `delay(10)` I2C | loop / WDT / RF |
| 11 | Canal: una fuente de verdad | evitar `[CONFIG] ch11` con RF en ch1 |
| 12 | `onMasterResponse` en **cualquier** RX del Master | SafetyWatchdog no “ciego” |

### No hacer en slave

- Schedules / cron / fotoperiodo con RTC
- MQTT / Supabase
- Segundo cerebro de recovery
- Reglas por nombre de relé

---

## 4. Modificar en el MASTER (otro repo)

### Prioridad alta

| # | Cambio | Hecho cuando |
|---|--------|--------------|
| 1 | `ALL_ON`/`ALL_OFF` → **un** `SET_RELAY_MASK` opcode **0x0F** | < 2 s, un RF, no 8 comandos |
| 2 | Opcode máscara = **0x0F** (no 0x10) | slave aplica máscara |
| 3 | Un in-flight por MAC (esperar ACK) | sin ráfaga ni mutex crash |
| 4 | Poll `status` 10–15 s (no ~3 s) | menos saturación RF conjunta |

### Prioridad media / producto

| # | Cambio |
|---|--------|
| 5 | `RelayCoordinator`: desired, owner, blocked; deny bits en máscara |
| 6 | MQTT/UI = último **0x0E** (actual), no desired |
| 7 | Schedules / fotoperiodo / Auto EC **solo aquí** |
| 8 | Tras reboot slave: re-sync desired si hace falta |

### No pedir al slave

- JSON de reglas
- 8× `RELAY_COMMAND` para ALL_ON
- Asumir éxito sin ACK + 0x0E

---

## 5. Contrato de opcodes (recordatorio)

```
0x01  RELAY_COMMAND      Master → Slave   un relé
0x0E  ALL_RELAYS_STATUS  Slave → Master   actual + timers
0x0F  SET_RELAY_MASK     Master → Slave   máscara atómica
```

ACK: `success=1` **solo** si PCF escribió.  
Máscara: Master manda lógica 1=ON; slave hace `write8(~mask)`.

Detalle completo: [`HANDOFF_SLAVE_MASTER.md`](HANDOFF_SLAVE_MASTER.md) §§2–5.

---

## 6. Orden de implementación conjunto

```
Sesión 1 — SLAVE
  [ ] Handler único
  [ ] NVS cuidada
  [ ] Radio calmada (throttle + un ping + logs)
  [ ] Flash y bancada 30 min cycle

Sesión 2 — MASTER
  [ ] SET_RELAY_MASK 0x0F
  [ ] Menos poll status
  [ ] Un in-flight

Sesión 3 — PRODUCTO
  [ ] Coordinator + MQTT actual
  [ ] Schedules en Master
```

Si solo se cambia el slave y el Master sigue con poll ~3 s o 8× `on`, la radio **sigue cargada**.

---

## 7. Test de aceptación

### Slave solo

1. Boot: PCF OK, relés OFF o restore NVS documentado
2. `relay 0 cycle 5 5` 10 min: sin spam `Estados persistentes guardados` cada flip
3. Serial sin mural `` / `📨` cada paquete
4. Sin ráfaga `Falha ao enviar` en idle

### Master + Slave

1. `on_all` → **un** log `[PROC] SET_RELAY_MASK`, ACK mask, 0x0E
2. `off_all` → mask 0x00
3. Poll status ≥ 10 s
4. Master offline 45 s+ → SafetyMode OFF en slave
5. Reboot slave con cycle activo → retoma fase; Master re-sync si aplica

---

## 8. Estado actual (snapshot 2026-09-09)

**Ya en slave (aprox.):** máscara 0x0F, ACK honesto, timed_on/cycle, NVS v2, SafetyWatchdog, telemetría 0x0E.

**Pendiente slave:** radio calmada, NVS cuidada, handler único, un heartbeat, silenciar logs, AutoComm off, canal coherente.

**Pendiente Master (verificar en su repo):** 0x0F real, poll suave, desired ≠ actual en UI.

---

## 9. Mensaje corto para el agente del Master

> El slave es tonto: PCF + ACK + 0x0F + 0x0E + timer/cycle.  
> Tú decides desired y schedules.  
> Usa máscara 0x0F, no spam status, pinta actual desde 0x0E.  
> No pidas reglas ni cron al slave.  
> Alinea poll (10–15 s) con la radio calmada del slave; si no, el enlace sigue saturado.
```