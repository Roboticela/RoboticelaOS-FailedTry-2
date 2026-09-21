#ifndef ROBO_VIRTIO_NET_H
#define ROBO_VIRTIO_NET_H

int VirtioNetRegister(void);
int VirtioNetReady(void);
void VirtioNetPoll(void);

#endif
