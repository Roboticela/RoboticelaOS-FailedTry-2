#ifndef ROBO_PROCESS_TABLE_H
#define ROBO_PROCESS_TABLE_H

#include "process/process.h"

void robo_ptable_init(void);
int robo_ptable_insert(RoboProcess *process);
void robo_ptable_remove(RoboProcess *process);
RoboProcess *robo_ptable_lookup(robo_process_id_t pid);
uint64_t robo_ptable_count(void);
RoboProcess *robo_ptable_head(void);
robo_process_id_t robo_ptable_alloc_id(void);

#endif
