#ifndef ROBOCOMPOSITOR_BACKEND_H
#define ROBOCOMPOSITOR_BACKEND_H

/*
 * Composition backend vtable. CPU is implemented. GPU is a future
 * drop-in: same initialize/compose/present/shutdown, no Window API change.
 */

typedef struct RoboCompositionBackend {
    const char *name;
    int (*initialize)(void *ctx);
    int (*compose)(void *ctx);
    int (*present)(void *ctx);
    void (*shutdown)(void *ctx);
} RoboCompositionBackend;

#endif
