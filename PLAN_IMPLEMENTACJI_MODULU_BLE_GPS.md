# Plan Implementacji Modułu BLE GPS dla Meshtastic

## Cel
Stworzenie modułu firmware Meshtastic, który będzie automatycznie wysyłał pozycję GPS przez BLE do aplikacji Android w regularnych odstępach czasu.

## Analiza wymagań

### Z aplikacji Android (BLUETOOTH_MODULE_DOCUMENTATION.md):
- Aplikacja oczekuje danych lokalizacji przez BLE na charakterystyce `TELEMETRY_CHARACTERISTIC_UUID` (`f75c76d2-129e-4dad-a1dd-786f440672e0`)
- Format danych (uproszczony):
  - Byte 0-1: Node ID (2 bytes)
  - Byte 2-5: Latitude (4 bytes, float)
  - Byte 6-9: Longitude (4 bytes, float)
  - Byte 10-13: Altitude (4 bytes, float, opcjonalne)
  - Byte 14-17: Timestamp (4 bytes, uint32)
  - Byte 18: Accuracy (1 byte, opcjonalne)
- **Uwaga:** Dokumentacja wskazuje, że rzeczywisty protokół Meshtastic używa protobuf, więc powinniśmy używać standardowego protokołu Meshtastic

### Z dokumentacji Meshtastic Module API:
- Moduły dziedziczą z `MeshModule`, `SinglePortModule` lub `ProtobufModule`
- Moduły mogą używać `OSThread` do okresowego wykonywania
- `service->sendToPhone(p)` wysyła dane przez BLE do telefonu
- Pozycja GPS jest dostępna przez `gps->p` lub `nodeDB->getMeshNode(nodeDB->getNodeNum())->position`

## Architektura rozwiązania

### Opcja 1: Użycie istniejącego protokołu Meshtastic (ZALECANE)
**Zalety:**
- Kompatybilność z istniejącym protokołem
- Wykorzystanie istniejącej infrastruktury BLE
- Automatyczne serializowanie przez `sendToPhone()`

**Implementacja:**
- Moduł będzie wysyłał pakiety `meshtastic_Position` przez `sendToPhone()`
- Aplikacja Android będzie musiała parsować standardowe pakiety Meshtastic zamiast uproszczonego formatu

### Opcja 2: Własny format danych (NIEZALECANE)
**Wady:**
- Wymaga modyfikacji aplikacji Android
- Bypass standardowego protokołu Meshtastic
- Trudniejsze w utrzymaniu

## Plan implementacji (Opcja 1 - ZALECANA)

### Faza 1: Przygotowanie struktury modułu

#### 1.1. Utworzenie plików modułu
- **Lokalizacja:** `src/modules/BleGpsModule.h` i `src/modules/BleGpsModule.cpp`
- **Baza klas:** 
  - Dziedziczenie z `ProtobufModule<meshtastic_Position>` dla obsługi protobuf
  - Dziedziczenie z `concurrency::OSThread` dla okresowego wykonywania

#### 1.2. Definicja klasy
```cpp
class BleGpsModule : public ProtobufModule<meshtastic_Position>, 
                     private concurrency::OSThread
{
  private:
    uint32_t lastSentToPhone = 0;
    uint32_t sendIntervalMs = 5000; // 5 sekund (można skonfigurować)
    
  public:
    BleGpsModule();
    
  protected:
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, 
                                        meshtastic_Position *p) override;
    virtual int32_t runOnce() override;
    
  private:
    void sendPositionToPhone();
    meshtastic_Position getCurrentPosition();
};
```

### Faza 2: Implementacja funkcjonalności

#### 2.1. Konstruktor modułu
- Inicjalizacja `ProtobufModule` z portem `meshtastic_PortNum_POSITION_APP` (lub `PRIVATE_APP` dla testów)
- Inicjalizacja `OSThread` z nazwą "BleGpsModule"
- Ustawienie początkowego interwału przez `setIntervalFromNow(setStartDelay())`
- Obserwowanie zmian statusu GPS (opcjonalnie)

#### 2.2. Metoda `getCurrentPosition()`
- Pobranie aktualnej pozycji z `nodeDB->getMeshNode(nodeDB->getNodeNum())->position`
- Alternatywnie: użycie `gps->p` jeśli GPS jest aktywny
- Sprawdzenie, czy pozycja jest ważna (`nodeDB->hasValidPosition()`)
- Zwrócenie `meshtastic_Position` z wypełnionymi danymi

#### 2.3. Metoda `sendPositionToPhone()`
- Pobranie aktualnej pozycji przez `getCurrentPosition()`
- Sprawdzenie, czy pozycja jest ważna
- Utworzenie pakietu przez `allocDataProtobuf(position)`
- Ustawienie `p->to = NODENUM_BROADCAST` (nie jest wymagane dla sendToPhone)
- Wywołanie `service->sendToPhone(p)`
- Logowanie operacji

#### 2.4. Metoda `runOnce()` (OSThread)
- Sprawdzenie, czy minął wystarczający czas od ostatniego wysłania
- Sprawdzenie, czy GPS ma ważną pozycję
- Wywołanie `sendPositionToPhone()` jeśli warunki są spełnione
- Zwrócenie `sendIntervalMs` jako interwału do następnego wykonania

#### 2.5. Metoda `handleReceivedProtobuf()`
- Opcjonalna obsługa otrzymanych pakietów pozycji
- Możliwość aktualizacji lokalnej bazy danych
- Zwrócenie `false` aby pozwolić innym modułom przetwarzać pakiet

### Faza 3: Rejestracja modułu

#### 3.1. Modyfikacja `src/modules/Modules.cpp`
- Dodanie `#include "BleGpsModule.h"`
- Dodanie deklaracji zmiennej globalnej: `BleGpsModule *bleGpsModule;`
- W funkcji `setupModules()`:
  ```cpp
  #if !MESHTASTIC_EXCLUDE_BLE_GPS
      bleGpsModule = new BleGpsModule();
  #endif
  ```

#### 3.2. Modyfikacja `src/modules/Modules.h` (jeśli istnieje)
- Dodanie deklaracji zewnętrznej: `extern BleGpsModule *bleGpsModule;`

### Faza 4: Konfiguracja i kompilacja

#### 4.1. Definicja makra wykluczającego (opcjonalnie)
- W plikach konfiguracyjnych dodać możliwość wyłączenia modułu:
  ```cpp
  #ifndef MESHTASTIC_EXCLUDE_BLE_GPS
  #define MESHTASTIC_EXCLUDE_BLE_GPS 0
  #endif
  ```

#### 4.2. Zależności
- Sprawdzenie, czy moduł wymaga GPS: `#if !MESHTASTIC_EXCLUDE_GPS`
- Sprawdzenie, czy BLE jest dostępne: `#if !MESHTASTIC_EXCLUDE_BLUETOOTH`

### Faza 5: Testowanie

#### 5.1. Testy jednostkowe (opcjonalnie)
- Test pobierania pozycji
- Test tworzenia pakietu
- Test interwału wysyłania

#### 5.2. Testy integracyjne
- Kompilacja firmware z modułem
- Flashowanie na urządzenie Meshtastic
- Połączenie z aplikacją Android przez BLE
- Weryfikacja odbierania danych lokalizacji w aplikacji

#### 5.3. Scenariusze testowe
1. **Podstawowy:** Moduł wysyła pozycję co 5 sekund gdy GPS ma fix
2. **Brak GPS:** Moduł nie wysyła gdy GPS nie ma fix
3. **Zmiana pozycji:** Moduł wysyła aktualizacje przy zmianie pozycji
4. **Brak połączenia BLE:** Moduł działa normalnie, pakiety są buforowane

### Faza 6: Optymalizacja (opcjonalnie)

#### 6.1. Konfiguracja interwału
- Dodanie możliwości konfiguracji interwału przez AdminMessage
- Użycie wartości z konfiguracji urządzenia

#### 6.2. Inteligentne wysyłanie
- Wysyłanie tylko gdy pozycja się zmieniła (oprócz okresowych aktualizacji)
- Wysyłanie tylko gdy BLE jest połączone (sprawdzenie `service->isToPhoneQueueEmpty()`)

#### 6.3. Zarządzanie zasobami
- Sprawdzenie dostępności pamięci przed alokacją pakietu
- Obsługa błędów przy wysyłaniu

## Szczegóły techniczne

### Port numery
- **Dla produkcji:** `meshtastic_PortNum_POSITION_APP` (już używany przez PositionModule)
- **Dla testów:** `meshtastic_PortNum_PRIVATE_APP` (256)
- **Uwaga:** Jeśli używamy `POSITION_APP`, pakiety będą również wysyłane do mesh. Jeśli chcemy tylko do telefonu, możemy użyć `PRIVATE_APP`.

### Format danych
- Używamy standardowego `meshtastic_Position` protobuf
- Aplikacja Android będzie musiała parsować pełne pakiety Meshtastic zamiast uproszczonego formatu
- Alternatywnie: możemy wysyłać surowe dane przez `allocDataPacket()` z własnym formatem

### Integracja z istniejącym kodem
- Moduł nie koliduje z `PositionModule` - może współistnieć
- `PositionModule` wysyła do mesh, `BleGpsModule` wysyła tylko do telefonu
- Oba mogą używać tego samego portu `POSITION_APP`

## Modyfikacje aplikacji Android (jeśli potrzebne)

### Opcja A: Parsowanie standardowych pakietów Meshtastic
- Modyfikacja `MeshtasticDataParser.kt` do parsowania pełnych pakietów protobuf
- Użycie biblioteki protobuf dla Meshtastic

### Opcja B: Zachowanie uproszczonego formatu
- Moduł firmware wysyła dane w uproszczonym formacie przez `allocDataPacket()`
- Aplikacja Android pozostaje bez zmian

## Harmonogram implementacji

1. **Dzień 1-2:** Faza 1 i 2 - Utworzenie struktury i podstawowej implementacji
2. **Dzień 3:** Faza 3 - Rejestracja modułu i kompilacja
3. **Dzień 4:** Faza 5 - Testowanie podstawowe
4. **Dzień 5:** Faza 6 - Optymalizacja i testy końcowe

## Potencjalne problemy i rozwiązania

### Problem 1: Konflikt z PositionModule
**Rozwiązanie:** Używamy tego samego portu, ale `PositionModule` obsługuje mesh, a `BleGpsModule` tylko telefon. Możemy też użyć `PRIVATE_APP`.

### Problem 2: Format danych w aplikacji Android
**Rozwiązanie:** Zaimplementować parser pełnych pakietów Meshtastic w aplikacji Android lub użyć uproszczonego formatu w module.

### Problem 3: Wydajność i zużycie baterii
**Rozwiązanie:** 
- Ustawić rozsądny interwał (5-10 sekund)
- Wysyłać tylko gdy pozycja się zmieniła
- Sprawdzać czy BLE jest połączone przed wysyłaniem

### Problem 4: Brak GPS
**Rozwiązanie:** Sprawdzać `gpsStatus->getHasLock()` przed wysyłaniem. Można też wysyłać ostatnią znaną pozycję.

## Pliki do utworzenia/modyfikacji

### Nowe pliki:
- `src/modules/BleGpsModule.h`
- `src/modules/BleGpsModule.cpp`

### Pliki do modyfikacji:
- `src/modules/Modules.cpp` - dodanie rejestracji modułu
- `src/modules/Modules.h` (jeśli istnieje) - deklaracja zewnętrzna

### Pliki do przeglądu (referencja):
- `src/modules/PositionModule.*` - przykład użycia pozycji GPS
- `src/modules/Telemetry/DeviceTelemetry.*` - przykład wysyłania do telefonu
- `src/modules/ReplyModule.*` - prosty przykład modułu

## Następne kroki

1. Przejrzeć przykładowe moduły (PositionModule, DeviceTelemetryModule)
2. Utworzyć szkielet modułu BleGpsModule
3. Zaimplementować podstawową funkcjonalność
4. Przetestować na urządzeniu
5. Zintegrować z aplikacją Android (jeśli potrzebne modyfikacje)

