/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#include <stdint.h>

void vlan_handler(void (*poller)(void *p), void *p);

extern int network_card_current;

int network_card_available(int card);
char *network_card_getname(int card);
struct device_t *network_card_getdevice(int card);
int network_card_has_config(int card);
char *network_card_get_internal_name(int card);
int network_card_get_from_internal_name(char *s);
void network_card_init();
void vlan_reset();

void initpcap();
void closepcap();
