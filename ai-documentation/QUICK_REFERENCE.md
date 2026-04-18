# SHARED_NODE Quick Reference

## 📋 Таблица файлов для изменения

| # | Файл | Статус | Назначение | Приоритет |
|---|------|--------|-----------|-----------|
| 1 | `src/mesh/VirtualNodeManager.h/cpp` | 🆕 НОВЫЙ | Управление виртуальными нодами и маршрутизация | P0 |
| 2 | `src/mesh/PhoneAPI.h/cpp` | ✏️ Модифицировать | Режимы подключения (админ/гость) | P0 |
| 3 | `src/mesh/MeshService.h/cpp` | ✏️ Модифицировать | Интеграция VirtualNodeManager | P0 |
| 4 | `src/mesh/Router.h/cpp` | ✏️ Модифицировать | Маршрутизация пакетов для виртуальных нод | P0 |
| 5 | `src/mesh/NodeDB.h/cpp` | ✏️ Модифицировать | Хранение зарегистрированных гостей | P1 |
| 6 | `src/mesh/Channels.h/cpp` | ✏️ Модифицировать | Контроль доступа (админ vs гость) | P1 |
| 7 | `protobufs/meshtastic/module_config.proto` | ✏️ Модифицировать | Новая конфигурация режимов | P0 |
| 8 | `src/nimble/NimbleBluetooth.cpp` | ✏️ Модифицировать | Множество BLE подключений (ESP32) | P1 |
| 9 | `src/NRF52Bluetooth.cpp` | ✏️ Модифицировать | Множество BLE подключений (nRF52) | P1 |
| 10 | `bin/regen-protos.sh` | 🔨 Использовать | После изменения .proto файлов | P0 |

---

## 🔧 Минимальный MVP (Quick Start)

Если нужен **только SHARED_NODE режим** (без ACCESS_NODE):

### Обязательные (P0):
1. ✅ Создать `VirtualNodeManager`
2. ✅ Модифицировать `PhoneAPI` - добавить `ConnectionMode`
3. ✅ Модифицировать `Router` - вызвать `VirtualNodeManager` при маршрутизации
4. ✅ Добавить конфиг в proto
5. ✅ Регенерировать proto: `./bin/regen-protos.sh`

### Дополнительные (P1):
- Модифицировать BLE слой для множественных подключений
- Контроль доступа в Channels и NodeDB

---

## 🎯 Ключевые точки интеграции

### В `MeshService::handleToRadio()`
```cpp
// Перехватить пакет перед отправкой в радио
if (config.shared_node.mode == MODE_SHARED_NODE) {
  virtualNodeManager->handleOutgoingPacket(p, sourcePhoneAPI);
}
```

### В `Router::sendLocal()`
```cpp
// Перехватить входящий пакет из меша
if (config.shared_node.mode == MODE_SHARED_NODE) {
  virtualNodeManager->handleIncomingPacket(p);
  return;  // VirtualNodeManager сам отправит гостям
}
```

### В `PhoneAPI::handleAdminMessageForDevice()`
```cpp
// Контроль доступа перед выполнением команды
if (!this->isAdmin() && isConfigModifyingCommand()) {
  return;  // Молча отклонить для гостей
}
```

### В BLE инициализации (NimbleBluetooth)
```cpp
// Вместо замены подключения - добавлять в лист
if (connections.size() < MAX_CONNECTIONS) {
  PhoneAPI* newApi = createNewPhoneAPI();
  connections.push_back({conn_handle, newApi});
}
```

---

## 📊 Data Flow для понимания

### Сценарий 1: Админ отправляет сообщение в меш
```
Admin App
    └─> PhoneAPI (isAdmin=true)
         └─> MeshService::handleToRadio()
              └─> VirtualNodeManager::handleOutgoingPacket()
                   (ничего не меняет, админ = физическая нода)
              └─> Router::sendLocal()
                   └─> RadioInterface
                        └─> LoRa Mesh
```

### Сценарий 2: Гость отправляет сообщение в меш
```
Guest App
    └─> PhoneAPI (isAdmin=false, virtualNodeId=0x0A)
         └─> MeshService::handleToRadio()
              └─> VirtualNodeManager::handleOutgoingPacket()
                   (переписать: from=0x0A, to физическую ноду)
              └─> Router::sendLocal()
                   └─> RadioInterface
                        └─> LoRa Mesh
```

### Сценарий 3: Меш отправляет пакет нам
```
LoRa Mesh
    └─> RadioInterface
         └─> Router::sendLocal() (from=0x12345, to=ourNodeId)
              └─> VirtualNodeManager::handleIncomingPacket()
                   (проверить: это для какого гостя?)
                   (если для гостя - переписать to=virtualNodeId)
              └─> PhoneAPI (Guest #1) ──> Guest App #1
              └─> PhoneAPI (Guest #2) ──> Guest App #2
```

---

## 🧪 Простой тестовый сценарий

1. **Компилируем с SHARED_NODE:**
   ```bash
   pio run -e esp32_diy -D MESHTASTIC_ENABLE_SHARED_NODE=1
   ```

2. **Загружаем на ноду**

3. **Через веб-интерфейс:**
   - Устанавливаем Mode = "Shared Node"
   - Устанавливаем Guest PIN = 987654
   - Save & Reboot

4. **Тестируем:**
   - Подключаемся админским приложением (RANDOM_PIN)
   - Проверяем что можем менять конфиг
   - Подключаемся гостевым приложением (PIN 987654)
   - Проверяем что гость видит свою "ноду" но в той же сетке
   - Проверяем что гость не может менять конфиг

---

## 🛠️ Вспомогательные структуры для копипасты

### VirtualNodeManager инициализация в MeshService:
```cpp
// В конструкторе MeshService
virtualNodeManager = std::make_unique<VirtualNodeManager>();
virtualNodeManager->setMode(static_cast<SharedNodeMode>(config.shared_node.mode));
```

### PhoneAPI проверки доступа шаблон:
```cpp
// Для админ-команд
bool isAllowed = this->isAdmin() || this->canModifyOwnData(command);
if (!isAllowed) {
  LOG_WARN("Guest attempted restricted operation: %d", command);
  return;
}
```

### NodeDB регистрация гостя:
```cpp
// Когда гость успешно аутентифицировался
nodeDB->registerGuest(guestId, connection->displayName);
```

---

## 📚 Дополнительная информация

### Текущая архитектура подключения (что остается)
- BLE Services & Characteristics (От приложения)
- Serial API (для веб-интерфейса)
- WiFi API (для удаленного подключения)

### Что НЕ меняется
- Процесс шифрования/расшифровки пакетов
- Работа меша и маршрутизации
- Чтение/запись конфига в НВП
- Обработка сообщений модулями

### Что ДОБАВЛЯЕТСЯ
- Слой виртуальных нод между приложением и мешем
- Проверка доступа для администратора vs гостя
- Управление списком гостей

---

## ⚠️ Важные замечания

1. **Node ID vs Virtual Node ID**
   - `myNodeInfo.my_node_num` = физический ID (8 байт, выбирается при первом запуске)
   - `virtualNodeId` = локальный ID внутри физической ноды (1-255)
   - Приложение видит `virtualNodeId` как свой `myNodeInfo.my_node_num`

2. **Packet Rewriting Safety**
   - При переписывании node_id в пакете - сохранять это в контексте
   - Для обратной маршрутизации нужно помнить что это виртуальная нода

3. **Thread Safety**
   - VirtualNodeManager вызывается из разных потоков
   - PhoneAPI обрабатывается в отдельном потоке BLE
   - Нужны `Lock` при доступе к `std::vector<VirtualNode>`

4. **Memory Management**
   - PhoneAPI создается PER подключение
   - Нужно удалять при отключении
   - RAII pattern через `unique_ptr` желателен

