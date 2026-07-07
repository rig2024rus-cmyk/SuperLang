#ifndef SUPERLANG_SYNTHESIZER_H
#define SUPERLANG_SYNTHESIZER_H

#include "graph.h"

typedef struct Rule {
    char *head;
    int head_arity;
    char **body_preds;
    int body_count;
    int *body_arities;
    int *body_negative;
    int is_recursive;
    char **aggregate_funcs;
    int *aggregate_fields;
    
    /* Arithmetic: expression and result variable name per body slot */
    Expr **arith_exprs;
    char **arith_result_vars;
    
    struct Rule *next;
} Rule;

typedef struct ClosureIR {
    Rule *rules;
    size_t rule_count;
    size_t lambda_count;
} ClosureIR;

ClosureIR *synthesize(const Graph *g);
void closure_free(ClosureIR *c);
void closure_dump(const ClosureIR *c);

#endif
