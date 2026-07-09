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
