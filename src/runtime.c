/*
 * SuperLang Runtime Engine v1.1.2
 * 
 * Runtime stage of the 8-stage compilation pipeline:
 *   Source → Lexer → Parser → TypeChecker → AST-to-Graph →
 *   Validator → Synthesizer → [Runtime Saturation] → Query Engine
 *
 * Ответственность:
 *   - Управление базой фактов (Config)
 *   - Вычисление арифметических выражений с поддержкой built-in функций
 *   - Применение правил трёх типов (aggregate, arithmetic, general)
 *   - Stratified saturation до неподвижной точки (Knaster-Tarski)
 *
 * История версий:
 *   v1.0   - Подключены фильтры сравнения в apply_rule_general
 *   v1.1   - Добавлены встроенные math-функции (sqrt, pow, sin, ...)
 *   v1.1.1 - КРИТИЧЕСКИЕ ИСПРАВЛЕНИЯ:
 *            * negation stratification для copy-правил
 *            * strip_impl_suffix ('\0' вместо '0')
 *            * UB при cast double→int
 *            * подробное документирование
 *   v1.1.2 - КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ stratification:
 *            * Self-reference = ТОЧНОЕ совпадение имён (не canonical)
 *            * Copy-правила теперь правильно наследуют stratum от impl-правил
 */

#include "runtime.h"
#include "synthesizer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <assert.h>

/* ========================================================================= */
/* ЧАСТЬ 1: CONFIG — база фактов                                              */
/* ========================================================================= */

/*
 * config_new: создать пустую конфигурацию
 * 
 * Конфигурация — это коллекция фактов, накопленных во время saturation.
 * Изначально пуста, факты добавляются из input{} блока и через apply_rule.
 */
Config *config_new(void) {
    return calloc(1, sizeof(Config));
}

/*
 * config_free: освободить конфигурацию и все её факты
 *
 * Каждый факт содержит strdup'нутые строки (predicate + args),
 * поэтому требуется аккуратное рекурсивное освобождение.
 */
void config_free(Config *c) {
    if (!c) return;
    Fact *f = c->facts;
    while (f) {
        Fact *next = f->next;
        free(f->predicate);
        for (int i = 0; i < f->arity; i++) free(f->args[i]);
        free(f->args);
        free(f);
        f = next;
    }
    free(c);
}

/*
 * config_add_fact: добавить факт в конфигурацию (публичный API)
 *
 * Используется в main.c для загрузки input{} фактов.
 * Автоматически дедуплицирует: если факт уже есть, ничего не делает.
 *
 * Variadic interface: config_add_fact(c, "parent", 2, "alice", "bob")
 */
void config_add_fact(Config *c, const char *pred, int arity, ...) {
    va_list args;
    va_start(args, arity);
    char **new_args = malloc(sizeof(char*) * arity);
    for (int i = 0; i < arity; i++) {
        new_args[i] = strdup(va_arg(args, const char*));
    }
    va_end(args);

    /* Проверка на дубликат */
    for (Fact *f = c->facts; f; f = f->next) {
        if (strcmp(f->predicate, pred) == 0 && f->arity == arity) {
            int match = 1;
            for (int i = 0; i < arity; i++) {
                if (strcmp(f->args[i], new_args[i]) != 0) { match = 0; break; }
            }
            if (match) {
                for (int i = 0; i < arity; i++) free(new_args[i]);
                free(new_args);
                return;
            }
        }
    }
    
    /* Добавление нового факта в голову списка */
    Fact *f = calloc(1, sizeof(Fact));
    f->predicate = strdup(pred);
    f->args = new_args;
    f->arity = arity;
    f->next = c->facts;
    c->facts = f;
    c->count++;
}

/*
 * config_has_fact: проверить наличие факта (публичный API, variadic)
 */
int config_has_fact(Config *c, const char *pred, int arity, ...) {
    va_list args;
    va_start(args, arity);
    char **query = malloc(sizeof(char*) * arity);
    for (int i = 0; i < arity; i++) query[i] = strdup(va_arg(args, const char*));
    va_end(args);

    int found = 0;
    for (Fact *f = c->facts; f; f = f->next) {
        if (strcmp(f->predicate, pred) == 0 && f->arity == arity) {
            int match = 1;
            for (int i = 0; i < arity; i++) {
                if (strcmp(f->args[i], query[i]) != 0) { match = 0; break; }
            }
            if (match) { found = 1; break; }
        }
    }
    for (int i = 0; i < arity; i++) free(query[i]);
    free(query);
    return found;
}

/*
 * fact_exists: внутренняя проверка существования факта (по char** args)
 *
 * Используется в apply_rule_* функциях для дедупликации.
 * 
 * ВАЖНО: это O(N) на каждый запрос, где N — общее число фактов.
 * Для production нужен индекс по предикату (TODO: semi-naive eval).
 */
static int fact_exists(Config *c, const char *pred, int arity, char **args) {
    for (Fact *f = c->facts; f; f = f->next) {
        if (strcmp(f->predicate, pred) == 0 && f->arity == arity) {
            int match = 1;
            for (int i = 0; i < arity; i++) {
                if (strcmp(f->args[i], args[i]) != 0) { match = 0; break; }
            }
            if (match) return 1;
        }
    }
    return 0;
}

/*
 * add_fact_direct: добавить факт с дедупликацией (внутренний API)
 *
 * Используется в apply_rule_* функциях. Каждый args[i] strdup'уется,
 * поэтому вызывающий сохраняет ownership оригинальных строк.
 */
static void add_fact_direct(Config *c, const char *pred, int arity, char **args) {
    if (fact_exists(c, pred, arity, args)) return;
    Fact *f = calloc(1, sizeof(Fact));
    f->predicate = strdup(pred);
    f->arity = arity;
    f->args = malloc(sizeof(char*) * arity);
    for (int i = 0; i < arity; i++) f->args[i] = strdup(args[i]);
    f->next = c->facts;
    c->facts = f;
    c->count++;
}

/*
 * config_dump: отладочный вывод всех фактов
 */
void config_dump(const Config *c) {
    printf("Configuration: %zu facts\n", c->count);
    for (Fact *f = c->facts; f; f = f->next) {
        printf("  %s(", f->predicate);
        for (int i = 0; i < f->arity; i++) {
            printf("%s%s", f->args[i], i < f->arity - 1 ? ", " : "");
        }
        printf(")\n");
    }
}

/*
 * config_visit_facts: обход всех фактов через callback (для query engine)
 */
void config_visit_facts(Config *c, FactVisitor visitor, void *ctx) {
    if (!c || !visitor) return;
    for (Fact *f = c->facts; f; f = f->next) {
        visitor(f->predicate, f->arity, (const char **)f->args, ctx);
    }
}

/* ========================================================================= */
/* ЧАСТЬ 2: EXPRESSION EVALUATOR                                             */
/* ========================================================================= */

/*
 * VarBindings: локальное окружение для вычисления выражений
 *
 * Связывает имена переменных с их строковыми значениями.
 * Значения — это указатели на строки из фактов (не ownership).
 */
typedef struct {
    const char **names;
    const char **values;
    int count;
} VarBindings;

/*
 * eval_expr: рекурсивный вычислитель выражений
 *
 * Поддерживает:
 *   - EXPR_NUMBER: числовые литералы
 *   - EXPR_VARIABLE: поиск в VarBindings с парсингом через strtod
 *   - EXPR_BINARY: +, -, *, /, % (через fmod для %)
 *   - EXPR_UNARY_MINUS: унарный минус
 *   - EXPR_CALL: built-in функции (sqrt, pow, sin, ...) [v1.1]
 *
 * Возвращает 0.0 для несвязанных переменных (защита от ошибок синтеза).
 * В production стоит добавить assert для несвязанных переменных.
 */
static double eval_expr(const Expr *e, const VarBindings *binds) {
    if (!e) return 0.0;
    switch (e->type) {
        case EXPR_NUMBER:
            return e->number;
            
        case EXPR_VARIABLE:
            for (int i = 0; i < binds->count; i++) {
                if (strcmp(binds->names[i], e->var_name) == 0) {
                    char *endptr;
                    double val = strtod(binds->values[i], &endptr);
                    if (*endptr == '\0') return val;
                    return 0.0;  /* не числовое значение */
                }
            }
            /* Несвязанная переменная — защита от ошибок синтеза.
             * В идеале type_checker должен это предотвращать. */
            return 0.0;
            
        case EXPR_BINARY: {
            double left = eval_expr(e->binary.left, binds);
            double right = eval_expr(e->binary.right, binds);
            switch (e->binary.op) {
                case '+': return left + right;
                case '-': return left - right;
                case '*': return left * right;
                case '/': return (right == 0.0) ? 0.0 : left / right;
                case '%': return (right == 0.0) ? 0.0 : fmod(left, right);
            }
            return 0.0;
        }
        
        case EXPR_UNARY_MINUS:
            return -eval_expr(e->operand, binds);
            
        case EXPR_CALL: {
            /* v1.1: Built-in math functions */
            double arg_values[8];  /* max 8 аргументов */
            int n_args = e->call.arg_count;
            if (n_args > 8) n_args = 8;
            for (int i = 0; i < n_args; i++) {
                arg_values[i] = eval_expr(e->call.args[i], binds);
            }
            const char *fn = e->call.func_name;
            
            /* 1-аргументные функции */
            if (n_args == 1) {
                double x = arg_values[0];
                if (strcmp(fn, "sqrt") == 0)  return (x >= 0.0) ? sqrt(x) : 0.0;
                if (strcmp(fn, "abs") == 0)   return fabs(x);
                if (strcmp(fn, "sin") == 0)   return sin(x);
                if (strcmp(fn, "cos") == 0)   return cos(x);
                if (strcmp(fn, "tan") == 0)   return tan(x);
                if (strcmp(fn, "asin") == 0)  return asin(x);
                if (strcmp(fn, "acos") == 0)  return acos(x);
                if (strcmp(fn, "atan") == 0)  return atan(x);
                if (strcmp(fn, "exp") == 0)   return exp(x);
                if (strcmp(fn, "log") == 0)   return (x > 0.0) ? log(x) : 0.0;
                if (strcmp(fn, "log10") == 0) return (x > 0.0) ? log10(x) : 0.0;
                if (strcmp(fn, "floor") == 0) return floor(x);
                if (strcmp(fn, "ceil") == 0)  return ceil(x);
                if (strcmp(fn, "round") == 0) return round(x);
            }
            /* 2-аргументные функции */
            else if (n_args == 2) {
                double x = arg_values[0];
                double y = arg_values[1];
                if (strcmp(fn, "pow") == 0)   return pow(x, y);
                if (strcmp(fn, "fmod") == 0)  return (y != 0.0) ? fmod(x, y) : 0.0;
                if (strcmp(fn, "atan2") == 0) return atan2(y, x);
                if (strcmp(fn, "fmin") == 0)  return fmin(x, y);
                if (strcmp(fn, "fmax") == 0)  return fmax(x, y);
            }
            /* Неизвестная функция или неправильная арность */
            return 0.0;
        }
    }
    return 0.0;
}

/*
 * eval_comparisons: проверка списка фильтров сравнения
 *
 * Возвращает 1 если ВСЕ сравнения истинны (AND semantics),
 * 0 если хоть одно ложно.
 */
static int eval_comparisons(const Comparison *cmps, int count, const VarBindings *binds) {
    for (int i = 0; i < count; i++) {
        const Comparison *cmp = &cmps[i];
        double left = eval_expr(cmp->left, binds);
        double right = eval_expr(cmp->right, binds);
        int result = 0;
        switch (cmp->op) {
            case CMP_EQ: result = (left == right); break;
            case CMP_NE: result = (left != right); break;
            case CMP_LT: result = (left < right); break;
            case CMP_LE: result = (left <= right); break;
            case CMP_GT: result = (left > right); break;
            case CMP_GE: result = (left >= right); break;
        }
        if (!result) return 0;
    }
    return 1;
}

/*
 * format_number: безопасное форматирование числа в строку [v1.1.1 FIX]
 *
 * ИСПРАВЛЕНИЕ v1.1.1: сначала проверяем границы, потом делаем cast в int.
 * Это предотвращает undefined behavior при cast больших double (>2^31).
 *
 * Раньше было:
 *   if (result == (int)result && fabs(result) < 1e15)  ← UB для больших result
 *
 * Теперь:
 *   if (fabs(value) < 1e15 && value == floor(value))   ← сначала проверка
 */
static void format_number(double value, char *buf, size_t buf_size) {
    if (fabs(value) < 1e15 && value == floor(value)) {
        snprintf(buf, buf_size, "%d", (int)value);
    } else {
        snprintf(buf, buf_size, "%.2f", value);
    }
}

/* ========================================================================= */
/* ЧАСТЬ 3: RULE APPLICATION — применение правил трёх типов                   */
/* ========================================================================= */

/*
 * apply_rule_aggregate: применение правила агрегации
 *
 * Пример:
 *   derive total(cat, s) from item(cat, price), s = sum(item, price)
 *
 * Группирует факты source-предиката по всем полям кроме agg_field,
 * вычисляет агрегат (sum/count/min/max), добавляет в head.
 */
static int apply_rule_aggregate(Config *c, const Rule *r) {
    int added = 0;
    char *agg_func = r->aggregate_funcs[0];
    int agg_field = r->aggregate_fields[0];
    int src_arity = r->body_arities[0];
    int dst_arity = r->head_arity;
    int needs_numeric = (strcmp(agg_func, "count") != 0);

    /* Структура для накопления агрегатов по группе */
    typedef struct {
        char **key;
        int key_count;
        double sum;
        int count;
        double min, max;
        int initialized;
    } Group;

    Group *groups = NULL;
    int group_count = 0, group_capacity = 0;

    /* Проходим по всем фактам source-предиката */
    for (Fact *f = c->facts; f; f = f->next) {
        if (strcmp(f->predicate, r->body_preds[0]) != 0) continue;
        if (f->arity != src_arity) continue;

        /* Извлекаем ключ группы (все поля кроме agg_field) */
        int key_count = src_arity - 1;
        char **key = malloc(sizeof(char*) * key_count);
        int ki = 0;
        for (int i = 0; i < src_arity; i++) {
            if (i != agg_field) key[ki++] = f->args[i];
        }

        /* Извлекаем числовое значение для агрегации */
        double value = 0.0;
        if (needs_numeric) {
            char *endptr;
            value = strtod(f->args[agg_field], &endptr);
            if (*endptr != '\0') { free(key); continue; }
        }

        /* Ищем существующую группу с таким же ключом */
        Group *g = NULL;
        for (int i = 0; i < group_count; i++) {
            int match = 1;
            for (int j = 0; j < key_count; j++) {
                if (strcmp(groups[i].key[j], key[j]) != 0) { match = 0; break; }
            }
            if (match) { g = &groups[i]; free(key); break; }
        }

        /* Создаём новую группу если не нашли */
        if (!g) {
            if (group_count >= group_capacity) {
                group_capacity = group_capacity == 0 ? 16 : group_capacity * 2;
                groups = realloc(groups, sizeof(Group) * group_capacity);
            }
            g = &groups[group_count++];
            g->key = key;
            g->key_count = key_count;
            g->sum = 0; g->count = 0;
            g->min = 0; g->max = 0;
            g->initialized = 0;
        }

        /* Обновляем агрегат */
        if (strcmp(agg_func, "count") == 0) {
            g->count++;
        } else {
            g->sum += value;
            g->count++;
            if (!g->initialized) {
                g->min = value; g->max = value;
                g->initialized = 1;
            } else {
                if (value < g->min) g->min = value;
                if (value > g->max) g->max = value;
            }
        }
    }

    /* Для каждой группы создаём head-факт */
    for (int i = 0; i < group_count; i++) {
        Group *g = &groups[i];
        double result;
        if (strcmp(agg_func, "sum") == 0) result = g->sum;
        else if (strcmp(agg_func, "count") == 0) result = g->count;
        else if (strcmp(agg_func, "min") == 0) result = g->initialized ? g->min : 0;
        else if (strcmp(agg_func, "max") == 0) result = g->initialized ? g->max : 0;
        else { free(g->key); continue; }

        char **args = malloc(sizeof(char*) * dst_arity);
        int ai = 0;
        for (int j = 0; j < g->key_count; j++) args[ai++] = g->key[j];

        char buf[64];
        format_number(result, buf, sizeof(buf));
        args[ai] = buf;

        if (!fact_exists(c, r->head, dst_arity, args)) {
            add_fact_direct(c, r->head, dst_arity, args);
            added++;
        }
        free(args);
        free(g->key);
    }
    free(groups);
    return added;
}

/*
 * apply_rule_arithmetic: применение правила с арифметикой
 *
 * Пример:
 *   derive doubled(x, y) from number(x), y = x * 2
 *
 * Перебирает все комбинации фактов для "обычных" атомов (не __arith__),
 * вычисляет арифметическое выражение, создаёт head-факт.
 *
 * Ограничение v1.1: только одно арифметическое присваивание на правило,
 * только в последней позиции head. Для множественной арифметики
 * нужен конвейер предикатов (декларативный обход).
 *
 * arith_slot — индекс body atom'а, содержащего арифметику.
 */
static int apply_rule_arithmetic(Config *c, const Rule *r, int arith_slot) {
    int added = 0;
    
    /* Собираем "обычные" body atoms (не __arith__) */
    int regular_slots[16], regular_count = 0;
    for (int i = 0; i < r->body_count && regular_count < 16; i++) {
        if (strcmp(r->body_preds[i], "__arith__") != 0)
            regular_slots[regular_count++] = i;
    }
    if (regular_count == 0) return added;

    /* Собираем списки фактов для каждого regular atom */
    Fact **fact_lists[16];
    int fact_counts[16];
    for (int s = 0; s < regular_count; s++) {
        int slot = regular_slots[s];
        fact_lists[s] = NULL;
        fact_counts[s] = 0;
        int fact_cap = 0;
        for (Fact *f = c->facts; f; f = f->next) {
            if (strcmp(f->predicate, r->body_preds[slot]) != 0) continue;
            if (f->arity != r->body_arities[slot]) continue;
            if (fact_counts[s] >= fact_cap) {
                fact_cap = fact_cap == 0 ? 16 : fact_cap * 2;
                fact_lists[s] = realloc(fact_lists[s], sizeof(Fact*) * fact_cap);
            }
            fact_lists[s][fact_counts[s]++] = f;
        }
    }

    /* Маппинг atom_index → pos slot для unification */
    int atom_to_pos[32];
    for (int i = 0; i < 32; i++) atom_to_pos[i] = -1;
    for (int s = 0; s < regular_count; s++) atom_to_pos[regular_slots[s]] = s;

    /* Перебор всех комбинаций фактов (n-way join) */
    int indices[16] = {0};
    while (1) {
        int valid = 1;
        for (int s = 0; s < regular_count; s++)
            if (indices[s] >= fact_counts[s]) { valid = 0; break; }
        if (!valid) break;

        Fact *current_facts[16];
        for (int s = 0; s < regular_count; s++)
            current_facts[s] = fact_lists[s][indices[s]];

        /* ШАГ 1: Unification — связывание переменных через ArgBindings */
        int combination_valid = 1;
        char **binding_values = r->arg_binding_count > 0
            ? malloc(sizeof(char*) * r->arg_binding_count) : NULL;

        for (int b = 0; b < r->arg_binding_count && combination_valid; b++) {
            ArgBinding *bind = &r->arg_bindings[b];
            if (bind->location_count == 0) { binding_values[b] = NULL; continue; }
            ArgLocation *first = &bind->locations[0];
            int pos = atom_to_pos[first->atom_index];
            if (pos < 0) { combination_valid = 0; break; }
            char *value = current_facts[pos]->args[first->arg_index];
            binding_values[b] = value;
            /* Проверяем что все локации этой переменной имеют одинаковое значение */
            for (int l = 1; l < bind->location_count; l++) {
                int p = atom_to_pos[bind->locations[l].atom_index];
                if (p < 0) { combination_valid = 0; break; }
                if (strcmp(value, current_facts[p]->args[bind->locations[l].arg_index]) != 0) {
                    combination_valid = 0; break;
                }
            }
        }

        /* ШАГ 2: Применение фильтров сравнения (v1.0) */
        if (combination_valid) {
            const char **bind_names = malloc(sizeof(const char*) * r->arg_binding_count);
            const char **bind_values_arr = malloc(sizeof(const char*) * r->arg_binding_count);
            int bind_count = 0;
            for (int b = 0; b < r->arg_binding_count; b++) {
                if (binding_values[b] == NULL) continue;
                bind_names[bind_count] = r->arg_bindings[b].var_name;
                bind_values_arr[bind_count] = binding_values[b];
                bind_count++;
            }
            VarBindings vbinds = { bind_names, bind_values_arr, bind_count };

            /* Проверка comparisons для arithmetic slot */
            if (r->comparisons && r->comparisons[arith_slot] && r->comparison_counts[arith_slot] > 0) {
                if (!eval_comparisons(r->comparisons[arith_slot], r->comparison_counts[arith_slot], &vbinds)) {
                    combination_valid = 0;
                }
            }

            /* ШАГ 3: Вычисление арифметики и создание head-факта */
            if (combination_valid) {
                double val = eval_expr(r->arith_exprs[arith_slot], &vbinds);

                char **result_args = calloc(r->head_arity, sizeof(char*));
                int *is_our_alloc = calloc(r->head_arity, sizeof(int));
                int head_valid = 1;

                for (int j = 0; j < r->head_arity; j++) {
                    int found = -1;
                    for (int b = 0; b < r->arg_binding_count; b++) {
                        if (r->arg_bindings[b].is_head && r->arg_bindings[b].head_arg_index == j) {
                            found = b; break;
                        }
                    }
                    if (found < 0 || binding_values[found] == NULL) {
                        /* Последняя позиция — для арифметического результата */
                        if (j == r->head_arity - 1) {
                            char buf[64];
                            format_number(val, buf, sizeof(buf));
                            result_args[j] = strdup(buf);
                            is_our_alloc[j] = 1;
                        } else { head_valid = 0; break; }
                    } else {
                        result_args[j] = binding_values[found];
                        is_our_alloc[j] = 0;
                    }
                }

                if (head_valid && !fact_exists(c, r->head, r->head_arity, result_args)) {
                    add_fact_direct(c, r->head, r->head_arity, result_args);
                    added++;
                }

                for (int j = 0; j < r->head_arity; j++) {
                    if (is_our_alloc[j]) free(result_args[j]);
                }
                free(result_args);
                free(is_our_alloc);
            }
            free(bind_names);
            free(bind_values_arr);
        }

        if (binding_values) free(binding_values);

        /* Инкремент счётчика комбинаций (lexicographic order) */
        int carry = 1;
        for (int s = regular_count - 1; s >= 0 && carry; s--) {
            indices[s]++;
            if (indices[s] < fact_counts[s]) carry = 0;
            else indices[s] = 0;
        }
        if (carry) break;
    }

    for (int s = 0; s < regular_count; s++) free(fact_lists[s]);
    return added;
}

/*
 * apply_rule_general: применение общего правила (n-way join с negation)
 *
 * Пример:
 *   derive blocked(id) from
 *       depends(id, dep),
 *       not finished(dep)
 *
 * Основной рабочий конь SuperLang. Работает в 4 шага:
 *   1. Разделить body atoms на positive и negative
 *   2. Перебрать комбинации фактов для positive atoms (n-way join)
 *   3. Unification через ArgBindings + filters + negation check
 *   4. Создание head-факта
 *
 * v1.0: добавлена поддержка comparison filters в general rules
 */
static int apply_rule_general(Config *c, const Rule *r) {
    if (r->body_count == 0) return 0;

    /* ШАГ 1: Разделить positive и negative atoms */
    int pos_indices[32], neg_indices[32];
    int pos_count = 0, neg_count = 0;
    for (int i = 0; i < r->body_count; i++) {
        if (r->body_negative[i]) neg_indices[neg_count++] = i;
        else pos_indices[pos_count++] = i;
    }
    if (pos_count == 0) return 0;

    /* Собрать списки фактов для каждого positive atom */
    Fact ***pos_fact_lists = malloc(sizeof(Fact**) * pos_count);
    int *pos_fact_counts = calloc(pos_count, sizeof(int));
    for (int p = 0; p < pos_count; p++) {
        int atom_idx = pos_indices[p];
        const char *pred = r->body_preds[atom_idx];
        int arity = r->body_arities[atom_idx];
        int count = 0;
        for (Fact *f = c->facts; f; f = f->next)
            if (strcmp(f->predicate, pred) == 0 && f->arity == arity) count++;
        pos_fact_counts[p] = count;
        pos_fact_lists[p] = count > 0 ? malloc(sizeof(Fact*) * count) : NULL;
        int fi = 0;
        for (Fact *f = c->facts; f; f = f->next)
            if (strcmp(f->predicate, pred) == 0 && f->arity == arity)
                pos_fact_lists[p][fi++] = f;
        if (count == 0) {
            /* Один из positive atoms пустой — всё правило не сработает */
            for (int j = 0; j <= p; j++) if (pos_fact_lists[j]) free(pos_fact_lists[j]);
            free(pos_fact_lists);
            free(pos_fact_counts);
            return 0;
        }
    }

    /* Маппинг atom_index → pos slot */
    int atom_to_pos[32];
    for (int i = 0; i < 32; i++) atom_to_pos[i] = -1;
    for (int p = 0; p < pos_count; p++) atom_to_pos[pos_indices[p]] = p;

    int *indices = calloc(pos_count, sizeof(int));
    int added = 0;
    char **binding_values = r->arg_binding_count > 0
        ? malloc(sizeof(char*) * r->arg_binding_count) : NULL;

    /* Перебор всех комбинаций positive facts */
    while (1) {
        int valid_indices = 1;
        for (int p = 0; p < pos_count; p++)
            if (indices[p] >= pos_fact_counts[p]) { valid_indices = 0; break; }
        if (!valid_indices) break;

        Fact *current_facts[32];
        for (int p = 0; p < pos_count; p++)
            current_facts[p] = pos_fact_lists[p][indices[p]];

        /* ===== ШАГ 2: Unification (связывание переменных) ===== */
        int combination_valid = 1;
        for (int b = 0; b < r->arg_binding_count; b++) {
            ArgBinding *bind = &r->arg_bindings[b];
            /* Находим первую positive локацию для этой переменной */
            int first_pos_loc = -1;
            for (int l = 0; l < bind->location_count; l++) {
                int atom_idx = bind->locations[l].atom_index;
                if (atom_to_pos[atom_idx] >= 0) { first_pos_loc = l; break; }
            }
            if (first_pos_loc < 0) { combination_valid = 0; break; }
            ArgLocation *first = &bind->locations[first_pos_loc];
            int pos_idx = atom_to_pos[first->atom_index];
            char *value = current_facts[pos_idx]->args[first->arg_index];
            binding_values[b] = value;
            /* Проверяем все остальные локации этой переменной */
            for (int l = 0; l < bind->location_count; l++) {
                if (l == first_pos_loc) continue;
                int atom_idx = bind->locations[l].atom_index;
                int pos = atom_to_pos[atom_idx];
                if (pos < 0) continue;  /* negative атом — проверим позже */
                if (strcmp(value, current_facts[pos]->args[bind->locations[l].arg_index]) != 0) {
                    combination_valid = 0; break;
                }
            }
            if (!combination_valid) break;
        }

        /* ===== ШАГ 3: Проверка фильтров сравнения (v1.0) ===== */
        if (combination_valid && r->comparisons && r->comparison_counts) {
            const char **bind_names = malloc(sizeof(const char*) * r->arg_binding_count);
            const char **bind_values_arr = malloc(sizeof(const char*) * r->arg_binding_count);
            int bind_count = 0;
            for (int b = 0; b < r->arg_binding_count; b++) {
                if (binding_values[b] == NULL) continue;
                bind_names[bind_count] = r->arg_bindings[b].var_name;
                bind_values_arr[bind_count] = binding_values[b];
                bind_count++;
            }
            VarBindings vbinds = { bind_names, bind_values_arr, bind_count };

            /* Проверяем фильтры для КАЖДОГО body atom */
            for (int i = 0; i < r->body_count && combination_valid; i++) {
                if (r->comparisons[i] && r->comparison_counts[i] > 0) {
                    if (!eval_comparisons(r->comparisons[i],
                                          r->comparison_counts[i],
                                          &vbinds)) {
                        combination_valid = 0;
                    }
                }
            }

            free(bind_names);
            free(bind_values_arr);
        }

        /* ===== ШАГ 4: Проверка negative atoms (stratified negation) ===== */
        if (combination_valid) {
            for (int n = 0; n < neg_count && combination_valid; n++) {
                int atom_idx = neg_indices[n];
                const char *pred = r->body_preds[atom_idx];
                int arity = r->body_arities[atom_idx];
                char *neg_args[32];
                int all_bound = 1;
                /* Все аргументы negative atom'а должны быть связаны через positive atoms
                 * (это requirement safety condition в Datalog) */
                for (int j = 0; j < arity; j++) {
                    int found_binding = -1;
                    for (int b = 0; b < r->arg_binding_count; b++) {
                        ArgBinding *bind = &r->arg_bindings[b];
                        for (int l = 0; l < bind->location_count; l++) {
                            if (bind->locations[l].atom_index == atom_idx &&
                                bind->locations[l].arg_index == j) {
                                found_binding = b; goto found;
                            }
                        }
                    }
                    found:
                    if (found_binding < 0) { all_bound = 0; break; }
                    neg_args[j] = binding_values[found_binding];
                }
                /* Если такой факт существует — negation fails, комбинация отбрасывается */
                if (all_bound && fact_exists(c, pred, arity, neg_args))
                    combination_valid = 0;
            }
        }

        /* ===== ШАГ 5: Создание head fact ===== */
        if (combination_valid && r->head_arity > 0) {
            char *head_args[32];
            int head_valid = 1;
            for (int j = 0; j < r->head_arity; j++) {
                int found_binding = -1;
                for (int b = 0; b < r->arg_binding_count; b++) {
                    if (r->arg_bindings[b].is_head && r->arg_bindings[b].head_arg_index == j) {
                        found_binding = b; break;
                    }
                }
                if (found_binding < 0) { head_valid = 0; break; }
                head_args[j] = binding_values[found_binding];
            }
            if (head_valid && !fact_exists(c, r->head, r->head_arity, head_args)) {
                add_fact_direct(c, r->head, r->head_arity, head_args);
                added++;
            }
        }

        /* Инкремент счётчика комбинаций */
        int carry = 1;
        for (int p = pos_count - 1; p >= 0 && carry; p--) {
            indices[p]++;
            if (indices[p] < pos_fact_counts[p]) carry = 0;
            else indices[p] = 0;
        }
        if (carry) break;
    }

    if (binding_values) free(binding_values);
    free(indices);
    for (int p = 0; p < pos_count; p++)
        if (pos_fact_lists[p]) free(pos_fact_lists[p]);
    free(pos_fact_lists);
    free(pos_fact_counts);
    return added;
}

/*
 * apply_rule: dispatcher — выбирает нужный apply_rule_* в зависимости от типа
 */
static int apply_rule(Config *c, const Rule *r) {
    if (r->aggregate_funcs && r->aggregate_funcs[0])
        return apply_rule_aggregate(c, r);
    if (r->arith_exprs) {
        for (int i = 0; i < r->body_count; i++)
            if (r->arith_exprs[i]) return apply_rule_arithmetic(c, r, i);
    }
    return apply_rule_general(c, r);
}

/* ========================================================================= */
/* ЧАСТЬ 4: STRATIFICATION — вычисление страт для правил                      */
/* ========================================================================= */

/*
 * strip_impl_suffix: убрать суффикс _impl_N из имени предиката [v1.1.1 FIX]
 *
 * Примеры:
 *   "blocked_impl_0" → "blocked"
 *   "ancestor" → "ancestor" (без изменений)
 *
 * ИСПРАВЛЕНИЕ v1.1.1: раньше было result[len] = '0' (символ '0'),
 * теперь result[len] = '\0' (нулевой терминатор).
 */
static char *strip_impl_suffix(const char *name) {
    const char *impl = strstr(name, "_impl_");
    if (impl) {
        size_t len = impl - name;
        char *result = malloc(len + 1);
        memcpy(result, name, len);
        result[len] = '\0';  /* v1.1.1 FIX: было '0' (символ) */
        return result;
    }
    return strdup(name);
}

/*
 * VisitedSet: множество посещённых предикатов для предотвращения
 * бесконечной рекурсии при вычислении страт.
 */
typedef struct {
    const char **items;
    int count;
    int capacity;
} VisitedSet;

static void visited_init(VisitedSet *v) {
    v->items = NULL; v->count = 0; v->capacity = 0;
}

static int visited_contains(const VisitedSet *v, const char *name) {
    for (int i = 0; i < v->count; i++)
        if (strcmp(v->items[i], name) == 0) return 1;
    return 0;
}

static void visited_add(VisitedSet *v, const char *name) {
    if (v->count >= v->capacity) {
        v->capacity = v->capacity == 0 ? 16 : v->capacity * 2;
        v->items = realloc(v->items, sizeof(const char*) * v->capacity);
    }
    v->items[v->count++] = name;
}

static void visited_remove_last(VisitedSet *v) {
    if (v->count > 0) v->count--;
}

static void visited_free(VisitedSet *v) {
    free(v->items);
    v->items = NULL; v->count = 0; v->capacity = 0;
}

/*
 * pred_strat_internal: рекурсивное вычисление страты предиката
 */
static int pred_strat_internal(const char *pred, const ClosureIR *c,
                               const char *canonical_head, VisitedSet *visited);

/*
 * rule_strat_fixed: вычислить stratum для правила
 * 
 * [КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ v1.1.2]
 * 
 * РАНЬШЕ: self-reference определялась как совпадение canonical forms
 * (после strip_impl_suffix), что приводило к тому, что copy-правило
 * `blocked <- blocked_impl_0` считалось self-reference и получало stratum 0.
 * 
 * СЕЙЧАС: self-reference = ТОЧНОЕ совпадение имён предикатов
 * (strcmp(body_pred, r->head) == 0). Copy-правила теперь правильно
 * наследуют stratum от impl-правил.
 * 
 * Пример:
 *   derive blocked(id) from depends(id, dep), not finished(dep)
 *   derive blocked(id) from blocked_impl_0(id)   ← copy-правило
 * 
 *   blocked_impl_0 содержит negation → stratum 1
 *   copy-правило blocked :- blocked_impl_0 теперь в stratum 1 (не 0!)
 */
static int rule_strat_fixed(const Rule *r, const ClosureIR *c,
                            const char *canonical_head, VisitedSet *visited) {
    int max_strat = 0;
    for (int i = 0; i < r->body_count; i++) {
        if (strcmp(r->body_preds[i], "__arith__") == 0) continue;

        const char *body_pred = r->body_preds[i];
        
        /* v1.1.2 FIX: Self-reference = ТОЧНОЕ совпадение имён,
         * а не совпадение после strip_impl_suffix */
        int is_self_ref = (strcmp(body_pred, r->head) == 0);

        if (is_self_ref) {
            /* Реальная рекурсия (self-reference через negation) */
            if (r->body_negative[i]) {
                int body_strat = pred_strat_internal(body_pred, c, canonical_head, visited);
                int required = body_strat + 1;
                if (required > max_strat) max_strat = required;
            }
            continue;
        }

        /* Разные предикаты — вычисляем stratum body */
        int body_strat = pred_strat_internal(body_pred, c, canonical_head, visited);
        int required = r->body_negative[i] ? (body_strat + 1) : body_strat;
        if (required > max_strat) max_strat = required;
    }
    return max_strat;
}

/*
 * pred_strat_internal: вычислить максимальный stratum среди всех правил
 * с данным head-предикатом.
 * 
 * [КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ v1.1.2]
 * 
 * Self-reference теперь определяется как ТОЧНОЕ совпадение имён,
 * а не совпадение canonical forms.
 */
static int pred_strat_internal(const char *pred, const ClosureIR *c,
                               const char *canonical_head, VisitedSet *visited) {
    if (visited_contains(visited, pred)) return 0;

    /* v1.1.2 FIX: Self-reference = ТОЧНОЕ совпадение,
     * не canonical form match */
    int same_exact = (strcmp(pred, canonical_head) == 0);
    if (same_exact) return 0;
    
    /* Если pred — это impl-версия canonical_head (например, "blocked_impl_0" vs "blocked"),
     * это НЕ self-reference, а зависимость от другого предиката.
     * Нужно вычислить stratum для pred. */

    /* Проверка: является ли предикат base (не имеет правил)? */
    int is_base = 1;
    for (Rule *r = c->rules; r; r = r->next) {
        if (strcmp(r->head, pred) == 0) { is_base = 0; break; }
    }
    if (is_base) return 0;

    visited_add(visited, pred);
    int max_strat = 0;
    for (Rule *r = c->rules; r; r = r->next) {
        if (strcmp(r->head, pred) == 0) {
            int r_strat = rule_strat_fixed(r, c, canonical_head, visited);
            if (r_strat > max_strat) max_strat = r_strat;
        }
    }
    visited_remove_last(visited);
    return max_strat;
}

/* ========================================================================= */
/* ЧАСТЬ 5: SATURATION — итеративное насыщение до fixpoint                   */
/* ========================================================================= */

/*
 * saturate: применить все правила по стратам до достижения fixpoint
 *
 * Это ядро декларативной семантики SuperLang. На каждой страте:
 *   1. Применяем все правила со stratum == s
 *   2. Повторяем до тех пор, пока на итерации не добавится ни одного факта
 *   3. Переходим к следующей страте
 *
 * Используется naive evaluation (не semi-naive), что работает для
 * небольших программ, но не масштабируется.
 * 
 * TODO: semi-naive evaluation для production
 */
void saturate(Config *c, const ClosureIR *closure) {
    printf("\n[Saturation]\n");
    
    /* Вычисляем stratum для каждого правила */
    int *strats = malloc(closure->rule_count * sizeof(int));
    int max_strat = 0;
    int i = 0;
    for (Rule *r = closure->rules; r; r = r->next) {
        char *canonical = strip_impl_suffix(r->head);
        VisitedSet visited;
        visited_init(&visited);
        strats[i] = rule_strat_fixed(r, closure, canonical, &visited);
        visited_free(&visited);
        free(canonical);
        if (strats[i] > max_strat) max_strat = strats[i];
        i++;
    }
    printf("  Stratification: %d levels (0 to %d)\n", max_strat + 1, max_strat);

    /* Отладочный вывод правил по стратам */
    printf("\n  [DEBUG] Rules by stratum:\n");
    for (int s = 0; s <= max_strat; s++) {
        printf("    Strat %d:\n", s);
        i = 0;
        for (Rule *r = closure->rules; r; r = r->next) {
            if (strats[i] == s) {
                printf("      %s <- ", r->head);
                for (int j = 0; j < r->body_count; j++) {
                    if (j > 0) printf(", ");
                    if (r->body_negative[j]) printf("not ");
                    printf("%s", r->body_preds[j]);
                }
                printf("\n");
            }
            i++;
        }
    }
    printf("\n");

    /* Применяем правила по стратам */
    int total_added = 0;
    for (int s = 0; s <= max_strat; s++) {
        printf("  Strat %d:\n", s);
        int iteration = 0;
        while (1) {
            iteration++;
            int added_this_round = 0;
            i = 0;
            for (Rule *r = closure->rules; r; r = r->next) {
                if (strats[i] == s) {
                    int added = apply_rule(c, r);
                    if (added > 0) {
                        printf("    [DEBUG] Rule '%s' added %d facts\n", r->head, added);
                    }
                    added_this_round += added;
                }
                i++;
            }
            printf("    Iteration %d: +%d facts\n", iteration, added_this_round);
            total_added += added_this_round;
            if (added_this_round == 0) {
                printf("    Fixpoint reached after %d iterations\n", iteration);
                break;
            }
        }
    }
    printf("  Total facts added: %d\n", total_added);
    free(strats);

    /* Диагностический вывод */
    printf("\n[Diagnostic] Facts by predicate:\n");
    for (Fact *f = c->facts; f; f = f->next) {
        int count = 0;
        for (Fact *f2 = c->facts; f2; f2 = f2->next)
            if (strcmp(f->predicate, f2->predicate) == 0) count++;
        int already = 0;
        for (Fact *f2 = c->facts; f2 != f; f2 = f2->next)
            if (strcmp(f->predicate, f2->predicate) == 0) { already = 1; break; }
        if (!already) printf("    %s: %d facts\n", f->predicate, count);
    }
}
