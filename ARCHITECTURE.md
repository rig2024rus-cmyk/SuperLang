# SuperLang Architecture (v0.6)

## Философия языка

SuperLang — декларативный язык для спецификации наблюдаемых свойств дискретных реляционных структур. Пользователь описывает ЧТО истинно о мире, компилятор синтезирует КАК это вычислить.

**Design axioms:**
1. Инверсия ответственности: user → WHAT, compiler → HOW
2. Semantic Graph как источник истины
3. Расширяемость через новые типы edges
4. Purity: без side effects, без мутации состояния

## Pipeline компиляции
.unq → Lexer → Parser → AST → TypeChecker → AST-to-Graph → Semantic Graph
                                                              ↓
                                              Validator (cycles, stratification)
                                                              ↓
                                              Synthesizer → Closure IR
                                                              ↓
                                              Runtime → Stratified saturation → Fixpoint
                                                              ↓
                                                    Query evaluation
                                                    

## Ключевые компоненты

### 1. AST (Abstract Syntax Tree)
- Структуры: `Program`, `EntityDecl`, `RelationDecl`, `DeriveDecl`, `ObserveDecl`
- `Condition` с `atoms[]` и `arith_assigns[]`
- `Expr` дерево для арифметики

### 2. Semantic Graph
- **Nodes:** `NODE_BASE`, `NODE_DERIVED`, `NODE_OBSERVATION`
- **Edges:**
  - `EDGE_DEFINED_BY_BASE` — observe → impl, derive → impl
  - `EDGE_DEFINED_BY_COMPOSITION` — обычные body atoms
  - `EDGE_DEFINED_BY_NEGATION` — negative atoms
  - `EDGE_DEFINED_BY_AGGREGATE` — sum/count/min/max
  - `EDGE_DEFINED_BY_ARITHMETIC` — expressions

### 3. Multi-rule derive (v0.6)
Каждое правило с одинаковым head получает отдельный `*_impl_N` node:
derive d(a,b) from P(a,b)              → d_impl_0
derive d(a,b) from P(a,c), d(c,b)      → d_impl_1
d ← d_impl_0, d ← d_impl_1             → union semantics


### 4. ArgBindings система
- `ArgBinding` в Rule: отслеживает где переменная встречается
- `ArgLocation`: (atom_index, arg_index)
- `is_head` + `head_arg_index`: для построения head fact
- Используется в general n-way join для unification

### 5. Stratification с cycle detection
- `strip_impl_suffix()`: `descendant_impl_0` → `descendant`
- `VisitedSet`: отслеживает предикаты в цепочке рекурсии
- Canonical head comparison: предотвращает бесконечную рекурсию через impl-ноды

## Runtime: General N-Way Join

**Алгоритм:**
1. Separate positive/negative atoms
2. Cross product всех positive atoms (nested loop)
3. Unification через ArgBindings (все locations переменной должны иметь одинаковое значение)
4. Check negative atoms (fact_exists)
5. Build head fact из head bindings

**Специальные случаи:**
- Aggregate rules: группировка по всем полям кроме aggregate field
- Arithmetic rules: positional binding arg0, arg1, ...

## Известные ограничения

1. **Нет pattern matching в queries** — возвращает только TRUE/FALSE \\ ✅ Pattern matching в queries с кастомным синтаксисом !X и unification
2. **Нет операторов сравнения** — только =, +, -, *, / \\ - ✅ Pattern matching в queries с синтаксисом `!X` (v0.8)
- ✅ Операторы сравнения (`>`, `<`, `>=`, `<=`, `==`, `!=`) (v0.8)
3. **Нет строковых литералов** — только identifiers
4. **Пустые группы в агрегатах** — не создаются facts (SQL semantics)
5. **Нет рекурсии + арифметики** — finite Herbrand universe

## Примеры программ

- `test1_ancestor.unq` — транзитивное замыкание
- `test4_ready.unq` — stratified negation
- `test5_total_price.unq` — aggregates
- `test8_arith.unq` — arithmetic expressions
- `hello_universe.unq` — existential quantification
- `cosmic_calculator.unq` — aggregates + arithmetic composition
- `employee_bonuses.unq` — projection + aggregation
- `family_inheritance.unq` — multi-rule derive + transitive closure
- `minimal_recursive.unq` — minimal multi-rule test

## Что дальше (roadmap)

### Приоритет 1: Pattern matching в queries
superlang
query ancestor(Alice, X)  # → [Bob, Charlie, David]
query ready(X)            # → [TaskA, TaskB]

Требует: изменить Query processing, добавить list results.

### Приоритет 2: Операторы сравнения
```superlang
derive high_value(order, total)
  from total_price(order, total), total > 1000
  
Требует: расширить Expr, добавить filter в runtime.
Приоритет 3: Документация

    MANIFEST.md (философия)
    SYNTAX.md (полная грамматика)
    TUTORIAL.md (пошаговое руководство)

Команды для работы
make clean && make              # Собрать
make test-all                   # Запустить 9 regression tests
make valgrind                   # Проверить memory safety
./superlang examples/foo.unq    # Запустить программу
./superlang --dump-ast foo.unq  # Посмотреть AST
./superlang --dump-graph foo.unq # Посмотреть Semantic Graph
./superlang --dump-ir foo.unq   # Посмотреть Closure IR

Git теги

    v0.3 — arithmetic v1 (e1ef59e)
    v0.4 — general n-way join (dd33741)
    v0.5 — arithmetic unification + clean memory (9409d90)
    v0.6 — stable multi-rule derive (9b068d3)
    

### 2. CHANGELOG.md

```markdown
# Changelog

## v0.6 (2026-01-07) — Stable multi-rule derive

### Исправления
- **Critical:** Бесконечная рекурсия в stratification через impl-ноды
  - Добавлен `strip_impl_suffix()` для canonical head names
  - Добавлен `VisitedSet` для cycle detection
  - Backtracking для независимых ветвей рекурсии
  
- **Critical:** Synthesizer неправильно обрабатывал несколько правил с одинаковым head
  - Каждое правило теперь получает отдельный `*_impl_N` node
  - Union semantics вместо intersection

### Результаты
- `family_inheritance.unq`: все 16 queries работают
- `minimal_recursive.unq`: транзитивное замыкание работает
- 9/9 regression tests проходят

## v0.5 (2026-01-07) — Arithmetic unification + clean memory

### Исправления
- **Memory leak:** double strdup в apply_rule_aggregate
- **Arithmetic unification:** ArgBindings для arithmetic rules
  - Правильная unification через имена переменных
  - Cross-atom unification работает

### Результаты
- `cosmic_calculator.unq`: aggregates + arithmetic composition
- Valgrind clean: 0 leaks, 0 errors

## v0.4 (2026-01-07) — General n-way join

### Архитектурные изменения
- **Synthesizer:** ArgBindings система
  - ArgBinding: отслеживает где переменная встречается
  - ArgLocation: (atom_index, arg_index)
  - is_head + head_arg_index для построения head fact

- **Runtime:** General n-way join algorithm
  - Заменяет 5 hardcoded patterns
  - Поддерживает:
    - Произвольное число positive atoms
    - Произвольные комбинации arities
    - Existential quantification (vars в body но не в head)
    - Mixed positive + negative atoms
    - Proper unification через ArgBindings

- **ast_to_graph.c:** BASE edges с bindings для observe → impl

### Результаты
- `hello_universe.unq`: existential quantification работает
- Больше нет silent skipping unsupported rule shapes

## v0.3 (2026-01-07) — Arithmetic v1

### Новые возможности
- Арифметические выражения: +, -, *, /, унарный минус
- Приоритеты операторов (recursive descent parser)
- Positional binding: arg0, arg1, ...
- Cross product для combinations
- Expression evaluator

### Тесты
- `test8_arith.unq`: basic arithmetic
- 9/9 regression tests проходят

## v0.2 (baseline)

### Core features
- Транзитивные и симметричные замыкания
- Stratified negation
- Aggregates (sum/count/min/max)
- TypeChecker с safety checks
- Semantic validator (cycles, stratification)
- Memory safe C implementation

