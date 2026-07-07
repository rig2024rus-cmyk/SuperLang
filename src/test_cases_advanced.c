#include "graph.h"
#include "synthesizer.h"
#include "runtime.h"
#include <stdio.h>
#include <string.h>

/* Test Case 4: ready(Task) with negation */
void test_ready(void) {
    printf("\n========================================\n");
    printf("TEST CASE 4: ready(Task) with negation\n");
    printf("========================================\n\n");

    printf("[LOGIC]\n");
    printf("  blocked(Task) <- Dependency(Task, Other), not Finished(Other)\n");
    printf("  ready(Task)   <- Task(Task), not blocked(Task)\n\n");

    Graph *g = graph_new();

    Node *n_task     = graph_add_node(g, "Task",       1, NODE_BASE);
    Node *n_dep      = graph_add_node(g, "Dependency", 2, NODE_BASE);
    Node *n_finished = graph_add_node(g, "Finished",   1, NODE_BASE);
    Node *n_blocked  = graph_add_node(g, "blocked",    1, NODE_DERIVED);
    Node *n_ready    = graph_add_node(g, "ready",      1, NODE_DERIVED);
    Node *n_obs      = graph_add_node(g, "is_ready",   1, NODE_OBSERVATION);

    /* Observation requires ready */
    graph_add_edge(g, n_obs, n_ready, EDGE_REQUIRES);

    /* blocked is defined by composition (Dependency) + negation (not Finished) */
    graph_add_edge(g, n_blocked, n_dep,      EDGE_DEFINED_BY_COMPOSITION);
    graph_add_edge(g, n_blocked, n_finished, EDGE_DEFINED_BY_NEGATION);

    /* ready is defined by composition (Task) + negation (not blocked) */
    graph_add_edge(g, n_ready, n_task,    EDGE_DEFINED_BY_COMPOSITION);
    graph_add_edge(g, n_ready, n_blocked, EDGE_DEFINED_BY_NEGATION);

    (void)n_dep; (void)n_finished; /* suppress unused warnings */

    printf("[1] Dependency graph:\n");
    graph_dump(g);

    printf("\n[2] Validation: %s\n", graph_is_valid(g) ? "OK" : "FAILED");

    printf("\n[3] Synthesizing...\n");
    ClosureIR *c = synthesize(g);
    closure_dump(c);

    /* Input: 3 tasks, chain of dependencies, only TaskA finished */
    printf("[4] Loading input data...\n");
    Config *config = config_new();
    config_add_fact(config, "Task",       1, "TaskA");
    config_add_fact(config, "Task",       1, "TaskB");
    config_add_fact(config, "Task",       1, "TaskC");
    config_add_fact(config, "Dependency", 2, "TaskB", "TaskA");
    config_add_fact(config, "Dependency", 2, "TaskC", "TaskB");
    config_add_fact(config, "Finished",   1, "TaskA");
    config_dump(config);

    printf("\n[5] Executing saturation (stratified)...\n");
    saturate(config, c);

    printf("\n[6] Expected results:\n");
    printf("    TaskA: ready   (no dependencies)\n");
    printf("    TaskB: ready   (depends on TaskA, which IS finished)\n");
    printf("    TaskC: NOT ready (depends on TaskB, which is NOT finished)\n\n");

    printf("[7] Queries:\n");
    int rA = config_has_fact(config, "ready", 1, "TaskA");
    int rB = config_has_fact(config, "ready", 1, "TaskB");
    int rC = config_has_fact(config, "ready", 1, "TaskC");
    printf("    ready(TaskA) = %s  %s\n", rA ? "TRUE" : "FALSE", rA ? "✓" : "✗");
    printf("    ready(TaskB) = %s  %s\n", rB ? "TRUE" : "FALSE", rB ? "✓" : "✗");
    printf("    ready(TaskC) = %s  %s\n", rC ? "TRUE" : "FALSE", !rC ? "✓" : "✗");

    config_free(config);
    closure_free(c);
    graph_free(g);

    int pass = rA && rB && !rC;
    printf("\n[8] Result: %s\n", pass ? "✓ PASS - negation works!" : "✗ FAIL");
}

/* Test Case 5: aggregation (still fails — not yet implemented) */
void test_total_price(void) {
    printf("\n========================================\n");
    printf("TEST CASE 5: total_price(Order, Sum)\n");
    printf("========================================\n\n");

    printf("[STATUS] Aggregation still not supported.\n");
    printf("[REQUIRED EXTENSION] EDGE_DEFINED_BY_AGGREGATE\n\n");

    printf("[RESULT] ✗ FAIL - architecture limitation (unchanged)\n");
}

void run_advanced_tests(void) {
    printf("\n\n");
    printf("SuperLang Prototype v0.5 - Negation Support Tests\n");
    printf("==================================================\n");

    test_ready();
    test_total_price();

    printf("\n========================================\n");
    printf("SUMMARY - Extension Results\n");
    printf("========================================\n");
    printf("Test 4 (ready/negation):      see above\n");
    printf("Test 5 (total_price/agg):     ✗ FAIL (not implemented)\n");
}
