# MQTT-контракт

Версия: 0.3.

## Брокер

| Параметр                          | Значение                                  |
|-----------------------------------|-------------------------------------------|
| Хост                              | machine running the broker (laptop в Distributed; localhost в Single-host) |
| MQTT TCP                          | `1883`                                     |
| MQTT WebSocket (для веб-UI)       | `9001`                                     |
| Auth                              | anonymous (стенд работает только на доверенной сети!) |

## Топики

| Топик                                  | Кто публикует             | Кто подписывается        | Retain |
|----------------------------------------|---------------------------|--------------------------|--------|
| `msh/+/json/+/+`                       | узлы (uplink)             | ML-сервер, UI            | NodeInfo — да, остальное — нет |
| `routing/recommendation/<nodeId>`      | ML-сервер                 | узел                     | нет |
| `routing/applied/<nodeId>`             | узел (после применения)   | ML-сервер, UI            | нет |

`<nodeId>` — формат `!hexhex` (восемь hex-символов после `!`), например `!000003e9`.

## 1. Метрики (узлы → ML)

Топик: `msh/+/json/<channel>/<gateway_id>`

Подписывайся на `msh/+/json/+/+` — захватит всё.

Payload — JSON-envelope:

```json
{
  "channel": 1,
  "from": 1001,
  "to": 4294967295,
  "id": 1618382525,
  "hop_start": 3,
  "hops_away": 0,
  "sender": "!000003e9",
  "timestamp": 0,
  "type": "nodeinfo",
  "payload": {
    "id": "!000003e9",
    "longname": "Meshtastic 03e9",
    "shortname": "03e9",
    "hardware": 37,
    "role": 0
  }
}
```

**Гарантировано есть:** `sender`, `from`, `type`, `payload`, `timestamp`.
**Обычно есть:** `hop_start`, `hops_away`, `channel`, `to`.

**Виды `type`:**
- `nodeinfo` — узел представился. **Публикуется с `retain=true`** — новый подписчик видит карту мира сразу, не ждёт периодического broadcast'а.
- `text` — текстовое сообщение (`payload.text`)
- `position` — координаты (`payload.latitude_i`, `payload.longitude_i`)
- `telemetry` — метрики устройства / окружения

**Routing-релевантные поля:** `from` (источник), `sender` (gateway, опубликовавший в MQTT), `hops_away` (сколько хопов прошёл пакет), `hop_start` (сколько было разрешено).

## 2. Рекомендация (ML → узел)

Топик: `routing/recommendation/<nodeId>` — каждый узел подписан **только на свой собственный**.

Payload:

```json
{
  "for_destination": "!000003ea",
  "use_next_hop":   "!000003eb",
  "ttl_s": 60,
  "score": 0.85
}
```

| Поле | Тип | Обязательное | Назначение |
|---|---|---|---|
| `for_destination` | string `!hex` | да | Для трафика, идущего в этот узел |
| `use_next_hop`    | string `!hex` | да | Через этот узел ретранслировать |
| `ttl_s`           | number        | нет | Зарезервировано (пока узлом не используется) |
| `score`           | number 0..1   | нет | Уверенность модели (для логов) |

**Что узел делает:**
1. Парсит JSON. Если поля отсутствуют/невалидны → `applied/<nodeId>` со статусом `skipped`.
2. Если `for_destination` не известен узлу (он ещё не «слышал» его NodeInfo) → `skipped`.
3. Иначе записывает next_hop в свою routing-таблицу и публикует `applied`.
4. Дальше: следующий пакет в адрес `for_destination` на этом узле использует рекомендованный hop.

## 3. Подтверждение (узел → ML)

Топик: `routing/applied/<nodeId>` — публикуется узлом `<nodeId>` после каждой попытки применить рекомендацию.

Payload:

```json
{ "status": "applied" }
```

или

```json
{ "status": "skipped" }
```

`applied` — рекомендация засветилась в routing-таблице. `skipped` — невалидный payload или unknown destination (см. выше).

**Гарантия:** для каждой публикации в `routing/recommendation/<X>` ровно одно сообщение в `routing/applied/<X>` (если узел онлайн).

## Доставка

QoS 0, retain как указано в таблице. Нет гарантии доставки/порядка. Подписчик, подключившийся ПОСЛЕ публикации без retain — её не получит. Для критичных команд лучше публиковать повторно (idempotent — узел применит без побочных эффектов).

## Версионирование

`v0.3`. Изменения схемы — через PR в `CONTRACT.md`.
