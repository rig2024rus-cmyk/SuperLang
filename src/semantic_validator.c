#include "semantic_validator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int index;
    int lowlink;
    int on_stack;
} NodeInfo;

typedef struct {
    Node **nodes;
    int count;
    int capacity;
    int has_negative_internal;
} SCC;

typedef struct {
    SCC *items;
    int count;
    int capacity;
} SCCList;

typedef struct {
    Node **node_array;
    int node_count;
    NodeInfo *info;
    Node **stack;
    int stack_size;
    int index_counter;
    SCCList *sccs;
} TarjanCtx;

static int node_index(TarjanCtx *ctx, Node *n) {
    for (int i = 0; i < ctx->node_count; i++) {
        if (ctx->node_array[i] == n) return i;
    }
    return -1;
}

static void tarjan_visit(TarjanCtx *ctx, int v_idx) {
    NodeInfo *vi = &ctx->info[v_idx];
    vi->index = ctx->index_counter;
    vi->lowlink = ctx->index_counter;
    ctx->index_counter++;
    vi->on_stack = 1;
    ctx->stack[ctx->stack_size++] = ctx->node_array[v_idx];

    Node *v = ctx->node_array[v_idx];
    for (Edge *e = v->outgoing; e; e = e->next) {
        int w_idx = node_index(ctx, e->target);
        if (w_idx == -1) continue;
        NodeInfo *wi = &ctx->info[w_idx];
        if (wi->index == -1) {
            tarjan_visit(ctx, w_idx);
            if (wi->lowlink < vi->lowlink) vi->lowlink = wi->lowlink;
        } else if (wi->on_stack) {
            if (wi->index < vi->lowlink) vi->lowlink = wi->index;
        }
    }

    if (vi->lowlink == vi->index) {
        if (ctx->sccs->count >= ctx->sccs->capacity) {
            ctx->sccs->capacity = ctx->sccs->capacity ? ctx->sccs->capacity * 2 : 4;
            ctx->sccs->items = realloc(ctx->sccs->items, sizeof(SCC) * ctx->sccs->capacity);
        }
        SCC *scc = &ctx->sccs->items[ctx->sccs->count];
        scc->nodes = NULL;
        scc->count = 0;
        scc->capacity = 0;
        scc->has_negative_internal = 0;

        Node *w;
        do {
            w = ctx->stack[--ctx->stack_size];
            ctx->info[node_index(ctx, w)].on_stack = 0;
            if (scc->count >= scc->capacity) {
                scc->capacity = scc->capacity ? scc->capacity * 2 : 4;
                scc->nodes = realloc(scc->nodes, sizeof(Node*) * scc->capacity);
            }
            scc->nodes[scc->count++] = w;
        } while (w != v);

        ctx->sccs->count++;
    }
}

static void compute_scc_internal_negation(TarjanCtx *ctx) {
    for (int s = 0; s < ctx->sccs->count; s++) {
        SCC *scc = &ctx->sccs->items[s];
        for (int i = 0; i < scc->count && !scc->has_negative_internal; i++) {
            Node *n = scc->nodes[i];
            for (Edge *e = n->outgoing; e; e = e->next) {
                if (e->type != EDGE_DEFINED_BY_NEGATION) continue;
                for (int j = 0; j < scc->count; j++) {
                    if (scc->nodes[j] == e->target) {
                        scc->has_negative_internal = 1;
                        break;
                    }
                }
                if (scc->has_negative_internal) break;
            }
        }
    }
}

static SCCList *find_sccs(const Graph *g) {
    TarjanCtx ctx;
    ctx.node_count = (int)g->node_count;
    ctx.node_array = malloc(sizeof(Node*) * ctx.node_count);
    ctx.info = malloc(sizeof(NodeInfo) * ctx.node_count);
    ctx.stack = malloc(sizeof(Node*) * ctx.node_count);
    ctx.stack_size = 0;
    ctx.index_counter = 0;

    int i = 0;
    for (Node *n = g->nodes; n; n = n->next, i++) {
        ctx.node_array[i] = n;
        ctx.info[i].index = -1;
        ctx.info[i].lowlink = -1;
        ctx.info[i].on_stack = 0;
    }

    ctx.sccs = calloc(1, sizeof(SCCList));
    for (i = 0; i < ctx.node_count; i++) {
        if (ctx.info[i].index == -1) {
            tarjan_visit(&ctx, i);
        }
    }

    compute_scc_internal_negation(&ctx);

    free(ctx.node_array);
    free(ctx.info);
    free(ctx.stack);
    return ctx.sccs;
}

static void free_scc_list(SCCList *list) {
    if (!list) return;
    for (int i = 0; i < list->count; i++) free(list->items[i].nodes);
    free(list->items);
    free(list);
}

static ValidationResult make_valid(void) {
    ValidationResult r = {1, NULL, 0};
    return r;
}

static ValidationResult make_invalid(const char *msg) {
    ValidationResult r = {0, strdup(msg), 1};
    return r;
}

void validation_result_free(ValidationResult *r) {
    if (r && r->error_message) {
        free(r->error_message);
        r->error_message = NULL;
    }
}

void validation_result_dump(const ValidationResult *r) {
    if (r->is_valid) printf("✓ PASSED\n");
    else printf("✗ FAILED: %s\n", r->error_message);
}

ValidationResult graph_validate_structure(const Graph *g) {
    if (!g) return make_invalid("Graph is NULL");
    for (Node *n = g->nodes; n; n = n->next) {
        if (n->type == NODE_DERIVED) {
            int has_def = 0;
            for (Edge *e = n->outgoing; e; e = e->next) {
                if (e->type == EDGE_DEFINED_BY_BASE ||
                    e->type == EDGE_DEFINED_BY_RECURSIVE ||
                    e->type == EDGE_DEFINED_BY_COMPOSITION ||
                    e->type == EDGE_DEFINED_BY_NEGATION ||
                    e->type == EDGE_DEFINED_BY_AGGREGATE ||
                    e->type == EDGE_DEFINED_BY_ARITHMETIC) {
                    has_def = 1; break;
                }
            }
            if (!has_def) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Derived node '%s' has no definition", n->name);
                return make_invalid(msg);
            }
        }
    }
    return make_valid();
}

ValidationResult graph_validate_semantics(const Graph *g) {
    if (!g) return make_invalid("Graph is NULL");
    SCCList *sccs = find_sccs(g);
    for (int i = 0; i < sccs->count; i++) {
        SCC *scc = &sccs->items[i];
        if (scc->count == 1) {
            Node *n = scc->nodes[0];
            int self_loop = 0;
            for (Edge *e = n->outgoing; e; e = e->next) {
                if (e->target == n) { self_loop = 1; break; }
            }
            if (!self_loop) continue;
        }
        if (scc->has_negative_internal) {
            char msg[512];
            snprintf(msg, sizeof(msg),
                "Negative recursion detected in SCC containing '%s'. Program is not stratifiable.",
                scc->nodes[0]->name);
            free_scc_list(sccs);
            return make_invalid(msg);
        }
    }
    free_scc_list(sccs);
    return make_valid();
}
