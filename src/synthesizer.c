#include "synthesizer.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ========================================================================= */
/* Deep copy expression (v1.1 с защитой от OOM и обработкой EXPR_CALL)        */
/* КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ: для EXPR_CALL делаем strdup(func_name) перед     */
/* вызовом expr_new_call, потому что expr_new_call теперь забирает ownership */
/* ========================================================================= */

static Expr *copy_expr(const Expr *e) {
    if (!e) return NULL;
    switch (e->type) {
        case EXPR_NUMBER:
            return expr_new_number(e->number, e->loc);
        case EXPR_VARIABLE:
            return expr_new_variable(e->var_name, e->loc);
        case EXPR_BINARY: {
            Expr *l = copy_expr(e->binary.left);
            Expr *r = copy_expr(e->binary.right);
            Expr *result = expr_new_binary(e->binary.op, l, r, e->loc);
            if (!result) {
                expr_free(l);
                expr_free(r);
                return NULL;
            }
            return result;
        }
        case EXPR_UNARY_MINUS: {
            Expr *inner = copy_expr(e->operand);
            Expr *result = expr_new_unary_minus(inner, e->loc);
            if (!result) {
                expr_free(inner);
                return NULL;
            }
            return result;
        }
        case EXPR_CALL: {
            Expr **args_copy = NULL;
            if (e->call.arg_count > 0) {
                args_copy = calloc(e->call.arg_count, sizeof(Expr*));
                if (!args_copy) return NULL;
                for (int i = 0; i < e->call.arg_count; i++) {
                    args_copy[i] = copy_expr(e->call.args[i]);
                    if (!args_copy[i]) {
                        for (int j = 0; j < i; j++) expr_free(args_copy[j]);
                        free(args_copy);
                        return NULL;
                    }
                }
            }
            char *func_copy = strdup(e->call.func_name);
            if (!func_copy) {
                for (int j = 0; j < e->call.arg_count; j++) expr_free(args_copy[j]);
                free(args_copy);
                return NULL;
            }
            Expr *result = expr_new_call(func_copy, args_copy, e->call.arg_count, e->loc);
            if (!result) {
                free(func_copy);
                for (int j = 0; j < e->call.arg_count; j++) expr_free(args_copy[j]);
                free(args_copy);
                return NULL;
            }
            return result;
        }
    }
    return NULL;
}

/* ========================================================================= */
/* Deep copy comparisons                                                     */
/* ========================================================================= */

static Comparison *copy_comparisons(const Comparison *src, int count, int *out_count) {
    if (count == 0) {
        *out_count = 0;
        return NULL;
    }
    Comparison *dst = malloc(sizeof(Comparison) * count);
    if (!dst) {
        *out_count = 0;
        return NULL;
    }
    for (int i = 0; i < count; i++) {
        dst[i].left = copy_expr(src[i].left);
        dst[i].op = src[i].op;
        dst[i].right = copy_expr(src[i].right);
        dst[i].loc = src[i].loc;
    }
    *out_count = count;
    return dst;
}

static void free_comparisons(Comparison *cmps, int count) {
    if (!cmps) return;
    for (int i = 0; i < count; i++) {
        comparison_free(&cmps[i]);
    }
    free(cmps);
}

/* ========================================================================= */
/* ArgBinding collection                                                      */
/* ========================================================================= */

typedef struct {
    ArgBinding *bindings;
    int count;
    int capacity;
} BindingTable;

static void binding_table_init(BindingTable *t) {
    t->bindings = NULL;
    t->count = 0;
    t->capacity = 0;
}

static int binding_table_find_or_create(BindingTable *t, const char *var_name) {
    for (int i = 0; i < t->count; i++) {
        if (strcmp(t->bindings[i].var_name, var_name) == 0) {
            return i;
        }
    }
    if (t->count >= t->capacity) {
        t->capacity = t->capacity == 0 ? 8 : t->capacity * 2;
        t->bindings = realloc(t->bindings, sizeof(ArgBinding) * t->capacity);
    }
    ArgBinding *b = &t->bindings[t->count];
    b->var_name = strdup(var_name);
    b->locations = NULL;
    b->location_count = 0;
    b->is_head = 0;
    b->head_arg_index = -1;
    return t->count++;
}

static void binding_table_add_location(BindingTable *t, int binding_idx,
                                       int atom_index, int arg_index) {
    ArgBinding *b = &t->bindings[binding_idx];
    b->location_count++;
    b->locations = realloc(b->locations, sizeof(ArgLocation) * b->location_count);
    b->locations[b->location_count - 1].atom_index = atom_index;
    b->locations[b->location_count - 1].arg_index = arg_index;
}

static void binding_table_mark_head(BindingTable *t, char **head_params, int head_param_count) {
    for (int h = 0; h < head_param_count; h++) {
        for (int i = 0; i < t->count; i++) {
            if (strcmp(t->bindings[i].var_name, head_params[h]) == 0) {
                t->bindings[i].is_head = 1;
                t->bindings[i].head_arg_index = h;
                break;
            }
        }
    }
}

/* ========================================================================= */
/* Helper: add rule to closure list                                          */
/* ========================================================================= */

static void add_rule_to_closure(ClosureIR *closure, Rule *r) {
    if (!closure->rules) {
        closure->rules = r;
    } else {
        Rule *last = closure->rules;
        while (last->next) last = last->next;
        last->next = r;
    }
    closure->rule_count++;
}

/* ========================================================================= */
/* Helper: build ArgBindings for a single body edge                          */
/* ========================================================================= */

static void build_bindings_for_edge(Rule *r, Edge *e, Node *n) {
    BindingTable bt;
    binding_table_init(&bt);
    for (int j = 0; j < e->var_binding_count; j++) {
        const char *var = e->var_bindings[j].var_name;
        int bidx = binding_table_find_or_create(&bt, var);
        binding_table_add_location(&bt, bidx, 0, e->var_bindings[j].arg_index);
    }
    if (n->head_params && n->head_param_count > 0) {
        binding_table_mark_head(&bt, n->head_params, n->head_param_count);
    }
    r->arg_bindings = bt.bindings;
    r->arg_binding_count = bt.count;
}

/* ========================================================================= */
/* Destructors                                                               */
/* ========================================================================= */

void rule_free(Rule *r) {
    if (!r) return;
    free(r->head);
    for (int i = 0; i < r->body_count; i++) {
        free(r->body_preds[i]);
        if (r->aggregate_funcs && r->aggregate_funcs[i]) {
            free(r->aggregate_funcs[i]);
        }
        if (r->arith_exprs && r->arith_exprs[i]) {
            expr_free(r->arith_exprs[i]);
        }
        if (r->arith_result_vars && r->arith_result_vars[i]) {
            free(r->arith_result_vars[i]);
        }
    }
    free(r->body_preds);
    free(r->body_arities);
    free(r->body_negative);
    if (r->aggregate_funcs) free(r->aggregate_funcs);
    if (r->aggregate_fields) free(r->aggregate_fields);
    if (r->arith_exprs) free(r->arith_exprs);
    if (r->arith_result_vars) free(r->arith_result_vars);

    if (r->comparisons) {
        for (int i = 0; i < r->body_count; i++) {
            if (r->comparisons[i]) {
                free_comparisons(r->comparisons[i], r->comparison_counts[i]);
            }
        }
        free(r->comparisons);
    }
    if (r->comparison_counts) free(r->comparison_counts);

    for (int i = 0; i < r->arg_binding_count; i++) {
        free(r->arg_bindings[i].var_name);
        free(r->arg_bindings[i].locations);
    }
    free(r->arg_bindings);
    free(r);
}

void closure_free(ClosureIR *c) {
    if (!c) return;
    Rule *r = c->rules;
    while (r) {
        Rule *next = r->next;
        rule_free(r);
        r = next;
    }
    free(c);
}

void closure_dump(const ClosureIR *c) {
    printf("\n=== Synthesized Closure IR ===\n");
    printf("Rule count: %zu\n", c->rule_count);
    printf("Lambda: %zu\n", c->lambda_count);
    printf("Rules:\n");
    for (Rule *r = c->rules; r; r = r->next) {
        printf("  %s( ", r->head);
        for (int i = 0; i < r->head_arity; i++) {
            if (i > 0) printf(", ");
            printf("?");
        }
        printf(") <- ");
        for (int i = 0; i < r->body_count; i++) {
            if (i > 0) printf(", ");
            if (r->aggregate_funcs && r->aggregate_funcs[i]) {
                printf("%s(%s, field_%d)",
                       r->aggregate_funcs[i],
                       r->body_preds[i],
                       r->aggregate_fields[i]);
            } else if (r->arith_exprs && r->arith_exprs[i]) {
                printf("%s = <expr>", r->arith_result_vars[i]);
            } else {
                if (r->body_negative[i]) printf("not ");
                printf("%s(...)", r->body_preds[i]);
            }
        }
        printf("\n");

        if (r->comparisons) {
            int total_cmps = 0;
            for (int i = 0; i < r->body_count; i++) total_cmps += r->comparison_counts[i];
            if (total_cmps > 0) {
                printf("    Filters: %d comparisons\n", total_cmps);
            }
        }

        if (r->arg_binding_count > 0) {
            printf("    Bindings:\n");
            for (int i = 0; i < r->arg_binding_count; i++) {
                ArgBinding *b = &r->arg_bindings[i];
                printf("      %s: ", b->var_name);
                for (int j = 0; j < b->location_count; j++) {
                    if (j > 0) printf(", ");
                    printf("atom[%d].arg[%d]",
                           b->locations[j].atom_index,
                           b->locations[j].arg_index);
                }
                if (b->is_head) {
                    printf(" [HEAD@%d]", b->head_arg_index);
                }
                printf("\n");
            }
        }
    }
    printf("==============================\n");
}

/* ========================================================================= */
/* Main synthesis                                                            */
/* ========================================================================= */

ClosureIR *synthesize(const Graph *g) {
    ClosureIR *closure = calloc(1, sizeof(ClosureIR));

    for (Node *n = g->nodes; n; n = n->next) {
        if (n->type != NODE_DERIVED) continue;

        /* ================================================================= */
        /* CASE 1: Node has BASE edges                                       */
        /* ================================================================= */
        int base_edge_count = 0;
        for (Edge *e = n->outgoing; e; e = e->next) {
            if (e->type == EDGE_DEFINED_BY_BASE) {
                base_edge_count++;
            }
        }
        if (base_edge_count > 0) {
            for (Edge *e = n->outgoing; e; e = e->next) {
                if (e->type != EDGE_DEFINED_BY_BASE) continue;
                Rule *r = calloc(1, sizeof(Rule));
                r->head = strdup(n->name);
                r->stratum = n->stratum;
                r->head_arity = n->arity;
                r->body_count = 1;
                r->body_preds = malloc(sizeof(char*));
                r->body_arities = malloc(sizeof(int));
                r->body_negative = malloc(sizeof(int));
                r->aggregate_funcs = NULL;
                r->aggregate_fields = NULL;
                r->arith_exprs = NULL;
                r->arith_result_vars = NULL;
                r->comparisons = calloc(1, sizeof(Comparison*));
                r->comparison_counts = calloc(1, sizeof(int));
                r->arg_bindings = NULL;
                r->arg_binding_count = 0;
                r->body_preds[0] = strdup(e->target->name);
                r->body_arities[0] = e->target->arity;
                r->body_negative[0] = 0;
                r->is_recursive = 0;
                build_bindings_for_edge(r, e, n);
                add_rule_to_closure(closure, r);
            }
            continue;
        }

        /* ================================================================= */
        /* CASE 2: Aggregate rule                                            */
        /* ================================================================= */
        Edge *agg_edge = NULL;
        for (Edge *e = n->outgoing; e; e = e->next) {
            if (e->type == EDGE_DEFINED_BY_AGGREGATE) {
                agg_edge = e;
                break;
            }
        }
        if (agg_edge) {
            Rule *r = calloc(1, sizeof(Rule));
            r->head = strdup(n->name);
            r->stratum = n->stratum;
            r->head_arity = n->arity;
            r->body_count = 1;
            r->body_preds = malloc(sizeof(char*));
            r->body_arities = malloc(sizeof(int));
            r->body_negative = malloc(sizeof(int));
            r->aggregate_funcs = malloc(sizeof(char*));
            r->aggregate_fields = malloc(sizeof(int));
            r->arith_exprs = NULL;
            r->arith_result_vars = NULL;
            r->comparisons = calloc(1, sizeof(Comparison*));
            r->comparison_counts = calloc(1, sizeof(int));
            r->arg_bindings = NULL;
            r->arg_binding_count = 0;
            r->body_preds[0] = strdup(agg_edge->target->name);
            r->body_arities[0] = agg_edge->target->arity;
            r->body_negative[0] = 0;
            r->aggregate_funcs[0] = strdup(agg_edge->aggregate_func);
            r->aggregate_fields[0] = agg_edge->aggregate_field;
            r->is_recursive = 0;
            add_rule_to_closure(closure, r);
            continue;
        }

        /* ================================================================= */
        /* CASE 3: Arithmetic rule (v1.2: MULTIPLE arithmetic assignments)  */
        /* ================================================================= */
        /* ИЗМЕНЕНИЕ v1.2: Собираем ВСЕ arithmetic edges в массив          */
        {
            Edge **arith_edges = NULL;
            int arith_count = 0;
            int arith_cap = 0;
            Edge *filter_edge = NULL;

            /* Собираем все арифметические рёбра */
            for (Edge *e = n->outgoing; e; e = e->next) {
                if (e->type == EDGE_DEFINED_BY_ARITHMETIC) {
                    if (arith_count >= arith_cap) {
                        arith_cap = arith_cap == 0 ? 4 : arith_cap * 2;
                        arith_edges = realloc(arith_edges, sizeof(Edge*) * arith_cap);
                    }
                    arith_edges[arith_count++] = e;
                } else if (e->type == EDGE_DEFINED_BY_FILTER) {
                    filter_edge = e;
                }
            }

            if (arith_count > 0) {
                /* Подсчёт обычных body predicates (не arithmetic, не filter) */
                int regular_count = 0;
                for (Edge *e = n->outgoing; e; e = e->next) {
                    if (e->type != EDGE_DEFINED_BY_ARITHMETIC &&
                        e->type != EDGE_DEFINED_BY_FILTER) {
                        regular_count++;
                    }
                }
                
                /* Общее количество слотов в теле правила */
                int total_count = regular_count + arith_count;
                
                Rule *r = calloc(1, sizeof(Rule));
                r->head = strdup(n->name);
                r->stratum = n->stratum;
                r->head_arity = n->arity;
                r->body_count = total_count;
                r->body_preds = calloc(total_count, sizeof(char*));
                r->body_arities = calloc(total_count, sizeof(int));
                r->body_negative = calloc(total_count, sizeof(int));
                r->aggregate_funcs = NULL;
                r->aggregate_fields = NULL;
                r->arith_exprs = calloc(total_count, sizeof(Expr*));
                r->arith_result_vars = calloc(total_count, sizeof(char*));
                r->comparisons = calloc(total_count, sizeof(Comparison*));
                r->comparison_counts = calloc(total_count, sizeof(int));
                r->arg_bindings = NULL;
                r->arg_binding_count = 0;
                r->is_recursive = 0;

                /* Заполнение обычных body predicates */
                int idx = 0;
                for (Edge *e = n->outgoing; e; e = e->next) {
                    if (e->type == EDGE_DEFINED_BY_ARITHMETIC) continue;
                    if (e->type == EDGE_DEFINED_BY_FILTER) continue;
                    r->body_preds[idx] = strdup(e->target->name);
                    r->body_arities[idx] = e->target->arity;
                    r->body_negative[idx] = (e->type == EDGE_DEFINED_BY_NEGATION);
                    idx++;
                }

                /* ИЗМЕНЕНИЕ v1.2: Создание нескольких __arith__ слотов */
                for (int a = 0; a < arith_count; a++) {
                    r->body_preds[idx] = strdup("__arith__");
                    r->body_arities[idx] = 0;
                    r->body_negative[idx] = 0;
                    r->arith_exprs[idx] = copy_expr(arith_edges[a]->arith_expr);
                    r->arith_result_vars[idx] = strdup(arith_edges[a]->arith_result_var);
                    idx++;
                }

                /* Привязка comparisons к последнему слоту */
                if (filter_edge) {
                    r->comparisons[total_count - 1] = copy_comparisons(
                        filter_edge->comparisons, filter_edge->comparison_count,
                        &r->comparison_counts[total_count - 1]);
                }

                /* Построение BindingTable */
                BindingTable bt;
                binding_table_init(&bt);
                int edge_idx = 0;
                for (Edge *e = n->outgoing; e; e = e->next) {
                    if (e->type == EDGE_DEFINED_BY_ARITHMETIC) continue;
                    if (e->type == EDGE_DEFINED_BY_FILTER) continue;
                    for (int j = 0; j < e->var_binding_count; j++) {
                        const char *var = e->var_bindings[j].var_name;
                        int bidx = binding_table_find_or_create(&bt, var);
                        binding_table_add_location(&bt, bidx, edge_idx, e->var_bindings[j].arg_index);
                    }
                    edge_idx++;
                }

                /* ИЗМЕНЕНИЕ v1.2: Добавляем ВСЕ arith_result_vars в таблицу */
                for (int a = 0; a < arith_count; a++) {
                    if (arith_edges[a]->arith_result_var) {
                        binding_table_find_or_create(&bt, arith_edges[a]->arith_result_var);
                    }
                }

                /* ИЗМЕНЕНИЕ v1.2: binding_table_mark_head автоматически проставит
                   правильные head_arg_index для ВСЕХ переменных из head,
                   включая arith_result_vars! */
                if (n->head_params && n->head_param_count > 0) {
                    binding_table_mark_head(&bt, n->head_params, n->head_param_count);
                }
                r->arg_bindings = bt.bindings;
                r->arg_binding_count = bt.count;
                add_rule_to_closure(closure, r);
                free(arith_edges);
                continue;
            }
        }

        /* ================================================================= */
        /* CASE 4: General rule                                              */
        /* ================================================================= */
        int body_count = 0;
        Edge *filter_edge = NULL;
        for (Edge *e = n->outgoing; e; e = e->next) {
            if (e->type == EDGE_DEFINED_BY_COMPOSITION ||
                e->type == EDGE_DEFINED_BY_NEGATION ||
                e->type == EDGE_DEFINED_BY_RECURSIVE ||
                e->type == EDGE_DEFINED_BY_BASE) {
                body_count++;
            } else if (e->type == EDGE_DEFINED_BY_FILTER) {
                filter_edge = e;
            }
        }
        if (body_count == 0) continue;

        Rule *r = calloc(1, sizeof(Rule));
        r->head = strdup(n->name);
        r->stratum = n->stratum;
        r->head_arity = n->arity;
        r->body_count = body_count;
        r->body_preds = malloc(sizeof(char*) * body_count);
        r->body_arities = malloc(sizeof(int) * body_count);
        r->body_negative = malloc(sizeof(int) * body_count);
        r->aggregate_funcs = NULL;
        r->aggregate_fields = NULL;
        r->arith_exprs = NULL;
        r->arith_result_vars = NULL;
        r->comparisons = calloc(body_count, sizeof(Comparison*));
        r->comparison_counts = calloc(body_count, sizeof(int));

        int i = 0;
        for (Edge *e = n->outgoing; e; e = e->next) {
            if (e->type == EDGE_DEFINED_BY_COMPOSITION ||
                e->type == EDGE_DEFINED_BY_NEGATION ||
                e->type == EDGE_DEFINED_BY_RECURSIVE ||
                e->type == EDGE_DEFINED_BY_BASE) {
                r->body_preds[i] = strdup(e->target->name);
                r->body_arities[i] = e->target->arity;
                r->body_negative[i] = (e->type == EDGE_DEFINED_BY_NEGATION);
                i++;
            }
        }

        r->is_recursive = 0;
        for (int k = 0; k < r->body_count; k++) {
            if (strcmp(r->body_preds[k], r->head) == 0) {
                r->is_recursive = 1;
                break;
            }
        }

        if (filter_edge) {
            for (int k = 0; k < body_count; k++) {
                r->comparisons[k] = NULL;
                r->comparison_counts[k] = 0;
            }
            r->comparisons[0] = copy_comparisons(
                filter_edge->comparisons, filter_edge->comparison_count,
                &r->comparison_counts[0]);
        }

        BindingTable bt;
        binding_table_init(&bt);
        int edge_idx = 0;
        for (Edge *e = n->outgoing; e; e = e->next) {
            if (e->type == EDGE_DEFINED_BY_COMPOSITION ||
                e->type == EDGE_DEFINED_BY_NEGATION ||
                e->type == EDGE_DEFINED_BY_RECURSIVE ||
                e->type == EDGE_DEFINED_BY_BASE) {
                for (int j = 0; j < e->var_binding_count; j++) {
                    const char *var = e->var_bindings[j].var_name;
                    int bidx = binding_table_find_or_create(&bt, var);
                    binding_table_add_location(&bt, bidx, edge_idx, e->var_bindings[j].arg_index);
                }
                edge_idx++;
            }
        }
        if (n->head_params && n->head_param_count > 0) {
            binding_table_mark_head(&bt, n->head_params, n->head_param_count);
        }
        r->arg_bindings = bt.bindings;
        r->arg_binding_count = bt.count;
        add_rule_to_closure(closure, r);
    }
    return closure;
}
