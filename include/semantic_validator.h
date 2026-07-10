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

/* Assigns Node->stratum to every node in the graph, based on the same SCC
 * condensation used for stratifiability checking (one stratum per SCC,
 * +1 when crossing a negation edge). Call only after graph_validate_semantics
 * has returned is_valid=1 — on a non-stratifiable graph the result is
 * meaningless, since no valid stratification exists. */
void graph_compute_strata(Graph *g);

/* Free validation result */
void validation_result_free(ValidationResult *result);

/* Print validation result */
void validation_result_dump(const ValidationResult *result);

#endif
