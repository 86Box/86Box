#include <stdint.h>

//void vlan_handler(int (*can_receive)(void *p), void (*receive)(void *p, const uint8_t *buf, int size), void *p);
void vlan_handler(void (*poller)(void *p), void *p);

extern int network_card_current;

int network_card_available(int card);
char *network_card_getname(int card);
struct device_t *network_card_getdevice(int card);
int network_card_has_config(int card);
void network_card_init();

void initpcap();
void closepcap();
