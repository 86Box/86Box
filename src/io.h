void io_init();

void io_sethandler(uint16_t base, int size, 
                   uint8_t  (*inb)(uint16_t addr, void *priv), 
                   uint16_t (*inw)(uint16_t addr, void *priv), 
                   uint32_t (*inl)(uint16_t addr, void *priv), 
                   void (*outb)(uint16_t addr, uint8_t  val, void *priv),
                   void (*outw)(uint16_t addr, uint16_t val, void *priv),
                   void (*outl)(uint16_t addr, uint32_t val, void *priv),
                   void *priv);
                   
void io_removehandler(uint16_t base, int size, 
                   uint8_t  (*inb)(uint16_t addr, void *priv), 
                   uint16_t (*inw)(uint16_t addr, void *priv), 
                   uint32_t (*inl)(uint16_t addr, void *priv), 
                   void (*outb)(uint16_t addr, uint8_t  val, void *priv),
                   void (*outw)(uint16_t addr, uint16_t val, void *priv),
                   void (*outl)(uint16_t addr, uint32_t val, void *priv),
                   void *priv);
