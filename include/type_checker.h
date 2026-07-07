#ifndef SUPERLANG_TYPE_CHECKER_H
#define SUPERLANG_TYPE_CHECKER_H

#include "ast_nodes.h"

typedef struct {
    char *message;
    int line;
    int column;
    char *hint;      /* Optional: e.g., "Did you mean 'blocked'?" */
} TypeError;

typedef struct {
    TypeError *errors;
    int count;
    int capacity;
} TypeCheckResult;

/* Run all type/existence/safety checks on program.
 * Returns result with list of errors (count == 0 means valid program). */
TypeCheckResult typecheck_program(const Program *program);

/* Free all memory associated with result */
void typecheck_result_free(TypeCheckResult *result);

/* Print errors in user-friendly format */
void typecheck_result_dump(const TypeCheckResult *result);

#endif
