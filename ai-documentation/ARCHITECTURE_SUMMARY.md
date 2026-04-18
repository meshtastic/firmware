# SHARED_NODE: Резюме архитектуры

## 🎯 Цель

Позволить нескольким приложениям одновременно подключаться к одной физической ноде, каждое думая что подключилось к собственной "виртуальной ноде".

---

## 3️⃣ Режима использования

### 1. DEFAULT (текущий)
```
Одно приложение → Одна физическая нода → Меш
```
- Полностью совместимо с существующей архитектурой
- Требует админ PIN для подключения

### 2. SHARED_NODE (основной результат)
```
Admin App → Физическая нода (0x00000001)
         ├─ Гость App #1 → Виртуальная нода (0x0A)
         └─ Гость App #2 → Виртуальная нода (0x0B)
         
Все → Один меш
```
- Админ может менять конфиг ноды
- Гости видят саму ноду как собственную, но это виртуальная нода
- Первый админ подключается по RANDOM_PIN
- Остальные подключаются по GUEST_PIN (фиксированный, установленный админом)

### 3. ACCESS_NODE (будущее)
```
Веб-браузер #1 → Веб-сервер × Bluetooth → Физическая нода → Меш
Веб-браузер #2 ↗
```
- Веб-интерфейс вместо мобильного приложения
- Сервер на устройстве с Bluetooth подключением к ноде

---

## 🏗️ Архитектура решения (MVP SHARED_NODE)

### Слои обработки пакетов

```
┌─────────────────────────────────────────────────┐
│ PhoneAPI (множество инстансов)                  │
│ - Admin: ConnectionMode = ADMIN                 │
│ - Guest#1: ConnectionMode = GUEST, vNodeId=0x0A │
│ - Guest#2: ConnectionMode = GUEST, vNodeId=0x0B │
└──────────────┬──────────────────────────────────┘
               │
        ┌──────▼──────────┐
        │ MeshService     │ ◄─ Интеграция точка #1
        └──────┬──────────┘
               │
┌──────────────▼────────────────────────┐
│ VirtualNodeManager (НОВЫЙ)            │
│ - Отслеживание 3+ подключений         │
│ - Переписывание node_id в пакетах     │ ◄─ Ключевая логика
│ - Маршрутизация к нужному гостю       │
└──────────────┬────────────────────────┘
               │ (преобразованные пакеты)
        ┌──────▼──────────┐
        │ Router          │ ◄─ Интеграция точка #2
        └──────┬──────────┘
               │
        ┌──────▼──────────┐
        │ RadioInterface  │
        │ LoRa Hardware   │
        └──────────────────┘
        
        ↕ (Меш сеть)
        
        ┌──────┬──────────────────────────┐
        │ Входящие пакеты из радио       │
        └──────┬──────────────────────────┘
               │
        ┌──────▼──────────┐
        │ Router.sendLocal│
        └──────┬──────────┘    ◄─ Интеграция точка #2
               │
        ┌──────▼────────────────────────────┐
        │ VirtualNodeManager.handleIncoming │
        │ - Распределить пакет гостям       │
        │ - Переписать TO = virtualNodeId   │
        └──────┬────────────────────────────┘
               │
        ┌──────┴──────┬──────────┐
        │             │          │
   Admin(0x00) Guest1(0x0A) Guest2(0x0B)
    PhoneAPI#1 PhoneAPI#2 PhoneAPI#3
```

---

## 📋 Что менять (Файлы)

### P0 - Критические (Неделя 1-2)

#### 🆕 Создать: `src/mesh/VirtualNodeManager.h/cpp`
Главный класс для управления виртуальными нодами:
- Регистрация админа и гостей
- Маршрутизация пакетов между ними
- Реписание node_id
- Контроль доступа

**Размер:** ~400-500 строк кода

#### ✏️ Модифицировать: `src/mesh/PhoneAPI.h/cpp`
Добавить поддержмку режимов:
- `enum ConnectionMode { CONN_NONE, ADMIN, GUEST }`
- Методы аутентификации: `authenticateAsAdmin()`, `authenticateAsGuest()`
- Методы контроля доступа: `isAdmin()`, `isGuest()`
- Препроцессинг пакетов перед отправкой

**Измен**+**:** ~50-100 строк

#### ✏️ Модифицировать: `src/mesh/MeshService.h/cpp`
Интеграция VirtualNodeManager:
- Создать глобальный экземпляр менеджера
- Вызвать `virtualNodeManager->handleOutgoingPacket()` в `handleToRadio()`
- Передать конфиг режима при инициализации

**Изменений:** ~20-30 строк

#### ✏️ Модифицировать: `src/mesh/Router.h/cpp`
Перехват пакетов:
- В `sendLocal()` вызвать `virtualNodeManager->handleIncomingPacket()`
- Для гостей переписать `from` на физический node_id

**Изменений:** ~30-40 строк

#### ✏️ Модифицировать: `protobufs/meshtastic/module_config.proto`
Добавить новую конфигурацию:
```proto
message SharedNodeConfig {
  SharedNodeMode mode = 1;
  fixed32 guest_pin = 2;
  int32 max_guests = 3;
}
```

**Изменений:** ~10-15 строк + регенерация (`./bin/regen-protos.sh`)

---

### P1 - Важные (Неделя 3-4)

#### ✏️ Модифицировать: `src/mesh/NodeDB.h/cpp`
Хранение гостей:
- `struct GuestRecord { guestId, displayName, registerTime }`
- `registerGuest()`, `unregisterGuest()`, `isGuestRegistered()`
- Сохранение/загрузка из НВП

**Изменений:** ~70-100 строк

#### ✏️ Модифицировать: `src/mesh/Channels.h/cpp`
Контроль доступа:
- `canModifyChannel(PhoneAPI*, channelIdx)`
- Проверка в обработчиках админ-команд

**Изменений:** ~20-30 строк

#### ✏️ Модифицировать: `src/nimble/NimbleBluetooth.cpp`
Множество подключений (ESP32):
- Менять архитектуру BLE callback'ов
- Поддерживать `std::vector<ActiveConnection>`
- Создавать отдельный `PhoneAPI` для каждого подключения

**Изменений:** ~100-150 строк

#### ✏️ Модифицировать: `src/NRF52Bluetooth.cpp`
Множество подключений (nRF52):
- Аналогично ESP32

**Изменений:** ~100-150 строк

---

## 🔑 Ключевые точки интеграции

### 1. В MeshService/main.cpp
```cpp
// После инициализации MeshService
virtualNodeManager = std::make_unique<VirtualNodeManager>();
virtualNodeManager->setMode((SharedNodeMode)config.shared_node.mode);
```

### 2. В Router::sendLocal() - входящий пакет
```cpp
// После получения пакета из радио
virtualNodeManager->handleIncomingPacket(p);
// Менеджер сам отправит нужным гостям
return ERRNO_OK;
```

### 3. В MeshService::handleToRadio() - исходящий пакет
```cpp
// Перед отправкой в радио
virtualNodeManager->handleOutgoingPacket(p, sourcePhoneAPI);
// Позволить менеджеру переписать from если это гость
```

### 4. В PhoneAPI::handleAdminMessageForDevice()
```cpp
// Контроль доступа
if (!this->isAdmin() && isConfigModifyingCommand()) {
  return;  // Молча отклонить для гостей
}
```

### 5. В BLE инициализации (NimbleBluetooth, NRF52Bluetooth)
```cpp
// Вместо одного подключения - вектор
std::vector<ActiveConnection> connections;

// При новом подключении
PhoneAPI* api = createNewPhoneAPI();
virtualNodeManager->connectAsAdmin(api);  // или connectAsGuest
```

---

## 🧪 Простой тестовый сценарий

1. Скомпилировать с флагом:
   ```bash
   pio run -e esp32_diy -D MESHTASTIC_ENABLE_SHARED_NODE=1
   ```

2. Загрузить прошивку на устройство

3. Через веб-интерфейс установить:
   - Mode = "Shared Node"
   - Guest PIN = 987654
   - Save & Reboot

4. Протестировать:
   - [ ] Админское приложение подключается с RANDOM_PIN
   - [ ] Админ может менять конфиг ноды
   - [ ] Второе приложение подключается с PIN 987654 (гостевое)
   - [ ] Гость видит эту же сеть но как "свою ноду"
   - [ ] Гость НЕ может менять конфиг ноды
   - [ ] Пакеты корректно маршрутизируются между ними

---

## 📚 Документация

- **[IMPLEMENTATION_GUIDE.md](IMPLEMENTATION_GUIDE.md)** - Пошаговая инструкция для каждого файла  
- **[VIRTUALNODEMANAGER_DESIGN.md](VIRTUALNODEMANAGER_DESIGN.md)** - Детальная архитектура класса  
- **[QUICK_REFERENCE.md](QUICK_REFERENCE.md)** - Быстрый справочник  
- **[PURPOSE.md](PURPOSE.md)** - Исходное описание задачи  

---

## 📊 Timeline (приблизительный)

```
Неделя 1-2: Фаза Foundation
├─ VirtualNodeManager (базовая структура)
├─ PhoneAPI (ConnectionMode)
├─ Router интеграция
├─ Proto конфигурация
└─ Базовая маршрутизация

Неделя 3: Фаза Guest Management  
├─ GUEST_PIN аутентификация
├─ NodeDB гостей
├─ Контроль доступа
└─ UI управления гостями

Неделя 4: Фаза BLE Multiplexing
├─ NimbleBluetooth множество подключений
├─ NRF52Bluetooth множество подключений
└─ Тестирование с несколькими клиентами

Неделя 5: Testing & Polish
├─ Unit тесты VirtualNodeManager
├─ Интеграционные тесты
├─ Документация примеров
└─ Bug fixes
```

---

## ⚠️ Важные замечания

1. **Обратная совместимость:**
   - DEFAULT режим = 100% текущее поведение
   - Приложение клиента не нужно менять
   - SHARED_NODE включается явно в конфиге

2. **Потокобезопасность:**
   - VirtualNodeManager использует `concurrency::Lock`
   - Вызывается из BLE, Router и MeshService потоков
   - Минимальное время под блокировкой

3. **Node ID vs Virtual Node ID:**
   - Физический узел: `myNodeInfo.my_node_num` (8 байт)
   - Виртуальный ID: локальный (1-254)
   - Переписывание происходит прозрачно

4. **Memory:**
   - Один новый класс (~500 строк)
   - Вектор из 3-4 подключений в памяти (pequeno overhead)
   - Гостей хранить в NVRAM (NodeDB)

---

## 🚀 Начало работы

1. Прочитать [IMPLEMENTATION_GUIDE.md](IMPLEMENTATION_GUIDE.md) полностью
2. Создать VirtualNodeManager по [VIRTUALNODEMANAGER_DESIGN.md](VIRTUALNODEMANAGER_DESIGN.md)
3. Интегрировать в PhoneAPI, Router, MeshService
4. Модифицировать proto и регенерировать
5. Добавить конфиг в BLE слой
6. Тестировать по сценарию выше

