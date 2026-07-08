# Context Transfer для нового чата

## Текущее состояние

**SuperLang v0.6** — декларативный язык для спецификации наблюдаемых свойств.

**Статус:** Production-ready для базовых случаев. 9/9 regression tests проходят.

## Что работает

✅ Транзитивные замыкания (ancestor, descendant)
✅ Stratified negation (ready/blocked)
✅ Multi-rule derive с union semantics
✅ General n-way join (любые комбинации atoms)
✅ Aggregates (sum/count/min/max)
✅ Arithmetic expressions (+, -, *, /)
✅ Existential quantification
✅ Memory safety (Valgrind clean)

## Что НЕ работает

❌ Pattern matching в queries (возвращает только TRUE/FALSE)
❌ Операторы сравнения (>, <, >=, <=)
❌ Строковые литералы
❌ Пустые группы в агрегатах

## Следующие шаги (roadmap)

1. **Pattern matching** (приоритет 1)
   - query ancestor(Alice, X) → [Bob, Charlie, David]
   - Требует: изменить Query processing, добавить list results
   - Оценка: 3-4 часа

2. **Операторы сравнения** (приоритет 2)
   - derive high_value(order) from total > 1000
   - Требует: расширить Expr, добавить filter
   - Оценка: 2-3 часа

3. **Документация** (приоритет 3)
   - MANIFEST.md, SYNTAX.md, TUTORIAL.md
   - Оценка: 3 часа

## Архитектурные решения (важно!)

### Multi-rule derive
Каждое правило с одинаковым head → отдельный `*_impl_N` node:
derive d(a,b) from P(a,b)           → d_impl_0
derive d(a,b) from P(a,c), d(c,b)   → d_impl_1

Union semantics: `d ← d_impl_0` и `d ← d_impl_1` как отдельные правила.

### Stratification с cycle detection
- `strip_impl_suffix()`: `descendant_impl_0` → `descendant`
- `VisitedSet`: предотвращает бесконечную рекурсию
- Canonical head comparison

### ArgBindings система
- Отслеживает где переменная встречается в body atoms
- Используется для unification в general n-way join
- is_head + head_arg_index для построения head fact

## Известные pitfalls

1. **Aggregate семантика:** группирует по ВСЕМ полям кроме aggregate field
   - Нужна projection если нужна группировка по подмножеству
   
2. **Пустые группы:** агрегат не создаёт facts для пустых групп
   - `team_size(Bob, 0)` = FALSE если у Bob нет подчинённых

3. **Keywords:** `count`, `sum`, `min`, `max` зарезервированы
   - Используйте `cnt`, `total`, `n` вместо них

## Примеры программ для изучения

- `examples/hello_universe.unq` — existential quantification
- `examples/cosmic_calculator.unq` — aggregates + arithmetic
- `examples/employee_bonuses.unq` — projection + aggregation
- `examples/family_inheritance.unq` — multi-rule derive + transitive closure
- `examples/minimal_recursive.unq` — minimal multi-rule test

## Команды

```bash
make clean && make              # Собрать
make test-all                   # 9 regression tests
./superlang examples/foo.unq    # Запустить программу
./superlang --dump-ir foo.unq   # Посмотреть Closure IR

git log --oneline
9b068d3 Fix stratification infinite recursion
7c8278a Fix multi-rule derive
9409d90 Fix memory leak
dd33741 General n-way join
48a6dbe ArgBindings
46b3e36 head params
64f62d1 ArgBinding infrastructure
e1ef59e Arithmetic v1
