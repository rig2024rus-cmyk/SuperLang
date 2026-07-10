#ifndef SUPERLANG_SYNTHESIZER_H
#define SUPERLANG_SYNTHESIZER_H

#include "graph.h"

/* Location of a variable in a body atom */
typedef struct {
    int atom_index;      /* which body atom (0, 1, 2, ...) */
    int arg_index;       /* which argument position in that atom */
} ArgLocation;

/* Binding information for a variable */
typedef struct {
    char *var_name;                    /* variable name (e.g., "being", "greeter") */
    ArgLocation *locations;            /* all places where this variable appears */
    int location_count;
    int is_head;                       /* does this variable appear in the head? */
    int head_arg_index;                /* if is_head, which position in head args */
} ArgBinding;

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
    
    /* Arithmetic */
    Expr **arith_exprs;
    char **arith_result_vars;
    
    /* Comparisons (filters) */
    Comparison **comparisons;
    int *comparison_counts;
    
    /* Variable bindings for general join */
    ArgBinding *arg_bindings;
    int arg_binding_count;
    
    /* Evaluation stratum, copied from the source Node's graph_compute_strata()
     * result. saturate() must apply rules in non-decreasing stratum order,
     * running each stratum to its own fixpoint before moving to the next. */
    int stratum;
    
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
