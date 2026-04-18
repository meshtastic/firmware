# VirtualNodeManager: Detailed Class Design

## Назначение

Управление виртуальными нодами, маршрутизация пакетов между физической нодой и несколькими одновременно подключенными приложениями.

---

## Структуры данных

### Enum для режимов

```cpp
namespace meshtastic {

enum SharedNodeMode : uint8_t {
    MODE_DEFAULT = 0,      // Один админ (обычный режим)
    MODE_SHARED_NODE = 1,  // Один админ + гости
    MODE_ACCESS_NODE = 2   // Веб-интерфейс (будущее)
};

struct VirtualNodeInfo {
    PhoneAPI* phoneAPI;              // Указатель на API клиента
    NodeNum virtualNodeId;           // Виртуальный ID этой "ноды" (1-254, 0=физическая, 255=broadcast)
    uint32_t createdTime;            // Когда подключился (для дебага)
    bool isAdmin;                    // true = может менять конфиг, false = только свой userInfo
    std::string guestId;             // Для гостей: уникальный идентификатор (пустой для админа)
};

} // namespace meshtastic
```

---

## Класс VirtualNodeManager

```cpp
namespace meshtastic {

class VirtualNodeManager {
  private:
    SharedNodeMode currentMode;
    std::vector<VirtualNodeInfo> virtualNodes;  // Активные подключения
    NodeNum nextVirtualNodeId = 1;              // Счетчик для генерации ID
    concurrency::Lock nodeLock;                 // Для thread-safe доступа к вектору
    
    // Конфигурация из NVRAM
    uint32_t guestPin;
    uint32_t adminPin;
    int32_t maxConcurrentGuests;
    
    // Внутренние методы
    NodeNum generateVirtualNodeId();
    VirtualNodeInfo* findVirtualNodeByApi(PhoneAPI* api);
    PPhoneAPI* findApiByVirtualNodeId(NodeNum vNodeId);
    bool matchesGuestPin(uint32_t pin);
    bool matchesAdminPin(uint32_t pin);
    
  public:
    VirtualNodeManager();
    ~VirtualNodeManager();
    
    // === УПРАВЛЕНИЕ РЕЖИМОМ ===
    
    /// Установить режим работы
    void setMode(SharedNodeMode mode);
    SharedNodeMode getMode() const { return currentMode; }
    
    // === УПРАВЛЕНИЕ ПОДКЛЮЧЕНИЯМИ ===
    
    /// Попытка подключиться как админ
    /// Параметры: PhoneAPI* api - новое подключение
    /// Возврат: true если удалось, false если админ уже подключен
    bool connectAsAdmin(PhoneAPI* api);
    
    /// Попытка подключиться как гость
    /// Параметры:
    ///   api - новое подключение
    ///   guestPin - PIN для аутентификации
    /// Возврат: true если удалось, false если (админ не разрешил, лимит, неверный PIN)
    bool connectAsGuest(PhoneAPI* api, uint32_t guestPin);
    
    /// Отключить клиента
    void disconnect(PhoneAPI* api);
    
    // === КОНТРОЛЬ ДОСТУПА ===
    
    /// Может ли этот клиент менять конфигурацию ноды?
    bool canModifyNodeConfig(const PhoneAPI* api) const;
    
    /// Может ли этот клиент менять свои пользовательские данные (owner info)?
    bool canModifyOwnUserData(const PhoneAPI* api) const;
    
    /// Может ли этот клиент менять ченнелы?
    bool canModifyChannels(const PhoneAPI* api) const;
    
    /// Может ли этот клиент управлять гостями?
    bool canManageGuests(const PhoneAPI* api) const;
    
    // === МАРШРУТИЗАЦИЯ ПАКЕТОВ ===
    
    /// Обработка пакета ИЗ меша (входящий)
    /// Позиция вызова: Router::sendLocal() после получения из радио
    /// Задача: если пакет для нас - распределить нужным гостям
    void handleIncomingPacket(meshtastic_MeshPacket* p);
    
    /// Обработка пакета В меш (исходящий)
    /// Позиция вызова: MeshService::handleToRadio() перед отправкой в радио
    /// Задача: если пакет от гостя - переписать source node_id на физический
    void handleOutgoingPacket(meshtastic_MeshPacket* p, PhoneAPI* source);
    
    /// Обработка специальных администраторских команд
    /// Позиция вызова: PhoneAPI::handleAdminMessageForDevice()
    /// Возврат: true если команда обработана здесь (и не нужно делать стандартную обработку)
    bool handleAdminCommand(const PhoneAPI* source, 
                           const meshtastic_AdminMessage& request);
    
    // === УТИЛИТЫ ===
    
    /// Получить количество активных подключений
    int getConnectionCount() const;
    
    /// Получить информацию о виртуальной ноде по API
    const VirtualNodeInfo* getVirtualNodeInfo(const PhoneAPI* api) const;
    
    /// Получить список всех виртуальных нод (для дебага)
    std::vector<VirtualNodeInfo> getAllVirtualNodes() const;
    
    /// Логирование состояния
    void logStatus();
};

} // namespace meshtastic
```

---

## Примеры реализации ключевых методов

### `connectAsAdmin()`

```cpp
bool VirtualNodeManager::connectAsAdmin(PhoneAPI* api) {
    concurrency::LockGuard guard(nodeLock);
    
    // Проверить что админа еще нет
    for (const auto& vn : virtualNodes) {
        if (vn.isAdmin) {
            LOG_INFO("Admin already connected, rejecting new connection");
            return false;  // Админ уже подключен
        }
    }
    
    // Добавить админа
    if (currentMode == MODE_SHARED_NODE) {
        // В SHARED_NODE админ = физическая нода (ID 0)
        virtualNodes.push_back({
            api,
            0,  // Админ всегда ID 0 (это реальная нода)
            millis(),
            true,  // isAdmin
            ""     // guestId пусто
        });
    } else {
        // В DEFAULT - это единственное подключение
        virtualNodes.push_back({
            api,
            0,
            millis(),
            true,
            ""
        });
    }
    
    LOG_INFO("Admin connected (virtualNodeId=0)");
    return true;
}
```

### `connectAsGuest()`

```cpp
bool VirtualNodeManager::connectAsGuest(PhoneAPI* api, uint32_t suppliedPin) {
    concurrency::LockGuard guard(nodeLock);
    
    if (currentMode != MODE_SHARED_NODE) {
        LOG_WARN("Guest connection attempt in non-SHARED_NODE mode");
        return false;
    }
    
    // Проверить наличие админа
    bool adminExists = false;
    for (const auto& vn : virtualNodes) {
        if (vn.isAdmin) {
            adminExists = true;
            break;
        }
    }
    if (!adminExists) {
        LOG_WARN("Guest cannot connect: no admin online");
        return false;
    }
    
    // Проверить PIN
    if (suppliedPin != guestPin) {
        LOG_WARN("Guest connection failed: invalid PIN");
        return false;
    }
    
    // Проверить лимит гостей
    int guestCount = 0;
    for (const auto& vn : virtualNodes) {
        if (!vn.isAdmin) guestCount++;
    }
    if (guestCount >= maxConcurrentGuests) {
        LOG_WARN("Guest connection rejected: max guests reached (%d)", maxConcurrentGuests);
        return false;
    }
    
    // Сгенерировать новый виртуальный ID для гостя (1..254)
    NodeNum newVirtualId = generateVirtualNodeId();
    
    std::string newGuestId = "guest_" + std::to_string(api->getNodeId()) + "_" + 
                             std::to_string(millis());
    
    virtualNodes.push_back({
        api,
        newVirtualId,
        millis(),
        false,  // isAdmin=false
        newGuestId
    });
    
    LOG_INFO("Guest connected: virtualNodeId=%d, guestId=%s", newVirtualId, newGuestId.c_str());
    return true;
}
```

### `handleOutgoingPacket()`

```cpp
void VirtualNodeManager::handleOutgoingPacket(meshtastic_MeshPacket* p, PhoneAPI* source) {
    concurrency::LockGuard guard(nodeLock);
    
    if (currentMode == MODE_DEFAULT) {
        // В DEFAULT режиме не трогаем пакеты
        return;
    }
    
    // Найти информацию об отправителе
    VirtualNodeInfo* senderInfo = nullptr;
    for (auto& vn : virtualNodes) {
        if (vn.phoneAPI == source) {
            senderInfo = &vn;
            break;
        }
    }
    
    if (!senderInfo) {
        LOG_WARN("handleOutgoingPacket: sender not found in virtual nodes");
        return;  // Странно, но игнорируем
    }
    
    // КЛЮЧЕВОЙ ШАГ: переписать source node_id если это гость
    if (!senderInfo->isAdmin) {
        // Пакет от гостя - переписать source с виртуального на физический
        uint32_t physicalNodeId = myNodeInfo.my_node_num;
        
        LOG_DEBUG("Rewriting package from guest: guest_nodeId=0x%x -> physical_nodeId=0x%x",
                  senderInfo->virtualNodeId, physicalNodeId);
        
        p->from = physicalNodeId;
        
        // Сохранить оригинальный ID в контексте для обратной маршрутизации
        // (Это можно делать через дополнительное поле в packet прилепив его)
        // На данный момент - просто логируем
    }
    
    // Админ - пакет идет как от физической ноды (0x%x = physicalNodeId)
}
```

### `handleIncomingPacket()`

```cpp
void VirtualNodeManager::handleIncomingPacket(meshtastic_MeshPacket* p) {
    concurrency::LockGuard guard(nodeLock);
    
    if (currentMode == MODE_DEFAULT) {
        // В DEFAULT режиме - это админ, отправляем естественным путем
        return;
    }
    
    // В SHARED_NODE режиме:
    // Пакет адресован нам (p->to == myNodeInfo.my_node_num)
    // Нужно определить: это для админа, для всех, или для конкретного гостя?
    
    // ЛОГИКА:
    // - Если это DM (p->want_ack == true или прямое сообщение) - 
    //   это может быть для конкретного гостя
    // - Если это broadcast - отправить всем
    // - Админ всегда получает все
    
    // Определить получателей
    std::vector<VirtualNodeInfo*> recipients;
    
    if (p->to == NODENUM_BROADCAST) {
        // Broadcast - отправить всем
        for (auto& vn : virtualNodes) {
            recipients.push_back(&vn);
        }
    } else if (p->to == myNodeInfo.my_node_num) {
        // Прямое сообщение нам
        // На данный момент - отправить админу
        // (В будущем можно внедрить логику определения целевого гостя)
        for (auto& vn : virtualNodes) {
            if (vn.isAdmin) {
                recipients.push_back(&vn);
                break;
            }
        }
    }
    
    // Отправить пакет нужным клиентам
    for (auto recipient : recipients) {
        // Для гостей - переписать TO на их виртуальный ID
        meshtastic_MeshPacket pCopy = *p;
        
        if (!recipient->isAdmin) {
            // Это гость - переписать целевой ID на виртуальный
            pCopy.to = recipient->virtualNodeId;
            
            LOG_DEBUG("Routing TO guest: virtualNodeId=0x%x", recipient->virtualNodeId);
        }
        
        // Отправить пакет через PhoneAPI этого клиента
        recipient->phoneAPI->sendToRadio(&pCopy);
    }
}
```

### `canModifyNodeConfig()`

```cpp
bool VirtualNodeManager::canModifyNodeConfig(const PhoneAPI* api) const {
    concurrency::LockGuard guard(nodeLock);
    
    // Найти клиента в списке
    for (const auto& vn : virtualNodes) {
        if (vn.phoneAPI == api) {
            // Только админ может менять конфиг
            return vn.isAdmin;
        }
    }
    
    return false;  // Клиент не найден - нельзя
}
```

---

## Интеграционные точки в других файлах

### MeshService::constructor()
```cpp
virtualNodeManager = std::make_unique<VirtualNodeManager>();

// При загрузке конфига
if (config.has_shared_node) {
    virtualNodeManager->setMode((SharedNodeMode)config.shared_node.mode);
}
```

### Router::sendLocal()
```cpp
ErrorCode Router::sendLocal(meshtastic_MeshPacket *p, RxSource src) {
    // ...
    
    if (src == RX_SRC_RADIO) {
        // Входящий пакет из меша
        virtualNodeManager->handleIncomingPacket(p);
        // Выход - пакет уже отправлен гостям
        return ERRNO_OK;
    }
    
    // ...обычная обработка...
}
```

### MeshService::handleToRadio()
```cpp
ErrorCode MeshService::handleToRadio(meshtastic_ToRadio *p) {
    // ...
    
    // Перед отправкой в радио
    virtualNodeManager->handleOutgoingPacket(decoded, sourcePhoneAPI);
    
    // ...отправка в радио...
}
```

### PhoneAPI::handleAdminMessageForDevice()
```cpp
void PhoneAPI::handleAdminMessageForDevice(const meshtastic_AdminMessage &request,
                                          meshtastic_AdminMessage *response) {
    
    // НОВОЕ: позволить VirtualNodeManager обработать команду
    if (virtualNodeManager->handleAdminCommand(this, request)) {
        return;  // Команда обработана менеджером
    }
    
    // ЗАЩИТА: если это гость - проверить доступ
    if (!virtualNodeManager->canModifyNodeConfig(this)) {
        // Гость не может менять конфиг
        if (request->which_payload_variant == meshtastic_AdminMessage_set_channel_tag ||
            request->which_payload_variant == meshtastic_AdminMessage_set_device_config_tag) {
            LOG_INFO("Guest attempted config modification");
            return;  // Молча отклонить
        }
    }
    
    // ...обычная обработка для админа...
}
```

---

## Threading и безопасность

### Потоки, в которых используется:

1. **BLE CallBack Thread** (из NimBLE)
   - вызывает `connectAsAdmin()`, `connectAsGuest()`, `disconnect()`
   - потокоопасно: используется `LockGuard`

2. **Mesh Router Thread**
   - вызывает `handleIncomingPacket()`
   - потокоопасно: используется `LockGuard`

3. **PhoneAPI Handler Thread**
   - вызывает `handleOutgoingPacket()`, `canModifyNodeConfig()`, etc
   - потокоопасно: используется `LockGuard`

### Lock Strategy:

```cpp
// Минимальное время под блокировкой
{
    concurrency::LockGuard guard(nodeLock);
    // Только критические операции с vectorом
    for (auto& vn : virtualNodes) { /* ... */ }
}
// Lock отпущен - исключаем deadlock
```

---

## Debug/Logging

```cpp
void VirtualNodeManager::logStatus() {
    concurrency::LockGuard guard(nodeLock);
    
    LOG_INFO("=== VirtualNodeManager Status ===");
    LOG_INFO("Mode: %d", currentMode);
    LOG_INFO("Active connections: %zu", virtualNodes.size());
    
    for (const auto& vn : virtualNodes) {
        const char* role = vn.isAdmin ? "ADMIN" : "GUEST";
        LOG_INFO("  - %s (vNodeId=0x%x, uptime=%lus)",
                 role, vn.virtualNodeId, (millis() - vn.createdTime) / 1000);
    }
    
    LOG_INFO("================================");
}
```

---

## Примеры тестирования

```cpp
// Юнит тест: подключение админа
TEST(VirtualNodeManager, AdminConnection) {
    VirtualNodeManager mgr;
    mgr.setMode(MODE_SHARED_NODE);
    
    MockPhoneAPI admin_api;
    EXPECT_TRUE(mgr.connectAsAdmin(&admin_api));
    EXPECT_FALSE(mgr.connectAsAdmin(&admin_api));  // Второй админ отклонен
}

// Юнит тест: подключение гостя
TEST(VirtualNodeManager, GuestConnection) {
    VirtualNodeManager mgr;
    mgr.setMode(MODE_SHARED_NODE);
    
    MockPhoneAPI admin_api, guest_api;
    mgr.setGuestPin(123456);
    
    mgr.connectAsAdmin(&admin_api);
    EXPECT_TRUE(mgr.connectAsGuest(&guest_api, 123456));
    EXPECT_FALSE(mgr.connectAsGuest(&guest_api, 999999));  // Неверный PIN
}

// Интеграционный тест: маршрутизация пакета
TEST(VirtualNodeManager, PacketRouting) {
    VirtualNodeManager mgr;
    mgr.setMode(MODE_SHARED_NODE);
    
    MockPhoneAPI admin_api, guest_api;
    mgr.connectAsAdmin(&admin_api);
    mgr.connectAsGuest(&guest_api, 123456);
    
    // Имитировать пакет из меша
    meshtastic_MeshPacket p = {0};
    p.to = myNodeInfo.my_node_num;  // Для нас
    
    mgr.handleIncomingPacket(&p);
    
    // Ожидаем вызов sendToRadio для обоих клиентов
    EXPECT_EQ(admin_api.sent_packets.size(), 1);
    EXPECT_EQ(guest_api.sent_packets.size(), 1);
}
```

