#include "runtime.h"
#include "synthesizer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
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
            for (int i = 0; i < binds->count; i++) {
                if (strcmp(binds->names[i], e->var_name) == 0) {
                    char *endptr;
                    double val = strtod(binds->values[i], &endptr);
                    if (*endptr == '\0') return val;
                    return 0.0;
                }
            }
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
            double arg_values[8];
            int n_args = e->call.arg_count;
            if (n_args > 8) n_args = 8;
            for (int i = 0; i < n_args; i++) {
                arg_values[i] = eval_expr(e->call.args[i], binds);
            }
            const char *fn = e->call.func_name;

            if (n_args == 1) {
                double x = arg_values[0];
                if (strcmp(fn, "sqrt") == 0) return (x >= 0.0) ? sqrt(x) : 0.0;
                if (strcmp(fn, "abs") == 0) return fabs(x);
                if (strcmp(fn, "sin") == 0) return sin(x);
                if (strcmp(fn, "cos") == 0) return cos(x);
                if (strcmp(fn, "tan") == 0) return tan(x);
                if (strcmp(fn, "asin") == 0) return asin(x);
                if (strcmp(fn, "acos") == 0) return acos(x);
                if (strcmp(fn, "atan") == 0) return atan(x);
                if (strcmp(fn, "exp") == 0) return exp(x);
                if (strcmp(fn, "log") == 0) return (x > 0.0) ? log(x) : 0.0;
                if (strcmp(fn, "log10") == 0) return (x > 0.0) ? log10(x) : 0.0;
                if (strcmp(fn, "floor") == 0) return floor(x);
                if (strcmp(fn, "ceil") == 0) return ceil(x);
                if (strcmp(fn, "round") == 0) return round(x);
            }
            else if (n_args == 2) {
                double x = arg_values[0];
                double y = arg_values[1];
                if (strcmp(fn, "pow") == 0) return pow(x, y);
                if (strcmp(fn, "fmod") == 0) return (y != 0.0) ? fmod(x, y) : 0.0;
                if (strcmp(fn, "atan2") == 0) return atan2(y, x);
                if (strcmp(fn, "fmin") == 0) return fmin(x, y);
                if (strcmp(fn, "fmax") == 0) return fmax(x, y);
            }
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

/* ========================================================================= */
/* ИЗМЕНЕНИЕ v1.2: Множественная арифметика                                  */
/* - Убран параметр int arith_slot                                           */
/* - Цикл по всем arith_exprs для вычисления всех выражений                  */
/* - Использование ArgBindings для привязки к правильным позициям в head     */
/* ========================================================================= */

static int apply_rule_arithmetic(Config *c, const Rule *r) {
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

            /* ИЗМЕНЕНИЕ v1.2: Проверка ВСЕХ фильтров сравнения */
            for (int i = 0; i < r->body_count && combination_valid; i++) {
                if (r->comparisons && r->comparisons[i] && r->comparison_counts[i] > 0) {
                    if (!eval_comparisons(r->comparisons[i], r->comparison_counts[i], &vbinds)) {
                        combination_valid = 0;
                    }
                }
            }

            if (combination_valid) {
                /* ИЗМЕНЕНИЕ v1.2: Вычисление ВСЕЙ арифметики */
                double arith_vals[16] = {0};
                for (int i = 0; i < r->body_count; i++) {
                    if (r->arith_exprs[i]) {
                        arith_vals[i] = eval_expr(r->arith_exprs[i], &vbinds);
                    }
                }

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
                        /* ИЗМЕНЕНИЕ v1.2: Ищем arith_result_var для этой позиции */
                        int arith_slot = -1;
                        if (found >= 0) {
                            const char *var_name = r->arg_bindings[found].var_name;
                            for (int i = 0; i < r->body_count; i++) {
                                if (r->arith_result_vars && r->arith_result_vars[i] &&
                                    strcmp(r->arith_result_vars[i], var_name) == 0) {
                                    arith_slot = i;
                                    break;
                                }
                            }
                        }
                        
                        if (arith_slot >= 0) {
                            char buf[64];
                            double val = arith_vals[arith_slot];
                            if (val == (int)val && fabs(val) < 1e15)
                                snprintf(buf, sizeof(buf), "%d", (int)val);
                            else
                                snprintf(buf, sizeof(buf), "%.2f", val);
                            result_args[j] = strdup(buf);
                            is_our_alloc[j] = 1;
                        } else {
                            head_valid = 0;
                            break;
                        }
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

    int pos_indices[32], neg_indices[32];
    int pos_count = 0, neg_count = 0;
    for (int i = 0; i < r->body_count; i++) {
        if (r->body_negative[i]) neg_indices[neg_count++] = i;
        else pos_indices[pos_count++] = i;
    }

    if (pos_count == 0) return 0;

    Fact **fact_lists[32];
    int fact_counts[32];
    for (int p = 0; p < pos_count; p++) {
        int slot = pos_indices[p];
        fact_lists[p] = NULL;
        fact_counts[p] = 0;
        int fact_cap = 0;
        for (Fact *f = c->facts; f; f = f->next) {
            if (strcmp(f->predicate, r->body_preds[slot]) != 0) continue;
            if (f->arity != r->body_arities[slot]) continue;
            if (fact_counts[p] >= fact_cap) {
                fact_cap = fact_cap == 0 ? 16 : fact_cap * 2;
                fact_lists[p] = realloc(fact_lists[p], sizeof(Fact*) * fact_cap);
            }
            fact_lists[p][fact_counts[p]++] = f;
        }
    }

    int atom_to_pos[32];
    for (int i = 0; i < 32; i++) atom_to_pos[i] = -1;
    for (int p = 0; p < pos_count; p++) atom_to_pos[pos_indices[p]] = p;

    int indices[32] = {0};
    int added = 0;

    while (1) {
        int valid = 1;
        for (int p = 0; p < pos_count; p++)
            if (indices[p] >= fact_counts[p]) { valid = 0; break; }
        if (!valid) break;

        Fact *current_facts[32];
        for (int p = 0; p < pos_count; p++)
            current_facts[p] = fact_lists[p][indices[p]];

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
            for (int n = 0; n < neg_count && combination_valid; n++) {
                int slot = neg_indices[n];
                const char *neg_pred = r->body_preds[slot];
                int neg_arity = r->body_arities[slot];
                
                char *neg_args[32];
                for (int a = 0; a < neg_arity; a++) neg_args[a] = NULL;
                
                for (int b = 0; b < r->arg_binding_count; b++) {
                    ArgBinding *bind = &r->arg_bindings[b];
                    for (int l = 0; l < bind->location_count; l++) {
                        if (bind->locations[l].atom_index == slot && binding_values[b] != NULL) {
                            neg_args[bind->locations[l].arg_index] = binding_values[b];
                        }
                    }
                }
                
                if (fact_exists(c, neg_pred, neg_arity, neg_args)) {
                    combination_valid = 0;
                }
            }
        }

        if (combination_valid) {
            char **result_args = calloc(r->head_arity, sizeof(char*));
            int *is_our_alloc = calloc(r->head_arity, sizeof(int));
            int head_valid = 1;

            for (int j = 0; j < r->head_arity && head_valid; j++) {
                int found = -1;
                for (int b = 0; b < r->arg_binding_count; b++) {
                    if (r->arg_bindings[b].is_head && r->arg_bindings[b].head_arg_index == j) {
                        found = b; break;
                    }
                }
                if (found >= 0 && binding_values[found] != NULL) {
                    result_args[j] = binding_values[found];
                    is_our_alloc[j] = 0;
                } else {
                    head_valid = 0;
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

        if (binding_values) free(binding_values);

        int carry = 1;
        for (int p = pos_count - 1; p >= 0 && carry; p--) {
            indices[p]++;
            if (indices[p] < fact_counts[p]) carry = 0;
            else indices[p] = 0;
        }
        if (carry) break;
    }

    for (int p = 0; p < pos_count; p++) free(fact_lists[p]);
    return added;
}

/* ИЗМЕНЕНИЕ v1.2: Диспетчер теперь вызывает apply_rule_arithmetic БЕЗ arith_slot */
static int apply_rule(Config *c, const Rule *r) {
    if (r->aggregate_funcs && r->aggregate_funcs[0]) {
        return apply_rule_aggregate(c, r);
    }
    
    int has_arith = 0;
    if (r->arith_exprs) {
        for (int i = 0; i < r->body_count; i++) {
            if (r->arith_exprs[i]) { has_arith = 1; break; }
        }
    }
    if (has_arith) return apply_rule_arithmetic(c, r);
    
    return apply_rule_general(c, r);
}

void saturate(Config *c, const ClosureIR *closure) {
    if (!c || !closure) return;
    
    int changed = 1;
    int max_iterations = 100;
    int iteration = 0;
    
    while (changed && iteration < max_iterations) {
        changed = 0;
        for (Rule *r = closure->rules; r; r = r->next) {
            int added = apply_rule(c, r);
            if (added > 0) changed = 1;
        }
        iteration++;
    }
}
