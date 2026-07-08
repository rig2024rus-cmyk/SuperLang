#include "semantic_validator.h"
#include "graph.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ====================================================================== */
/* Tarjan's SCC algorithm                                                  */
/* ====================================================================== */

typedef struct {
    int *index;
    int *lowlink;
    int *on_stack;
    int *stack;
    int stack_top;
    int current_index;
    size_t node_count;
    int **sccs;
    int *scc_sizes;
    int scc_count;
    int scc_capacity;
} TarjanState;

static TarjanState *tarjan_state_new(size_t node_count) {
    TarjanState *state = calloc(1, sizeof(TarjanState));
    if (!state) return NULL;

    state->index = malloc(node_count * sizeof(int));
    state->lowlink = malloc(node_count * sizeof(int));
    state->on_stack = calloc(node_count, sizeof(int));
    state->stack = malloc(node_count * sizeof(int));
    state->stack_top = 0;
    state->current_index = 0;
    state->node_count = node_count;
    state->sccs = NULL;
    state->scc_sizes = NULL;
    state->scc_count = 0;
    state->scc_capacity = 0;

    for (size_t i = 0; i < node_count; i++) {
        state->index[i] = -1;
        state->lowlink[i] = -1;
    }

    return state;
}

static void tarjan_state_free(TarjanState *state) {
    if (!state) return;
    free(state->index);
    free(state->lowlink);
    free(state->on_stack);
    free(state->stack);
    for (int i = 0; i < state->scc_count; i++) {
        free(state->sccs[i]);
    }
    free(state->sccs);
    free(state->scc_sizes);
    free(state);
}

/* Build array of node pointers from linked list for indexed access.
 * Caller must free the returned array (but not the Nodes it points to). */
static Node **build_nodes_array(const Graph *g) {
    Node **arr = malloc(g->node_count * sizeof(Node*));
    if (!arr) return NULL;
    size_t i = 0;
    for (Node *n = g->nodes; n && i < g->node_count; n = n->next) {
        arr[i++] = n;
    }
    return arr;
}

/* Find index of target node in array. Returns -1 if not found. */
static int find_node_index(Node **nodes_arr, size_t node_count, const Node *target) {
    for (size_t i = 0; i < node_count; i++) {
        if (nodes_arr[i] == target) return (int)i;
    }
    return -1;
}

static void tarjan_strongconnect(Node **nodes_arr, size_t node_count,
                                  int v, TarjanState *state) {
    state->index[v] = state->current_index;
    state->lowlink[v] = state->current_index;
    state->current_index++;
    state->stack[state->stack_top++] = v;
    state->on_stack[v] = 1;

    Node *node = nodes_arr[v];
    for (Edge *e = node->outgoing; e; e = e->next) {
        /* Only dependency edges form the SCC graph */
        if (e->type != EDGE_DEFINED_BY_COMPOSITION &&
            e->type != EDGE_DEFINED_BY_RECURSIVE &&
            e->type != EDGE_DEFINED_BY_NEGATION &&
            e->type != EDGE_DEFINED_BY_BASE) {
            continue;
        }

        int w = find_node_index(nodes_arr, node_count, e->target);
        if (w == -1) continue;

        if (state->index[w] == -1) {
            tarjan_strongconnect(nodes_arr, node_count, w, state);
            if (state->lowlink[w] < state->lowlink[v]) {
                state->lowlink[v] = state->lowlink[w];
            }
        } else if (state->on_stack[w]) {
            if (state->index[w] < state->lowlink[v]) {
                state->lowlink[v] = state->index[w];
            }
        }
    }

    if (state->lowlink[v] == state->index[v]) {
        if (state->scc_count >= state->scc_capacity) {
            state->scc_capacity = state->scc_capacity == 0 ? 8 : state->scc_capacity * 2;
            state->sccs = realloc(state->sccs, state->scc_capacity * sizeof(int*));
            state->scc_sizes = realloc(state->scc_sizes, state->scc_capacity * sizeof(int));
        }

        int *scc = malloc(state->node_count * sizeof(int));
        int scc_size = 0;
        int w;
        do {
            w = state->stack[--state->stack_top];
            state->on_stack[w] = 0;
            scc[scc_size++] = w;
        } while (w != v);

        state->sccs[state->scc_count] = malloc(scc_size * sizeof(int));
        memcpy(state->sccs[state->scc_count], scc, scc_size * sizeof(int));
        state->scc_sizes[state->scc_count] = scc_size;
        state->scc_count++;
        free(scc);
    }
}

static TarjanState *tarjan_find_sccs(const Graph *g, Node **nodes_arr) {
    TarjanState *state = tarjan_state_new(g->node_count);
    if (!state) return NULL;

    for (size_t i = 0; i < g->node_count; i++) {
        if (state->index[i] == -1) {
            tarjan_strongconnect(nodes_arr, g->node_count, (int)i, state);
        }
    }

    return state;
}

/* ====================================================================== */
/* Structural validation                                                   */
/* ====================================================================== */

ValidationResult graph_validate_structure(const Graph *g) {
    ValidationResult result;
    memset(&result, 0, sizeof(result));
    result.is_valid = 1;
    result.error_message = NULL;

    if (!g) {
        result.is_valid = 0;
        result.error_message = strdup("Graph is NULL");
        return result;
    }

    if (g->node_count == 0) {
        result.is_valid = 0;
        result.error_message = strdup("Graph has no nodes");
        return result;
    }

    /* Iterate linked list of nodes */
    for (Node *n = g->nodes; n; n = n->next) {
        if (n->type == NODE_DERIVED && !n->outgoing) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "Derived node '%s' has no definition (no outgoing edges)",
                     n->name);
            result.is_valid = 0;
            result.error_message = strdup(msg);
            return result;
        }
    }

    return result;
}

/* ====================================================================== */
/* Semantic validation (stratification check)                              */
/* ====================================================================== */

ValidationResult graph_validate_semantics(const Graph *g) {
    ValidationResult result;
    memset(&result, 0, sizeof(result));
    result.is_valid = 1;
    result.error_message = NULL;

    if (!g) {
        result.is_valid = 0;
        result.error_message = strdup("Graph is NULL");
        return result;
    }

    if (g->node_count == 0) {
        return result;
    }

    /* Build indexed array from linked list for Tarjan */
    Node **nodes_arr = build_nodes_array(g);
    if (!nodes_arr) {
        result.is_valid = 0;
        result.error_message = strdup("Out of memory building node index");
        return result;
    }

    TarjanState *state = tarjan_find_sccs(g, nodes_arr);
    if (!state) {
        free(nodes_arr);
        result.is_valid = 0;
        result.error_message = strdup("Out of memory in Tarjan SCC");
        return result;
    }

    for (int i = 0; i < state->scc_count; i++) {
        int scc_size = state->scc_sizes[i];

        /* Single-node SCC: check for self-negation */
        if (scc_size == 1) {
            int node_idx = state->sccs[i][0];
            Node *node = nodes_arr[node_idx];
            for (Edge *e = node->outgoing; e; e = e->next) {
                if (e->type == EDGE_DEFINED_BY_NEGATION && e->target == node) {
                    char msg[256];
                    snprintf(msg, sizeof(msg),
                             "Self-negation detected in '%s'. "
                             "Program is not stratifiable.",
                             node->name);
                    result.is_valid = 0;
                    result.error_message = strdup(msg);
                    tarjan_state_free(state);
                    free(nodes_arr);
                    return result;
                }
            }
            continue;
        }

        /* Multi-node SCC: check for negation edges within */
        int has_negation = 0;
        char *neg_source = NULL;
        char *neg_target = NULL;

        for (int j = 0; j < scc_size && !has_negation; j++) {
            int node_idx = state->sccs[i][j];
            Node *node = nodes_arr[node_idx];

            for (Edge *e = node->outgoing; e; e = e->next) {
                if (e->type != EDGE_DEFINED_BY_NEGATION) continue;

                /* Check if target is in the same SCC */
                for (int k = 0; k < scc_size; k++) {
                    if (nodes_arr[state->sccs[i][k]] == e->target) {
                        has_negation = 1;
                        neg_source = node->name;
                        neg_target = e->target->name;
                        break;
                    }
                }
                if (has_negation) break;
            }
        }

        if (has_negation) {
            char msg[512];
            snprintf(msg, sizeof(msg),
                     "Negative recursion detected in SCC containing '%s' -> '%s' "
                     "(%d nodes). Program is not stratifiable.",
                     neg_source, neg_target, scc_size);
            result.is_valid = 0;
            result.error_message = strdup(msg);
            tarjan_state_free(state);
            free(nodes_arr);
            return result;
        }
    }

    tarjan_state_free(state);
    free(nodes_arr);
    return result;
}

/* ====================================================================== */
/* Result helpers                                                          */
/* ====================================================================== */

void validation_result_free(ValidationResult *r) {
    if (!r) return;
    if (r->error_message) {
        free(r->error_message);
        r->error_message = NULL;
    }
}

void validation_result_dump(const ValidationResult *r) {
    if (r->is_valid) {
        printf("PASSED\n");
    } else {
        printf("FAILED: %s\n", r->error_message ? r->error_message : "(no message)");
    }
}
