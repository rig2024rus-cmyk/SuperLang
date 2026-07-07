#include "graph.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Graph *graph_new(void) {
    Graph *g = calloc(1, sizeof(Graph));
    return g;
}

void graph_free(Graph *g) {
    if (!g) return;
    Node *n = g->nodes;
    while (n) {
        Node *next_n = n->next;
        Edge *e = n->outgoing;
        while (e) {
            Edge *next_e = e->next;
            if (e->aggregate_func) free(e->aggregate_func);
            if (e->arith_result_var) free(e->arith_result_var);
            if (e->arith_expr) expr_free(e->arith_expr);
            free(e);
            e = next_e;
        }
        free(n->name);
        free(n);
        n = next_n;
    }
    free(g);
}

Node *graph_add_node(Graph *g, const char *name, int arity, NodeType type) {
    Node *existing = graph_find_node(g, name);
    if (existing) return existing;
    
    Node *n = calloc(1, sizeof(Node));
    n->name = strdup(name);
    n->arity = arity;
    n->type = type;
    n->outgoing = NULL;
    n->next = g->nodes;
    g->nodes = n;
    g->node_count++;
    return n;
}

void graph_add_edge(Graph *g, Node *from, Node *to, EdgeType type) {
    Edge *e = calloc(1, sizeof(Edge));
    e->target = to;
    e->type = type;
    e->aggregate_func = NULL;
    e->aggregate_field = -1;
    e->arith_expr = NULL;
    e->arith_result_var = NULL;
    e->next = NULL;
    
    if (!from->outgoing) {
        from->outgoing = e;
    } else {
        Edge *last = from->outgoing;
        while (last->next) last = last->next;
        last->next = e;
    }
    g->edge_count++;
}

Node *graph_find_node(Graph *g, const char *name) {
    for (Node *n = g->nodes; n; n = n->next) {
        if (strcmp(n->name, name) == 0) return n;
    }
    return NULL;
}

const char *node_type_str(NodeType t) {
    switch (t) {
        case NODE_BASE: return "base";
        case NODE_DERIVED: return "derived";
        case NODE_OBSERVATION: return "observation";
    }
    return "?";
}

const char *edge_type_str(EdgeType t) {
    switch (t) {
        case EDGE_REQUIRES: return "requires";
        case EDGE_DEFINED_BY_BASE: return "defined_by_base";
        case EDGE_DEFINED_BY_RECURSIVE: return "defined_by_recursive";
        case EDGE_DEFINED_BY_COMPOSITION: return "defined_by_composition";
        case EDGE_DEFINED_BY_NEGATION: return "defined_by_negation";
        case EDGE_DEFINED_BY_AGGREGATE: return "defined_by_aggregate";
        case EDGE_DEFINED_BY_ARITHMETIC: return "defined_by_arithmetic";
    }
    return "?";
}

void graph_dump(const Graph *g) {
    printf("Dependency Graph: %zu nodes, %zu edges\n",
           g->node_count, g->edge_count);
    for (Node *n = g->nodes; n; n = n->next) {
        printf("  [%s] %s/%d\n", node_type_str(n->type), n->name, n->arity);
        for (Edge *e = n->outgoing; e; e = e->next) {
            if (e->type == EDGE_DEFINED_BY_AGGREGATE) {
                printf("    --%s(%s, field_%d)--> %s\n",
                       edge_type_str(e->type), e->aggregate_func,
                       e->aggregate_field, e->target->name);
            } else if (e->type == EDGE_DEFINED_BY_ARITHMETIC) {
                printf("    --%s(%s = <expr>)--> %s\n",
                       edge_type_str(e->type), e->arith_result_var,
                       e->target->name);
            } else {
                printf("    --%s--> %s\n", edge_type_str(e->type), e->target->name);
            }
        }
    }
}
