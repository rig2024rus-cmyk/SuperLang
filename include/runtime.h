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

/* Saturation */
void saturate(Config *c, const ClosureIR *closure);

/* Output */
void config_dump(const Config *c);

#endif
