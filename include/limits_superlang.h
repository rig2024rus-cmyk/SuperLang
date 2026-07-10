#ifndef SUPERLANG_LIMITS_H
#define SUPERLANG_LIMITS_H

/* Runtime.c's rule evaluators use fixed-size stack buffers, sized from
 * these same constants, for per-atom and per-argument bookkeeping
 * (atom_to_pos[], pos_indices[], neg_args[], etc). type_checker.c rejects
 * any rule that would exceed them *before* synthesis, so runtime.c can
 * safely assume these limits hold rather than needing its own bounds
 * checks in the hot path. If either limit is raised, raise the matching
 * array sizes in runtime.c to match. */
#define SUPERLANG_MAX_BODY_ATOMS 32
#define SUPERLANG_MAX_ARITY 32

#endif
