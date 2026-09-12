#ifndef ROBOCOMPOSITOR_SERVER_H
#define ROBOCOMPOSITOR_SERVER_H

#include <stdint.h>
#include <robocompositor/protocol.h>
#include <robocompositor/output.h>

int cs_init(RoboOutput *out, int headless);
void cs_drop(void);
int cs_handle(uint64_t pid, RoboCompMsg *m);
int cs_dirty(void);
int cs_compose_present(void);
void cs_log_status(void);

#endif
