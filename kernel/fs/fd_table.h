#ifndef ROBO_FD_TABLE_H
#define ROBO_FD_TABLE_H

#include "fs/file.h"

typedef struct RoboFDEntry {
    RoboFile *file;
    uint32_t flags;
} RoboFDEntry;

typedef struct RoboFDTable {
    RoboFDEntry ent[ROBO_MAX_FDS];
    unsigned open_count;
} RoboFDTable;

RoboFDTable *robo_fd_table_create(void);
void robo_fd_table_destroy(RoboFDTable *table);
int robo_fd_table_clone(const RoboFDTable *source, RoboFDTable **destination);
unsigned robo_fd_table_open_count(const RoboFDTable *table);

robo_fd_t robo_fd_table_alloc(RoboFDTable *table, RoboFile *file,
                              uint32_t flags);
int robo_fd_table_close(RoboFDTable *table, robo_fd_t fd);
RoboFile *robo_fd_table_get(RoboFDTable *table, robo_fd_t fd);

#endif
