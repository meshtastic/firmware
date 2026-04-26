# Mesh routing bench

Стенд для разработки ML-сервера маршрутизации поверх Meshtastic-mesh:
- 2+ виртуальных mesh-узла, которые публикуют свои события в MQTT
- локальный MQTT-брокер
- веб-UI для ручного теста и наблюдения

ML-сервер подписывается на метрики, публикует рекомендации `next_hop` для конкретного узла, узел применяет — и встроенный routing начинает использовать твою рекомендацию для решения «через кого ретранслировать». Контракт топиков и payload — [CONTRACT.md](CONTRACT.md).

---

## Два режима развёртки

| Режим | Где работает | Когда использовать |
|---|---|---|
| **Single-host** | Всё на одной машине (твой ноут или RPi) | Быстро потыкать, отладка ML локально |
| **Distributed** | Узлы — на N разных RPi; брокер + UI — на ноуте | Реалистичный mesh, тест модели на «настоящем» трафике |

Можно начать с Single-host, потом без изменения кода ML переехать на Distributed.

---

## Что нужно поставить (один раз)

### Везде, где запускается стек

- **Docker** — Docker Desktop на Windows/macOS, либо `curl -fsSL https://get.docker.com | sh && sudo usermod -aG docker $USER` на Linux/RPi (потом перелогинься).
- **Python 3.10+** — нужен только для CLI-утилиты `meshtastic`, которой настраиваются узлы (одной командой через pip).
  - Linux/RPi: `sudo apt install -y python3 python3-venv python3-pip git`
  - macOS: `brew install python@3.12 git` (системный 3.9 не подходит)
  - Windows: `winget install Python.Python.3.12 Git.Git`
- **Node.js 20+** — нужен только там, где запускается UI (см. ниже). Можно пропустить, если работаешь без UI.
  - Linux/RPi: `curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash - && sudo apt install -y nodejs`
  - macOS/Windows: с [nodejs.org](https://nodejs.org/) или через `winget install OpenJS.NodeJS.LTS`

### Клонировать репо и поставить `meshtastic` CLI

```bash
git clone <repo-url>
cd <repo>/docs/diploma/mesh-routing-bench

# venv для meshtastic CLI (изолирован от системы)
python3 -m venv .venv               # macOS: python3.12 -m venv .venv
source .venv/bin/activate           # Windows: .venv\Scripts\activate
pip install -r requirements.txt
```

> Все команды `meshtastic ...` ниже выполняются в активированном venv.

---

## Режим A — Single-host

Поднимаем брокер + два узла на одной машине.

### 1. Запустить контейнеры

```bash
cd compose
docker compose --profile single-host up -d --build
```

Первая сборка собирает кастомный образ узла (10–30 мин — PlatformIO компилирует firmware). Дальше из кэша — секунды.

### 2. Сконфигурировать узлы (один раз после первого старта)

Узлы при первом старте не знают, куда публиковать. Скажем им:

```bash
for HOST in localhost:4403 localhost:4404; do
  meshtastic --host $HOST \
    --set mqtt.address mosquitto \
    --set mqtt.username "" \
    --set mqtt.password "" \
    --set mqtt.enabled true \
    --set mqtt.encryption_enabled false \
    --set mqtt.json_enabled true \
    --set mqtt.tls_enabled false \
    --ch-index 0 --ch-set uplink_enabled true --ch-set downlink_enabled true
  meshtastic --host $HOST --ch-add mqtt
  meshtastic --host $HOST --ch-index 1 \
    --ch-set uplink_enabled true --ch-set downlink_enabled true
done

# Конфиг записан, рестартуем чтобы пересоздались MQTT-подписки
docker compose restart nodeA nodeB
```

> Конфигурация сохраняется в Docker-volume. Повторно делать не надо до тех пор, пока ты не сделаешь `docker compose down -v` (полный сброс).

### 3. Поднять UI

В **отдельном** терминале (UI работает в foreground):

```bash
cd ../web
npm install                  # один раз
npm run dev
```

UI откроется на http://localhost:5173. Если зашёл из другого устройства в той же сети — http://&lt;host-ip&gt;:5173.

### 4. Готово

Открой UI → должны появиться два узла. Если пусто — обнови страницу через 5 сек (узлы шлют свой статус с retain-флагом, должно подгрузиться сразу).

---

## Режим B — Distributed (RPi-узлы + ноут-сервер)

### На ноуте — поднимаем брокер + UI

```bash
cd compose
docker compose --profile host up -d
cd ../web
npm install                  # один раз
npm run dev                  # foreground, держи открытым
```

Запиши **IP ноута в LAN** — он понадобится для RPi:
```bash
# macOS / Linux:
ipconfig getifaddr en0     # или ifconfig | grep "inet "
# Windows:
ipconfig | findstr IPv4
```

### На каждом Raspberry Pi — один узел

Каждому узлу — **уникальный** `NODE_HWID` (1001, 1002, 1003, …).

```bash
cd <repo>/docs/diploma/mesh-routing-bench/compose

NODE_HWID=1001 docker compose --profile node up -d --build
```

Первая сборка — 10–30 мин на RPi 4. Дальше из кэша.

Когда поднялся — настроим, чтобы публиковал на ноут (замени `192.168.1.10` на IP ноута):

```bash
source ../.venv/bin/activate

meshtastic --host localhost:4403 \
  --set mqtt.address 192.168.1.10 \
  --set mqtt.username "" \
  --set mqtt.password "" \
  --set mqtt.enabled true \
  --set mqtt.encryption_enabled false \
  --set mqtt.json_enabled true \
  --set mqtt.tls_enabled false \
  --ch-index 0 --ch-set uplink_enabled true --ch-set downlink_enabled true
meshtastic --host localhost:4403 --ch-add mqtt
meshtastic --host localhost:4403 --ch-index 1 \
  --ch-set uplink_enabled true --ch-set downlink_enabled true

NODE_HWID=1001 docker compose --profile node restart node
```

После этого узел появится в UI на ноуте через несколько секунд. Повтори на каждом RPi с другим `NODE_HWID`.

---

## Как тестить

### Через UI (визуально)

1. Открой UI (http://localhost:5173 в Single-host или http://&lt;laptop-ip&gt;:5173 в Distributed).
2. Таблица «Узлы» показывает все узлы. Должны появиться сразу — узлы шлют свой статус с retain.
3. В блоке «Опубликовать рекомендацию»:
   - выбери target nodeId из dropdown
   - в textarea — JSON рекомендации (шаблон уже подставлен)
   - жми **publish →**
4. В колонке «last applied» у целевого узла появится **`applied`** (зелёным) или **`skipped`** (если узел ещё не знает destination'а).
5. Журнал внизу — лог отправленных рекомендаций.

### Из ML-сервера (Python)

Готовый стартер: [examples/ml_consumer.py](examples/ml_consumer.py).

```bash
pip install paho-mqtt
BROKER_HOST=<broker-ip> python examples/ml_consumer.py
```

Подписывается на метрики, для каждой видимой NodeInfo шлёт фейковую рекомендацию назад. Подмени тело callback'а на свою модель.

### С консоли (без UI и без Python — только MQTT)

```bash
# подписаться на всё что летает
mosquitto_sub -h <broker-ip> -t '#' -v

# опубликовать рекомендацию вручную (замени nodeId на реальный)
mosquitto_pub -h <broker-ip> -t 'routing/recommendation/!000003e9' \
  -m '{"for_destination":"!000003ea","use_next_hop":"!000003ea","ttl_s":60}'

# проверить ACK
mosquitto_sub -h <broker-ip> -t 'routing/applied/+' -v
# ожидаем: routing/applied/!000003e9 {"status":"applied"}
```

---

## Контракт MQTT (кратко)

Полная схема — [CONTRACT.md](CONTRACT.md).

| Топик | Направление | Payload |
|---|---|---|
| `msh/+/json/+/+` | узлы → ML | Meshtastic JSON envelope (NodeInfo, Position, Telemetry, Text…) |
| `routing/recommendation/<nodeId>` | ML → узел | `{"for_destination": "!hex", "use_next_hop": "!hex", "ttl_s": 60, "score": 0.85}` |
| `routing/applied/<nodeId>` | узел → ML | `{"status": "applied"}` или `{"status": "skipped"}` |

---

## Полезные команды

```bash
# что крутится
docker compose -f compose/docker-compose.yml ps

# логи узлов (single-host)
docker logs -f mesh-nodeA
docker logs -f mesh-nodeB

# логи RPi-узла (distributed)
docker logs -f mesh-node-1001

# снять стек (с сохранением конфига узлов в volume)
docker compose -f compose/docker-compose.yml --profile single-host down

# полный сброс — стирает volumes, конфиг узлов теряется (потребует повторной настройки)
docker compose -f compose/docker-compose.yml --profile single-host down -v

# триггерить трафик с CLI (когда хочется свежие метрики прямо сейчас):
meshtastic --host localhost:4403 --sendtext "ping" --ch-index 1
```

---

## Типичные грабли

| Симптом | Что делать |
|---|---|
| `docker compose build` падает (OOM) | На RPi 3 (1 GB RAM) сборка не вмещается. Нужен RPi 4 (≥2 GB) или ноут |
| ML-сервер с Windows не подключается к ноуту | Проверь firewall: 1883 должен быть открыт. `Test-NetConnection <ip> -Port 1883` |
| В UI пусто после старта | Скорее всего узлы не сконфигурированы — раздел «Сконфигурировать узлы» |
| `last applied: skipped` | Узел ещё не «слышал» destination'а из твоей рекомендации. На стенде с 2 узлами обмен NodeInfo проходит за минуту. Триггерь `meshtastic --sendtext` |
| Vite UI не открывается с другого хоста | Зайди в `web/package.json`, убедись что в скрипте dev есть `--host` |
| `mosquitto_pub: Connection refused` | Брокер не запущен — `docker compose ps` |
| `meshtastic: command not found` | venv не активирован. `source .venv/bin/activate` |

---

## Когда firmware-сторона выкатит апдейт

```bash
git pull
cd compose
docker compose --profile single-host build --no-cache nodeA   # пересборка образа
docker compose --profile single-host up -d
# узлы могут потерять конфиг — повтори раздел "Сконфигурировать узлы"
```
