#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const TokenList *tokens;
    size_t pos;
} Parser;

Program *program_new(void) {
    Program *p = calloc(1, sizeof(Program));
    return p;
}

/* ====================================================================== */
/* Expression constructors                                                */
/* ====================================================================== */

Expr *expr_new_number(double n, SourceLocation loc) {
    Expr *e = calloc(1, sizeof(Expr));
    e->type = EXPR_NUMBER;
    e->number = n;
    e->loc = loc;
    return e;
}

Expr *expr_new_variable(const char *name, SourceLocation loc) {
    Expr *e = calloc(1, sizeof(Expr));
    e->type = EXPR_VARIABLE;
    e->var_name = strdup(name);
    e->loc = loc;
    return e;
}

Expr *expr_new_binary(char op, Expr *left, Expr *right, SourceLocation loc) {
    Expr *e = calloc(1, sizeof(Expr));
    e->type = EXPR_BINARY;
    e->binary.op = op;
    e->binary.left = left;
    e->binary.right = right;
    e->loc = loc;
    return e;
}

Expr *expr_new_unary_minus(Expr *operand, SourceLocation loc) {
    Expr *e = calloc(1, sizeof(Expr));
    e->type = EXPR_UNARY_MINUS;
    e->operand = operand;
    e->loc = loc;
    return e;
}

void expr_free(Expr *e) {
    if (!e) return;
    switch (e->type) {
        case EXPR_NUMBER:
            break;
        case EXPR_VARIABLE:
            free(e->var_name);
            break;
        case EXPR_BINARY:
            expr_free(e->binary.left);
            expr_free(e->binary.right);
            break;
        case EXPR_UNARY_MINUS:
            expr_free(e->operand);
            break;
    }
    free(e);
}

void expr_dump(const Expr *e, int indent) {
    if (!e) return;
    for (int i = 0; i < indent; i++) printf("  ");
    switch (e->type) {
        case EXPR_NUMBER:
            printf("NUM %g\n", e->number);
            break;
        case EXPR_VARIABLE:
            printf("VAR %s\n", e->var_name);
            break;
        case EXPR_BINARY:
            printf("BIN '%c'\n", e->binary.op);
            expr_dump(e->binary.left, indent + 1);
            expr_dump(e->binary.right, indent + 1);
            break;
        case EXPR_UNARY_MINUS:
            printf("UNARY -\n");
            expr_dump(e->operand, indent + 1);
            break;
    }
}

/* ====================================================================== */
/* Other AST constructors                                                 */
/* ====================================================================== */

void atom_init(Atom *a, const char *predicate, int negated, SourceLocation loc) {
    a->predicate = strdup(predicate);
    a->args = NULL;
    a->arg_count = 0;
    a->negated = negated;
    a->aggregate_func = NULL;
    a->aggregate_field = -1;
    a->loc = loc;
}

void atom_add_arg(Atom *a, const char *arg) {
    a->arg_count++;
    a->args = realloc(a->args, sizeof(char*) * a->arg_count);
    a->args[a->arg_count - 1] = strdup(arg);
}

void condition_init(Condition *c, SourceLocation loc) {
    c->atoms = NULL;
    c->atom_count = 0;
    c->arith_assigns = NULL;
    c->arith_count = 0;
    c->loc = loc;
}

void condition_add_atom(Condition *c, const Atom *atom) {
    c->atom_count++;
    c->atoms = realloc(c->atoms, sizeof(Atom) * c->atom_count);
    c->atoms[c->atom_count - 1] = *atom;
}

void condition_add_arith(Condition *c, const ArithAssignment *a) {
    c->arith_count++;
    c->arith_assigns = realloc(c->arith_assigns, sizeof(ArithAssignment) * c->arith_count);
    c->arith_assigns[c->arith_count - 1] = *a;
}

/* ====================================================================== */
/* Destructors                                                            */
/* ====================================================================== */

void atom_free(Atom *a) {
    if (!a) return;
    free(a->predicate);
    for (int i = 0; i < a->arg_count; i++) free(a->args[i]);
    free(a->args);
    if (a->aggregate_func) free(a->aggregate_func);
}

void arith_assignment_free(ArithAssignment *a) {
    if (!a) return;
    if (a->result_var) free(a->result_var);
    if (a->expr) expr_free(a->expr);
}

void condition_free(Condition *c) {
    if (!c) return;
    for (int i = 0; i < c->atom_count; i++) {
        atom_free(&c->atoms[i]);
    }
    free(c->atoms);
    for (int i = 0; i < c->arith_count; i++) {
        arith_assignment_free(&c->arith_assigns[i]);
    }
    free(c->arith_assigns);
}

void input_fact_free(InputFact *f) {
    if (!f) return;
    free(f->predicate);
    for (int i = 0; i < f->arg_count; i++) free(f->args[i]);
    free(f->args);
}

void query_free(Query *q) {
    if (!q) return;
    free(q->predicate);
    for (int i = 0; i < q->arg_count; i++) free(q->args[i]);
    free(q->args);
}

static void entity_decl_free(EntityDecl *e) {
    if (!e) return;
    free(e->name);
}

static void relation_decl_free(RelationDecl *r) {
    if (!r) return;
    free(r->name);
    for (int i = 0; i < r->param_count; i++) free(r->params[i]);
    free(r->params);
}

static void observe_decl_free(ObserveDecl *o) {
    if (!o) return;
    free(o->name);
    for (int i = 0; i < o->param_count; i++) free(o->params[i]);
    free(o->params);
    condition_free(&o->condition);
}

static void derive_decl_free(DeriveDecl *d) {
    if (!d) return;
    free(d->name);
    for (int i = 0; i < d->param_count; i++) free(d->params[i]);
    free(d->params);
    condition_free(&d->condition);
}

void program_free(Program *p) {
    if (!p) return;
    for (int i = 0; i < p->entity_count; i++) entity_decl_free(&p->entities[i]);
    free(p->entities);
    for (int i = 0; i < p->relation_count; i++) relation_decl_free(&p->relations[i]);
    free(p->relations);
    for (int i = 0; i < p->observe_count; i++) observe_decl_free(&p->observes[i]);
    free(p->observes);
    for (int i = 0; i < p->derive_count; i++) derive_decl_free(&p->derives[i]);
    free(p->derives);
    for (int i = 0; i < p->input_count; i++) input_fact_free(&p->inputs[i]);
    free(p->inputs);
    for (int i = 0; i < p->query_count; i++) query_free(&p->queries[i]);
    free(p->queries);
    free(p);
}

void program_dump(const Program *p) {
    printf("Program:\n");
    for (int i = 0; i < p->entity_count; i++) {
        printf("  entity %s  @%d:%d\n", p->entities[i].name,
               p->entities[i].loc.line, p->entities[i].loc.column);
    }
    for (int i = 0; i < p->relation_count; i++) {
        printf("  relation %s(", p->relations[i].name);
        for (int j = 0; j < p->relations[i].param_count; j++) {
            if (j > 0) printf(", ");
            printf("%s", p->relations[i].params[j]);
        }
        printf(")  @%d:%d\n", p->relations[i].loc.line, p->relations[i].loc.column);
    }
    for (int i = 0; i < p->observe_count; i++) {
        ObserveDecl *o = &p->observes[i];
        printf("  observe %s(", o->name);
        for (int j = 0; j < o->param_count; j++) {
            if (j > 0) printf(", ");
            printf("%s", o->params[j]);
        }
        printf(") where ...\n");
    }
    for (int i = 0; i < p->derive_count; i++) {
        DeriveDecl *d = &p->derives[i];
        printf("  derive %s(", d->name);
        for (int j = 0; j < d->param_count; j++) {
            if (j > 0) printf(", ");
            printf("%s", d->params[j]);
        }
        printf(") from ");
        for (int j = 0; j < d->condition.atom_count; j++) {
            if (j > 0) printf(", ");
            Atom *a = &d->condition.atoms[j];
            if (a->negated) printf("not ");
            printf("%s(...)", a->predicate);
        }
        if (d->condition.arith_count > 0) {
            for (int j = 0; j < d->condition.arith_count; j++) {
                printf(", %s = <expr>", d->condition.arith_assigns[j].result_var);
            }
        }
        printf("\n");
    }
    if (p->input_count > 0) {
        printf("  input: %d facts\n", p->input_count);
    }
    for (int i = 0; i < p->query_count; i++) {
        printf("  query %s(", p->queries[i].predicate);
        for (int j = 0; j < p->queries[i].arg_count; j++) {
            if (j > 0) printf(", ");
            printf("%s", p->queries[i].args[j]);
        }
        printf(")  @%d:%d\n", p->queries[i].loc.line, p->queries[i].loc.column);
    }
}

/* ====================================================================== */
/* Parser helpers                                                         */
/* ====================================================================== */

static Token *parser_current(Parser *p) {
    if (p->pos < p->tokens->count) {
        return &p->tokens->tokens[p->pos];
    }
    return &p->tokens->tokens[p->tokens->count - 1];
}

static Token *parser_consume(Parser *p, TokenType expected, ParseResult *result) {
    Token *tok = parser_current(p);
    if (tok->type != expected) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Expected %s, got %s '%s'",
                 token_type_str(expected), token_type_str(tok->type), tok->value);
        result->error_message = strdup(msg);
        result->error_line = tok->line;
        result->error_column = tok->column;
        result->is_valid = 0;
        return NULL;
    }
    p->pos++;
    return tok;
}

static int parser_match(Parser *p, TokenType expected) {
    if (parser_current(p)->type == expected) {
        p->pos++;
        return 1;
    }
    return 0;
}

static SourceLocation parser_loc(Parser *p) {
    Token *t = parser_current(p);
    SourceLocation loc = {t->line, t->column};
    return loc;
}

static void parser_parse_ident_list(Parser *p, char ***out_params, int *out_count, ParseResult *result) {
    *out_params = NULL;
    *out_count = 0;
    
    if (parser_current(p)->type == TOK_IDENT) {
        (*out_count)++;
        *out_params = realloc(*out_params, sizeof(char*) * (*out_count));
        (*out_params)[*out_count - 1] = strdup(parser_current(p)->value);
        p->pos++;
        
        while (parser_match(p, TOK_COMMA)) {
            if (parser_current(p)->type != TOK_IDENT) {
                result->error_message = strdup("Expected identifier after comma");
                result->error_line = parser_current(p)->line;
                result->error_column = parser_current(p)->column;
                result->is_valid = 0;
                return;
            }
            (*out_count)++;
            *out_params = realloc(*out_params, sizeof(char*) * (*out_count));
            (*out_params)[*out_count - 1] = strdup(parser_current(p)->value);
            p->pos++;
        }
    }
}

static void parser_parse_arg_list(Parser *p, char ***out_args, int *out_count, ParseResult *result) {
    *out_args = NULL;
    *out_count = 0;
    
    TokenType t = parser_current(p)->type;
    if (t == TOK_IDENT || t == TOK_STRING || t == TOK_NUMBER) {
        (*out_count)++;
        *out_args = realloc(*out_args, sizeof(char*) * (*out_count));
        (*out_args)[*out_count - 1] = strdup(parser_current(p)->value);
        p->pos++;
        
        while (parser_match(p, TOK_COMMA)) {
            t = parser_current(p)->type;
            if (t != TOK_IDENT && t != TOK_STRING && t != TOK_NUMBER) {
                result->error_message = strdup("Expected identifier, string or number");
                result->error_line = parser_current(p)->line;
                result->error_column = parser_current(p)->column;
                result->is_valid = 0;
                return;
            }
            (*out_count)++;
            *out_args = realloc(*out_args, sizeof(char*) * (*out_count));
            (*out_args)[*out_count - 1] = strdup(parser_current(p)->value);
            p->pos++;
        }
    }
}

/* ====================================================================== */
/* Expression parser (recursive descent with precedence)                 */
/* ====================================================================== */

static Expr *parse_expr(Parser *p, ParseResult *result);

static Expr *parse_factor(Parser *p, ParseResult *result) {
    SourceLocation loc = parser_loc(p);
    Token *tok = parser_current(p);
    
    /* Number */
    if (tok->type == TOK_NUMBER) {
        double n = strtod(tok->value, NULL);
        p->pos++;
        return expr_new_number(n, loc);
    }
    
    /* Unary minus */
    if (tok->type == TOK_MINUS) {
        p->pos++;
        Expr *operand = parse_factor(p, result);
        if (!result->is_valid) return NULL;
        return expr_new_unary_minus(operand, loc);
    }
    
    /* Parenthesized expression */
    if (tok->type == TOK_LPAREN) {
        p->pos++;
        Expr *inner = parse_expr(p, result);
        if (!result->is_valid) return NULL;
        if (!parser_consume(p, TOK_RPAREN, result)) {
            expr_free(inner);
            return NULL;
        }
        return inner;
    }
    
    /* Variable */
    if (tok->type == TOK_IDENT) {
        Expr *e = expr_new_variable(tok->value, loc);
        p->pos++;
        return e;
    }
    
    /* Error */
    char msg[256];
    snprintf(msg, sizeof(msg), "Unexpected token in expression: %s '%s'",
             token_type_str(tok->type), tok->value);
    result->error_message = strdup(msg);
    result->error_line = tok->line;
    result->error_column = tok->column;
    result->is_valid = 0;
    return NULL;
}

static Expr *parse_term(Parser *p, ParseResult *result) {
    Expr *left = parse_factor(p, result);
    if (!result->is_valid) return NULL;
    
    while (parser_current(p)->type == TOK_STAR ||
           parser_current(p)->type == TOK_SLASH) {
        SourceLocation loc = parser_loc(p);
        char op = (parser_current(p)->type == TOK_STAR) ? '*' : '/';
        p->pos++;
        
        Expr *right = parse_factor(p, result);
        if (!result->is_valid) {
            expr_free(left);
            return NULL;
        }
        
        left = expr_new_binary(op, left, right, loc);
    }
    
    return left;
}

static Expr *parse_expr(Parser *p, ParseResult *result) {
    Expr *left = parse_term(p, result);
    if (!result->is_valid) return NULL;
    
    while (parser_current(p)->type == TOK_PLUS ||
           parser_current(p)->type == TOK_MINUS) {
        SourceLocation loc = parser_loc(p);
        char op = (parser_current(p)->type == TOK_PLUS) ? '+' : '-';
        p->pos++;
        
        Expr *right = parse_term(p, result);
        if (!result->is_valid) {
            expr_free(left);
            return NULL;
        }
        
        left = expr_new_binary(op, left, right, loc);
    }
    
    return left;
}

/* ====================================================================== */
/* Atom parser                                                            */
/* ====================================================================== */

static void parser_parse_atom(Parser *p, Atom *out, ParseResult *result) {
    SourceLocation loc = parser_loc(p);
    int negated = parser_match(p, TOK_NOT);
    
    char *result_var = NULL;
    int is_aggregate = 0;
    
    if (parser_current(p)->type == TOK_IDENT) {
        size_t saved_pos = p->pos;
        char *possible_var = parser_current(p)->value;
        p->pos++;
        
        if (parser_current(p)->type == TOK_ASSIGN) {
            p->pos++;
            TokenType next = parser_current(p)->type;
            if (next == TOK_SUM || next == TOK_COUNT || next == TOK_MIN || next == TOK_MAX) {
                is_aggregate = 1;
                result_var = strdup(possible_var);
            } else {
                p->pos = saved_pos;
            }
        } else {
            p->pos = saved_pos;
        }
    }
    
    if (is_aggregate) {
        Token *agg_tok = parser_current(p);
        p->pos++;
        
        char *agg_func = NULL;
        switch (agg_tok->type) {
            case TOK_SUM: agg_func = "sum"; break;
            case TOK_COUNT: agg_func = "count"; break;
            case TOK_MIN: agg_func = "min"; break;
            case TOK_MAX: agg_func = "max"; break;
            default: break;
        }
        
        if (!parser_consume(p, TOK_LPAREN, result)) {
            free(result_var);
            return;
        }
        
        Token *pred = parser_consume(p, TOK_IDENT, result);
        if (!pred) {
            free(result_var);
            return;
        }
        
        if (!parser_consume(p, TOK_LPAREN, result)) {
            free(result_var);
            return;
        }
        
        char **args = NULL;
        int arg_count = 0;
        parser_parse_arg_list(p, &args, &arg_count, result);
        if (!result->is_valid) {
            for (int i = 0; i < arg_count; i++) free(args[i]);
            free(args);
            free(result_var);
            return;
        }
        
        if (!parser_consume(p, TOK_RPAREN, result)) {
            for (int i = 0; i < arg_count; i++) free(args[i]);
            free(args);
            free(result_var);
            return;
        }
        
        if (!parser_consume(p, TOK_COMMA, result)) {
            for (int i = 0; i < arg_count; i++) free(args[i]);
            free(args);
            free(result_var);
            return;
        }
        
        Token *agg_arg = parser_consume(p, TOK_IDENT, result);
        if (!agg_arg) {
            for (int i = 0; i < arg_count; i++) free(args[i]);
            free(args);
            free(result_var);
            return;
        }
        
        int agg_field = -1;
        for (int i = 0; i < arg_count; i++) {
            if (strcmp(args[i], agg_arg->value) == 0) {
                agg_field = i;
                break;
            }
        }
        
        if (agg_field == -1) {
            char msg[256];
            snprintf(msg, sizeof(msg),
                     "Aggregate argument '%s' not found in source predicate args",
                     agg_arg->value);
            result->error_message = strdup(msg);
            result->error_line = agg_arg->line;
            result->error_column = agg_arg->column;
            result->is_valid = 0;
            for (int i = 0; i < arg_count; i++) free(args[i]);
            free(args);
            free(result_var);
            return;
        }
        
        if (!parser_consume(p, TOK_RPAREN, result)) {
            for (int i = 0; i < arg_count; i++) free(args[i]);
            free(args);
            free(result_var);
            return;
        }
        
        atom_init(out, pred->value, negated, loc);
        out->aggregate_func = strdup(agg_func);
        out->aggregate_field = agg_field;
        for (int i = 0; i < arg_count; i++) {
            atom_add_arg(out, args[i]);
        }
        for (int i = 0; i < arg_count; i++) free(args[i]);
        free(args);
        
        char **new_args = malloc(sizeof(char*) * (out->arg_count + 1));
        new_args[0] = result_var;
        for (int i = 0; i < out->arg_count; i++) {
            new_args[i + 1] = out->args[i];
        }
        free(out->args);
        out->args = new_args;
        out->arg_count++;
    } else {
        Token *pred = parser_consume(p, TOK_IDENT, result);
        if (!pred) return;
        
        if (!parser_consume(p, TOK_LPAREN, result)) return;
        
        char **args = NULL;
        int arg_count = 0;
        parser_parse_arg_list(p, &args, &arg_count, result);
        if (!result->is_valid) {
            for (int i = 0; i < arg_count; i++) free(args[i]);
            free(args);
            return;
        }
        
        if (!parser_consume(p, TOK_RPAREN, result)) {
            for (int i = 0; i < arg_count; i++) free(args[i]);
            free(args);
            return;
        }
        
        atom_init(out, pred->value, negated, loc);
        for (int i = 0; i < arg_count; i++) {
            atom_add_arg(out, args[i]);
        }
        for (int i = 0; i < arg_count; i++) free(args[i]);
        free(args);
    }
}

static void parser_parse_condition(Parser *p, Condition *out, ParseResult *result) {
    SourceLocation loc = parser_loc(p);
    condition_init(out, loc);
    
    while (1) {
        /* Check if this is an arithmetic assignment: IDENT '=' expr */
        int is_arith = 0;
        if (parser_current(p)->type == TOK_IDENT) {
            size_t saved_pos = p->pos;
            const char *var_name = parser_current(p)->value;
            p->pos++;
            
            if (parser_current(p)->type == TOK_ASSIGN) {
                p->pos++;
                TokenType next = parser_current(p)->type;
                
                /* It's an aggregate if next is sum/count/min/max followed by '(' */
                if ((next == TOK_SUM || next == TOK_COUNT || next == TOK_MIN || next == TOK_MAX) &&
                    p->pos + 1 < p->tokens->count &&
                    p->tokens->tokens[p->pos + 1].type == TOK_LPAREN) {
                    /* Aggregate — rollback and parse as atom */
                    p->pos = saved_pos;
                    is_arith = 0;
                } else {
                    /* Arithmetic assignment */
                    is_arith = 1;
                    
                    SourceLocation arith_loc = parser_loc(p);
                    Expr *expr = parse_expr(p, result);
                    if (!result->is_valid) {
                        /* FIXED: Don't free var_name — it belongs to the token */
                        if (expr) expr_free(expr);
                        return;
                    }
                    
                    ArithAssignment a;
                    a.result_var = strdup(var_name);  /* Make a copy */
                    a.expr = expr;
                    a.loc = arith_loc;
                    condition_add_arith(out, &a);
                }
            } else {
                /* Not an assignment, rollback */
                p->pos = saved_pos;
            }
        }
        
        if (!is_arith) {
            /* Regular atom */
            Atom atom;
            parser_parse_atom(p, &atom, result);
            if (!result->is_valid) return;
            condition_add_atom(out, &atom);
        }
        
        if (!parser_match(p, TOK_COMMA)) break;
    }
}

static void parser_parse_entity(Parser *p, Program *prog, ParseResult *result) {
    SourceLocation loc = parser_loc(p);
    parser_consume(p, TOK_ENTITY, result);
    if (!result->is_valid) return;
    
    Token *name = parser_consume(p, TOK_IDENT, result);
    if (!name) return;
    
    if (prog->entity_count >= prog->entity_capacity) {
        prog->entity_capacity = prog->entity_capacity == 0 ? 4 : prog->entity_capacity * 2;
        prog->entities = realloc(prog->entities, sizeof(EntityDecl) * prog->entity_capacity);
    }
    
    EntityDecl *e = &prog->entities[prog->entity_count++];
    e->name = strdup(name->value);
    e->loc = loc;
}

static void parser_parse_relation(Parser *p, Program *prog, ParseResult *result) {
    SourceLocation loc = parser_loc(p);
    parser_consume(p, TOK_RELATION, result);
    if (!result->is_valid) return;
    
    Token *name = parser_consume(p, TOK_IDENT, result);
    if (!name) return;
    
    if (!parser_consume(p, TOK_LPAREN, result)) return;
    
    char **params = NULL;
    int param_count = 0;
    parser_parse_ident_list(p, &params, &param_count, result);
    if (!result->is_valid) {
        for (int i = 0; i < param_count; i++) free(params[i]);
        free(params);
        return;
    }
    
    if (!parser_consume(p, TOK_RPAREN, result)) {
        for (int i = 0; i < param_count; i++) free(params[i]);
        free(params);
        return;
    }
    
    if (prog->relation_count >= prog->relation_capacity) {
        prog->relation_capacity = prog->relation_capacity == 0 ? 4 : prog->relation_capacity * 2;
        prog->relations = realloc(prog->relations, sizeof(RelationDecl) * prog->relation_capacity);
    }
    
    RelationDecl *r = &prog->relations[prog->relation_count++];
    r->name = strdup(name->value);
    r->params = params;
    r->param_count = param_count;
    r->loc = loc;
}

static void parser_parse_observe(Parser *p, Program *prog, ParseResult *result) {
    SourceLocation loc = parser_loc(p);
    parser_consume(p, TOK_OBSERVE, result);
    if (!result->is_valid) return;
    
    Token *name = parser_consume(p, TOK_IDENT, result);
    if (!name) return;
    
    if (!parser_consume(p, TOK_LPAREN, result)) return;
    
    char **params = NULL;
    int param_count = 0;
    parser_parse_ident_list(p, &params, &param_count, result);
    if (!result->is_valid) {
        for (int i = 0; i < param_count; i++) free(params[i]);
        free(params);
        return;
    }
    
    if (!parser_consume(p, TOK_RPAREN, result)) {
        for (int i = 0; i < param_count; i++) free(params[i]);
        free(params);
        return;
    }
    
    if (!parser_consume(p, TOK_WHERE, result)) {
        for (int i = 0; i < param_count; i++) free(params[i]);
        free(params);
        return;
    }
    
    Condition cond;
    parser_parse_condition(p, &cond, result);
    if (!result->is_valid) {
        for (int i = 0; i < param_count; i++) free(params[i]);
        free(params);
        return;
    }
    
    if (prog->observe_count >= prog->observe_capacity) {
        prog->observe_capacity = prog->observe_capacity == 0 ? 4 : prog->observe_capacity * 2;
        prog->observes = realloc(prog->observes, sizeof(ObserveDecl) * prog->observe_capacity);
    }
    
    ObserveDecl *o = &prog->observes[prog->observe_count++];
    o->name = strdup(name->value);
    o->params = params;
    o->param_count = param_count;
    o->condition = cond;
    o->loc = loc;
}

static void parser_parse_derive(Parser *p, Program *prog, ParseResult *result) {
    SourceLocation loc = parser_loc(p);
    parser_consume(p, TOK_DERIVE, result);
    if (!result->is_valid) return;
    
    Token *name = parser_consume(p, TOK_IDENT, result);
    if (!name) return;
    
    if (!parser_consume(p, TOK_LPAREN, result)) return;
    
    char **params = NULL;
    int param_count = 0;
    parser_parse_ident_list(p, &params, &param_count, result);
    if (!result->is_valid) {
        for (int i = 0; i < param_count; i++) free(params[i]);
        free(params);
        return;
    }
    
    if (!parser_consume(p, TOK_RPAREN, result)) {
        for (int i = 0; i < param_count; i++) free(params[i]);
        free(params);
        return;
    }
    
    if (!parser_consume(p, TOK_FROM, result)) {
        for (int i = 0; i < param_count; i++) free(params[i]);
        free(params);
        return;
    }
    
    Condition cond;
    parser_parse_condition(p, &cond, result);
    if (!result->is_valid) {
        for (int i = 0; i < param_count; i++) free(params[i]);
        free(params);
        return;
    }
    
    if (prog->derive_count >= prog->derive_capacity) {
        prog->derive_capacity = prog->derive_capacity == 0 ? 4 : prog->derive_capacity * 2;
        prog->derives = realloc(prog->derives, sizeof(DeriveDecl) * prog->derive_capacity);
    }
    
    DeriveDecl *d = &prog->derives[prog->derive_count++];
    d->name = strdup(name->value);
    d->params = params;
    d->param_count = param_count;
    d->condition = cond;
    d->loc = loc;
}

static void parser_parse_input_block(Parser *p, Program *prog, ParseResult *result) {
    parser_consume(p, TOK_INPUT, result);
    if (!result->is_valid) return;
    
    if (!parser_consume(p, TOK_LBRACE, result)) return;
    
    while (parser_current(p)->type != TOK_RBRACE && parser_current(p)->type != TOK_EOF) {
        SourceLocation loc = parser_loc(p);
        
        Token *pred = parser_consume(p, TOK_IDENT, result);
        if (!pred) return;
        
        if (!parser_consume(p, TOK_LPAREN, result)) return;
        
        char **args = NULL;
        int arg_count = 0;
        parser_parse_arg_list(p, &args, &arg_count, result);
        if (!result->is_valid) {
            for (int i = 0; i < arg_count; i++) free(args[i]);
            free(args);
            return;
        }
        
        if (!parser_consume(p, TOK_RPAREN, result)) {
            for (int i = 0; i < arg_count; i++) free(args[i]);
            free(args);
            return;
        }
        
        if (prog->input_count >= prog->input_capacity) {
            prog->input_capacity = prog->input_capacity == 0 ? 8 : prog->input_capacity * 2;
            prog->inputs = realloc(prog->inputs, sizeof(InputFact) * prog->input_capacity);
        }
        
        InputFact *f = &prog->inputs[prog->input_count++];
        f->predicate = strdup(pred->value);
        f->args = args;
        f->arg_count = arg_count;
        f->loc = loc;
    }
    
    parser_consume(p, TOK_RBRACE, result);
}

static void parser_parse_query(Parser *p, Program *prog, ParseResult *result) {
    SourceLocation loc = parser_loc(p);
    parser_consume(p, TOK_QUERY, result);
    if (!result->is_valid) return;
    
    Token *name = parser_consume(p, TOK_IDENT, result);
    if (!name) return;
    
    if (!parser_consume(p, TOK_LPAREN, result)) return;
    
    char **args = NULL;
    int arg_count = 0;
    parser_parse_arg_list(p, &args, &arg_count, result);
    if (!result->is_valid) {
        for (int i = 0; i < arg_count; i++) free(args[i]);
        free(args);
        return;
    }
    
    if (!parser_consume(p, TOK_RPAREN, result)) {
        for (int i = 0; i < arg_count; i++) free(args[i]);
        free(args);
        return;
    }
    
    if (prog->query_count >= prog->query_capacity) {
        prog->query_capacity = prog->query_capacity == 0 ? 4 : prog->query_capacity * 2;
        prog->queries = realloc(prog->queries, sizeof(Query) * prog->query_capacity);
    }
    
    Query *q = &prog->queries[prog->query_count++];
    q->predicate = strdup(name->value);
    q->args = args;
    q->arg_count = arg_count;
    q->loc = loc;
}

ParseResult parser_parse(const TokenList *tokens) {
    ParseResult result;
    result.program = program_new();
    result.is_valid = 1;
    result.error_message = NULL;
    result.error_line = 0;
    result.error_column = 0;
    
    if (tokens->error_message) {
        result.is_valid = 0;
        result.error_message = strdup(tokens->error_message);
        result.error_line = tokens->error_line;
        result.error_column = tokens->error_column;
        return result;
    }
    
    Parser parser;
    parser.tokens = tokens;
    parser.pos = 0;
    
    while (parser_current(&parser)->type != TOK_EOF) {
        Token *tok = parser_current(&parser);
        
        switch (tok->type) {
            case TOK_ENTITY:
                parser_parse_entity(&parser, result.program, &result);
                break;
            case TOK_RELATION:
                parser_parse_relation(&parser, result.program, &result);
                break;
            case TOK_OBSERVE:
                parser_parse_observe(&parser, result.program, &result);
                break;
            case TOK_DERIVE:
                parser_parse_derive(&parser, result.program, &result);
                break;
            case TOK_INPUT:
                parser_parse_input_block(&parser, result.program, &result);
                break;
            case TOK_QUERY:
                parser_parse_query(&parser, result.program, &result);
                break;
            default: {
                char msg[128];
                snprintf(msg, sizeof(msg), "Unexpected token '%s' at top level",
                         token_type_str(tok->type));
                result.error_message = strdup(msg);
                result.error_line = tok->line;
                result.error_column = tok->column;
                result.is_valid = 0;
                break;
            }
        }
        
        if (!result.is_valid) break;
    }
    
    return result;
}

void parse_result_free(ParseResult *result) {
    if (!result) return;
    program_free(result->program);
    if (result->error_message) free(result->error_message);
    result->program = NULL;
    result->error_message = NULL;
}
