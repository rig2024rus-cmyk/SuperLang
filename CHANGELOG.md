## v0.8 (2026-01-07) — Pattern matching + Comparison operators

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
