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
/* Expression evaluator                                                    */
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
/* Rule application                                                        */
/* ====================================================================== */

static int apply_rule(Config *c, const Rule *r) {
    int added = 0;
    
    /* Aggregate rules */
    if (r->aggregate_funcs && r->aggregate_funcs[0]) {
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
    
    /* Arithmetic rules: head(...) <- body1(...), ..., result = expr */
    int has_arith = 0;
    int arith_slot = -1;
    if (r->arith_exprs) {
        for (int i = 0; i < r->body_count; i++) {
            if (r->arith_exprs[i]) {
                has_arith = 1;
                arith_slot = i;
                break;
            }
        }
    }
    
    if (has_arith) {
        /* Count and collect regular (non-arithmetic) body atoms */
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
            /* Check if current indices are valid */
            int valid = 1;
            for (int s = 0; s < regular_count; s++) {
                if (indices[s] >= fact_counts[s]) {
                    valid = 0;
                    break;
                }
            }
            if (!valid) break;
            
            /* Get current combination of facts */
            Fact *current_facts[16];
            int total_args = 0;
            for (int s = 0; s < regular_count; s++) {
                current_facts[s] = fact_lists[s][indices[s]];
                total_args += current_facts[s]->arity;
            }
            
            /* Build variable bindings: args become "arg0", "arg1", ...
             * in order of body atoms, then args within each atom. */
            char **bind_names = malloc(sizeof(char*) * total_args);
            const char **bind_values = malloc(sizeof(const char*) * total_args);
            int bind_count = 0;
            
            for (int s = 0; s < regular_count; s++) {
                Fact *f = current_facts[s];
                for (int a = 0; a < f->arity; a++) {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "arg%d", bind_count);
                    bind_names[bind_count] = strdup(buf);
                    bind_values[bind_count] = f->args[a];
                    bind_count++;
                }
            }
            
            VarBindings binds;
            binds.names = (const char**)bind_names;
            binds.values = bind_values;
            binds.count = bind_count;
            
            /* Evaluate expression */
            double val = eval_expr(r->arith_exprs[arith_slot], &binds);
            
            /* Build head fact args: use first body fact's args as base,
             * append computed value as last arg */
            char **result_args = malloc(sizeof(char*) * r->head_arity);
            int filled = 0;
            
            for (int s = 0; s < regular_count && filled < r->head_arity - 1; s++) {
                Fact *f = current_facts[s];
                for (int a = 0; a < f->arity && filled < r->head_arity - 1; a++) {
                    result_args[filled++] = f->args[a];
                }
            }
            
            /* Pad with empty strings if not enough args */
            while (filled < r->head_arity - 1) {
                result_args[filled++] = "";
            }
            
            /* Last arg is the computed value */
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
            
            /* Free only the strdup'd parts */
            free(result_args[r->head_arity - 1]);
            free(result_args);
            
            /* Free bindings */
            for (int i = 0; i < bind_count; i++) free(bind_names[i]);
            free(bind_names);
            free(bind_values);
            
            /* Advance to next combination */
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
        
        /* Free fact lists */
        for (int s = 0; s < regular_count; s++) {
            free(fact_lists[s]);
        }
        
        return added;
    }
    
    /* Simple copy rule: head(X) <- body(X) */
    if (r->body_count == 1 && !r->is_recursive && !r->body_negative[0]) {
        for (Fact *f = c->facts; f; f = f->next) {
            if (strcmp(f->predicate, r->body_preds[0]) == 0 &&
                f->arity == r->head_arity) {
                if (!fact_exists(c, r->head, f->arity, f->args)) {
                    add_fact_direct(c, r->head, f->arity, f->args);
                    added++;
                }
            }
        }
    }
    /* Transitive closure rule with arity 2 */
    else if (r->body_count == 2 && r->is_recursive && r->head_arity == 2 &&
             !r->body_negative[0] && !r->body_negative[1]) {
        int rec_idx = -1, ext_idx = -1;
        if (strcmp(r->body_preds[0], r->head) == 0) {
            rec_idx = 0; ext_idx = 1;
        } else if (strcmp(r->body_preds[1], r->head) == 0) {
            rec_idx = 1; ext_idx = 0;
        }
        
        if (rec_idx < 0) return added;
        
        Fact **head_facts = NULL;
        int head_count = 0, head_capacity = 0;
        
        for (Fact *f = c->facts; f; f = f->next) {
            if (strcmp(f->predicate, r->head) == 0 && f->arity == 2) {
                if (head_count >= head_capacity) {
                    head_capacity = head_capacity == 0 ? 16 : head_capacity * 2;
                    head_facts = realloc(head_facts, sizeof(Fact*) * head_capacity);
                }
                head_facts[head_count++] = f;
            }
        }
        
        if (rec_idx == 0) {
            for (int i = 0; i < head_count; i++) {
                Fact *hf = head_facts[i];
                const char *x = hf->args[0];
                const char *z = hf->args[1];
                
                for (Fact *ef = c->facts; ef; ef = ef->next) {
                    if (strcmp(ef->predicate, r->body_preds[ext_idx]) == 0 &&
                        ef->arity == 2 && strcmp(ef->args[0], z) == 0) {
                        char *new_args[2] = { (char*)x, ef->args[1] };
                        if (!fact_exists(c, r->head, 2, new_args)) {
                            add_fact_direct(c, r->head, 2, new_args);
                            added++;
                        }
                    }
                }
            }
        } else {
            for (Fact *ef = c->facts; ef; ef = ef->next) {
                if (strcmp(ef->predicate, r->body_preds[ext_idx]) == 0 && ef->arity == 2) {
                    const char *x = ef->args[0];
                    const char *z = ef->args[1];
                    
                    for (int i = 0; i < head_count; i++) {
                        Fact *hf = head_facts[i];
                        if (strcmp(hf->args[0], z) == 0) {
                            char *new_args[2] = { (char*)x, hf->args[1] };
                            if (!fact_exists(c, r->head, 2, new_args)) {
                                add_fact_direct(c, r->head, 2, new_args);
                                added++;
                            }
                        }
                    }
                }
            }
        }
        free(head_facts);
    }
    /* Negation rule: head(X) <- pos(X), not neg(X) */
    else if (r->body_count == 2 && !r->is_recursive &&
             r->head_arity == 1 &&
             r->body_arities[0] == 1 && !r->body_negative[0] &&
             r->body_arities[1] == 1 && r->body_negative[1]) {

        for (Fact *f = c->facts; f; f = f->next) {
            if (strcmp(f->predicate, r->body_preds[0]) == 0 && f->arity == 1) {
                char *x = f->args[0];
                char *neg_args[1] = { x };
                if (!fact_exists(c, r->body_preds[1], 1, neg_args)) {
                    char *head_args[1] = { x };
                    if (!fact_exists(c, r->head, 1, head_args)) {
                        add_fact_direct(c, r->head, 1, head_args);
                        added++;
                    }
                }
            }
        }
    }
    /* Negation rule with arity mismatch: head(X) <- pos(X, Y), not neg(Y) */
    else if (r->body_count == 2 && !r->is_recursive &&
             r->head_arity == 1 &&
             r->body_arities[0] == 2 && !r->body_negative[0] &&
             r->body_arities[1] == 1 && r->body_negative[1]) {

        for (Fact *f = c->facts; f; f = f->next) {
            if (strcmp(f->predicate, r->body_preds[0]) == 0 && f->arity == 2) {
                char *x = f->args[0];
                char *y = f->args[1];
                char *neg_args[1] = { y };
                if (!fact_exists(c, r->body_preds[1], 1, neg_args)) {
                    char *head_args[1] = { x };
                    if (!fact_exists(c, r->head, 1, head_args)) {
                        add_fact_direct(c, r->head, 1, head_args);
                        added++;
                    }
                }
            }
        }
    }

    return added;
}

/* ====================================================================== */
/* Stratification                                                          */
/* ====================================================================== */

static int pred_strat_internal(const char *pred, const ClosureIR *c);

static int rule_strat_fixed(const Rule *r, const ClosureIR *c) {
    int max_strat = 0;
    
    for (int i = 0; i < r->body_count; i++) {
        /* Skip recursive body predicates */
        if (strcmp(r->body_preds[i], r->head) == 0) continue;
        /* Skip arithmetic slots */
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
