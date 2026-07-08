#ifndef SUPERLANG_AST_NODES_H
#define SUPERLANG_AST_NODES_H

#include <stddef.h>

/* ========================================================================= */
/* Source location tracking for error messages                               */
/* ========================================================================= */

typedef struct {
    int line;
    int column;
} SourceLocation;

/* ========================================================================= */
/* Expression AST for arithmetic and built-in function calls                 */
/* ========================================================================= */

typedef enum {
    EXPR_NUMBER,       /* Numeric literal: 42, 3.14 */
    EXPR_VARIABLE,     /* Variable reference: x, total */
    EXPR_BINARY,       /* Binary operator: a + b, x * y */
    EXPR_UNARY_MINUS,  /* Unary minus: -x */
    EXPR_CALL          /* Built-in function: sqrt(x), pow(a, b) */
} ExprType;

typedef struct Expr {
    ExprType type;
    union {
        double number;
        char *var_name;
        struct {
            char op;               /* '+', '-', '*', '/', '%' */
            struct Expr *left;
            struct Expr *right;
        } binary;
        struct Expr *operand;
        /* NEW in v1.1: function call with name and argument list */
        struct {
            char *func_name;       /* "sqrt", "pow", "sin", etc. */
            struct Expr **args;    /* array of argument expressions */
            int arg_count;         /* number of arguments */
        } call;
    };
    SourceLocation loc;
} Expr;

/* ========================================================================= */
/* Comparison operators for filters                                          */
/* ========================================================================= */

typedef enum {
    CMP_EQ,   /* == */
    CMP_NE,   /* != */
    CMP_LT,   /* <  */
    CMP_LE,   /* <= */
    CMP_GT,   /* >  */
    CMP_GE    /* >= */
} CmpOp;

typedef struct {
    Expr *left;
    CmpOp op;
    Expr *right;
    SourceLocation loc;
} Comparison;

/* ========================================================================= */
/* Arithmetic assignment: result_var = expr                                  */
/* ========================================================================= */

typedef struct {
    char *result_var;
    Expr *expr;
    SourceLocation loc;
} ArithAssignment;

/* ========================================================================= */
/* Declaration types                                                         */
/* ========================================================================= */

typedef struct {
    char *name;
    SourceLocation loc;
} EntityDecl;

typedef struct {
    char *name;
    char **params;
    int param_count;
    SourceLocation loc;
} RelationDecl;

typedef struct {
    char *predicate;
    char **args;
    int arg_count;
    int negated;
    char *aggregate_func;
    int aggregate_field;
    SourceLocation loc;
} Atom;

typedef struct {
    Atom *atoms;
    int atom_count;
    ArithAssignment *arith_assigns;
    int arith_count;
    Comparison *comparisons;
    int comparison_count;
    SourceLocation loc;
} Condition;

typedef struct {
    char *name;
    char **params;
    int param_count;
    Condition condition;
    SourceLocation loc;
} ObserveDecl;

typedef struct {
    char *name;
    char **params;
    int param_count;
    Condition condition;
    SourceLocation loc;
} DeriveDecl;

typedef struct {
    char *predicate;
    char **args;
    int arg_count;
    SourceLocation loc;
} InputFact;

typedef struct {
    char *predicate;
    char **args;
    int arg_count;
    SourceLocation loc;
} Query;

/* ========================================================================= */
/* Top-level program AST                                                     */
/* ========================================================================= */

typedef struct {
    EntityDecl *entities;
    int entity_count;
    int entity_capacity;
    RelationDecl *relations;
    int relation_count;
    int relation_capacity;
    ObserveDecl *observes;
    int observe_count;
    int observe_capacity;
    DeriveDecl *derives;
    int derive_count;
    int derive_capacity;
    InputFact *inputs;
    int input_count;
    int input_capacity;
    Query *queries;
    int query_count;
    int query_capacity;
} Program;

/* ========================================================================= */
/* Constructors and destructors                                              */
/* ========================================================================= */

Program *program_new(void);

/* Atom construction */
void atom_init(Atom *a, const char *predicate, int negated, SourceLocation loc);
void atom_add_arg(Atom *a, const char *arg);

/* Condition construction */
void condition_init(Condition *c, SourceLocation loc);
void condition_add_atom(Condition *c, const Atom *atom);
void condition_add_arith(Condition *c, const ArithAssignment *a);
void condition_add_comparison(Condition *c, const Comparison *cmp);

/* Expression constructors */
Expr *expr_new_number(double n, SourceLocation loc);
Expr *expr_new_variable(const char *name, SourceLocation loc);
Expr *expr_new_binary(char op, Expr *left, Expr *right, SourceLocation loc);
Expr *expr_new_unary_minus(Expr *operand, SourceLocation loc);
/* NEW in v1.1: construct function call expression */
Expr *expr_new_call(const char *func_name, Expr **args, int arg_count, SourceLocation loc);

/* Destructors */
void expr_free(Expr *e);
void comparison_free(Comparison *c);
void program_free(Program *p);
void atom_free(Atom *a);
void condition_free(Condition *c);
void arith_assignment_free(ArithAssignment *a);
void input_fact_free(InputFact *f);
void query_free(Query *q);

/* Debug output */
void expr_dump(const Expr *e, int indent);
void program_dump(const Program *p);

#endif
