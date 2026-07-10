## v1.3 (2026-07-10) — Correct stratification, unification fix, aggregate-cycle detection

### Критические исправления

- **Стратификация вычисляется заново, из SCC-конденсации, а не эвристикой.**
  `graph_compute_strata()` (semantic_validator.c) переиспользует уже
  существующий Tarjan-проход: один stratum на SCC истинного графа
  зависимостей, +1 при пересечении negation-ребра между SCC. Заменяет
  и старую per-rule эвристику через `strip_impl_suffix`/self-reference
  (которая могла присвоить copy-правилу `P :- P_impl_N` stratum ниже,
  чем нужно `P_impl_N`, и `P` навсегда оставался пустым), и v1.2-й
  плоский неупорядоченный фикспоинт (несостоятельный для negation: без
  retraction фактов порядок правил в списке мог навсегда зафиксировать
  факт, выведенный по неполной картине того, что негируется).

- **Регрессия в unification исправлена.** В `apply_rule_general` и
  `apply_rule_arithmetic` переменная, встречающаяся одновременно в
  positive- и negative-атоме (обычная форма stratified negation),
  безусловно отклоняла всю комбинацию вместо того, чтобы отложить
  проверку negative-вхождения до блока проверки негации. Поиск первого
  positive-вхождения сделан независимым от порядка локаций.

- **Дыра в проверке stratifiability через агрегаты закрыта.**
  `EDGE_DEFINED_BY_AGGREGATE` добавлено в SCC-граф зависимостей — ранее
  негативный цикл, замыкающийся через агрегат, был невидим для Tarjan и
  молча принимался как stratifiable.

### Робастность

- Tarjan исправлен с O(V·E) до O(V+E): `find_node_index` делал линейный
  проход по узлам на каждое ребро; теперь индекс проштампован на `Node`.
- Исправлено UB при касте float→int (`result == (int)result` вычислялся
  до проверки границы `fabs(...) < 1e15`, а не после).
- Несогласованные размеры фиксированных буферов (16 vs 32) объединены в
  `SUPERLANG_MAX_BODY_ATOMS`/`SUPERLANG_MAX_ARITY`; правило, превышающее
  лимит, теперь отклоняется на этапе type-checking с понятной ошибкой,
  а не переполняет стек или молча обрезается в рантайме.
- Загрузка входных фактов с arity ≥ 4 больше не отбрасывается молча
  (`config_add_fact`'s varargs 1/2/3 switch → `add_fact_direct` с массивом).

### Тестовая инфраструктура

- `run_semantic_tests.sh` теперь проверяет фактические значения в выводе,
  а не только exit code (ранее test3/test4 отчитывались как PASSED,
  печатая неверные ответы).
- Удалён `run_tests.sh` (указывал на несуществующие пути/файлы, полностью
  заменён `run_semantic_tests.sh` + `run_valgrind_all.sh`).
- Добавлены `test10_aggregate_cycle.unq` (негативный цикл через агрегат
  должен отклоняться) и `test11_deep_negation_chain.unq` (4-уровневая
  цепочка negation, должна чередоваться TRUE/FALSE/TRUE/FALSE).

Проверено: 12/12 semantic-тестов с реальными assert'ами на значения,
12/12 valgrind (0 errors, 0 leaks), все существующие examples/ работают,
чистая сборка -Wall -Wextra.

## v1.1 (2026-07-09) — Built-in math functions + zero memory leaks

### Новые возможности
- **15 встроенных математических функций** из libm:
  - Одноаргументные: `sqrt`, `abs`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `exp`, `log`, `log10`, `floor`, `ceil`, `round`
  - Двухаргументные: `pow`, `fmod`, `atan2`, `fmin`, `fmax`
- **Синтаксис вызова функций** в выражениях: `d = sqrt(x*x + y*y)`
- **Правильное разделение пространств имён**: `fmin`/`fmax` для математики (min/max зарезервированы для агрегатов)

### Качество
- **Полное устранение утечки памяти**: 0 leaks, 0 errors во всех сценариях
- **Perfect heap balance**: 1762 allocs / 1762 frees
- **Защита от OOM** во всех критичных местах (copy_expr, expr_new_call)
- **Гарантированный cleanup** во всех ветках ошибок парсера

### Архитектура
- Добавлен новый тип узла AST: `EXPR_CALL`
- Парсер распознаёт синтаксис `func(arg1, arg2, ...)`
- Runtime диспетчеризирует вызовы на функции libm
- Чёткие ownership semantics: `expr_new_call` принимает strdup'd строку

### Пример использования
```prolog
relation point(x, y)
input { point(3, 4) point(5, 12) }

derive distance(x, y, d) from
    point(x, y),
    d = sqrt(x*x + y*y)

query distance(!X, !Y, !D)
# → [(3, 4, 5), (5, 12, 13)]## v0.8 (2026-01-07) — Pattern matching + Comparison operators

### Новые возможности
- **Pattern matching в queries:** возвращает список bindings вместо TRUE/FALSE
  - Синтаксис переменных: `!X`, `!Child`, `!Parent` (префикс `!`)
  - Unification: `query p(!X, !X)` требует совпадения аргументов
  - Ground queries (без переменных) возвращают TRUE/FALSE
  - Пример: `query ancestor(alice, !X)` → `[bob, charlie, david]`

- **Операторы сравнения:** фильтры в derive/observe conditions
  - Поддержка: `>`, `<`, `>=`, `<=`, `==`, `!=`
  - Пример: `derive high_value(id) from order(id, total), total > 1000`
  - Safety check: все переменные в comparisons должны быть bound
  - Evaluation: фильтры применяются после unification, до создания head fact

### Архитектурные изменения
- **Lexer:** поддержка `!identifier` как токена `TOK_IDENT`
- **Parser:** disambiguation между negated atoms, arithmetic, и comparisons
- **AST:** добавлена структура `Comparison` и массив `comparisons[]` в `Condition`
- **Semantic Graph:** новый тип ребра `EDGE_DEFINED_BY_FILTER`
- **Synthesizer:** deep copy comparisons в Rule
- **Runtime:** `eval_comparisons()` для проверки фильтров
- **TypeChecker:** проверка arity для queries + safety check для comparisons

### Исправления
- **Regression:** Parser теперь корректно обрабатывает negated atoms (`not predicate(...)`)
- **TypeChecker:** проверка arity для queries (было: принимал любую arity)

### Результаты
- `test_bang_pattern.unq`: pattern matching работает
- `test_comparisons.unq`: операторы сравнения работают
- 9/9 regression tests проходят
- Valgrind clean: 0 leaks, 0 errors

---


## v0.7 (2026-01-07) — Pattern matching in queries

### Новые возможности
- **Pattern matching:** queries теперь возвращают список bindings вместо простого TRUE/FALSE.
  - Кастомный синтаксис переменных с префиксом `!` (например, `!X`, `!Child`).
  - Unification: `query p(!X, !X)` требует совпадения аргументов факта.
  - Ground queries (без переменных) возвращают TRUE/FALSE.
- **Lexer:** поддержка `!identifier` как токена `TOK_IDENT`.
- **TypeChecker:** строгая проверка arity для queries.
- **Runtime:** Visitor pattern (`config_visit_facts`) для безопасной итерации по фактам.

### Результаты
- Полная поддержка реляционных запросов в стиле Datalog/Prolog.
- Valgrind clean: 0 leaks, 0 errors.
