#include "input_gate.h"
unsigned g_input_neutral_generation;
void rt_input_neutralize(void) {
    __atomic_add_fetch(&g_input_neutral_generation, 1u, __ATOMIC_RELAXED);
}
