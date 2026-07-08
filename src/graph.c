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
            for (int i = 0; i < e->var_binding_count; i++) {
                free(e->var_bindings[i].var_name);
            }
            free(e->var_bindings);
            for (int i = 0; i < e->atom_arg_count; i++) {
                free(e->atom_args[i]);
            }
            free(e->atom_args);
            /* Освобождение comparisons (v0.8) */
            for (int i = 0; i < e->comparison_count; i++) {
                comparison_free(&e->comparisons[i]);
            }
            free(e->comparisons);
            free(e);
            e = next_e;
        }
        free(n->name);
        for (int i = 0; i < n->head_param_count; i++) {
            free(n->head_params[i]);
        }
        free(n->head_params);
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
    n->head_params = NULL;
    n->head_param_count = 0;
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
    e->var_bindings = NULL;
    e->var_binding_count = 0;
    e->atom_args = NULL;
    e->atom_arg_count = 0;
    e->comparisons = NULL;
    e->comparison_count = 0;
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
        case EDGE_DEFINED_BY_FILTER: return "defined_by_filter";
    }
    return "?";
}

void graph_dump(const Graph *g) {
    printf("Dependency Graph: %zu nodes, %zu edges\n",
           g->node_count, g->edge_count);
    for (Node *n = g->nodes; n; n = n->next) {
        printf("  [%s] %s/%d", node_type_str(n->type), n->name, n->arity);
        if (n->head_param_count > 0) {
            printf(" (");
            for (int i = 0; i < n->head_param_count; i++) {
                if (i > 0) printf(", ");
                printf("%s", n->head_params[i]);
            }
            printf(")");
        }
        printf("\n");
        for (Edge *e = n->outgoing; e; e = e->next) {
            if (e->type == EDGE_DEFINED_BY_AGGREGATE) {
                printf("    --%s(%s, field_%d)--> %s\n",
                       edge_type_str(e->type), e->aggregate_func,
                       e->aggregate_field, e->target->name);
            } else if (e->type == EDGE_DEFINED_BY_ARITHMETIC) {
                printf("    --%s(%s = <expr>)--> %s\n",
                       edge_type_str(e->type), e->arith_result_var,
                       e->target->name);
            } else if (e->type == EDGE_DEFINED_BY_FILTER) {
                printf("    --%s(%d comparisons)--> %s\n",
                       edge_type_str(e->type), e->comparison_count,
                       e->target->name);
            } else {
                printf("    --%s--> %s", edge_type_str(e->type), e->target->name);
                if (e->var_binding_count > 0) {
                    printf(" [");
                    for (int i = 0; i < e->var_binding_count; i++) {
                        if (i > 0) printf(", ");
                        printf("%s@%d", e->var_bindings[i].var_name, e->var_bindings[i].arg_index);
                    }
                    printf("]");
                }
                printf("\n");
            }
        }
    }
}
