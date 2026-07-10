#ifndef SUPERLANG_GRAPH_H
#define SUPERLANG_GRAPH_H

#include <stddef.h>
#include "ast_nodes.h"

typedef enum {
    NODE_BASE,
    NODE_DERIVED,
    NODE_OBSERVATION
} NodeType;

typedef enum {
    EDGE_REQUIRES,
    EDGE_DEFINED_BY_BASE,
    EDGE_DEFINED_BY_RECURSIVE,
    EDGE_DEFINED_BY_COMPOSITION,
    EDGE_DEFINED_BY_NEGATION,
    EDGE_DEFINED_BY_AGGREGATE,
    EDGE_DEFINED_BY_ARITHMETIC,
    EDGE_DEFINED_BY_FILTER
} EdgeType;

typedef struct {
    char *var_name;
    int arg_index;
} EdgeVarBinding;

typedef struct Edge {
    struct Node *target;
    EdgeType type;
    char *aggregate_func;
    int aggregate_field;
    Expr *arith_expr;
    char *arith_result_var;
    EdgeVarBinding *var_bindings;
    int var_binding_count;
    char **atom_args;
    int atom_arg_count;
    Comparison *comparisons;
    int comparison_count;
    struct Edge *next;
} Edge;

typedef struct Node {
    char *name;
    int arity;
    NodeType type;
    char **head_params;
    int head_param_count;
    Edge *outgoing;
    struct Node *next;
    int stratum;      /* computed by graph_compute_strata(); valid only after validation passes */
    int temp_idx;      /* scratch index used by Tarjan's SCC pass; not meaningful outside it */
} Node;

typedef struct Graph {
    Node *nodes;
    size_t node_count;
    size_t edge_count;
} Graph;

Graph *graph_new(void);
void graph_free(Graph *g);
Node *graph_add_node(Graph *g, const char *name, int arity, NodeType type);
void graph_add_edge(Graph *g, Node *from, Node *to, EdgeType type);
Node *graph_find_node(Graph *g, const char *name);
const char *node_type_str(NodeType t);
const char *edge_type_str(EdgeType t);
void graph_dump(const Graph *g);

#endif
