#ifndef ROBOCOMPOSITOR_SERVER_H
#define ROBOCOMPOSITOR_SERVER_H

#include <stdint.h>
#include <robocompositor/protocol.h>
#include <robocompositor/output.h>

int CsInit(RoboOutput *out, int headless);
void CsDrop(void);
int CsHandle(uint64_t pid, RoboCompMsg *m);
int CsDirty(void);
int CsComposePresent(void);
void CsLogStatus(void);

#endif
