# Результаты проверок модифицированной прошивки Meshtastic

## Версия кода

Ветка: control-point-routing  
Commit: 0b9e99d25  
Сообщение commit: Add Heltec V4 build compatibility helpers

## Проверка сборки

| Environment | Результат | Время сборки | RAM | Flash | Примечание |
|---|---|---:|---:|---:|---|
| heltec-v3 | SUCCESS | 00:00:37.699 | 39.3% | 66.4% | Сформированы ELF, BIN, factory BIN и LittleFS |
| heltec-v4 | SUCCESS | 00:01:52.723 | — | — | Сформированы ELF, BIN, factory BIN и LittleFS, flash size 16MB |

## Проверка функциональной логики контрольных точек

Подтверждено наличие и подключение ControlPointModule:
- src/modules/Modules.cpp: controlPointModule = new ControlPointModule();

Подтверждено наличие алгоритма выбора preferred relay:
- ControlPointModule::choosePreferredRelay(NodeNum to)

Порядок выбора:
1. valid metric;
2. usable metric;
3. fresh metric;
4. relay_id != 0;
5. higher node_priority;
6. lower route_cost;
7. lower current_load;
8. fresher last_update_ms;
9. lower relay_id as tie-breaker.

## Проверка интеграции в NextHopRouter

Подтверждено:
- NextHopRouter::send() вызывает controlPointModule->choosePreferredRelay(p->to);
- при наличии подходящей контрольной точки p->next_hop назначается через ControlPointModule;
- при отсутствии подходящей контрольной точки используется штатный getNextHop();
- повторная перезапись p->next_hop после выбора контрольной точки отсутствует.

## Проверка формата ControlPointMessage

Подтверждены поля:
- schema_version;
- is_control_point;
- node_priority;
- route_cost;
- current_load;
- aggregation_enabled;
- directions.

## Проверка хранения и обработки метрик

Подтверждено:
- upsertMetric() добавляет или обновляет запись peerMetrics;
- relay_id формируется из младшего байта NodeNum;
- last_update_ms обновляется через millis();
- isMetricFresh() проверяет TTL;
- isMetricUsable() проверяет route_cost, load и priority;
- pruneExpiredMetrics() помечает устаревшие записи invalid.

## Диагностические сообщения

Подтверждены LOG_DEBUG / LOG_WARN для:
- selected relay;
- ignored empty from_node;
- unsupported schema_version;
- non-control-point message;
- failed broadcast allocation;
- broadcasting announcement;
- next_hop set through ControlPointModule.
