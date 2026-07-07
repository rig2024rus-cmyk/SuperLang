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
    EDGE_DEFINED_BY_ARITHMETIC
} EdgeType;

typedef struct Edge {
    struct Node *target;
    EdgeType type;
    char *aggregate_func;
    int aggregate_field;
    Expr *arith_expr;              /* for EDGE_DEFINED_BY_ARITHMETIC */
    char *arith_result_var;        /* for EDGE_DEFINED_BY_ARITHMETIC */
    struct Edge *next;
} Edge;

typedef struct Node {
    char *name;
    int arity;
    NodeType type;
    Edge *outgoing;
    struct Node *next;
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
