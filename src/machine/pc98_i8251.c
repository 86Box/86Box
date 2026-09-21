/* Clean Intel 8251A register-level implementation for PC-98. */
#include <string.h>
#include <86box/pc98_i8251.h>

static void line(pc98_i8251_t *u, pc98_i8251_line_cb cb, int level)
{ if (cb) cb(u->opaque, level); }
static void publish(pc98_i8251_t *u, uint8_t old)
{
    if ((old ^ u->status) & PC98_I8251_ST_RXRDY) line(u,u->rxrdy_cb,!!(u->status&PC98_I8251_ST_RXRDY));
    if ((old ^ u->status) & PC98_I8251_ST_TXRDY) line(u,u->txrdy_cb,!!(u->status&PC98_I8251_ST_TXRDY));
    if ((old ^ u->status) & PC98_I8251_ST_TXE) line(u,u->txempty_cb,!!(u->status&PC98_I8251_ST_TXE));
}
void pc98_i8251_reset(pc98_i8251_t *u)
{
    void *op=u->opaque; pc98_i8251_tx_cb tx=u->tx_cb; pc98_i8251_line_cb rr=u->rxrdy_cb,tr=u->txrdy_cb,te=u->txempty_cb;
    memset(u,0,sizeof(*u)); u->opaque=op; u->tx_cb=tx; u->rxrdy_cb=rr; u->txrdy_cb=tr; u->txempty_cb=te;
    u->state=0; u->cts=1; u->status=PC98_I8251_ST_TXRDY|PC98_I8251_ST_TXE;
    publish(u,0);
}
void pc98_i8251_init(pc98_i8251_t *u, void *opaque, pc98_i8251_tx_cb tx_cb,
                     pc98_i8251_line_cb rxrdy_cb, pc98_i8251_line_cb txrdy_cb,
                     pc98_i8251_line_cb txempty_cb)
{
    memset(u,0,sizeof(*u)); u->opaque=opaque; u->tx_cb=tx_cb; u->rxrdy_cb=rxrdy_cb; u->txrdy_cb=txrdy_cb; u->txempty_cb=txempty_cb; pc98_i8251_reset(u);
}
static void decode_mode(pc98_i8251_t *u, uint8_t v)
{
    u->mode=v; u->baud_factor=(uint8_t)(v&3u); u->sync_mode=(u->baud_factor==0);
    u->char_bits=(uint8_t)(5+((v>>2)&3u)); u->parity_enable=(uint8_t)((v>>4)&1u); u->parity_even=(uint8_t)((v>>5)&1u); u->stop_code=(uint8_t)((v>>6)&3u);
    if (u->sync_mode) { u->external_sync=(uint8_t)((v>>6)&1u); u->state=u->external_sync?3:1; }
    else u->state=3;
}
static void command_write(pc98_i8251_t *u, uint8_t v)
{
    uint8_t old=u->status;
    if (v & 0x40) { /* internal reset returns to mode-instruction state */
        u->command=0; u->state=0; u->status=(uint8_t)((u->status & PC98_I8251_ST_DSR)|PC98_I8251_ST_TXRDY|PC98_I8251_ST_TXE); publish(u,old); return;
    }
    u->command=v;
    if (v & 0x10) u->status &= (uint8_t)~(PC98_I8251_ST_PE|PC98_I8251_ST_OE|PC98_I8251_ST_FE);
    if (!(v&0x01)) u->status |= PC98_I8251_ST_TXRDY|PC98_I8251_ST_TXE;
    publish(u,old);
}
uint8_t pc98_i8251_read(pc98_i8251_t *u, int control)
{
    if (control) return (uint8_t)((u->status & 0x7f) | (u->dsr?PC98_I8251_ST_DSR:0));
    uint8_t old=u->status, v=u->rx_data; u->status &= (uint8_t)~PC98_I8251_ST_RXRDY; publish(u,old); return v;
}
void pc98_i8251_write(pc98_i8251_t *u, int control, uint8_t v)
{
    if (control) {
        if (u->state==0) decode_mode(u,v);
        else if (u->state==1) { u->sync[0]=v; u->state=2; }
        else if (u->state==2) { u->sync[1]=v; u->state=3; }
        else command_write(u,v);
        return;
    }
    uint8_t old=u->status; u->tx_data=v;
    if (!(u->command&0x01) || !u->cts) return;
    u->status &= (uint8_t)~(PC98_I8251_ST_TXRDY|PC98_I8251_ST_TXE); publish(u,old);
    if (u->tx_cb) u->tx_cb(u->opaque,v);
    old=u->status; u->status |= PC98_I8251_ST_TXRDY|PC98_I8251_ST_TXE; publish(u,old);
}
void pc98_i8251_receive(pc98_i8251_t *u, uint8_t v, int pe, int fe)
{
    if (!(u->command&0x04)) return; /* RxE */
    uint8_t old=u->status;
    if (u->status&PC98_I8251_ST_RXRDY) u->status|=PC98_I8251_ST_OE;
    u->rx_data=v; if(pe)u->status|=PC98_I8251_ST_PE; if(fe)u->status|=PC98_I8251_ST_FE; u->status|=PC98_I8251_ST_RXRDY; publish(u,old);
}
void pc98_i8251_set_cts(pc98_i8251_t *u,int a){u->cts=!!a;}
void pc98_i8251_set_dsr(pc98_i8251_t *u,int a){u->dsr=!!a;}
