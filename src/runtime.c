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

/* ====================================================================== */
/* Expression evaluator (for arithmetic rules)                            */
/* ====================================================================== */

typedef struct {
    const char **names;
    const char **values;
    int count;
} VarBindings;

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
            }
            return 0.0;
        }
            
        case EXPR_UNARY_MINUS:
            return -eval_expr(e->operand, binds);
    }
    return 0.0;
}

/* ====================================================================== */
/* Aggregate rules (special-case, unchanged)                              */
/* ====================================================================== */

static int apply_rule_aggregate(Config *c, const Rule *r) {
    int added = 0;
    char *agg_func = r->aggregate_funcs[0];
    int agg_field = r->aggregate_fields[0];
    int src_arity = r->body_arities[0];
    int dst_arity = r->head_arity;
    
    typedef struct {
        char **key;
        int key_count;
        double sum;
        int count;
        double min;
        double max;
    } Group;
    
    Group *groups = NULL;
    int group_count = 0;
    int group_capacity = 0;
    
    for (Fact *f = c->facts; f; f = f->next) {
        if (strcmp(f->predicate, r->body_preds[0]) != 0) continue;
        if (f->arity != src_arity) continue;
        
        int key_count = src_arity - 1;
        char **key = malloc(sizeof(char*) * key_count);
        int ki = 0;
        for (int i = 0; i < src_arity; i++) {
            if (i != agg_field) {
                key[ki++] = f->args[i];
            }
        }
        
        char *endptr;
        double value = strtod(f->args[agg_field], &endptr);
        if (*endptr != '\0') {
            free(key);
            continue;
        }
        
        Group *g = NULL;
        for (int i = 0; i < group_count; i++) {
            int match = 1;
            for (int j = 0; j < key_count; j++) {
                if (strcmp(groups[i].key[j], key[j]) != 0) {
                    match = 0;
                    break;
                }
            }
            if (match) {
                g = &groups[i];
                free(key);
                break;
            }
        }
        
        if (!g) {
            if (group_count >= group_capacity) {
                group_capacity = group_capacity == 0 ? 16 : group_capacity * 2;
                groups = realloc(groups, sizeof(Group) * group_capacity);
            }
            g = &groups[group_count++];
            g->key = key;
            g->key_count = key_count;
            g->sum = 0;
            g->count = 0;
            g->min = value;
            g->max = value;
        }
        
        g->sum += value;
        g->count++;
        if (value < g->min) g->min = value;
        if (value > g->max) g->max = value;
    }
    
    for (int i = 0; i < group_count; i++) {
        Group *g = &groups[i];
        double result;
        if (strcmp(agg_func, "sum") == 0) result = g->sum;
        else if (strcmp(agg_func, "count") == 0) result = g->count;
        else if (strcmp(agg_func, "min") == 0) result = g->min;
        else if (strcmp(agg_func, "max") == 0) result = g->max;
        else { free(g->key); continue; }
        
        char **args = malloc(sizeof(char*) * dst_arity);
        int ai = 0;
        for (int j = 0; j < g->key_count; j++) {
            args[ai++] = g->key[j];
        }
        char buf[64];
        if (result == (int)result && fabs(result) < 1e15) {
            snprintf(buf, sizeof(buf), "%d", (int)result);
        } else {
            snprintf(buf, sizeof(buf), "%.2f", result);
        }
        args[ai] = strdup(buf);
        
        if (!fact_exists(c, r->head, dst_arity, args)) {
            add_fact_direct(c, r->head, dst_arity, args);
            added++;
        } else {
            free(args[ai]);
        }
        free(args);
        free(g->key);
    }
    free(groups);
    
    return added;
}

/* ====================================================================== */
/* Arithmetic rules (special-case, unchanged)                             */
/* ====================================================================== */

static int apply_rule_arithmetic(Config *c, const Rule *r, int arith_slot) {
    int added = 0;
    
    /* Find all regular (non-arithmetic) body atoms */
    int regular_slots[16];
    int regular_count = 0;
    for (int i = 0; i < r->body_count && regular_count < 16; i++) {
        if (strcmp(r->body_preds[i], "__arith__") != 0) {
            regular_slots[regular_count++] = i;
        }
    }
    
    if (regular_count == 0) return added;
    
    /* Collect matching facts for each regular body atom */
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
    
    /* Iterate through all combinations (cross product) */
    int indices[16] = {0};
    
    while (1) {
        int valid = 1;
        for (int s = 0; s < regular_count; s++) {
            if (indices[s] >= fact_counts[s]) {
                valid = 0;
                break;
            }
        }
        if (!valid) break;
        
        Fact *current_facts[16];
        int total_args = 0;
        for (int s = 0; s < regular_count; s++) {
            current_facts[s] = fact_lists[s][indices[s]];
            total_args += current_facts[s]->arity;
        }
        
        /* Build variable bindings: args become "arg0", "arg1", ... */
        char **bind_names = malloc(sizeof(char*) * total_args);
        const char **bind_values = malloc(sizeof(const char*) * total_args);
        int bind_count = 0;
        char name_bufs[64][32];
        
        for (int s = 0; s < regular_count; s++) {
            Fact *f = current_facts[s];
            for (int a = 0; a < f->arity; a++) {
                snprintf(name_bufs[bind_count], 32, "arg%d", bind_count);
                bind_names[bind_count] = strdup(name_bufs[bind_count]);
                bind_values[bind_count] = f->args[a];
                bind_count++;
            }
        }
        
        VarBindings binds;
        binds.names = (const char**)bind_names;
        binds.values = bind_values;
        binds.count = bind_count;
        
        double val = eval_expr(r->arith_exprs[arith_slot], &binds);
        
        /* Build head args */
        char **result_args = malloc(sizeof(char*) * r->head_arity);
        int filled = 0;
        
        for (int s = 0; s < regular_count && filled < r->head_arity - 1; s++) {
            Fact *f = current_facts[s];
            for (int a = 0; a < f->arity && filled < r->head_arity - 1; a++) {
                result_args[filled++] = f->args[a];
            }
        }
        
        while (filled < r->head_arity - 1) {
            result_args[filled++] = "";
        }
        
        char buf[64];
        if (val == (int)val && fabs(val) < 1e15) {
            snprintf(buf, sizeof(buf), "%d", (int)val);
        } else {
            snprintf(buf, sizeof(buf), "%.2f", val);
        }
        result_args[r->head_arity - 1] = strdup(buf);
        
        if (!fact_exists(c, r->head, r->head_arity, result_args)) {
            add_fact_direct(c, r->head, r->head_arity, result_args);
            added++;
        }
        
        free(result_args[r->head_arity - 1]);
        free(result_args);
        
        for (int i = 0; i < bind_count; i++) free(bind_names[i]);
        free(bind_names);
        free(bind_values);
        
        /* Increment indices (odometer) */
        int carry = 1;
        for (int s = regular_count - 1; s >= 0 && carry; s--) {
            indices[s]++;
            if (indices[s] < fact_counts[s]) {
                carry = 0;
            } else {
                indices[s] = 0;
            }
        }
        if (carry) break;
    }
    
    for (int s = 0; s < regular_count; s++) {
        free(fact_lists[s]);
    }
    
    return added;
}

/* ====================================================================== */
/* GENERAL N-WAY JOIN — replaces all hardcoded patterns                   */
/* ====================================================================== */

static int apply_rule_general(Config *c, const Rule *r) {
    if (r->body_count == 0) return 0;
    
    /* Step 1: Separate positive and negative body atoms */
    int pos_indices[32], neg_indices[32];
    int pos_count = 0, neg_count = 0;
    
    for (int i = 0; i < r->body_count; i++) {
        if (r->body_negative[i]) {
            neg_indices[neg_count++] = i;
        } else {
            pos_indices[pos_count++] = i;
        }
    }
    
    if (pos_count == 0) return 0;
    
    /* Step 2: Collect matching facts for each positive atom */
    Fact ***pos_fact_lists = malloc(sizeof(Fact**) * pos_count);
    int *pos_fact_counts = calloc(pos_count, sizeof(int));
    
    for (int p = 0; p < pos_count; p++) {
        int atom_idx = pos_indices[p];
        const char *pred = r->body_preds[atom_idx];
        int arity = r->body_arities[atom_idx];
        
        int count = 0;
        for (Fact *f = c->facts; f; f = f->next) {
            if (strcmp(f->predicate, pred) == 0 && f->arity == arity) {
                count++;
            }
        }
        
        pos_fact_counts[p] = count;
        pos_fact_lists[p] = count > 0 ? malloc(sizeof(Fact*) * count) : NULL;
        
        int fi = 0;
        for (Fact *f = c->facts; f; f = f->next) {
            if (strcmp(f->predicate, pred) == 0 && f->arity == arity) {
                pos_fact_lists[p][fi++] = f;
            }
        }
        
        if (count == 0) {
            for (int j = 0; j <= p; j++) {
                if (pos_fact_lists[j]) free(pos_fact_lists[j]);
            }
            free(pos_fact_lists);
            free(pos_fact_counts);
            return 0;
        }
    }
    
    /* Step 3: Create mapping from global atom_idx to positive position */
    int atom_to_pos[32];
    for (int i = 0; i < 32; i++) atom_to_pos[i] = -1;
    for (int p = 0; p < pos_count; p++) {
        atom_to_pos[pos_indices[p]] = p;
    }
    
    /* Step 4: Iterate through all combinations */
    int *indices = calloc(pos_count, sizeof(int));
    int added = 0;
    
    char **binding_values = r->arg_binding_count > 0
        ? malloc(sizeof(char*) * r->arg_binding_count)
        : NULL;
    
    while (1) {
        int valid_indices = 1;
        for (int p = 0; p < pos_count; p++) {
            if (indices[p] >= pos_fact_counts[p]) {
                valid_indices = 0;
                break;
            }
        }
        if (!valid_indices) break;
        
        Fact *current_facts[32];
        for (int p = 0; p < pos_count; p++) {
            current_facts[p] = pos_fact_lists[p][indices[p]];
        }
        
        /* Step 5: Build bindings — only from POSITIVE atoms */
        int combination_valid = 1;
        
        for (int b = 0; b < r->arg_binding_count; b++) {
            ArgBinding *bind = &r->arg_bindings[b];
            
            /* Find first location in a positive atom */
            int first_pos_loc = -1;
            for (int l = 0; l < bind->location_count; l++) {
                int atom_idx = bind->locations[l].atom_index;
                if (atom_to_pos[atom_idx] >= 0) {
                    first_pos_loc = l;
                    break;
                }
            }
            
            if (first_pos_loc < 0) {
                /* Variable not bound by any positive atom — should not happen */
                combination_valid = 0;
                break;
            }
            
            ArgLocation *first = &bind->locations[first_pos_loc];
            int pos_idx = atom_to_pos[first->atom_index];
            char *value = current_facts[pos_idx]->args[first->arg_index];
            binding_values[b] = value;
            
            /* Check unification with other POSITIVE atom locations */
            for (int l = 0; l < bind->location_count; l++) {
                if (l == first_pos_loc) continue;
                
                int atom_idx = bind->locations[l].atom_index;
                int pos = atom_to_pos[atom_idx];
                if (pos < 0) continue; /* Skip negative atoms */
                
                char *other = current_facts[pos]->args[bind->locations[l].arg_index];
                if (strcmp(value, other) != 0) {
                    combination_valid = 0;
                    break;
                }
            }
            if (!combination_valid) break;
        }
        
        /* Step 6: Check negative atoms */
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
                                found_binding = b;
                                goto found;
                            }
                        }
                    }
                    found:
                    
                    if (found_binding < 0) {
                        all_bound = 0;
                        break;
                    }
                    neg_args[j] = binding_values[found_binding];
                }
                
                if (all_bound && fact_exists(c, pred, arity, neg_args)) {
                    combination_valid = 0;
                }
            }
        }
        
        /* Step 7: Build head fact */
        if (combination_valid && r->head_arity > 0) {
            char *head_args[32];
            int head_valid = 1;
            
            for (int j = 0; j < r->head_arity; j++) {
                int found_binding = -1;
                for (int b = 0; b < r->arg_binding_count; b++) {
                    if (r->arg_bindings[b].is_head &&
                        r->arg_bindings[b].head_arg_index == j) {
                        found_binding = b;
                        break;
                    }
                }
                if (found_binding < 0) {
                    head_valid = 0;
                    break;
                }
                head_args[j] = binding_values[found_binding];
            }
            
            if (head_valid && !fact_exists(c, r->head, r->head_arity, head_args)) {
                add_fact_direct(c, r->head, r->head_arity, head_args);
                added++;
            }
        }
        
        /* Increment indices */
        int carry = 1;
        for (int p = pos_count - 1; p >= 0 && carry; p--) {
            indices[p]++;
            if (indices[p] < pos_fact_counts[p]) {
                carry = 0;
            } else {
                indices[p] = 0;
            }
        }
        if (carry) break;
    }
    
    if (binding_values) free(binding_values);
    free(indices);
    for (int p = 0; p < pos_count; p++) {
        if (pos_fact_lists[p]) free(pos_fact_lists[p]);
    }
    free(pos_fact_lists);
    free(pos_fact_counts);
    
    return added;
}

/* ====================================================================== */
/* Dispatcher                                                              */
/* ====================================================================== */

static int apply_rule(Config *c, const Rule *r) {
    /* Aggregate rules (special-case) */
    if (r->aggregate_funcs && r->aggregate_funcs[0]) {
        return apply_rule_aggregate(c, r);
    }
    
    /* Arithmetic rules (special-case) */
    if (r->arith_exprs) {
        for (int i = 0; i < r->body_count; i++) {
            if (r->arith_exprs[i]) {
                return apply_rule_arithmetic(c, r, i);
            }
        }
    }
    
    /* General n-way join for all other rules */
    return apply_rule_general(c, r);
}

/* ====================================================================== */
/* Stratification                                                          */
/* ====================================================================== */

static int pred_strat_internal(const char *pred, const ClosureIR *c);

static int rule_strat_fixed(const Rule *r, const ClosureIR *c) {
    int max_strat = 0;
    
    for (int i = 0; i < r->body_count; i++) {
        if (strcmp(r->body_preds[i], r->head) == 0) continue;
        if (strcmp(r->body_preds[i], "__arith__") == 0) continue;
        
        int body_strat = pred_strat_internal(r->body_preds[i], c);
        int required = r->body_negative[i] ? (body_strat + 1) : body_strat;
        if (required > max_strat) max_strat = required;
    }
    
    return max_strat;
}

static int pred_strat_internal(const char *pred, const ClosureIR *c) {
    int is_base = 1;
    for (Rule *r = c->rules; r; r = r->next) {
        if (strcmp(r->head, pred) == 0) {
            is_base = 0;
            break;
        }
    }
    if (is_base) return 0;
    
    int max_strat = 0;
    for (Rule *r = c->rules; r; r = r->next) {
        if (strcmp(r->head, pred) == 0) {
            int r_strat = rule_strat_fixed(r, c);
            if (r_strat > max_strat) max_strat = r_strat;
        }
    }
    return max_strat;
}

void saturate(Config *c, const ClosureIR *closure) {
    printf("\n[Saturation]\n");

    int *strats = malloc(closure->rule_count * sizeof(int));
    int max_strat = 0;
    int i = 0;
    for (Rule *r = closure->rules; r; r = r->next) {
        strats[i] = rule_strat_fixed(r, closure);
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
                if (strats[i] == s) {
                    added_this_round += apply_rule(c, r);
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

    printf("\n  [Diagnostic] Facts by predicate:\n");
    for (Fact *f = c->facts; f; f = f->next) {
        int count = 0;
        for (Fact *f2 = c->facts; f2; f2 = f2->next) {
            if (strcmp(f->predicate, f2->predicate) == 0) count++;
        }
        int already = 0;
        for (Fact *f2 = c->facts; f2 != f; f2 = f2->next) {
            if (strcmp(f->predicate, f2->predicate) == 0) { already = 1; break; }
        }
        if (!already) printf("    %s: %d facts\n", f->predicate, count);
    }
}
