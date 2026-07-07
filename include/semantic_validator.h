#ifndef SUPERLANG_SEMANTIC_VALIDATOR_H
#define SUPERLANG_SEMANTIC_VALIDATOR_H

#include "graph.h"

typedef struct ValidationResult {
    int is_valid;
    char *error_message;
    int error_count;
} ValidationResult;

/* Structural validation: checks graph integrity */
ValidationResult graph_validate_structure(const Graph *g);

/* Semantic validation: checks stratification, negative cycles, etc. */
ValidationResult graph_validate_semantics(const Graph *g);

/* Free validation result */
void validation_result_free(ValidationResult *result);

/* Print validation result */
void validation_result_dump(const ValidationResult *result);

#endif
