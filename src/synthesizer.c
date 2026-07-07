#include "synthesizer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ====================================================================== */
/* Expression deep copy                                                    */
/* ====================================================================== */

static Expr *copy_expr(const Expr *e) {
    if (!e) return NULL;
    switch (e->type) {
        case EXPR_NUMBER:
            return expr_new_number(e->number, e->loc);
        case EXPR_VARIABLE:
            return expr_new_variable(e->var_name, e->loc);
        case EXPR_BINARY:
            return expr_new_binary(e->binary.op,
                                   copy_expr(e->binary.left),
                                   copy_expr(e->binary.right),
                                   e->loc);
        case EXPR_UNARY_MINUS:
            return expr_new_unary_minus(copy_expr(e->operand), e->loc);
    }
    return NULL;
}

/* ====================================================================== */
/* ArgBinding collection                                                   */
/* ====================================================================== */

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

/* Collect all edges of certain types into an array (preserves order) */
static Edge **collect_body_edges(Node *n, int *out_count) {
    int count = 0;
    for (Edge *e = n->outgoing; e; e = e->next) {
        if (e->type == EDGE_DEFINED_BY_BASE ||
            e->type == EDGE_DEFINED_BY_RECURSIVE ||
            e->type == EDGE_DEFINED_BY_COMPOSITION ||
            e->type == EDGE_DEFINED_BY_NEGATION) {
            count++;
        }
    }
    
    Edge **edges = malloc(sizeof(Edge*) * count);
    int i = 0;
    for (Edge *e = n->outgoing; e; e = e->next) {
        if (e->type == EDGE_DEFINED_BY_BASE ||
            e->type == EDGE_DEFINED_BY_RECURSIVE ||
            e->type == EDGE_DEFINED_BY_COMPOSITION ||
            e->type == EDGE_DEFINED_BY_NEGATION) {
            edges[i++] = e;
        }
    }
    
    *out_count = count;
    return edges;
}

/* ====================================================================== */
/* Destructors                                                             */
/* ====================================================================== */

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
    printf("Lambda: %zu\n\n", c->lambda_count);
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

/* ====================================================================== */
/* Main synthesis                                                          */
/* ====================================================================== */

ClosureIR *synthesize(const Graph *g) {
    ClosureIR *closure = calloc(1, sizeof(ClosureIR));
    
    for (Node *n = g->nodes; n; n = n->next) {
        if (n->type != NODE_DERIVED) continue;
        
        /* Check for aggregate edge */
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
            r->head_arity = n->arity;
            r->body_count = 1;
            r->body_preds = malloc(sizeof(char*));
            r->body_arities = malloc(sizeof(int));
            r->body_negative = malloc(sizeof(int));
            r->aggregate_funcs = malloc(sizeof(char*));
            r->aggregate_fields = malloc(sizeof(int));
            r->arith_exprs = NULL;
            r->arith_result_vars = NULL;
            r->arg_bindings = NULL;
            r->arg_binding_count = 0;
            
            r->body_preds[0] = strdup(agg_edge->target->name);
            r->body_arities[0] = agg_edge->target->arity;
            r->body_negative[0] = 0;
            r->aggregate_funcs[0] = strdup(agg_edge->aggregate_func);
            r->aggregate_fields[0] = agg_edge->aggregate_field;
            r->is_recursive = 0;
            
            if (!closure->rules) {
                closure->rules = r;
            } else {
                Rule *last = closure->rules;
                while (last->next) last = last->next;
                last->next = r;
            }
            closure->rule_count++;
            continue;
        }
        
        /* Check for arithmetic edge */
        Edge *arith_edge = NULL;
        for (Edge *e = n->outgoing; e; e = e->next) {
            if (e->type == EDGE_DEFINED_BY_ARITHMETIC) {
                arith_edge = e;
                break;
            }
        }
        
        if (arith_edge) {
            int regular_count = 0;
            for (Edge *e = n->outgoing; e; e = e->next) {
                if (e->type != EDGE_DEFINED_BY_ARITHMETIC) {
                    regular_count++;
                }
            }
            
            int total_count = regular_count + 1;
            Rule *r = calloc(1, sizeof(Rule));
            r->head = strdup(n->name);
            r->head_arity = n->arity;
            r->body_count = total_count;
            r->body_preds = calloc(total_count, sizeof(char*));
            r->body_arities = calloc(total_count, sizeof(int));
            r->body_negative = calloc(total_count, sizeof(int));
            r->aggregate_funcs = NULL;
            r->aggregate_fields = NULL;
            r->arith_exprs = calloc(total_count, sizeof(Expr*));
            r->arith_result_vars = calloc(total_count, sizeof(char*));
            r->arg_bindings = NULL;
            r->arg_binding_count = 0;
            r->is_recursive = 0;
            
            int idx = 0;
            for (Edge *e = n->outgoing; e; e = e->next) {
                if (e->type == EDGE_DEFINED_BY_ARITHMETIC) continue;
                
                r->body_preds[idx] = strdup(e->target->name);
                r->body_arities[idx] = e->target->arity;
                r->body_negative[idx] = (e->type == EDGE_DEFINED_BY_NEGATION);
                idx++;
            }
            
            r->body_preds[idx] = strdup("__arith__");
            r->body_arities[idx] = 0;
            r->body_negative[idx] = 0;
            r->arith_exprs[idx] = copy_expr(arith_edge->arith_expr);
            r->arith_result_vars[idx] = strdup(arith_edge->arith_result_var);
            
            /* Build ArgBindings from regular edges */
            BindingTable bt;
            binding_table_init(&bt);
            
            int edge_idx = 0;
            for (Edge *e = n->outgoing; e; e = e->next) {
                if (e->type == EDGE_DEFINED_BY_ARITHMETIC) continue;
                
                for (int j = 0; j < e->var_binding_count; j++) {
                    const char *var = e->var_bindings[j].var_name;
                    int bidx = binding_table_find_or_create(&bt, var);
                    binding_table_add_location(&bt, bidx, edge_idx, e->var_bindings[j].arg_index);
                }
                edge_idx++;
            }
            
            /* Add arithmetic result variable as HEAD binding at last position */
            if (arith_edge->arith_result_var) {
                int bidx = binding_table_find_or_create(&bt, arith_edge->arith_result_var);
                bt.bindings[bidx].is_head = 1;
                bt.bindings[bidx].head_arg_index = n->arity - 1;
            }
            
            /* Mark other head params */
            if (n->head_params && n->head_param_count > 0) {
                binding_table_mark_head(&bt, n->head_params, n->head_param_count);
            }
            
            /* Transfer to rule */
            r->arg_bindings = bt.bindings;
            r->arg_binding_count = bt.count;
            
            if (!closure->rules) {
                closure->rules = r;
            } else {
                Rule *last = closure->rules;
                while (last->next) last = last->next;
                last->next = r;
            }
            closure->rule_count++;
            continue;
        }
        
        /* Non-aggregate, non-arithmetic rule — build with full ArgBindings */
        int body_count = 0;
        Edge **body_edges = collect_body_edges(n, &body_count);
        
        if (body_count == 0) {
            free(body_edges);
            continue;
        }
        
        Rule *r = calloc(1, sizeof(Rule));
        r->head = strdup(n->name);
        r->head_arity = n->arity;
        r->body_count = body_count;
        r->body_preds = malloc(sizeof(char*) * body_count);
        r->body_arities = malloc(sizeof(int) * body_count);
        r->body_negative = malloc(sizeof(int) * body_count);
        r->aggregate_funcs = NULL;
        r->aggregate_fields = NULL;
        r->arith_exprs = NULL;
        r->arith_result_vars = NULL;
        
        for (int i = 0; i < body_count; i++) {
            Edge *e = body_edges[i];
            r->body_preds[i] = strdup(e->target->name);
            r->body_arities[i] = e->target->arity;
            r->body_negative[i] = (e->type == EDGE_DEFINED_BY_NEGATION);
        }
        
        r->is_recursive = 0;
        for (int k = 0; k < r->body_count; k++) {
            if (strcmp(r->body_preds[k], r->head) == 0) {
                r->is_recursive = 1;
                break;
            }
        }
        
        /* Build ArgBindings from edges */
        BindingTable bt;
        binding_table_init(&bt);
        
        for (int i = 0; i < body_count; i++) {
            Edge *e = body_edges[i];
            for (int j = 0; j < e->var_binding_count; j++) {
                const char *var = e->var_bindings[j].var_name;
                int bidx = binding_table_find_or_create(&bt, var);
                binding_table_add_location(&bt, bidx, i, e->var_bindings[j].arg_index);
            }
        }
        
        if (n->head_params && n->head_param_count > 0) {
            binding_table_mark_head(&bt, n->head_params, n->head_param_count);
        }
        
        r->arg_bindings = bt.bindings;
        r->arg_binding_count = bt.count;
        
        free(body_edges);
        
        if (!closure->rules) {
            closure->rules = r;
        } else {
            Rule *last = closure->rules;
            while (last->next) last = last->next;
            last->next = r;
        }
        closure->rule_count++;
    }
    
    return closure;
}
