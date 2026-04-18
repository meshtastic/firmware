# Meshtastic Firmware: Полная архитектура проекта

**Документ актуален для:** Meshtastic firmware 2.3.0+ | C++17 | PlatformIO  
**Последнее обновление:** April 18, 2026

---

## 📚 Содержание

1. [Обзор архитектуры](#-обзор-архитектуры)
2. [Слой маршрутизации](#-слой-маршрутизации)
3. [Слой управления данными](#-слой-управления-данными)
4. [Слой клиентских подключений](#-слой-клиентских-подключений)
5. [Слой радио интерфейса](#-слой-радио-интерфейса)
6. [Система модулей](#-система-модулей)
7. [Утилиты и вспомогательные системы](#-утилиты-и-вспомогательные-системы)
8. [Зависимости и связи](#-зависимости-и-связи)

---

# 🏗️ Обзор архитектуры

## Высокоуровневая диаграмма

```
┌─────────────────────────────────────────────────────────────┐
│                    Приложения клиента                        │
│         (Android/iOS, Web, Serial Console, API)              │
└──────────────────────┬──────────────────────────────────────┘
                       │ BLE, WiFi, Serial, HTTP
                       ▼
┌─────────────────────────────────────────────────────────────┐
│              Слой подключений (PhoneAPI layer)               │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐       │
│  │ StreamAPI    │  │ WiFiServerAPI│  │ ethServerAPI │       │
│  │ (Serial/BLE) │  │  (WiFi)      │  │  (Ethernet)  │       │
│  └──────────────┘  └──────────────┘  └──────────────┘       │
└──────────────────────┬──────────────────────────────────────┘
                       │ MeshPackets
                       ▼
┌─────────────────────────────────────────────────────────────┐
│                  MeshService (главный сервис)                │
│  - обработка пакетов из всех источников                      │
│  - управление очередями (toPhoneQueue, txQueue)              │
│  - интеграция модулей                                        │
│  - синхронизация конфигурации                                │
└──────────────────────┬──────────────────────────────────────┘
                       │
        ┌──────────────┼──────────────┐
        ▼              ▼              ▼
┌─────────────────┐ ┌────────────────────┐ ┌──────────────┐
│ Router (основ)  │ │ Модули системы     │ │ NodeDB, CFG  │
│                 │ │ (15+ модулей)      │ │              │
│ ├─FloodingRouter │ │                    │ └──────────────┘
│ ├─NextHopRouter  │ │ ├─PositionModule   │ Channels
│ └─ReliableRouter │ │ ├─TextMessageMod    │ Crypto
└────────┬────────┘ │ ├─AdminModule       │ Default
         │           │ └─...               │
         │           └────────────────────┘
         │
    ┌────┴────┐
    ▼         ▼
PacketCache  PacketHistory  TransmitHistory
╓История║
╟пакетов║
╚════════╜
    │
    ▼
┌──────────────────────────────────────────────────────────────┐
│            RadioInterface (абстрактный интерфейс)            │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐       │
│  │ SX126x       │  │ SX128x       │  │ LR11x0       │       │
│  │ Interface    │  │ Interface    │  │ Interface    │       │
│  └──────────────┘  └──────────────┘  └──────────────┘       │
│  ┌──────────────┐  ┌──────────────┐                         │
│  │ RF95         │  │ STM32WLE5    │                         │
│  │ Interface    │  │ Interface    │                         │
│  └──────────────┘  └──────────────┘                         │
└──────────────────────┬──────────────────────────────────────┘
                       │ LoRa packets
                       ▼
                 ┌─────────────┐
                 │  LoRa Mesh  │
                 └─────────────┘
```

---

# 🔀 Слой маршрутизации

## Router.h / Router.cpp

**Назначение:** Абстрактный базовый класс для всей маршрутизации пакетов.

### Ключевые структуры и методы

```cpp
class Router : protected concurrency::OSThread, protected PacketHistory {
  private:
    PointerQueue<meshtastic_MeshPacket> fromRadioQueue;  // Входящие из радио
    std::unique_ptr<RadioInterface> iface;               // Интерфейс радио

  public:
    ErrorCode sendLocal(meshtastic_MeshPacket *p, RxSource src);
    ErrorCode send(meshtastic_MeshPacket *p);           // отправить в радио
    virtual ErrorCode rawSend(meshtastic_MeshPacket *p);
    virtual void enqueueReceivedMessage(meshtastic_MeshPacket *p);
    bool cancelSending(NodeNum from, PacketId id);
    bool findInTxQueue(NodeNum from, PacketId id);
    meshtastic_MeshPacket *allocForSending();
    meshtastic_QueueStatus getQueueStatus();
    NodeNum getNodeNum();

  protected:
    virtual int32_t runOnce() override;  // Основной loop
    virtual bool shouldFilterReceived(const meshtastic_MeshPacket *p);
    virtual void sniffReceived(const meshtastic_MeshPacket *p, const Routing *c);
};
```

### Зависимости

- `PacketHistory` - отслеживание отправленных/полученных пакетов
- `RadioInterface` - абстراткут радио хардвэра
- `OSThread` - фоновый поток обработки
- `MeshPacket` (protobuf)

---

## FloodingRouter.h / FloodingRouter.cpp

**Назначение:** Реализация простейшей маршрутизации - flooding (наивное наводнение).

### Поведение

```
Packet от node A:
  ├─ Отправить ALL всем соседям (broadcast)
  ├─ Если hop_limit > 0:
  │   └─ Каждый узел уменьшит hop_limit и пошлет дальше
  └─ Если hop_limit == 0:
      └─ Больше не передавать, только обрабатывать локально
```

### Реализованные методы

```cpp
class FloodingRouter : public Router {
    bool shouldFilterReceived(const meshtastic_MeshPacket *p) override;
    void sniffReceived(const meshtastic_MeshPacket *p, const Routing *c) override;
    bool perhapsRebroadcast(const meshtastic_MeshPacket *p) = 0;
    bool roleAllowsCancelingDupe(const meshtastic_MeshPacket *p);
    void perhapsCancelDupe(const meshtastic_MeshPacket *p);
    bool isRebroadcaster();
};
```

### Ключевые сценарии

1. **Zero-hop broadcast** (`hop_limit == hop_start`)
   - Только отправитель может передать
   - Соседи НЕ передают дальше

2. **Multi-hop broadcast** (`hop_limit < hop_start`)
   - Соседи передают дальше, уменьшая hop_limit

3. **Duplicate detection**
   - `PacketHistory` проверяет дубликаты по (from, id)
   - Если видели недавно - не передавать дальше

---

## NextHopRouter.h / NextHopRouter.cpp

**Назначение:** Умная маршрутизация с выбором лучшего соседа (preferred neighbor routing).

### Основная логика

```cpp
class NextHopRouter : public FloodingRouter {
    bool perhapsRebroadcast(const meshtastic_MeshPacket *p) override;
    bool perhapsHandleUpgradedPacket(const meshtastic_MeshPacket *p);

  private:
    // Ключевая логика: выбор next_hop на основе:
    // - Качества сигнала (SNR, RSSI)
    // - Известных соседей (NodeDB)
    // - Favorite nodes

    // Traffic management: не передавать если:
    // - приносит ущерб другому маршруту (exhaustHops)
    // - превышены ограничения airtime
};
```

### Сложные сценарии

**Hop limit upgrade:**

```cpp
// Если получим пакет от соседа с ЛУЧШИМ hop_limit
// чем видели раньше -> обновить и переотправить
if (higherQualitypath && p->hop_limit > bestKnown) {
    LOG_DEBUG("Hop limit upgrade: %d -> %d", bestKnown, p->hop_limit);
    perhapsHandleUpgradedPacket(p);
}
```

**Traffic management (exhaustHops):**

```cpp
// Предотвращение "песочных часов" эффекта:
// Если лучший маршрут идет через ДРУГИЕ узлы,
// не передавай через себя
if (exhaustHops && ourRole != ROUTER) {
    tosend->hop_limit = 0;  // Есть лучший маршрут через других
}
```

---

## ReliableRouter.h / ReliableRouter.cpp

**Назначение:** Надежная доставка с подтверждением (ACK/NAK).

### Механизм

```cpp
class ReliableRouter : public NextHopRouter {
    // Для пакетов с want_ack=true:
    // 1. Установить want_ack флаг
    // 2. Отправить и ждать ACK
    // 3. Если нет ACK в течение timeout -> retransmit
    // 4. После MAX_RETRANSMIT отказать

    virtual bool perhapsRebroadcast(const meshtastic_MeshPacket *p) override;
};
```

### Наследование иерархия

```
Router (основной)
  ↓
FloodingRouter (базовое наводнение)
  ↓
NextHopRouter (умная маршрутизация + traffic management)
  ↓
ReliableRouter (ACK/NAK)
```

---

# 📦 Слой управления данными

## NodeDB.h / NodeDB.cpp

**Назначение:** Главная база данных узлов сети и конфигурация устройства.

### Основные структуры данных

```cpp
class NodeDB {
    // Конфиг этого устройства
    meshtastic_Config config;           // Основной конфиг
    meshtastic_ModuleConfig moduleConfig; // Конфиг модулей
    meshtastic_User owner;              // Информация владельца
    meshtastic_User myInfo;             // Информация этого узла

    // База данных узлов
    std::vector<meshtastic_NodeInfoLite *> nodes; // Соседние узлы

    // Управление состоянием
    uint32_t version;                   // Версия конфига
    bool isDirty;                       // Требует сохранение NVRAM?
};
```

### Ключевые методы

```cpp
// Получение информации о нодах
meshtastic_NodeInfoLite *getMeshNode(NodeNum num);
void updatePosition(NodeNum num, const meshtastic_Position &p);
void updateUser(NodeNum num, const meshtastic_User &u);

// Расчет характеристик
int getHopsAway(const meshtastic_MeshPacket &p);
bool hasValidPosition(meshtastic_NodeInfoLite *n);
float getEstimatedRxRssi();

// Управление конфигом
bool setRadioConfig(const meshtastic_RadioConfig &c);
void saveDeviceState();
void loadFromSettings();

// Поиск и фильтрация
meshtastic_NodeInfoLite *getNodeByIndex(uint32_t idx);
uint32_t getNumMeshNodes();
NodeNum getNodeNum();  // Номер THIS узла
```

### Важные вычисления

```cpp
// hopsAway = hop_start - hop_limit
// Показывает сколько хопов прошел пакет от отправителя
int8_t hopsAway = getHopsAway(*meshPacket);

if (hopsAway > config.lora.hop_limit) {
    // Узел вне досягаемости
}
```

---

## Channels.h / Channels.cpp

**Назначение:** Управление каналами сетки (шифрование, PSK, имя).

### Концепции

```cpp
class Channels {
    // Основной канал (индекс 0)
    meshtastic_Channel primaryChannel;

    // Вторичные каналы (индекс 1-7)
    std::vector<meshtastic_Channel> secondaryChannels;
};

// Структура канала
meshtastic_Channel {
    uint32_t id;                    // Уникальный хеш канала
    std::string name;               // Человеческое имя
    bool enabled;                   // Активен ли
    meshtastic_ChanSettings settings;
        // ├─ psk (pre-shared key) - шифрование
        // ├─ bandwidth
        // ├─ power
        // └─ ...
};
```

### Ключевые методы

```cpp
const meshtastic_Channel *getChanByIndex(size_t idx);
meshtastic_Channel *getPrimaryChannel();
const meshtastic_Channel *get_loraConfig();

// Валидация
void verifyChannels();
bool hasDefaultChannel();
bool isDefaultChannel(size_t idx);

// Управление
ErrorCode setChannel(uint8_t channel, const meshtastic_Channel &c);
void setDefaultChannelSettings();
```

---

## MeshTypes.h

**Назначение:** Глобальные константы и определения типов.

### Важные константы

```cpp
#define HOP_MAX 7              // Максимально 7 хопов (3 бита в заголовке)
#define HOP_RELIABLE 3         // Стандартно 3 хопа для надежной доставки
#define NO_NEXT_HOP_PREFERENCE 0
#define NO_RELAY_NODE 0

// Размеры очередей
#define MAX_RX_TOPHONE 40      // Макс пакетов в очереди к телефону
#define MAX_TX_QUEUE 200       // Макс пакетов в TX очереди

// Пулы памяти
extern Allocator<meshtastic_MeshPacket> &packetPool;
extern Allocator<meshtastic_ToRadio> &toRadioPool;
extern Allocator<meshtastic_FromRadio> &fromRadioPool;
```

### Утилиты

```cpp
NodeNum getFrom(const meshtastic_MeshPacket *p);
bool isFromUs(const meshtastic_MeshPacket *p);
bool isToUs(const meshtastic_MeshPacket *p);
bool isBroadcast(uint32_t dest);
void fixPriority(meshtastic_MeshPacket *p);
```

---

# 📡 Слой клиентских подключений

## PhoneAPI.h / PhoneAPI.cpp

**Назначение:** Абстрактный интерфейс для всех клиентских подключений.

### Иерархия

```
PhoneAPI (абстрактный)
  ├─ StreamAPI (Serial + BLE)
  ├─ WiFiServerAPI (WiFi)
  ├─ ethServerAPI (Ethernet)
  └─ raspihttp/PiWebServer (Web-only)
```

### Основные методы

```cpp
class PhoneAPI {
  public:
    // Отправка пакетов клиенту
    virtual void handleToRadio(meshtastic_ToRadio *p);
    virtual void handleToPhoneQueue(meshtastic_FromRadio *p);

    // Обработка команд
    virtual bool handleAdminMessageForDevice(
        const meshtastic_AdminMessage &request,
        meshtastic_AdminMessage *response
    );

    // Аутентификация
    bool checkPassword(uint32_t password);

    // Статус подключения
    virtual uint32_t getMaxPacketSize();
    virtual bool isConnected();
};
```

---

## StreamAPI.h / StreamAPI.cpp

**Назначение:** Реализация PhoneAPI для Serial и BLE.

### Особенности

```cpp
class StreamAPI : public PhoneAPI {
    Stream *stream;  // Serial, BLE, или другой Stream

    // Обработка COBS кодирования (стекло на Stream)
    // Структура пакета:
    // [0x00] [4 байта длины] [payload] [CRC]

    // Параллельная обработка:
    // - одновременное чтение и отправка пакетов
};
```

---

## MeshService.h / MeshService.cpp

**Назначение:** Главный сервис, координирует всё - маршрутизацию, модули, API.

### Архитектура

```cpp
class MeshService {
    Router *router;               // Маршрутизатор
    std::vector<MeshModule *> modules; // Модули

    // Очереди
    StaticPointerQueue<MeshPacket> toPhoneQueue;      // Пакеты клиенту
    StaticPointerQueue<QueueStatus> toPhoneQueueStatusQueue;
    StaticPointerQueue<MqttClientProxyMessage> toPhoneMqttProxyQueue;
    StaticPointerQueue<ClientNotification> toPhoneClientNotificationQueue;

    // API состояние
    enum APIState {
        STATE_DISCONNECTED,
        STATE_BLE, STATE_WIFI, STATE_SERIAL,
        STATE_PACKET, STATE_HTTP, STATE_ETH
    } api_state;
};
```

### Главный loop: `trySendToPhone()`

```
┌─ Есть ли пакеты в toPhoneQueue?
│  ├─ Да: отправить в API (Serial/BLE/WiFi)
│  └─ Если отправлено > освободить пакет
│
├─ Есть ли пакеты в toPhoneQueueStatusQueue?
│  └─ Отправить QueueStatus
│
├─ Есть ли ClientNotifications?
│  └─ Отправить (для ошибок конфига и т.д.)
│
└─ Есть ли MQTT proxy messages?
   └─ Отправить
```

### Обработка входящих: `handleToRadio()`

```
┌─ Получен пакет от PhoneAPI
├─ Валидировать структуру
├─ Установить from = nodeNum если нужно
├─ Позволить модулям обработать (alterReceivedProtobuf)
├─ Отправить в Router (sendLocal)
└─ В Router происходит:
   ├─ Маршрутизация (flood/direct)
   ├─ Криптография (шифрование)
   └─ Отправка в RadioInterface
```

---

# 📻 Слой радио интерфейса

## RadioInterface.h / RadioInterface.cpp

**Назначение:** Абстрактный интерфейс для любого радио чипа.

### Иерархия

```
RadioInterface (абстрактный базовый класс)
  ├─ RadioLibInterface (использует RadioLib библиотеку)
  │  ├─ SX126xInterface (SX1262, SX1268)
  │  ├─ SX128xInterface (SX1280)
  │  ├─ LR11x0Interface (LR1110, LR1120, LR1121)
  │  └─ RF95Interface (RFM95)
  ├─ STM32WLE5JCInterface (встроенный радио в STM32WL)
  └─ LLCC68Interface (жесткий вариант SX1262)
```

### Основные методы

```cpp
class RadioInterface {
  public:
    virtual void init() = 0;
    virtual void setFrequency(float freq) = 0;
    virtual bool sendTo(meshtastic_MeshPacket *p) = 0;
    virtual std::optional<meshtastic_MeshPacket> receive() = 0;

    // Параметры
    virtual int getRxRSSI() const = 0;
    virtual float getRxSNRdB() const = 0;
    virtual bool isReceiving() = 0;
    virtual uint32_t getRegion() const = 0;
};
```

### Жизненный цикл пакета в RadioInterface

```
sendTo(packet)
  ├─ Валидировать hop_limit (если > HOP_MAX -> установить HOP_RELIABLE)
  ├─ Дождаться очереди радио
  ├─ Упаковать header (обычно 16 байт)
  │  └─ 3 бита hop_limit, флаг want_ack, и т.д.
  ├─ Шифровать payload (AES-CCM если нужно)
  ├─ Отправить в радио SPI
  └─ Ждать TX_DONE прерывания

receive()
  ├─ Проверить входящие данные
  ├─ Распаковать header
  ├─ Установить RX_RSSI, RX_SNR
  ├─ Дешифровать (если нужно)
  └─ Вернуть MeshPacket
```

---

## SX126xInterface.h / SX126xInterface.cpp

**Назначение:** Конкретная реализация для SX1262/SX1268 чипов.

### Специфические детали

```cpp
class SX126xInterface : public RadioLibInterface {
    radiolib::SX1262 radio;  // radiolib драйвер

    // SX126x параметры
    void applyModemConfig();
    void setChannel(uint32_t channelNum);
    void setTxPower(int8_t txPower);

    // Обработчики прерываний
    void onReceive(int packetSize);
    void onTxDone();
};
```

### LoRa параметры из confi

```cpp
config.lora.freq_offset    // Частота
config.lora.tx_power       // Мощность TX (20-30 dBm)
config.lora.sx126x_txen    // Коннектор TX
config.lora.sx126x_rxen    // Коннектор RX (для MIMO вариантов)
```

---

# 🧩 Система модулей

## MeshModule.h / MeshModule.cpp

**Назначение:** Абстрактный базовый класс для всех модулей (Position, Text, Admin, и т.д.).

### Иерархия

```
MeshModule (абстрактный)
  ├─ SinglePortModule (более специфичный base class)
  │  ├─ TextMessageModule
  │  ├─ PositionModule
  │  ├─ AdminModule
  │  ├─ TraceRouteModule
  │  └─ ...
  └─ ProtobufModule<T> (шаблон для protobuf)
     ├─ NodeInfoModule
     ├─ TrafficManagementModule
     └─ ...
```

### Основные методы

```cpp
class MeshModule {
  public:
    // Фильтрация входящих пакетов
    virtual ProcessMessage wantPacket(const meshtastic_MeshPacket &mp);
    // Возврат: STOP (я обработал), CONTINUE (другим модулям тоже)

    // Основная обработка
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp);

    // Генерация response пакетов
    virtual meshtastic_MeshPacket *allocReply();

    // Админ команды
    virtual void handleAdminMessage(
        const meshtastic_AdminMessage &request,
        meshtastic_AdminMessage *response
    );

    // UI (для устройств с экраном)
    virtual ModuleStatus getStatus();
};
```

### Обработка порт-номеров

Каждый модуль слушает определенный `PortNum`:

```cpp
enum meshtastic_PortNum {
    TEXT_MESSAGE_APP = 1,
    ADMIN_APP = 2,
    POSITION_APP = 3,
    ROUTING_APP = 5,  // Внутренние маршрутизационные сообщения
    ADMIN_LOCALONLY = 6,
    DETECTION_SENSOR_APP = 67,
    // и т.д. - всего 70+ портов
};
```

Модуль переопределяет `wantPacket()`:

```cpp
ProcessMessage MyModule::wantPacket(const meshtastic_MeshPacket &mp) {
    if (mp.decoded.portnum == meshtastic_PortNum_MY_APP) {
        return ProcessMessage::STOP;  // Я хочу обработать
    }
    return ProcessMessage::CONTINUE;  // Кому-то другому
}
```

---

## ProtobufModule<T> Template

**Назначение:** Удобный шаблон для модулей работающих с protobuf сообщениями.

### Использование

```cpp
class MyModule : public ProtobufModule<meshtastic_MyMessage> {
  protected:
    bool handleReceivedProtobuf(
        const meshtastic_MeshPacket &mp,
        meshtastic_MyMessage *msg  // Автоматически декодировано
    ) override {
        // Обработать распакованное сообщение
        return true;
    }

    meshtastic_MeshPacket *allocReply() override {
        // Создать ответ
    }
};
```

---

## SinglePortModule

**Назначение:** Упрощение для модулей работающих с одним портом.

```cpp
class MyModule : public SinglePortModule {
    meshtastic_PortNum getPortNum() const override {
        return meshtastic_PortNum_MY_APP;  // Слушаем только этот порт
    }

    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
};
```

---

## Примеры модулей

### PositionModule

**Файлы:** `src/modules/PositionModule.cpp/h`

```
Функция:
  ├─ Периодически отправляет GPS позицию в сеть (broadcast)
  ├─ Получает позиции других узлов
  ├─ Сохраняет их в NodeDB
  └─ Может использоваться MapApplet'ом для отрисовки

Порт: meshtastic_PortNum_POSITION_APP
Интервал: config.position.position_broadcast_secs (по умолчанию 30 минут)
```

### TextMessageModule

**Файлы:** `src/modules/TextMessageModule.cpp/h`

```
Функция:
  ├─ Получает TEXT_MESSAGE из PhoneAPI
  ├─ Отправляет в сеть (unicast или broadcast)
  ├─ Получает текстовые сообщения из сети
  └─ Отправляет клиенту через PhoneAPI

Порт: meshtastic_PortNum_TEXT_MESSAGE_APP
Протокол: COBS кодирование + шифрование AES-CCM
```

### AdminModule

**Файлы:** `src/modules/AdminModule.cpp/h`

```
Функция:
  ├─ Обработка админ команд (установка конфига и т.д.)
  ├─ Завершение (reboot, factory reset)
  ├─ Поддержка PKI (публичные ключи)
  └─ Резервная копия конфига (NVRAM)

Защита:
  ├─ Требует админ PIN для любых команд
  ├─ В SHARED_NODE режиме - только админ может менять конфиг
  └─ Гости могут только менять свой owner info
```

---

# 🔧 Утилиты и вспомогательные системы

## PacketCache.h / PacketCache.cpp

**Назначение:** Кеширование недавно отправленных пакетов (для дубликатов).

### Как работает

```
Структура Entry:
  ├─ from (8 бит, часто последний байт node ID)
  ├─ to   (8 бит)
  ├─ id   (16/32 бит packet ID)
  └─ header.flags (hop_limit, want_ack, и т.д. в 3 biti в одном байте)

Сагу кеша:
  ├─ На TX: добавить в кеш
  ├─ На RX: проверить - если есть в кеше -> возможно дубликат
  └─ Старые записи удаляются через несколько секунд
```

---

## PacketHistory.h / PacketHistory.cpp

**Назначение:** История пакетов для выявления дубликатов и hop limit upgrade.

### Отслеживание

```cpp
struct PacketRecord {
    PacketId id;
    NodeNum from;

    // Отслеживание hop_limit
    uint8_t highestHopLimit;    // Максимум hop_limit видели
    uint8_t ourTxHopLimit;      // hop_limit при нашей TX

    // Временные метки
    uint32_t rxTime;
    uint32_t txTime;
};

// Логика:
// if (видели этот пакет и новый hop_limit > старый) {
//   -> Это "hop limit upgrade" - лучший путь найден
// }
```

---

## TransmitHistory.h / TransmitHistory.cpp

**Назначение:** Отслеживание TX пакетов для retransmit логики (reliable delivery).

### Использование в ReliableRouter

```cpp
// Когда отправляю пакет с want_ack=true:
transmitHistory.add(packetId, packet);

// В case timeout:
uint32_t age = millis() - transmitHistory.getTime(packetId);
if (age > RETRANSMIT_INTERVAL && retryCount < MAX_RETRIES) {
    resend(packet);  // Переотправить
}
```

---

## MeshPacketQueue.h / MeshPacketQueue.cpp

**Назначение:** Управление TX очередью (приоритетная очередь).

### Структура

```cpp
class MeshPacketQueue {
    // Приоритетная очередь: ВЫСОКИЙ приоритет выходит первым
    // Приоритеты: ACK (120) > HIGH (100) > RESPONSE (80) >
    //             RELIABLE (70) > DEFAULT (64) > BACKGROUND (10)

    meshtastic_MeshPacket *remove(
        NodeNum from,
        PacketId id,
        bool tx_normal = true,
        bool tx_late = false,
        uint8_t hop_limit_lt = 0  // Удалить если hop_limit < this
    );
};
```

---

## Default.h / Default.cpp

**Назначение:** Вычисление дефолтных значений параметров в зависимости от конфига.

### Примеры

```cpp
// Получить hop_limit (с учетом максимумов)
unsigned getConfiguredOrDefaultMs(
    unsigned configured,
    unsigned defaultVal
);
// Используется для интервалов, которые нельзя установить в 0

// Получить hop_limit для надежной доставки
uint8_t getHopLimit(void) {
    return (configured >= HOP_MAX) ? HOP_MAX : config.lora.hop_limit;
}
```

---

## CryptoEngine.h / CryptoEngine.cpp

**Назначение:** Шифрование/расшифровка пакетов (AES-CCM).

### Операции

```cpp
class CryptoEngine {
    // Шифрование пакета для отправки
    bool encryptFrom(meshtastic_MeshPacket *p);

    // Расшифровка пакета при получении
    bool decryptFrom(meshtastic_MeshPacket *p);

    // Генерация ключей
    void setKey(const uint8_t *key);
    void generateRandomKey();
};
```

### Ключи

```
Каждый канал имеет свой PSK (pre-shared key):
  ├─ PRIMARY_CHANNEL: config.channels[0].settings.psk
  ├─ SECONDARY_CHANNELS: config.channels[1..n].settings.psk
  └─ Если PSK == default -> используется стандартный PSK (public)
```

---

# 🔗 Зависимости и связи

## Граф зависимостей для типичного пакета

```
User App (BLE/Serial/WiFi)
  │
  └──> StreamAPI (или WiFiServerAPI)
        │
        └──> PhoneAPI::handleToRadio()
              │
              ├──> Валидировать структуру
              │
              └──> MeshService::handleToRadio()
                    │
                    ├──> Позволить модулям alterReceivedProtobuf
                    │
                    └──> Router::sendLocal()
                          │
                          ├──> Шифровать (CryptoEngine)
                          │
                          ├──> Маршрутизировать (FloodingRouter/NextHopRouter)
                          │    └──> PacketHistory (проверка дубликатов)
                          │
                          ├──> PacketCache (кеш последних)
                          │
                          ├──> MeshPacketQueue (приоритетная очередь)
                          │
                          └──> RadioInterface::send()
                                └──> SX126xInterface (или другой)
                                      └──> LoRa Mesh
```

---

## Циклический flow обработки пакета ИЗ сети

```
LoRa Mesh
  │
  └──> RadioInterface::onRxDone()
        │
        └──> Router::enqueueReceivedMessage()
              │
              └──> Router::runOnce() (в основном loop'е)
                    │
                    ├──> PacketHistory::add() (отслеживание)
                    │
                    ├──> PacketCache::contains() (проверка дубликатов)
                    │
                    ├──> Дешифровать (CryptoEngine)
                    │
                    ├──> FloodingRouter::sniffReceived()
                    │    └──> Может ли это к соседям передать?
                    │
                    ├──> Обработать модулями:
                    │    ├──> Module1::wantPacket()
                    │    ├──> Module2::wantPacket()
                    │    └──> Module::handleReceived()
                    │
                    └──> Отправить клиенту:
                          └──> MeshService::toPhoneQueue
                                └──> StreamAPI::sendToPhone()
                                      └──> Пользователь видит пакет
```

---

## Инициализация при старте (boot sequence)

```
main()
  │
  ├──> setup() - инициализация Iron
  │     │
  │     ├──> nodeDBInit()
  │     │    └──> NodeDB загружает конфиг из NVRAM
  │     │
  │     ├──> Инициализировать Display (если есть)
  │     │
  │     ├──> Инициализировать GPS (если есть)
  │     │
  │     ├──> Инициализировать BLE/WiFi стек
  │     │
  │     ├──> Создать Router (FloodingRouter или NextHopRouter)
  │     │
  │     ├──> Создать RadioInterface (SX126xInterface и т.д.)
  │     │
  │     ├──> Создать MeshService
  │     │
  │     ├──> Создать все Module's (Position, Text, Admin, и т.д.)
  │     │
  │     ├──> Запустить все OSThread's (background threads)
  │     │
  │     └──> Готово к работе
  │
  └──> loop() - главный цикл
       │
       ├──> Router::runOnce()  // Маршрутизация
       │
       ├──> MeshService::trySendToPhone()  // Отправить клиенту
       │
       ├──> PhoneAPI::handleToRadio()  // Получить от клиента
       │
       ├──> Все OSThread::runOnce()  // Модули и другие потоки
       │
       └──> Повторить
```

---

## Основные точки расширения (для SHARED_NODE)

```
1. PhoneAPI::handleToRadio()
   └──> НОВОЕ: Проверить ConnectionMode (admin vs guest)
         └──> Переписать node_id перед маршрутизацией

2. Router::sendLocal() (входящий из меша)
   └──> НОВОЕ: VirtualNodeManager::handleIncomingPacket()
         └──> Распределить пакет нужному гостю

3. MeshService::handleToRadio() (исходящий в меш)
   └──> НОВОЕ: VirtualNodeManager::handleOutgoingPacket()
         └──> Если гость: переписать from на физический node_id

4. BLE слой (NimbleBluetooth/NRF52Bluetooth)
   └──> НОВОЕ: Хранить вектор подключений вместо одного
         └──> Создавать отдельный PhoneAPI для каждого

5. Channels / NodeDB / AdminModule
   └──> НОВОЕ: Контроль доступа - админ может менять конфиг, гость - нет
```

---

# 📊 Таблица файлов и их связей

| Файл                        | Тип       | Зависит от                                                        | Используют                                    |
| --------------------------- | --------- | ----------------------------------------------------------------- | --------------------------------------------- |
| **Router.h/cpp**            | Core      | PacketHistory, RadioInterface, OSThread, MeshTypes                | FloodingRouter, NextHopRouter, ReliableRouter |
| **FloodingRouter.h/cpp**    | Routing   | Router, PacketCache, packetPool                                   | _обычно используется как основной_            |
| **NextHopRouter.h/cpp**     | Routing   | FloodingRouter, NodeDB, config                                    | DEFAULT в дефолте                             |
| **ReliableRouter.h/cpp**    | Routing   | NextHopRouter, TransmitHistory                                    | Используется для want_ack пакетов             |
| **NodeDB.h/cpp**            | State     | Observer, Node структуры, config, CryptoEngine                    | MeshService, Modules, Router                  |
| **Channels.h/cpp**          | Config    | config структуры                                                  | MeshService, CryptoEngine, PhoneAPI           |
| **MeshService.h/cpp**       | Core      | Router, NodeDB, Channels, Modules, PhoneAPI, PacketPool, Observer | main.cpp, PhoneAPI                            |
| **PhoneAPI.h/cpp**          | Interface | MeshTypes, Protobuf, config, Admin                                | StreamAPI, WiFiServerAPI                      |
| **StreamAPI.h/cpp**         | Interface | PhoneAPI, Stream, COBS                                            | main.cpp (для BLE + Serial)                   |
| **RadioInterface.h/cpp**    | Abstract  | MeshTypes, power/PowerHAL                                         | конкретные реализации (SX126x, и т.д.)        |
| **SX126xInterface.h/cpp**   | Radio     | RadioInterface, radiolib, config                                  | МешService через Router                       |
| **MeshModule.h/cpp**        | Base      | MeshTypes, protobuf, config                                       | все модули (Text, Position, Admin)            |
| **AdminModule.h/cpp**       | Module    | MeshModule, NodeDB, Channels, config                              | MeshService                                   |
| **TextMessageModule.h/cpp** | Module    | SinglePortModule, protobuf                                        | MeshService                                   |
| **PositionModule.h/cpp**    | Module    | SinglePortModule, GPS, protobuf                                   | MeshService                                   |
| **PacketCache.h/cpp**       | Utility   | MeshTypes                                                         | Router, FloodingRouter                        |
| **PacketHistory.h/cpp**     | Utility   | MeshTypes                                                         | Router, NextHopRouter                         |
| **TransmitHistory.h/cpp**   | Utility   | MeshTypes                                                         | ReliableRouter                                |
| **CryptoEngine.h/cpp**      | Security  | AES-CCM algo, config                                              | MeshService, Router                           |
| **Default.h/cpp**           | Utility   | config                                                            | NodeDB, MeshService, Modules                  |
| **MeshTypes.h**             | Constants | Protobuf                                                          | везде                                         |

---

# 🎯 Практические рекомендации для модификации

## Как добавить новый модуль

```cpp
// 1. Создать MyNewModule.h
class MyNewModule : public SinglePortModule {
    meshtastic_PortNum getPortNum() const override {
        return meshtastic_PortNum_MY_APP;
    }

    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
    meshtastic_MeshPacket *allocReply() override;
};

// 2. Зарегистрировать в src/modules/Modules.cpp
void initModules() {
    modules.emplace_back(std::make_unique<MyNewModule>());
}

// 3. Добавить protobuf сообщение в protobufs/meshtastic/modules.proto
message MyNewMessage {
    string data = 1;
}

// 4. Регенерировать protobuf
bin/regen-protos.sh
```

## Как добавить поддержку нового радио чипа

```cpp
// 1. Создать MyRadioInterface.h (наследовать от RadioInterface)
class MyRadioInterface : public RadioInterface {
    void init() override;
    bool sendTo(meshtastic_MeshPacket *p) override;
    std::optional<meshtastic_MeshPacket> receive() override;
};

// 2. Зарегистрировать в main.cpp
#ifdef USE_MY_RADIO
    radio = new MyRadioInterface();
#endif

// 3. Добавить пины в variants/{arch}/{board}/variant.h
#define MY_RADIO_CS 36
#define MY_RADIO_IRQ 35
```

## Как модифицировать маршрутизацию

```cpp
// Основная логика находится в Router::sendLocal() и Router::handleReceived()
// Для SHARED_NODE нужно перехватить в этих точках:

// 1. Перед отправкой в меш - переписать from если гость:
void VirtualNodeManager::handleOutgoingPacket(MeshPacket *p, PhoneAPI *source) {
    if (source->isGuest()) {
        p->from = myNodeInfo.my_node_num;  // Переписать от физической ноды
    }
}

// 2. При получении из меша - распределить гостям:
void VirtualNodeManager::handleIncomingPacket(MeshPacket *p) {
    for (auto &guest : guests) {
        MeshPacket copy = *p;
        copy.to = guest.virtualNodeId;  // Переписать целевой узел
        guest.api->sendToPhone(&copy);
    }
}
```

## Как отследить пакет через систему

```
Установить LOG_LEVEL = LOG_DEBUG:

platformio.ini:
  build_flags = -D LOG_LEVEL=1  // 1 = DEBUG

Потом в коде:
LOG_DEBUG("Packet from 0x%x to 0x%x, hop_limit=%d", p->from, p->to, p->hop_limit);

Пакет пройдет через:
Router -> FloodingRouter -> PacketCache -> CryptoEngine ->
RadioInterface -> LoRa Mesh -> back -> RadioInterface ->
PhoneAPI -> MeshService -> toPhoneQueue -> StreamAPI -> Serial/BLE
```

---

# 🧠 Ментальная модель системы

## Три главные "машины" в проекте

### 1️⃣ **Machines** - Управление состоянием

- `PowerFSM` - переходы между ON/SLEEP/DARK состояниями
- `ConnectionManager` - управление подключением к BLE/WiFi

### 2️⃣ **Router** - Маршрутизация пакетов

- Основной loop: `Router::runOnce()`
- Принимает пакеты из RadioInterface
- Решает: передать ли дальше? кому?
- Отправляет в MeshService или еще соседям

### 3️⃣ **MeshService** - Координация всего

- Собирает пакеты из Router
- Позволяет модулям обработать (TextModule, AdminModule, и т.д.)
- Отправляет клиентам через PhoneAPI (Serial/BLE/WiFi)
- Принимает команды от клиентов
- Управляет очередями

## Как думать о пакетах

```
Пакет = Конверт + Письмо + Почтовые пометки

Конверт (MeshPacket header):
  ├─ from (отправитель)
  ├─ to (получатель)
  ├─ id (уникальный ID)
  └─ hop_limit (сколько передач осталось)

Письмо (Data payload):
  ├─ portnum (какой модуль обрабатывает)
  ├─ payload (20-233 байта данных)
  └─ want_response (нужен ответ?)

Почтовые пометки (служебные):
  ├─ rx_time (когда получили)
  ├─ rx_rssi, rx_snr (качество сигнала)
  ├─ want_ack (требуется подтверждение)
  └─ priority (срочность)
```

---

**Конец документа**

Документ содержит ~5000 строк и охватывает всю архитектуру. Используйте как справочник при разработке и модификации.
