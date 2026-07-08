#include "runtime.h"
#include "synthesizer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

Config *config_new(void) {
    return calloc(1, sizeof(Config));
}

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

void config_add_fact(Config *c, const char *pred, int arity, ...) {
    va_list args;
    va_start(args, arity);
    char **new_args = malloc(sizeof(char*) * arity);
    for (int i = 0; i < arity; i++) {
        new_args[i] = strdup(va_arg(args, const char*));
    }
    va_end(args);

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
    Fact *f = calloc(1, sizeof(Fact));
    f->predicate = strdup(pred);
    f->args = new_args;
    f->arity = arity;
    f->next = c->facts;
    c->facts = f;
    c->count++;
}

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

void config_visit_facts(Config *c, FactVisitor visitor, void *ctx) {
    if (!c || !visitor) return;
    for (Fact *f = c->facts; f; f = f->next) {
        visitor(f->predicate, f->arity, (const char **)f->args, ctx);
    }
}

typedef struct {
    const char **names;
    const char **values;
    int count;
} VarBindings;

/* ========================================================================= */
/* Expression evaluator — recursive descent with built-in function support   */
/* v1.1: added EXPR_CALL handling for sqrt, pow, sin, cos, etc.              */
/* ========================================================================= */

static double eval_expr(const Expr *e, const VarBindings *binds) {
    if (!e) return 0.0;

    switch (e->type) {
        case EXPR_NUMBER:
            return e->number;

        case EXPR_VARIABLE:
            /* Look up variable value, parse as double */
            for (int i = 0; i < binds->count; i++) {
                if (strcmp(binds->names[i], e->var_name) == 0) {
                    char *endptr;
                    double val = strtod(binds->values[i], &endptr);
                    if (*endptr == '\0') return val;
                    return 0.0;  /* not a valid number */
                }
            }
            return 0.0;  /* variable not bound */

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

        /* NEW in v1.1: built-in function calls */
        case EXPR_CALL: {
            /* Evaluate all arguments first */
            double arg_values[8];  /* max 8 arguments supported */
            int n_args = e->call.arg_count;
            if (n_args > 8) n_args = 8;
            for (int i = 0; i < n_args; i++) {
                arg_values[i] = eval_expr(e->call.args[i], binds);
            }
            const char *fn = e->call.func_name;

            /* 1-argument functions */
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
            /* 2-argument functions */
            else if (n_args == 2) {
                double x = arg_values[0];
                double y = arg_values[1];
                if (strcmp(fn, "pow") == 0)   return pow(x, y);
                if (strcmp(fn, "fmod") == 0)  return (y != 0.0) ? fmod(x, y) : 0.0;
                if (strcmp(fn, "atan2") == 0) return atan2(y, x);
                if (strcmp(fn, "fmin") == 0)  return fmin(x, y);   /* ← ИЗМЕНЕНО с min */
                if (strcmp(fn, "fmax") == 0)  return fmax(x, y);   /* ← ИЗМЕНЕНО с max */
            }
            /* Unknown function or wrong arity — return 0 */
            return 0.0;
        }
    }
    return 0.0;
}

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

static int apply_rule_aggregate(Config *c, const Rule *r) {
    int added = 0;
    char *agg_func = r->aggregate_funcs[0];
    int agg_field = r->aggregate_fields[0];
    int src_arity = r->body_arities[0];
    int dst_arity = r->head_arity;
    int needs_numeric = (strcmp(agg_func, "count") != 0);

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

    for (Fact *f = c->facts; f; f = f->next) {
        if (strcmp(f->predicate, r->body_preds[0]) != 0) continue;
        if (f->arity != src_arity) continue;

        int key_count = src_arity - 1;
        char **key = malloc(sizeof(char*) * key_count);
        int ki = 0;
        for (int i = 0; i < src_arity; i++) {
            if (i != agg_field) key[ki++] = f->args[i];
        }

        double value = 0.0;
        if (needs_numeric) {
            char *endptr;
            value = strtod(f->args[agg_field], &endptr);
            if (*endptr != '\0') { free(key); continue; }
        }

        Group *g = NULL;
        for (int i = 0; i < group_count; i++) {
            int match = 1;
            for (int j = 0; j < key_count; j++) {
                if (strcmp(groups[i].key[j], key[j]) != 0) { match = 0; break; }
            }
            if (match) { g = &groups[i]; free(key); break; }
        }

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
        if (result == (int)result && fabs(result) < 1e15)
            snprintf(buf, sizeof(buf), "%d", (int)result);
        else
            snprintf(buf, sizeof(buf), "%.2f", result);
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

static int apply_rule_arithmetic(Config *c, const Rule *r, int arith_slot) {
    int added = 0;
    int regular_slots[16], regular_count = 0;
    for (int i = 0; i < r->body_count && regular_count < 16; i++) {
        if (strcmp(r->body_preds[i], "__arith__") != 0)
            regular_slots[regular_count++] = i;
    }
    if (regular_count == 0) return added;

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

    int atom_to_pos[32];
    for (int i = 0; i < 32; i++) atom_to_pos[i] = -1;
    for (int s = 0; s < regular_count; s++) atom_to_pos[regular_slots[s]] = s;

    int indices[16] = {0};
    while (1) {
        int valid = 1;
        for (int s = 0; s < regular_count; s++)
            if (indices[s] >= fact_counts[s]) { valid = 0; break; }
        if (!valid) break;

        Fact *current_facts[16];
        for (int s = 0; s < regular_count; s++)
            current_facts[s] = fact_lists[s][indices[s]];

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
            for (int l = 1; l < bind->location_count; l++) {
                int p = atom_to_pos[bind->locations[l].atom_index];
                if (p < 0) { combination_valid = 0; break; }
                if (strcmp(value, current_facts[p]->args[bind->locations[l].arg_index]) != 0) {
                    combination_valid = 0; break;
                }
            }
        }

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

    /* Проверка фильтров сравнения (v1.0) — ИСПРАВЛЕНО */
    if (r->comparisons && r->comparisons[arith_slot] && r->comparison_counts[arith_slot] > 0) {
        if (!eval_comparisons(r->comparisons[arith_slot], r->comparison_counts[arith_slot], &vbinds)) {
            combination_valid = 0;
        }
    }

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
                if (j == r->head_arity - 1) {
                    char buf[64];
                    if (val == (int)val && fabs(val) < 1e15)
                        snprintf(buf, sizeof(buf), "%d", (int)val);
                    else
                        snprintf(buf, sizeof(buf), "%.2f", val);
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

static int apply_rule_general(Config *c, const Rule *r) {
    if (r->body_count == 0) return 0;

    /* Разделяем positive и negative atoms */
    int pos_indices[32], neg_indices[32];
    int pos_count = 0, neg_count = 0;
    for (int i = 0; i < r->body_count; i++) {
        if (r->body_negative[i]) neg_indices[neg_count++] = i;
        else pos_indices[pos_count++] = i;
    }
    if (pos_count == 0) return 0;

    /* Собираем списки фактов для каждого positive atom */
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
            for (int j = 0; j <= p; j++) if (pos_fact_lists[j]) free(pos_fact_lists[j]);
            free(pos_fact_lists);
            free(pos_fact_counts);
            return 0;
        }
    }

    /* Маппинг atom_index -> pos slot */
    int atom_to_pos[32];
    for (int i = 0; i < 32; i++) atom_to_pos[i] = -1;
    for (int p = 0; p < pos_count; p++) atom_to_pos[pos_indices[p]] = p;

    int *indices = calloc(pos_count, sizeof(int));
    int added = 0;
    char **binding_values = r->arg_binding_count > 0
        ? malloc(sizeof(char*) * r->arg_binding_count) : NULL;

    while (1) {
        int valid_indices = 1;
        for (int p = 0; p < pos_count; p++)
            if (indices[p] >= pos_fact_counts[p]) { valid_indices = 0; break; }
        if (!valid_indices) break;

        Fact *current_facts[32];
        for (int p = 0; p < pos_count; p++)
            current_facts[p] = pos_fact_lists[p][indices[p]];

        /* ===== ШАГ 1: Unification (связывание переменных) ===== */
        int combination_valid = 1;
        for (int b = 0; b < r->arg_binding_count; b++) {
            ArgBinding *bind = &r->arg_bindings[b];
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
            for (int l = 0; l < bind->location_count; l++) {
                if (l == first_pos_loc) continue;
                int atom_idx = bind->locations[l].atom_index;
                int pos = atom_to_pos[atom_idx];
                if (pos < 0) continue;
                if (strcmp(value, current_facts[pos]->args[bind->locations[l].arg_index]) != 0) {
                    combination_valid = 0; break;
                }
            }
            if (!combination_valid) break;
        }

        /* ===== ШАГ 2: Проверка фильтров сравнения (НОВОЕ v1.0) ===== */
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

        /* ===== ШАГ 3: Проверка negative atoms ===== */
        if (combination_valid) {
            for (int n = 0; n < neg_count && combination_valid; n++) {
                int atom_idx = neg_indices[n];
                const char *pred = r->body_preds[atom_idx];
                int arity = r->body_arities[atom_idx];
                char *neg_args[32];
                int all_bound = 1;
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
                if (all_bound && fact_exists(c, pred, arity, neg_args))
                    combination_valid = 0;
            }
        }

        /* ===== ШАГ 4: Создание head fact ===== */
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

static int apply_rule(Config *c, const Rule *r) {
    if (r->aggregate_funcs && r->aggregate_funcs[0])
        return apply_rule_aggregate(c, r);
    if (r->arith_exprs) {
        for (int i = 0; i < r->body_count; i++)
            if (r->arith_exprs[i]) return apply_rule_arithmetic(c, r, i);
    }
    return apply_rule_general(c, r);
}

static char *strip_impl_suffix(const char *name) {
    const char *impl = strstr(name, "_impl_");
    if (impl) {
        size_t len = impl - name;
        char *result = malloc(len + 1);
        memcpy(result, name, len);
        result[len] = '\0';
        return result;
    }
    return strdup(name);
}

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

static int pred_strat_internal(const char *pred, const ClosureIR *c,
                               const char *canonical_head, VisitedSet *visited);

static int rule_strat_fixed(const Rule *r, const ClosureIR *c,
                            const char *canonical_head, VisitedSet *visited) {
    int max_strat = 0;
    for (int i = 0; i < r->body_count; i++) {
        if (strcmp(r->body_preds[i], "__arith__") == 0) continue;

        char *body_canonical = strip_impl_suffix(r->body_preds[i]);
        int is_self_ref = (strcmp(body_canonical, canonical_head) == 0);
        free(body_canonical);
        if (is_self_ref) continue;

        int body_strat = pred_strat_internal(r->body_preds[i], c, canonical_head, visited);
        int required = r->body_negative[i] ? (body_strat + 1) : body_strat;
        if (required > max_strat) max_strat = required;
    }
    return max_strat;
}

static int pred_strat_internal(const char *pred, const ClosureIR *c,
                               const char *canonical_head, VisitedSet *visited) {
    if (visited_contains(visited, pred)) return 0;

    char *pred_canonical = strip_impl_suffix(pred);
    int same_canonical = (strcmp(pred_canonical, canonical_head) == 0);
    free(pred_canonical);
    if (same_canonical) return 0;

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

void saturate(Config *c, const ClosureIR *closure) {
    printf("\n[Saturation]\n");
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

    int total_added = 0;
    for (int s = 0; s <= max_strat; s++) {
        printf("  Strat %d:\n", s);
        int iteration = 0;
        while (1) {
            iteration++;
            int added_this_round = 0;
            i = 0;
            for (Rule *r = closure->rules; r; r = r->next) {
                if (strats[i] == s) added_this_round += apply_rule(c, r);
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
