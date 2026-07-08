#include "ast_to_graph.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char **names;
    int count;
    int capacity;
} StringSet;

static void string_set_init(StringSet *s) {
    s->names = NULL;
    s->count = 0;
    s->capacity = 0;
}

static void string_set_add(StringSet *s, const char *name) {
    for (int i = 0; i < s->count; i++) {
        if (strcmp(s->names[i], name) == 0) return;
    }
    if (s->count >= s->capacity) {
        s->capacity = s->capacity == 0 ? 8 : s->capacity * 2;
        s->names = realloc(s->names, sizeof(char*) * s->capacity);
    }
    s->names[s->count++] = strdup(name);
}

static int string_set_contains(StringSet *s, const char *name) {
    for (int i = 0; i < s->count; i++) {
        if (strcmp(s->names[i], name) == 0) return 1;
    }
    return 0;
}

static void string_set_free(StringSet *s) {
    for (int i = 0; i < s->count; i++) free(s->names[i]);
    free(s->names);
    s->names = NULL;
    s->count = 0;
    s->capacity = 0;
}

static void append_edge(Node *from, Edge *e) {
    if (!from->outgoing) {
        from->outgoing = e;
    } else {
        Edge *last = from->outgoing;
        while (last->next) last = last->next;
        last->next = e;
    }
}

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

/* Deep copy a comparison (v0.8) */
static Comparison copy_comparison(const Comparison *c) {
    Comparison result;
    result.left = copy_expr(c->left);
    result.op = c->op;
    result.right = copy_expr(c->right);
    result.loc = c->loc;
    return result;
}

static void node_set_head_params(Node *n, char **params, int param_count) {
    if (n->head_param_count > 0) return;
    n->head_params = malloc(sizeof(char*) * param_count);
    n->head_param_count = param_count;
    for (int i = 0; i < param_count; i++) {
        n->head_params[i] = strdup(params[i]);
    }
}

static void add_base_edge(Graph *g, Node *head, Node *impl, char **params, int param_count) {
    Edge *e = calloc(1, sizeof(Edge));
    e->target = impl;
    e->type = EDGE_DEFINED_BY_BASE;
    e->aggregate_func = NULL;
    e->aggregate_field = -1;
    e->arith_expr = NULL;
    e->arith_result_var = NULL;
    e->atom_args = NULL;
    e->atom_arg_count = 0;
    e->comparisons = NULL;
    e->comparison_count = 0;
    e->next = NULL;
    e->var_bindings = malloc(sizeof(EdgeVarBinding) * param_count);
    e->var_binding_count = param_count;
    for (int j = 0; j < param_count; j++) {
        e->var_bindings[j].var_name = strdup(params[j]);
        e->var_bindings[j].arg_index = j;
    }
    append_edge(head, e);
    g->edge_count++;
}

static void add_condition_edges(Graph *g, const char *target_name,
                                const Condition *cond,
                                StringSet *base_relations,
                                StringSet *derived_predicates) {
    /* Process positive atoms */
    for (int i = 0; i < cond->atom_count; i++) {
        const Atom *atom = &cond->atoms[i];
        if (atom->negated || atom->aggregate_func) continue;
        EdgeType edge_type;
        if (string_set_contains(base_relations, atom->predicate) ||
            string_set_contains(derived_predicates, atom->predicate)) {
            edge_type = EDGE_DEFINED_BY_COMPOSITION;
        } else {
            graph_add_node(g, atom->predicate, atom->arg_count, NODE_BASE);
            string_set_add(base_relations, atom->predicate);
            edge_type = EDGE_DEFINED_BY_COMPOSITION;
        }
        Node *from = graph_find_node(g, target_name);
        Node *to = graph_find_node(g, atom->predicate);
        if (from && to) {
            Edge *e = calloc(1, sizeof(Edge));
            e->target = to;
            e->type = edge_type;
            e->aggregate_func = NULL;
            e->aggregate_field = -1;
            e->arith_expr = NULL;
            e->arith_result_var = NULL;
            e->comparisons = NULL;
            e->comparison_count = 0;
            e->next = NULL;
            e->var_bindings = malloc(sizeof(EdgeVarBinding) * atom->arg_count);
            e->var_binding_count = atom->arg_count;
            for (int j = 0; j < atom->arg_count; j++) {
                e->var_bindings[j].var_name = strdup(atom->args[j]);
                e->var_bindings[j].arg_index = j;
            }
            e->atom_args = malloc(sizeof(char*) * atom->arg_count);
            e->atom_arg_count = atom->arg_count;
            for (int j = 0; j < atom->arg_count; j++) {
                e->atom_args[j] = strdup(atom->args[j]);
            }
            append_edge(from, e);
            g->edge_count++;
        }
    }
    
    /* Process negative atoms */
    for (int i = 0; i < cond->atom_count; i++) {
        const Atom *atom = &cond->atoms[i];
        if (!atom->negated) continue;
        if (!string_set_contains(base_relations, atom->predicate) &&
            !string_set_contains(derived_predicates, atom->predicate)) {
            graph_add_node(g, atom->predicate, atom->arg_count, NODE_BASE);
            string_set_add(base_relations, atom->predicate);
        }
        Node *from = graph_find_node(g, target_name);
        Node *to = graph_find_node(g, atom->predicate);
        if (from && to) {
            Edge *e = calloc(1, sizeof(Edge));
            e->target = to;
            e->type = EDGE_DEFINED_BY_NEGATION;
            e->aggregate_func = NULL;
            e->aggregate_field = -1;
            e->arith_expr = NULL;
            e->arith_result_var = NULL;
            e->comparisons = NULL;
            e->comparison_count = 0;
            e->next = NULL;
            e->var_bindings = malloc(sizeof(EdgeVarBinding) * atom->arg_count);
            e->var_binding_count = atom->arg_count;
            for (int j = 0; j < atom->arg_count; j++) {
                e->var_bindings[j].var_name = strdup(atom->args[j]);
                e->var_bindings[j].arg_index = j;
            }
            e->atom_args = malloc(sizeof(char*) * atom->arg_count);
            e->atom_arg_count = atom->arg_count;
            for (int j = 0; j < atom->arg_count; j++) {
                e->atom_args[j] = strdup(atom->args[j]);
            }
            append_edge(from, e);
            g->edge_count++;
        }
    }
    
    /* Process aggregate atoms */
    for (int i = 0; i < cond->atom_count; i++) {
        const Atom *atom = &cond->atoms[i];
        if (!atom->aggregate_func) continue;
        if (!string_set_contains(base_relations, atom->predicate) &&
            !string_set_contains(derived_predicates, atom->predicate)) {
            graph_add_node(g, atom->predicate, atom->arg_count - 1, NODE_BASE);
            string_set_add(base_relations, atom->predicate);
        }
        Node *from = graph_find_node(g, target_name);
        Node *to = graph_find_node(g, atom->predicate);
        if (from && to) {
            Edge *e = calloc(1, sizeof(Edge));
            e->target = to;
            e->type = EDGE_DEFINED_BY_AGGREGATE;
            e->aggregate_func = strdup(atom->aggregate_func);
            e->aggregate_field = atom->aggregate_field;
            e->arith_expr = NULL;
            e->arith_result_var = NULL;
            e->var_bindings = NULL;
            e->var_binding_count = 0;
            e->atom_args = NULL;
            e->atom_arg_count = 0;
            e->comparisons = NULL;
            e->comparison_count = 0;
            e->next = NULL;
            append_edge(from, e);
            g->edge_count++;
        }
    }
    
    /* Process arithmetic assignments */
    for (int i = 0; i < cond->arith_count; i++) {
        const ArithAssignment *a = &cond->arith_assigns[i];
        Node *from = graph_find_node(g, target_name);
        if (!from) continue;
        Edge *e = calloc(1, sizeof(Edge));
        e->target = from;
        e->type = EDGE_DEFINED_BY_ARITHMETIC;
        e->aggregate_func = NULL;
        e->aggregate_field = -1;
        e->arith_expr = copy_expr(a->expr);
        e->arith_result_var = strdup(a->result_var);
        e->var_bindings = NULL;
        e->var_binding_count = 0;
        e->atom_args = NULL;
        e->atom_arg_count = 0;
        e->comparisons = NULL;
        e->comparison_count = 0;
        e->next = NULL;
        append_edge(from, e);
        g->edge_count++;
    }
    
    /* Process comparisons (FILTER edge) - v0.8 */
    if (cond->comparison_count > 0) {
        Node *from = graph_find_node(g, target_name);
        if (from) {
            Edge *e = calloc(1, sizeof(Edge));
            e->target = from;  /* Filter edge points to self */
            e->type = EDGE_DEFINED_BY_FILTER;
            e->aggregate_func = NULL;
            e->aggregate_field = -1;
            e->arith_expr = NULL;
            e->arith_result_var = NULL;
            e->var_bindings = NULL;
            e->var_binding_count = 0;
            e->atom_args = NULL;
            e->atom_arg_count = 0;
            e->comparison_count = cond->comparison_count;
            e->comparisons = malloc(sizeof(Comparison) * cond->comparison_count);
            for (int i = 0; i < cond->comparison_count; i++) {
                e->comparisons[i] = copy_comparison(&cond->comparisons[i]);
            }
            e->next = NULL;
            append_edge(from, e);
            g->edge_count++;
        }
    }
}

typedef struct {
    char *name;
    int count;
} ImplCounter;

static int impl_next_id(ImplCounter **counters, int *counter_count, int *counter_cap, const char *name) {
    for (int i = 0; i < *counter_count; i++) {
        if (strcmp((*counters)[i].name, name) == 0) {
            return (*counters)[i].count++;
        }
    }
    if (*counter_count >= *counter_cap) {
        *counter_cap = *counter_cap == 0 ? 8 : *counter_cap * 2;
        *counters = realloc(*counters, sizeof(ImplCounter) * (*counter_cap));
    }
    (*counters)[*counter_count].name = strdup(name);
    (*counters)[*counter_count].count = 1;
    (*counter_count)++;
    return 0;
}

TranslationResult ast_to_graph_translate(const Program *program) {
    TranslationResult result;
    result.graph = graph_new();
    result.is_valid = 1;
    result.error_message = NULL;
    result.error_line = 0;
    result.error_column = 0;
    if (!program) {
        result.is_valid = 0;
        result.error_message = strdup("Program is NULL");
        return result;
    }
    StringSet base_relations;
    StringSet derived_predicates;
    string_set_init(&base_relations);
    string_set_init(&derived_predicates);
    
    for (int i = 0; i < program->relation_count; i++) {
        const RelationDecl *r = &program->relations[i];
        graph_add_node(result.graph, r->name, r->param_count, NODE_BASE);
        string_set_add(&base_relations, r->name);
    }
    
    for (int i = 0; i < program->derive_count; i++) {
        const DeriveDecl *d = &program->derives[i];
        Node *n = graph_add_node(result.graph, d->name, d->param_count, NODE_DERIVED);
        node_set_head_params(n, d->params, d->param_count);
        string_set_add(&derived_predicates, d->name);
    }
    
    for (int i = 0; i < program->observe_count; i++) {
        const ObserveDecl *o = &program->observes[i];
        Node *n = graph_add_node(result.graph, o->name, o->param_count, NODE_DERIVED);
        node_set_head_params(n, o->params, o->param_count);
        string_set_add(&derived_predicates, o->name);
    }
    
    ImplCounter *counters = NULL;
    int counter_count = 0;
    int counter_cap = 0;
    
    for (int i = 0; i < program->derive_count; i++) {
        const DeriveDecl *d = &program->derives[i];
        if (d->condition.atom_count == 0 && 
            d->condition.arith_count == 0 && 
            d->condition.comparison_count == 0) {
            continue;
        }
        int id = impl_next_id(&counters, &counter_count, &counter_cap, d->name);
        char impl_name[256];
        snprintf(impl_name, sizeof(impl_name), "%s_impl_%d", d->name, id);
        Node *impl_node = graph_add_node(result.graph, impl_name, d->param_count, NODE_DERIVED);
        node_set_head_params(impl_node, d->params, d->param_count);
        Node *head_node = graph_find_node(result.graph, d->name);
        add_base_edge(result.graph, head_node, impl_node, d->params, d->param_count);
        add_condition_edges(result.graph, impl_name, &d->condition,
                            &base_relations, &derived_predicates);
        string_set_add(&derived_predicates, impl_name);
    }
    
    for (int i = 0; i < program->observe_count; i++) {
        const ObserveDecl *o = &program->observes[i];
        if (o->condition.atom_count == 0 && 
            o->condition.arith_count == 0 && 
            o->condition.comparison_count == 0) {
            continue;
        }
        int id = impl_next_id(&counters, &counter_count, &counter_cap, o->name);
        char impl_name[256];
        snprintf(impl_name, sizeof(impl_name), "%s_impl_%d", o->name, id);
        Node *impl_node = graph_add_node(result.graph, impl_name, o->param_count, NODE_DERIVED);
        node_set_head_params(impl_node, o->params, o->param_count);
        Node *head_node = graph_find_node(result.graph, o->name);
        add_base_edge(result.graph, head_node, impl_node, o->params, o->param_count);
        add_condition_edges(result.graph, impl_name, &o->condition,
                            &base_relations, &derived_predicates);
        string_set_add(&derived_predicates, impl_name);
    }
    
    for (int i = 0; i < counter_count; i++) free(counters[i].name);
    free(counters);
    string_set_free(&base_relations);
    string_set_free(&derived_predicates);
    return result;
}

void translation_result_free(TranslationResult *result) {
    if (!result) return;
    if (result->graph) {
        graph_free(result->graph);
        result->graph = NULL;
    }
    if (result->error_message) {
        free(result->error_message);
        result->error_message = NULL;
    }
}
