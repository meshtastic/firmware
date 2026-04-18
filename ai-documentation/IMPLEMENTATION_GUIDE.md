# SHARED_NODE Implementation Guide

## Обзор

Три режима:
- **DEFAULT** - стандартное подключение (физическая нода = пользовательская нода)
- **SHARED_NODE** - финальная архитектура (одна физическая нода = множество виртуальных с разными пользователями)
- **ACCESS_NODE** - веб-интерфейс вместо BLE приложений (опционально)

## Ключевые компоненты для создания/изменения

### 1️⃣ НОВЫЙ: `src/mesh/VirtualNodeManager.h/cpp`

**Назначение:** Управление виртуальными нодами и маршрутизация пакетов между ними

```cpp
// Структура
enum SharedNodeMode { 
  MODE_DEFAULT,      // Обычное поведение
  MODE_SHARED_NODE,  // Множество гостей через PIN
  MODE_ACCESS_NODE   // Веб-интерфейс
};

struct VirtualNode {
  PhoneAPI* api;
  NodeNum virtualNodeId;    // Уникальный ID в рамках физической ноды
  uint32_t vialconnectTime;
  bool isAdmin;
  std::string guestId;
};

class VirtualNodeManager {
  // Управление режимом
  void setMode(SharedNodeMode mode);
  
  // Управление подключениями
  bool onAdminConnect(PhoneAPI* api);
  bool onGuestConnect(PhoneAPI* api, const std::string_view guestPin);
  void onDisconnect(PhoneAPI* api);
  
  // Маршрутизация пакетов
  void handleIncomingPacket(meshtastic_MeshPacket* p);  // Из меша в виртуальные ноды
  void handleOutgoingPacket(meshtastic_MeshPacket* p, PhoneAPI* source);  // Из приложения в меш
  
  // Конфигурация доступа
  bool canModifyConfig(PhoneAPI* api);  // Только админ
  bool canModifyOwner(PhoneAPI* api);   // Админ + каждый гость может менять свой
  
  // Утилиты
  PhoneAPI* findApiForVirtualNode(NodeNum vNodeId);
  NodeNum generateVirtualNodeId();
};
```

**Где подключить:**
- `MeshService::handleToRadio()` - перехватить пакет перед отправкой
- `Router::sendLocal()` - перехватить входящий пакет из меша
- `PhoneAPI::handleAdminMessageForDevice()` - контроль доступа

---

### 2️⃣ МОДИФИЦИРОВАТЬ: `src/mesh/PhoneAPI.h/cpp`

**Текущее состояние:** Одно подключение, требует админ PIN

**Что добавить:**
```cpp
enum ConnectionMode {
  CONN_MODE_NONE,
  CONN_MODE_ADMIN,      // Подключено как админ (RANDOM_PIN)
  CONN_MODE_GUEST       // Подключено как гость (GUEST_PIN)
};

class PhoneAPI {
  ConnectionMode connectionMode = CONN_MODE_NONE;
  std::string associatedGuestId;  // Если гость - его ID
  
  // Новые методы
  bool authenticateAsAdmin(uint32_t supplied_pin);
  bool authenticateAsGuest(uint32_t supplied_pin);
  
  // Вызывается перед отправкой пакета
  void preprocessOutgoingPacket(meshtastic_MeshPacket* p);
  
  // Контроль доступа к функциям
  bool isAdmin() const { return connectionMode == CONN_MODE_ADMIN; }
  bool isGuest() const { return connectionMode == CONN_MODE_GUEST; }
};
```

**Где менять:**
- Конструктор - инициализация `connectionMode`
- `handleAdminMessageForDevice()` - проверка `isAdmin()` перед выполнением команд
- Перед `sendToRadio()` - вызвать `preprocessOutgoingPacket()`

**Примеры:**
```cpp
// В handleAdminMessageForDevice():
if (request->which_payload_variant == meshtastic_AdminMessage_set_owner_tag) {
  if (!isAdmin()) {
    // Гость может менять только свой owner
    if (!canModifyOwnOwner()) return;
  }
  // ... обработка
}

// В preprocess:
if (isGuest()) {
  p->from = virtualNodeManager->getVirtualNodeIdForApi(this);
} else {
  p->from = myNodeInfo.my_node_num;
}
```

---

### 3️⃣ МОДИФИЦИРОВАТЬ: `src/mesh/MeshService.h/cpp`

**Текущее состояние:** Центральный сервис для управления мешем

**Что добавить:**
```cpp
class MeshService {
  VirtualNodeManager virtualNodeManager;
  
  // Вызывается при incoming пакете из радио
  virtual ErrorCode handleToRadio(meshtastic_ToRadio *p) override {
    // ... старый код
    
    // НОВОЕ: отправить пакет в VirtualNodeManager для маршрутизации гостям
    virtualNodeManager.handleIncomingPacket(decoded);
  }
  
  // Вызывается перед отправкой в радио
  void preProcessPacket(meshtastic_MeshPacket* p) {
    virtualNodeManager.handleOutgoingPacket(p);
  }
};
```

**Архитектурные точки:**
- При инициализации MeshService:
  ```cpp
  // Создать глобальный экземпляр менеджера
  virtualNodeManager = new VirtualNodeManager();
  virtualNodeManager->setMode(config.shared_node.mode);
  ```

---

### 4️⃣ МОДИФИЦИРОВАТЬ: `src/mesh/Router.h/cpp`

**Текущее состояние:** Маршрутизирует пакеты из радио в API клиентов

**Что менять:**

В методе `Router::handleReceived()`:
```cpp
void Router::handleReceived(meshtastic_MeshPacket *p) {
  // ... старый код обработки
  
  if (p->to == myNodeInfo.my_node_num) {
    // НОВОЕ: пакет адресован нам - проверить режим
    virtualNodeManager->handleIncomingPacket(p);
    // Теперь менеджер сам отправит пакет нужному гостю
    return;
  }
}
```

В методе `Router::sendLocal()`:
```cpp
ErrorCode Router::sendLocal(meshtastic_MeshPacket *p, RxSource src) {
  if (src == RX_SRC_LOCAL) {
    // Пакет из приложения через PhoneAPI
    // Позволить VirtualNodeManager модифицировать его перед отправкой
    virtualNodeManager->handleOutgoingPacket(p, sourcePhoneAPI);
  }
  // ... дальше обычная отправка в радио
}
```

---

### 5️⃣ МОДИФИЦИРОВАТЬ: `src/mesh/NodeDB.h/cpp`

**Текущее состояние:** Хранит конфиг ноды и информацию о соседних нодах

**Что добавить:**

```cpp
class NodeDB {
  struct GuestRecord {
    std::string guestId;
    std::string displayName;
    uint32_t registerTime;
    uint32_t lastSeen;
    bool enabled;
  };
  
  std::vector<GuestRecord> guests;  // Зарегистрированные гости
  
  // Методы управления гостями
  bool registerGuest(const std::string& guestId, const std::string& displayName);
  bool unregisterGuest(const std::string& guestId);
  bool isGuestRegistered(const std::string& guestId);
  std::vector<GuestRecord> listGuests();
  
  // Сохранение/загрузка из НВП
  void saveGuestList();
  void loadGuestList();
};
```

**Где менять:**
- При загрузке конфига - загрузить список гостей из НВП
- При отключении гостя - обновить `lastSeen`
- Методы сохранения НВП

---

### 6️⃣ МОДИФИЦИРОВАТЬ: `src/mesh/Channels.h/cpp`

**Текущее состояние:** Управление каналами меша и их параметрами

**Что менять:**

```cpp
class Channels {
  // Добавить метод для проверки доступа
  bool canModifyChannel(const PhoneAPI& api, uint8_t channelIdx) {
    if (api.isAdmin()) return true;  // Админ может всё
    if (api.isGuest()) return false; // Гость не может менять каналы
    return true;
  }
};
```

**В обработчиках админ-команд:**
```cpp
// В PhoneAPI::handleAdminMessageForDevice()
case meshtastic_AdminMessage_set_channel_tag: {
  if (!channels.canModifyChannel(*this, request->channel_index)) {
    LOG_WARN("Guest attempted to modify channel");
    return;  // Молча игнорировать
  }
  // ... обычная обработка
}
```

---

### 7️⃣ МОДИФИЦИРОВАТЬ: `protobufs/meshtastic/module_config.proto`

**Добавить новую конфигурацию:**

```proto
enum SharedNodeMode {
  MODE_DEFAULT = 0;
  MODE_SHARED_NODE = 1;
  MODE_ACCESS_NODE = 2;
}

message SharedNodeConfig {
  SharedNodeMode mode = 1;
  fixed32 guest_pin = 2;        // PIN для гостей (только в SHARED_NODE)
  bool admin_locked = 3;        // Запретить гостям менять конфиг
  int32 max_guests = 4;         // Максимум одновременных гостей (0 = без ограничений)
  bool require_guest_approval = 5;  // Требовать одобрения админом для новых гостей
}

message ModuleConfig {
  // ... existing fields ...
  SharedNodeConfig shared_node = 50;  // Новое поле в конце
}
```

**После изменения proto:**
```bash
./bin/regen-protos.sh
```

---

### 8️⃣ МОДИФИЦИРОВАТЬ: `src/nimble/NimbleBluetooth.cpp` (ESP32)

**Текущее состояние:** Один BLE клиент за раз

**Что менять:**

```cpp
class NimbleBluetooth {
  struct ActiveConnection {
    uint16_t conn_handle;
    PhoneAPI* api;
  };
  
  std::vector<ActiveConnection> connections;  // Множество подключений
  static const int MAX_CONNECTIONS = 4;
  
  // Callback при новом подключении
  static int onConnect(uint16_t conn_handle, ble_gap_conn_desc *desc) {
    // Вместо замены старого подключения - добавить новое
    if (connections.size() < MAX_CONNECTIONS) {
      PhoneAPI* api = createNewPhoneAPI();
      connections.push_back({conn_handle, api});
    }
  }
  
  // При отключении
  static int onDisconnect(uint16_t conn_handle, ble_gap_conn_desc *desc) {
    auto it = std::find_if(connections.begin(), connections.end(),
      [=](const ActiveConnection& c) { return c.conn_handle == conn_handle; });
    if (it != connections.end()) {
      deletePhoneAPI(it->api);
      connections.erase(it);
    }
  }
};
```

---

### 9️⃣ МОДИФИЦИРОВАТЬ: `src/NRF52Bluetooth.cpp` (nRF52)

**Аналогично ESP32:**
- Поддерживать множество одновременных подключений
- Создавать отдельный PhoneAPI для каждого
- Управлять жизненным циклом подключений

---

### 🔟 КОНФИГУРАЦИЯ: `platformio.ini`

**Добавить флаги для нового функционала (опционально):**

```ini
[env:tbeam]
build_flags =
  # ... existing flags ...
  -D MESHTASTIC_ENABLE_SHARED_NODE=1  ; Включить поддержку SHARED_NODE
  -D MESHTASTIC_MAX_VIRTUAL_NODES=4   ; Максимум виртуальных нод
```

---

## Порядок реализации (этапы)

### Фаза 1: Foundation (Неделя 1-2)
- [ ] Создать VirtualNodeManager (базовая структура)
- [ ] Добавить SharedNodeConfig в proto + регенерировать
- [ ] Модифицировать PhoneAPI для ConnectionMode
- [ ] Интегрировать в MeshService и Router
- [ ] Базовая маршрутизация пакетов

### Фаза 2: Guest Management (Неделя 3)
- [ ] Реализовать GUEST_PIN認証 в PhoneAPI
- [ ] Реализовать хранение гостей в NodeDB
- [ ] Контроль доступа к админ-командам (Channels, Config)
- [ ] UI для управления гостями (админ)

### Фаза 3: BLE Multiplexing (Неделя 4)
- [ ] Модифицировать NimbleBluetooth для множества подключений
- [ ] Модифицировать NRF52Bluetooth для множества подключений
- [ ] Тестирование с несколькими приложениями

### Фаза 4: Testing & Polish (Неделя 5)
- [ ] Unit тесты на VirtualNodeManager
- [ ] Интеграционные тесты с несколькими клиентами
- [ ] Документация и примеры

### Фаза 5: ACCESS_NODE (опционально, Неделя 6+)
- [ ] WebSocket вместо BLE
- [ ] Web UI
- [ ] Server логика


## Логика аутентификации

```
┌─ Устройство пытается подключиться ─┐
│                                      │
└─ Отправляет StartConfig message    ─┘
         │
         ├─> old_pin == RANDOM_PIN  ──> ADMIN подключение
         │                             (setAuthCallsign запросит у админа)
         │
         └─> old_pin == GUEST_PIN    ──> GUEST подключение
                                        (не требует админ интервенции)

В SHARED_NODE режиме:
- RANDOM_PIN = текущий админ PIN (изначально 0)
- GUEST_PIN = фиксированный PIN (настраивается админом)
- Остальные PIN = отклонить с ошибкой "уже есть админ"
```

## Примеры использования

### Для пользователя (приложение):
```
// Ничего не меняется!
// Приложение видит, что это его персональная нода
// Все его настройки хранятся локально в приложении
```

### Для админа (настройка режима):
```
1. В веб-интерфейсе перейти в Settings → Advanced
2. Выбрать Mode = "Shared Node"
3. Установить Guest PIN (например 789456)
4. Save & Reboot
```

### Для гостя (подключение):
```
1. Запустить приложение Meshtastic
2. Выбрать "нашу ноду" при сканировании
3. Введение Guest PIN 789456
4. Готово! Видит туже сеть, но как отдельная нода
```

## Обратная совместимость

- DEFAULT режим = 100% текущее поведение
- Приложение клиента не нужно менять
- Админ может в любой момент вернуть в DEFAULT режим

---

## Заполнение таблицы зависимостей

```
Phase 1 Foundation:
  VirtualNodeManager ---> PhoneAPI
       |                      |
       +----------> MeshService
                        |
                    Router

Phase 2 Guest Management:
  PhoneAPI ---> Channels (доступ)
                    |
                 NodeDB (хранение гостей)

Phase 3 BLE Multiplexing:
  NimbleBluetooth ---> PhoneAPI (создание экземпляров)
                          |
                      VirtualNodeManager (отслеживание)
```

