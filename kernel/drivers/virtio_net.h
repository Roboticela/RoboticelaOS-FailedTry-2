#ifndef ROBO_VIRTIO_NET_H
#define ROBO_VIRTIO_NET_H

int robo_virtio_net_register(void);
int robo_virtio_net_ready(void);
void robo_virtio_net_poll(void);

#endif
