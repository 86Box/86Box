/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
void piix_init(int card);
void piix3_init(int card);

uint8_t piix_bus_master_read(uint16_t port, void *priv);
void piix_bus_master_write(uint16_t port, uint8_t val, void *priv);

int piix_bus_master_get_count(int channel);

int piix_bus_master_dma_read_ex(int channel, uint8_t *data);
