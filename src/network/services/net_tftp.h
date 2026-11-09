/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Interface for the virtual network server's read-only TFTP service.
 *
 * Authors: Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2025-2026 Jasmine Iwanek.
 */
typedef struct vserver_tftp_t vserver_tftp_t;
typedef void (*vserver_tftp_output_t)(void *priv, uint32_t destination, uint16_t source_port, uint16_t port,
                                      const uint8_t *data, int length);

vserver_tftp_t *vserver_tftp_create(vserver_tftp_output_t output, void *priv);
void            vserver_tftp_close(vserver_tftp_t *tftp);
bool            vserver_tftp_input(vserver_tftp_t *tftp, uint32_t source, uint16_t source_port,
                                   uint16_t destination_port, const uint8_t *data, int length);
void            vserver_tftp_tick(vserver_tftp_t *tftp);
