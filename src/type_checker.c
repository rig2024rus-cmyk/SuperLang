#include "type_checker.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ====================================================================== */
/* Helpers                                                                */
/* ====================================================================== */

typedef struct {
    const char *name;
    int arity;
    int is_derived;  /* 1 = derived/observe, 0 = base relation */
} PredInfo;

typedef struct {
    PredInfo *items;
    int count;
    int capacity;
} PredTable;

static void pred_table_init(PredTable *t) {
    t->items = NULL;
    t->count = 0;
    t->capacity = 0;
}

static void pred_table_add(PredTable *t, const char *name, int arity, int is_derived) {
    /* Don't add duplicates */
    for (int i = 0; i < t->count; i++) {
        if (strcmp(t->items[i].name, name) == 0) return;
    }
    if (t->count >= t->capacity) {
        t->capacity = t->capacity == 0 ? 16 : t->capacity * 2;
        t->items = realloc(t->items, sizeof(PredInfo) * t->capacity);
    }
    t->items[t->count].name = name;
    t->items[t->count].arity = arity;
    t->items[t->count].is_derived = is_derived;
    t->count++;
}

static const PredInfo *pred_table_find(const PredTable *t, const char *name) {
    for (int i = 0; i < t->count; i++) {
        if (strcmp(t->items[i].name, name) == 0) return &t->items[i];
    }
    return NULL;
}

static void pred_table_free(PredTable *t) {
    free(t->items);
    t->items = NULL;
    t->count = 0;
    t->capacity = 0;
}

/* ====================================================================== */
/* TypeCheckResult                                                        */
/* ====================================================================== */

static void result_add(TypeCheckResult *r, const char *message,
                       int line, int column, const char *hint) {
    if (r->count >= r->capacity) {
        r->capacity = r->capacity == 0 ? 8 : r->capacity * 2;
        r->errors = realloc(r->errors, sizeof(TypeError) * r->capacity);
    }
    TypeError *e = &r->errors[r->count++];
    e->message = strdup(message);
    e->line = line;
    e->column = column;
    e->hint = hint ? strdup(hint) : NULL;
}

void typecheck_result_free(TypeCheckResult *result) {
    if (!result) return;
    for (int i = 0; i < result->count; i++) {
        free(result->errors[i].message);
        if (result->errors[i].hint) free(result->errors[i].hint);
    }
    free(result->errors);
    result->errors = NULL;
    result->count = 0;
    result->capacity = 0;
}

void typecheck_result_dump(const TypeCheckResult *result) {
    if (result->count == 0) {
        printf("✓ TypeCheck: PASSED (0 errors)\n");
        return;
    }
    
    printf("✗ TypeCheck: FAILED (%d error%s)\n",
           result->count, result->count > 1 ? "s" : "");
    
    for (int i = 0; i < result->count; i++) {
        const TypeError *e = &result->errors[i];
        printf("\n  [%d] %d:%d: %s\n", i + 1, e->line, e->column, e->message);
        if (e->hint) {
            printf("      hint: %s\n", e->hint);
        }
    }
}

/* ====================================================================== */
/* Simple Levenshtein edit distance (for "Did you mean?" suggestions)    */
/* ====================================================================== */

static int levenshtein(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    
    int *prev = calloc(lb + 1, sizeof(int));
    int *curr = calloc(lb + 1, sizeof(int));
    
    for (size_t j = 0; j <= lb; j++) prev[j] = (int)j;
    
    for (size_t i = 1; i <= la; i++) {
        curr[0] = (int)i;
        for (size_t j = 1; j <= lb; j++) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            int del = prev[j] + 1;
            int ins = curr[j - 1] + 1;
            int sub = prev[j - 1] + cost;
            int min = del;
            if (ins < min) min = ins;
            if (sub < min) min = sub;
            curr[j] = min;
        }
        int *tmp = prev;
        prev = curr;
        curr = tmp;
    }
    
    int result = prev[lb];
    free(prev);
    free(curr);
    return result;
}

static const char *find_closest_predicate(const PredTable *t, const char *name) {
    const char *best = NULL;
    int best_dist = 1000;
    
    for (int i = 0; i < t->count; i++) {
        int d = levenshtein(name, t->items[i].name);
        if (d < best_dist && d <= 3) {
            best_dist = d;
            best = t->items[i].name;
        }
    }
    return best;
}

/* ====================================================================== */
/* Safety: track which variables are bound                                */
/* ====================================================================== */

typedef struct {
    char **names;
    int count;
    int capacity;
} VarSet;

static void var_set_init(VarSet *s) {
    s->names = NULL;
    s->count = 0;
    s->capacity = 0;
}

static void var_set_add(VarSet *s, const char *name) {
    for (int i = 0; i < s->count; i++) {
        if (strcmp(s->names[i], name) == 0) return;
    }
    if (s->count >= s->capacity) {
        s->capacity = s->capacity == 0 ? 8 : s->capacity * 2;
        s->names = realloc(s->names, sizeof(char*) * s->capacity);
    }
    s->names[s->count++] = strdup(name);
}

static int var_set_contains(const VarSet *s, const char *name) {
    for (int i = 0; i < s->count; i++) {
        if (strcmp(s->names[i], name) == 0) return 1;
    }
    return 0;
}

static void var_set_free(VarSet *s) {
    for (int i = 0; i < s->count; i++) free(s->names[i]);
    free(s->names);
    s->names = NULL;
    s->count = 0;
    s->capacity = 0;
}

/* ====================================================================== */
/* Check a condition: arity, existence, safety                           */
/* ====================================================================== */

static void check_condition(const Condition *cond, const PredTable *preds,
                            VarSet *bound_vars, TypeCheckResult *result) {
    /* First pass: bind variables from positive atoms, check arity/existence */
    for (int i = 0; i < cond->atom_count; i++) {
        const Atom *a = &cond->atoms[i];
        if (a->negated) continue;
        
        if (a->aggregate_func) {
            const char *source_pred = a->predicate;
            int source_arity = a->arg_count - 1;
            
            const PredInfo *info = pred_table_find(preds, source_pred);
            if (!info) {
                char msg[256];
                snprintf(msg, sizeof(msg), "Unknown predicate '%s'", source_pred);
                const char *suggestion = find_closest_predicate(preds, source_pred);
                char *hint = NULL;
                if (suggestion) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "Did you mean '%s'?", suggestion);
                    hint = strdup(buf);
                }
                result_add(result, msg, a->loc.line, a->loc.column, hint);
                if (hint) free(hint);
            } else if (info->arity != source_arity) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "Arity mismatch for '%s': declared %d, used with %d",
                         source_pred, info->arity, source_arity);
                result_add(result, msg, a->loc.line, a->loc.column, NULL);
            }
            
            var_set_add(bound_vars, a->args[0]);
            for (int j = 1; j < a->arg_count; j++) {
                var_set_add(bound_vars, a->args[j]);
            }
            continue;
        }
        
        const PredInfo *info = pred_table_find(preds, a->predicate);
        if (!info) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Unknown predicate '%s'", a->predicate);
            
            const char *suggestion = find_closest_predicate(preds, a->predicate);
            char *hint = NULL;
            if (suggestion) {
                char buf[128];
                snprintf(buf, sizeof(buf), "Did you mean '%s'?", suggestion);
                hint = strdup(buf);
            }
            
            result_add(result, msg, a->loc.line, a->loc.column, hint);
            if (hint) free(hint);
        } else if (info->arity != a->arg_count) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "Arity mismatch for '%s': declared %d, used with %d",
                     a->predicate, info->arity, a->arg_count);
            result_add(result, msg, a->loc.line, a->loc.column, NULL);
        }
        
        for (int j = 0; j < a->arg_count; j++) {
            var_set_add(bound_vars, a->args[j]);
        }
    }
    
    /* Second pass: check negated atoms */
    for (int i = 0; i < cond->atom_count; i++) {
        const Atom *a = &cond->atoms[i];
        if (!a->negated) continue;
        
        const PredInfo *info = pred_table_find(preds, a->predicate);
        if (!info) {
            char msg[256];
            snprintf(msg, sizeof(msg), "Unknown predicate '%s'", a->predicate);
            const char *suggestion = find_closest_predicate(preds, a->predicate);
            char *hint = NULL;
            if (suggestion) {
                char buf[128];
                snprintf(buf, sizeof(buf), "Did you mean '%s'?", suggestion);
                hint = strdup(buf);
            }
            result_add(result, msg, a->loc.line, a->loc.column, hint);
            if (hint) free(hint);
        } else if (info->arity != a->arg_count) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "Arity mismatch for '%s': declared %d, used with %d",
                     a->predicate, info->arity, a->arg_count);
            result_add(result, msg, a->loc.line, a->loc.column, NULL);
        }
        
        for (int j = 0; j < a->arg_count; j++) {
            if (!var_set_contains(bound_vars, a->args[j])) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "Safety violation: variable '%s' used in negated atom '%s' "
                         "but not bound by any positive atom",
                         a->args[j], a->predicate);
                char *hint = strdup(
                    "Add a positive atom that binds this variable before the negation"
                );
                result_add(result, msg, a->loc.line, a->loc.column, hint);
                free(hint);
            }
        }
    }
    
    /* Third pass: bind variables from arithmetic assignments */
    for (int i = 0; i < cond->arith_count; i++) {
        const ArithAssignment *a = &cond->arith_assigns[i];
        var_set_add(bound_vars, a->result_var);
    }
}

/* ====================================================================== */
/* Main checker                                                           */
/* ====================================================================== */

TypeCheckResult typecheck_program(const Program *program) {
    TypeCheckResult result;
    result.errors = NULL;
    result.count = 0;
    result.capacity = 0;
    
    if (!program) {
        result_add(&result, "Program is NULL", 0, 0, NULL);
        return result;
    }
    
    /* Build predicate table */
    PredTable preds;
    pred_table_init(&preds);
    
    /* Entities automatically create base predicates with arity 1 */
    for (int i = 0; i < program->entity_count; i++) {
        const EntityDecl *e = &program->entities[i];
        pred_table_add(&preds, e->name, 1, 0);
    }
    
    /* Explicit relations */
    for (int i = 0; i < program->relation_count; i++) {
        const RelationDecl *r = &program->relations[i];
        pred_table_add(&preds, r->name, r->param_count, 0);
    }
    
    for (int i = 0; i < program->derive_count; i++) {
        const DeriveDecl *d = &program->derives[i];
        pred_table_add(&preds, d->name, d->param_count, 1);
    }
    
    for (int i = 0; i < program->observe_count; i++) {
        const ObserveDecl *o = &program->observes[i];
        pred_table_add(&preds, o->name, o->param_count, 1);
    }
    
    /* Check observe declarations */
    for (int i = 0; i < program->observe_count; i++) {
        const ObserveDecl *o = &program->observes[i];
        VarSet bound;
        var_set_init(&bound);
        
        check_condition(&o->condition, &preds, &bound, &result);
        
        /* All head params must be bound in body */
        for (int j = 0; j < o->param_count; j++) {
            if (!var_set_contains(&bound, o->params[j])) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "Safety violation: head variable '%s' of observe '%s' "
                         "not bound in body",
                         o->params[j], o->name);
                result_add(&result, msg, o->loc.line, o->loc.column, NULL);
            }
        }
        
        var_set_free(&bound);
    }
    
    /* Check derive declarations */
    for (int i = 0; i < program->derive_count; i++) {
        const DeriveDecl *d = &program->derives[i];
        VarSet bound;
        var_set_init(&bound);
        
        check_condition(&d->condition, &preds, &bound, &result);
        
        /* All head params must be bound in body */
        for (int j = 0; j < d->param_count; j++) {
            if (!var_set_contains(&bound, d->params[j])) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "Safety violation: head variable '%s' of derive '%s' "
                         "not bound in body",
                         d->params[j], d->name);
                result_add(&result, msg, d->loc.line, d->loc.column, NULL);
            }
        }
        
        var_set_free(&bound);
    }
    
    /* Check input facts: predicates must be base (relations), arity match */
    for (int i = 0; i < program->input_count; i++) {
        const InputFact *f = &program->inputs[i];
        const PredInfo *info = pred_table_find(&preds, f->predicate);
        
        if (!info) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "Unknown predicate '%s' in input fact", f->predicate);
            const char *suggestion = find_closest_predicate(&preds, f->predicate);
            char *hint = NULL;
            if (suggestion) {
                char buf[128];
                snprintf(buf, sizeof(buf), "Did you mean '%s'?", suggestion);
                hint = strdup(buf);
            }
            result_add(&result, msg, f->loc.line, f->loc.column, hint);
            if (hint) free(hint);
        } else if (info->is_derived) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "Cannot input derived/observable predicate '%s' "
                     "(only base relations can have input facts)",
                     f->predicate);
            result_add(&result, msg, f->loc.line, f->loc.column, NULL);
        } else if (info->arity != f->arg_count) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "Arity mismatch in input: '%s' declared with arity %d, "
                     "fact has %d args",
                     f->predicate, info->arity, f->arg_count);
            result_add(&result, msg, f->loc.line, f->loc.column, NULL);
        }
    }
    
    /* Check queries: predicates must exist */
    for (int i = 0; i < program->query_count; i++) {
        const Query *q = &program->queries[i];
        const PredInfo *info = pred_table_find(&preds, q->predicate);
        
        if (!info) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "Cannot query unknown predicate '%s'", q->predicate);
            const char *suggestion = find_closest_predicate(&preds, q->predicate);
            char *hint = NULL;
            if (suggestion) {
                char buf[128];
                snprintf(buf, sizeof(buf), "Did you mean '%s'?", suggestion);
                hint = strdup(buf);
            }
            result_add(&result, msg, q->loc.line, q->loc.column, hint);
            if (hint) free(hint);
        }
    }
    
    pred_table_free(&preds);
    return result;
}
