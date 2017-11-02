/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
extern void io_init(void);

extern void io_sethandler(uint16_t base, int size, 
                   uint8_t  (*inb)(uint16_t addr, void *priv), 
                   uint16_t (*inw)(uint16_t addr, void *priv), 
                   uint32_t (*inl)(uint16_t addr, void *priv), 
                   void (*outb)(uint16_t addr, uint8_t  val, void *priv),
                   void (*outw)(uint16_t addr, uint16_t val, void *priv),
                   void (*outl)(uint16_t addr, uint32_t val, void *priv),
                   void *priv);

extern void io_sethandler_interleaved(uint16_t base, int size, 
                   uint8_t  (*inb)(uint16_t addr, void *priv), 
                   uint16_t (*inw)(uint16_t addr, void *priv), 
                   uint32_t (*inl)(uint16_t addr, void *priv), 
                   void (*outb)(uint16_t addr, uint8_t  val, void *priv),
                   void (*outw)(uint16_t addr, uint16_t val, void *priv),
                   void (*outl)(uint16_t addr, uint32_t val, void *priv),
                   void *priv);

extern void io_removehandler(uint16_t base, int size, 
                   uint8_t  (*inb)(uint16_t addr, void *priv), 
                   uint16_t (*inw)(uint16_t addr, void *priv), 
                   uint32_t (*inl)(uint16_t addr, void *priv), 
                   void (*outb)(uint16_t addr, uint8_t  val, void *priv),
                   void (*outw)(uint16_t addr, uint16_t val, void *priv),
                   void (*outl)(uint16_t addr, uint32_t val, void *priv),
                   void *priv);

extern void io_removehandler_interleaved(uint16_t base, int size, 
                   uint8_t  (*inb)(uint16_t addr, void *priv), 
                   uint16_t (*inw)(uint16_t addr, void *priv), 
                   uint32_t (*inl)(uint16_t addr, void *priv), 
                   void (*outb)(uint16_t addr, uint8_t  val, void *priv),
                   void (*outw)(uint16_t addr, uint16_t val, void *priv),
                   void (*outl)(uint16_t addr, uint32_t val, void *priv),
                   void *priv);

extern uint8_t	inb(uint16_t port);
extern void	outb(uint16_t port, uint8_t  val);
extern uint16_t	inw(uint16_t port);
extern void	outw(uint16_t port, uint16_t val);
extern uint32_t	inl(uint16_t port);
extern void	outl(uint16_t port, uint32_t val);
