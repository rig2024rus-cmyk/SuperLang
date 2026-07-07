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
            /* Count non-arithmetic edges */
            int regular_count = 0;
            for (Edge *e = n->outgoing; e; e = e->next) {
                if (e->type != EDGE_DEFINED_BY_ARITHMETIC) {
                    regular_count++;
                }
            }
            
            /* Build rule: regular atoms + 1 arithmetic slot */
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
            r->is_recursive = 0;
            
            int idx = 0;
            for (Edge *e = n->outgoing; e; e = e->next) {
                if (e->type == EDGE_DEFINED_BY_ARITHMETIC) continue;
                
                r->body_preds[idx] = strdup(e->target->name);
                r->body_arities[idx] = e->target->arity;
                r->body_negative[idx] = (e->type == EDGE_DEFINED_BY_NEGATION);
                idx++;
            }
            
            /* Add arithmetic slot at the end */
            r->body_preds[idx] = strdup("__arith__");
            r->body_arities[idx] = 0;
            r->body_negative[idx] = 0;
            r->arith_exprs[idx] = copy_expr(arith_edge->arith_expr);
            r->arith_result_vars[idx] = strdup(arith_edge->arith_result_var);
            
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
        
        /* Non-aggregate, non-arithmetic rule */
        int body_count = 0;
        for (Edge *e = n->outgoing; e; e = e->next) {
            if (e->type == EDGE_DEFINED_BY_BASE ||
                e->type == EDGE_DEFINED_BY_RECURSIVE ||
                e->type == EDGE_DEFINED_BY_COMPOSITION ||
                e->type == EDGE_DEFINED_BY_NEGATION) {
                body_count++;
            }
        }
        
        if (body_count == 0) continue;
        
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
        
        int i = 0;
        for (Edge *e = n->outgoing; e; e = e->next) {
            if (e->type == EDGE_DEFINED_BY_BASE ||
                e->type == EDGE_DEFINED_BY_RECURSIVE ||
                e->type == EDGE_DEFINED_BY_COMPOSITION ||
                e->type == EDGE_DEFINED_BY_NEGATION) {
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
