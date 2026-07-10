#ifndef SUPERLANG_RUNTIME_H
#define SUPERLANG_RUNTIME_H

#include <stddef.h>
#include "synthesizer.h"

/* A fact is a predicate with arguments (strings for simplicity) */
typedef struct Fact {
    char *predicate;
    char **args;
    int arity;
    struct Fact *next;
} Fact;

/* Configuration = set of facts */
typedef struct Config {
    Fact *facts;
    size_t count;
} Config;

/* Lifecycle */
Config *config_new(void);
void config_free(Config *c);

/* Mutation */
void config_add_fact(Config *c, const char *pred, int arity, ...);
int config_has_fact(Config *c, const char *pred, int arity, ...);

/* Array-based fact insertion (any arity, deduplicated) — use this instead
 * of config_add_fact's varargs form when args are already in an array,
 * e.g. when loading parsed input facts. */
void add_fact_direct(Config *c, const char *pred, int arity, char **args);

/* Saturation */
void saturate(Config *c, const ClosureIR *closure);

/* Output */
void config_dump(const Config *c);

/* Pattern Matching Support */
typedef void (*FactVisitor)(const char *pred, int arity, const char **args, void *ctx);
void config_visit_facts(Config *c, FactVisitor visitor, void *ctx);

#endif
