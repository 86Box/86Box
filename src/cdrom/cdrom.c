/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the CD-ROM drive with SCSI(-like)
 *		commands, for both ATAPI and SCSI usage.
 *
 * Version:	@(#)cdrom.c	1.0.17	2017/10/14
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2016,2017 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>
#include "../86box.h"
#include "../config.h"
#include "../ibm.h"
#include "../timer.h"
#include "../device.h"
#include "../piix.h"
#include "../scsi/scsi.h"
#include "../nvr.h"
#include "../disk/hdc.h"
#include "../disk/hdc_ide.h"
#include "../plat.h"
#include "../ui.h"
#include "cdrom.h"
#include "cdrom_image.h"
#include "cdrom_null.h"


/* Bits of 'status' */
#define ERR_STAT		0x01
#define DRQ_STAT		0x08 /* Data request */
#define DSC_STAT                0x10
#define SERVICE_STAT            0x10
#define READY_STAT		0x40
#define BUSY_STAT		0x80

/* Bits of 'error' */
#define ABRT_ERR		0x04 /* Command aborted */
#define MCR_ERR			0x08 /* Media change request */

cdrom_t cdrom[CDROM_NUM];
cdrom_drive_t cdrom_drives[CDROM_NUM];
uint8_t atapi_cdrom_drives[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
uint8_t scsi_cdrom_drives[16][8] =	{	{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
						{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
						{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
						{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
						{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
						{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
						{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
						{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
						{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
						{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
						{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
						{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
						{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
						{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
						{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
						{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }	};


#pragma pack(push,1)
static struct
{
	uint8_t opcode;
	uint8_t polled;
	uint8_t reserved2[2];
	uint8_t class;
	uint8_t reserved3[2];
	uint16_t len;
	uint8_t control;
} *gesn_cdb;
#pragma pack(pop)

#pragma pack(push,1)
static struct
{
	uint16_t len;
	uint8_t notification_class;
	uint8_t supported_events;
} *gesn_event_header;
#pragma pack(pop)


/* Table of all SCSI commands and their flags, needed for the new disc change / not ready handler. */
uint8_t cdrom_command_flags[0x100] =
{
	IMPLEMENTED | CHECK_READY | NONDATA,
	IMPLEMENTED | ALLOW_UA | NONDATA | SCSI_ONLY,
	0,
	IMPLEMENTED | ALLOW_UA,
	0, 0, 0, 0,
	IMPLEMENTED | CHECK_READY,
	0, 0,
	IMPLEMENTED | CHECK_READY | NONDATA,
	0, 0, 0, 0, 0, 0,
	IMPLEMENTED | ALLOW_UA,
	IMPLEMENTED | CHECK_READY | NONDATA | SCSI_ONLY,
	0,
	IMPLEMENTED,
	0, 0, 0, 0,
	IMPLEMENTED,
	IMPLEMENTED | CHECK_READY,
	0, 0,
	IMPLEMENTED | CHECK_READY,
	0, 0, 0, 0, 0, 0,
	IMPLEMENTED | CHECK_READY,
	0, 0,
	IMPLEMENTED | CHECK_READY,
	0, 0,
	IMPLEMENTED | CHECK_READY | NONDATA,
	0, 0, 0,
	IMPLEMENTED | CHECK_READY | NONDATA | SCSI_ONLY,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0,
	IMPLEMENTED | CHECK_READY,
	IMPLEMENTED | CHECK_READY,	/* Read TOC - can get through UNIT_ATTENTION, per VIDE-CDD.SYS
					   NOTE: The ATAPI reference says otherwise, but I think this is a question of
					   interpreting things right - the UNIT ATTENTION condition we have here
					   is a tradition from not ready to ready, by definition the drive
					   eventually becomes ready, make the condition go away. */
	IMPLEMENTED | CHECK_READY,
	IMPLEMENTED | CHECK_READY,
	IMPLEMENTED | ALLOW_UA,
	IMPLEMENTED | CHECK_READY,
	IMPLEMENTED | CHECK_READY,
	0,
	IMPLEMENTED | ALLOW_UA,
	IMPLEMENTED | CHECK_READY,
	0, 0,
	IMPLEMENTED | CHECK_READY,
	0, 0,
	IMPLEMENTED | CHECK_READY,
	IMPLEMENTED | CHECK_READY,
	0, 0,
	IMPLEMENTED,
	0, 0, 0, 0,
	IMPLEMENTED,
	0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0,
	IMPLEMENTED | CHECK_READY,
	0, 0,
	IMPLEMENTED | CHECK_READY,
	0, 0, 0, 0,
	IMPLEMENTED | CHECK_READY,
	0,
	IMPLEMENTED | CHECK_READY | NONDATA | SCSI_ONLY,
	0, 0, 0, 0,
	IMPLEMENTED | CHECK_READY | ATAPI_ONLY,
	0, 0, 0,
	IMPLEMENTED | CHECK_READY | ATAPI_ONLY,
	IMPLEMENTED | CHECK_READY,
	IMPLEMENTED | CHECK_READY,
	IMPLEMENTED,
	IMPLEMENTED | CHECK_READY,
	IMPLEMENTED,
	IMPLEMENTED | CHECK_READY,
	IMPLEMENTED | CHECK_READY,
	0, 0,
	IMPLEMENTED | CHECK_READY | SCSI_ONLY,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	IMPLEMENTED | CHECK_READY | SCSI_ONLY,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	IMPLEMENTED | SCSI_ONLY,
	0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

uint64_t cdrom_mode_sense_page_flags[CDROM_NUM] = { (1LL << GPMODE_R_W_ERROR_PAGE) | (1LL << GPMODE_CDROM_PAGE) | (1LL << GPMODE_CDROM_AUDIO_PAGE) | (1LL << GPMODE_CAPABILITIES_PAGE) | (1LL << GPMODE_ALL_PAGES),
						    (1LL << GPMODE_R_W_ERROR_PAGE) | (1LL << GPMODE_CDROM_PAGE) | (1LL << GPMODE_CDROM_AUDIO_PAGE) | (1LL << GPMODE_CAPABILITIES_PAGE) | (1LL << GPMODE_ALL_PAGES),
						    (1LL << GPMODE_R_W_ERROR_PAGE) | (1LL << GPMODE_CDROM_PAGE) | (1LL << GPMODE_CDROM_AUDIO_PAGE) | (1LL << GPMODE_CAPABILITIES_PAGE) | (1LL << GPMODE_ALL_PAGES),
						    (1LL << GPMODE_R_W_ERROR_PAGE) | (1LL << GPMODE_CDROM_PAGE) | (1LL << GPMODE_CDROM_AUDIO_PAGE) | (1LL << GPMODE_CAPABILITIES_PAGE) | (1LL << GPMODE_ALL_PAGES) };

const uint8_t cdrom_mode_sense_pages_default[CDROM_NUM][0x40][0x40] =
{
	{	{                        0,    0 },
		{    GPMODE_R_W_ERROR_PAGE,    6, 0, 5, 0,  0, 0, 0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{        GPMODE_CDROM_PAGE,    6, 0, 1, 0, 60, 0, 75 },
		{                     0x8E,  0xE, 4, 0, 0,  0, 0, 75, 1, 0xFF, 2, 0xFF, 0, 0, 0, 0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{ GPMODE_CAPABILITIES_PAGE, 0x12, 0, 0, 1,  0, 0, 0, 0x02, 0xC2, 0, 2, 0, 0, 0x02, 0xC2, 0, 0, 0, 0 }	},
	{	{                        0,    0 },
		{    GPMODE_R_W_ERROR_PAGE,    6, 0, 5, 0,  0, 0, 0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{        GPMODE_CDROM_PAGE,    6, 0, 1, 0, 60, 0, 75 },
		{                     0x8E,  0xE, 4, 0, 0,  0, 0, 75, 1, 0xFF, 2, 0xFF, 0, 0, 0, 0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{ GPMODE_CAPABILITIES_PAGE, 0x12, 0, 0, 1,  0, 0, 0, 0x02, 0xC2, 0, 2, 0, 0, 0x02, 0xC2, 0, 0, 0, 0 }	},
	{	{                        0,    0 },
		{    GPMODE_R_W_ERROR_PAGE,    6, 0, 5, 0,  0, 0, 0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{        GPMODE_CDROM_PAGE,    6, 0, 1, 0, 60, 0, 75 },
		{                     0x8E,  0xE, 4, 0, 0,  0, 0, 75, 1, 0xFF, 2, 0xFF, 0, 0, 0, 0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{ GPMODE_CAPABILITIES_PAGE, 0x12, 0, 0, 1,  0, 0, 0, 0x02, 0xC2, 0, 2, 0, 0, 0x02, 0xC2, 0, 0, 0, 0 }	},
	{	{                        0,    0 },
		{    GPMODE_R_W_ERROR_PAGE,    6, 0, 5, 0,  0, 0, 0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{        GPMODE_CDROM_PAGE,    6, 0, 1, 0, 60, 0, 75 },
		{                     0x8E,  0xE, 4, 0, 0,  0, 0, 75, 1, 0xFF, 2, 0xFF, 0, 0, 0, 0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{ GPMODE_CAPABILITIES_PAGE, 0x12, 0, 0, 1,  0, 0, 0, 0x02, 0xC2, 0, 2, 0, 0, 0x02, 0xC2, 0, 0, 0, 0 }	}
};

uint8_t cdrom_mode_sense_pages_changeable[CDROM_NUM][0x40][0x40] =
{
	{	{                        0,    0 },
		{    GPMODE_R_W_ERROR_PAGE,    6, 0, 5, 0,  0, 0, 0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{        GPMODE_CDROM_PAGE,    6, 0, 1, 0, 60, 0, 75 },
		{                     0x8E,  0xE, 4, 0, 0,  0, 0, 75, 1, 0xFF, 2, 0xFF, 0, 0, 0, 0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{ GPMODE_CAPABILITIES_PAGE, 0x12, 0, 0, 1,  0, 0, 0, 0x02, 0xC2, 0, 2, 0, 0, 0x02, 0xC2, 0, 0, 0, 0 }	},
	{	{                        0,    0 },
		{    GPMODE_R_W_ERROR_PAGE,    6, 0, 5, 0,  0, 0, 0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{        GPMODE_CDROM_PAGE,    6, 0, 1, 0, 60, 0, 75 },
		{                     0x8E,  0xE, 4, 0, 0,  0, 0, 75, 1, 0xFF, 2, 0xFF, 0, 0, 0, 0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{ GPMODE_CAPABILITIES_PAGE, 0x12, 0, 0, 1,  0, 0, 0, 0x02, 0xC2, 0, 2, 0, 0, 0x02, 0xC2, 0, 0, 0, 0 }	},
	{	{                        0,    0 },
		{    GPMODE_R_W_ERROR_PAGE,    6, 0, 5, 0,  0, 0, 0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{        GPMODE_CDROM_PAGE,    6, 0, 1, 0, 60, 0, 75 },
		{                     0x8E,  0xE, 4, 0, 0,  0, 0, 75, 1, 0xFF, 2, 0xFF, 0, 0, 0, 0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{ GPMODE_CAPABILITIES_PAGE, 0x12, 0, 0, 1,  0, 0, 0, 0x02, 0xC2, 0, 2, 0, 0, 0x02, 0xC2, 0, 0, 0, 0 }	},
	{	{                        0,    0 },
		{    GPMODE_R_W_ERROR_PAGE,    6, 0, 5, 0,  0, 0, 0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{        GPMODE_CDROM_PAGE,    6, 0, 1, 0, 60, 0, 75 },
		{                     0x8E,  0xE, 4, 0, 0,  0, 0, 75, 1, 0xFF, 2, 0xFF, 0, 0, 0, 0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{ GPMODE_CAPABILITIES_PAGE, 0x12, 0, 0, 1,  0, 0, 0, 0x02, 0xC2, 0, 2, 0, 0, 0x02, 0xC2, 0, 0, 0, 0 }	}
};

uint8_t cdrom_mode_sense_pages_saved[CDROM_NUM][0x40][0x40] =
{
	{	{                        0,    0 },
		{    GPMODE_R_W_ERROR_PAGE,    6, 0, 5, 0,  0, 0, 0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{        GPMODE_CDROM_PAGE,    6, 0, 1, 0, 60, 0, 75 },
		{                     0x8E,  0xE, 4, 0, 0,  0, 0, 75, 1, 0xFF, 2, 0xFF, 0, 0, 0, 0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{ GPMODE_CAPABILITIES_PAGE, 0x12, 0, 0, 1,  0, 0, 0, 0x02, 0xC2, 0, 2, 0, 0, 0x02, 0xC2, 0, 0, 0, 0 }	},
	{	{                        0,    0 },
		{    GPMODE_R_W_ERROR_PAGE,    6, 0, 5, 0,  0, 0, 0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{        GPMODE_CDROM_PAGE,    6, 0, 1, 0, 60, 0, 75 },
		{                     0x8E,  0xE, 4, 0, 0,  0, 0, 75, 1, 0xFF, 2, 0xFF, 0, 0, 0, 0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{ GPMODE_CAPABILITIES_PAGE, 0x12, 0, 0, 1,  0, 0, 0, 0x02, 0xC2, 0, 2, 0, 0, 0x02, 0xC2, 0, 0, 0, 0 }	},
	{	{                        0,    0 },
		{    GPMODE_R_W_ERROR_PAGE,    6, 0, 5, 0,  0, 0, 0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{        GPMODE_CDROM_PAGE,    6, 0, 1, 0, 60, 0, 75 },
		{                     0x8E,  0xE, 4, 0, 0,  0, 0, 75, 1, 0xFF, 2, 0xFF, 0, 0, 0, 0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{ GPMODE_CAPABILITIES_PAGE, 0x12, 0, 0, 1,  0, 0, 0, 0x02, 0xC2, 0, 2, 0, 0, 0x02, 0xC2, 0, 0, 0, 0 }	},
	{	{                        0,    0 },
		{    GPMODE_R_W_ERROR_PAGE,    6, 0, 5, 0,  0, 0, 0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{        GPMODE_CDROM_PAGE,    6, 0, 1, 0, 60, 0, 75 },
		{                     0x8E,  0xE, 4, 0, 0,  0, 0, 75, 1, 0xFF, 2, 0xFF, 0, 0, 0, 0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{                        0,    0 },
		{ GPMODE_CAPABILITIES_PAGE, 0x12, 0, 0, 1,  0, 0, 0, 0x02, 0xC2, 0, 2, 0, 0, 0x02, 0xC2, 0, 0, 0, 0 }	}
};

#ifdef ENABLE_CDROM_LOG
int cdrom_do_log = ENABLE_CDROM_LOG;
#endif

void cdrom_log(const char *format, ...)
{
#ifdef ENABLE_CDROM_LOG
	if (cdrom_do_log)
	{
		va_list ap;
		va_start(ap, format);
		vprintf(format, ap);
		va_end(ap);
		fflush(stdout);
	}
#endif
}

int cdrom_mode_select_terminate(uint8_t id, int force);

int find_cdrom_for_channel(uint8_t channel)
{
	uint8_t i = 0;

	for (i = 0; i < CDROM_NUM; i++)
	{
		if (((cdrom_drives[i].bus_type == CDROM_BUS_ATAPI_PIO_ONLY) || (cdrom_drives[i].bus_type == CDROM_BUS_ATAPI_PIO_AND_DMA)) && (cdrom_drives[i].ide_channel == channel))
		{
			return i;
		}
	}
	return 0xff;
}

void cdrom_init(int id, int cdb_len_setting);

void build_atapi_cdrom_map()
{
	uint8_t i = 0;

	memset(atapi_cdrom_drives, 0xff, 8);

	for (i = 0; i < 8; i++)
	{
		atapi_cdrom_drives[i] = find_cdrom_for_channel(i);
		if (atapi_cdrom_drives[i] != 0xff)
		{
			cdrom_init(atapi_cdrom_drives[i], 12);
		}
	}
}

int find_cdrom_for_scsi_id(uint8_t scsi_id, uint8_t scsi_lun)
{
	uint8_t i = 0;

	for (i = 0; i < CDROM_NUM; i++)
	{
		if ((cdrom_drives[i].bus_type == CDROM_BUS_SCSI) && (cdrom_drives[i].scsi_device_id == scsi_id) && (cdrom_drives[i].scsi_device_lun == scsi_lun))
		{
			return i;
		}
	}
	return 0xff;
}

void build_scsi_cdrom_map()
{
	uint8_t i = 0;
	uint8_t j = 0;

	for (i = 0; i < 16; i++)
	{
		memset(scsi_cdrom_drives[i], 0xff, 8);
	}

	for (i = 0; i < 16; i++)
	{
		for (j = 0; j < 8; j++)
		{
			scsi_cdrom_drives[i][j] = find_cdrom_for_scsi_id(i, j);
			if (scsi_cdrom_drives[i][j] != 0xff)
			{
				cdrom_init(scsi_cdrom_drives[i][j], 12);
			}
		}
	}
}

void cdrom_set_cdb_len(int id, int cdb_len)
{
	cdrom[id].cdb_len = cdb_len;
}

void cdrom_reset_cdb_len(int id)
{
	cdrom[id].cdb_len = cdrom[id].cdb_len_setting ? 16 : 12;
}

void cdrom_set_signature(int id)
{
	if (id >= CDROM_NUM)
	{
		return;
	}
	cdrom[id].phase = 1;
	cdrom[id].request_length = 0xEB14;
}

void cdrom_init(int id, int cdb_len_setting)
{
	if (id >= CDROM_NUM)
	{
		return;
	}
	memset(&(cdrom[id]), 0, sizeof(cdrom_t));
	cdrom[id].requested_blocks = 1;
	if (cdb_len_setting <= 1)
	{
		cdrom[id].cdb_len_setting = cdb_len_setting;
	}
	cdrom_reset_cdb_len(id);
	cdrom_mode_select_terminate(id, 1);
	cdrom[id].cd_status = CD_STATUS_EMPTY;
	cdrom[id].sense[0] = 0xf0;
	cdrom[id].sense[7] = 10;
	cdrom_drives[id].bus_mode = 0;
	if (cdrom_drives[id].bus_type > CDROM_BUS_ATAPI_PIO_AND_DMA)
	{
		cdrom_drives[id].bus_mode |= 2;
	}
	if (cdrom_drives[id].bus_type < CDROM_BUS_SCSI)
	{
		cdrom_drives[id].bus_mode |= 1;
	}
	cdrom_log("CD-ROM %i: Bus type %i, bus mode %i\n", id, cdrom_drives[id].bus_type, cdrom_drives[id].bus_mode);
	if (cdrom_drives[id].bus_type < CDROM_BUS_SCSI)
	{
		cdrom_set_signature(id);
		cdrom_drives[id].max_blocks_at_once = 1;
	}
	else
	{
		cdrom_drives[id].max_blocks_at_once = 85;
	}
	cdrom[id].status = READY_STAT | DSC_STAT;
	cdrom[id].pos = 0;
	cdrom[id].packet_status = 0xff;
	cdrom_sense_key = cdrom_asc = cdrom_ascq = cdrom[id].unit_attention = 0;
	cdrom[id].cdb_len_setting = 0;
	cdrom[id].cdb_len = 12;
}

int cdrom_supports_pio(int id)
{
	return (cdrom_drives[id].bus_mode & 1);
}

int cdrom_supports_dma(int id)
{
	return (cdrom_drives[id].bus_mode & 2);
}

/* Returns: 0 for none, 1 for PIO, 2 for DMA. */
int cdrom_current_mode(int id)
{
	if (!cdrom_supports_pio(id) && !cdrom_supports_dma(id))
	{
		return 0;
	}
	if (cdrom_supports_pio(id) && !cdrom_supports_dma(id))
	{
		return 1;
	}
	if (!cdrom_supports_pio(id) && cdrom_supports_dma(id))
	{
		return 2;
	}
	if (cdrom_supports_pio(id) && cdrom_supports_dma(id))
	{
		return (cdrom[id].features & 1) ? 2 : 1;
	}

	return 0;
}

/* Translates ATAPI status (ERR_STAT flag) to SCSI status. */
int cdrom_CDROM_PHASE_to_scsi(uint8_t id)
{
	if (cdrom[id].status & ERR_STAT)
	{
		return SCSI_STATUS_CHECK_CONDITION;
	}
	else
	{
		return SCSI_STATUS_OK;
	}
}

/* Translates ATAPI phase (DRQ, I/O, C/D) to SCSI phase (MSG, C/D, I/O). */
int cdrom_atapi_phase_to_scsi(uint8_t id)
{
	if (cdrom[id].status & 8)
	{
		switch (cdrom[id].phase & 3)
		{
			case 0:
				return 0;
			case 1:
				return 2;
			case 2:
				return 1;
			case 3:
				return 7;
		}
	}
	else
	{
		if ((cdrom[id].phase & 3) == 3)
		{
			return 3;
		}
		else
		{
			/* Translate reserved ATAPI phase to reserved SCSI phase. */
			return 4;
		}
	}

	return 0;
}

int cdrom_lba_to_msf_accurate(int lba)
{
	int temp_pos;
	int m, s, f;
	
	temp_pos = lba + 150;
	f = temp_pos % 75;
	temp_pos -= f;
	temp_pos /= 75;
	s = temp_pos % 60;
	temp_pos -= s;
	temp_pos /= 60;
	m = temp_pos;
	
	return ((m << 16) | (s << 8) | f);
}

uint32_t cdrom_mode_sense_get_channel(uint8_t id, int channel)
{
	return cdrom_mode_sense_pages_saved[id][GPMODE_CDROM_AUDIO_PAGE][channel ? 10 : 8];
}

uint32_t cdrom_mode_sense_get_volume(uint8_t id, int channel)
{
	return cdrom_mode_sense_pages_saved[id][GPMODE_CDROM_AUDIO_PAGE][channel ? 11 : 9];
}

void cdrom_mode_sense_load(uint8_t id)
{
	FILE *f;
	switch(id)
	{
		case 0:
			f = plat_fopen(nvr_path(L"cdrom_1_mode_sense.bin"), L"rb");
			break;
		case 1:
			f = plat_fopen(nvr_path(L"cdrom_2_mode_sense.bin"), L"rb");
			break;
		case 2:
			f = plat_fopen(nvr_path(L"cdrom_3_mode_sense.bin"), L"rb");
			break;
		case 3:
			f = plat_fopen(nvr_path(L"cdrom_4_mode_sense.bin"), L"rb");
			break;
		default:
			return;
	}
	if (!f)
	{
		return;
	}
	fread(cdrom_mode_sense_pages_saved[id][GPMODE_CDROM_AUDIO_PAGE], 1, 0x10, f);
	fclose(f);
}

void cdrom_mode_sense_save(uint8_t id)
{
	FILE *f;
	switch(id)
	{
		case 0:
			f = plat_fopen(nvr_path(L"cdrom_1_mode_sense.bin"), L"wb");
			break;
		case 1:
			f = plat_fopen(nvr_path(L"cdrom_2_mode_sense.bin"), L"wb");
			break;
		case 2:
			f = plat_fopen(nvr_path(L"cdrom_3_mode_sense.bin"), L"wb");
			break;
		case 3:
			f = plat_fopen(nvr_path(L"cdrom_4_mode_sense.bin"), L"wb");
			break;
		default:
			return;
	}
	if (!f)
	{
		return;
	}
	fwrite(cdrom_mode_sense_pages_saved[id][GPMODE_CDROM_AUDIO_PAGE], 1, 0x10, f);
	fclose(f);
}

int cdrom_mode_select_init(uint8_t id, uint8_t command, uint16_t pl_length, uint8_t do_save)
{
	switch(command)
	{
		case GPCMD_MODE_SELECT_6:
			cdrom[id].current_page_len = 4;
			break;
		case GPCMD_MODE_SELECT_10:
			cdrom[id].current_page_len = 8;
			break;
		default:
			cdrom_log("CD-ROM %i: Attempting to initialize MODE SELECT with unrecognized command: %02X\n", id, command);
			return -1;
	}
	if (pl_length == 0)
	{
		cdrom_log("CD-ROM %i: Attempting to initialize MODE SELECT with zero parameter list length: %02X\n", id, command);
		return -2;
	}
	cdrom[id].current_page_pos = 0;
	cdrom[id].mode_select_phase = MODE_SELECT_PHASE_HEADER;
	cdrom[id].total_length = pl_length;
	cdrom[id].written_length = 0;
	cdrom[id].do_page_save = do_save;
	return 1;
}

int cdrom_mode_select_terminate(uint8_t id, int force)
{
	if (((cdrom[id].written_length >= cdrom[id].total_length) || force) && (cdrom[id].mode_select_phase != MODE_SELECT_PHASE_IDLE))
	{
		cdrom_log("CD-ROM %i: MODE SELECT terminate: %i\n", id, force);
		cdrom[id].current_page_pos = cdrom[id].current_page_len = cdrom[id].block_descriptor_len = 0;
		cdrom[id].total_length = cdrom[id].written_length = 0;
		cdrom[id].mode_select_phase = MODE_SELECT_PHASE_IDLE;
		if (force)
		{
			cdrom_mode_sense_load(id);
		}
		return 1;
	}
	else
	{
		return 0;
	}
}

int cdrom_mode_select_header(uint8_t id, uint8_t val)
{
	if (cdrom[id].current_page_pos == 0)
	{
		cdrom[id].block_descriptor_len = 0;
	}
	else if (cdrom[id].current_page_pos == (cdrom[id].current_page_len - 2))
	{
		if ((cdrom_drives[id].bus_type == CDROM_BUS_SCSI) && (cdrom[id].current_page_len == 8))
		{
			cdrom[id].block_descriptor_len |= ((uint16_t) val) << 8;
			cdrom_log("CD-ROM %i: Position: %02X, value: %02X, block descriptor length: %02X\n", id, cdrom[id].current_page_pos, val, cdrom[id].block_descriptor_len);
		}
	}
	else if (cdrom[id].current_page_pos == (cdrom[id].current_page_len - 1))
	{
		if (cdrom_drives[id].bus_type == CDROM_BUS_SCSI)
		{
			cdrom[id].block_descriptor_len |= (uint16_t) val;
			cdrom_log("CD-ROM %i: Position: %02X, value: %02X, block descriptor length: %02X\n", id, cdrom[id].current_page_pos, val, cdrom[id].block_descriptor_len);
		}
	}

	cdrom[id].current_page_pos++;

	if (cdrom[id].current_page_pos >= cdrom[id].current_page_len)
	{
		cdrom[id].current_page_pos = 0;
		if (cdrom[id].block_descriptor_len)
		{
			cdrom[id].mode_select_phase = MODE_SELECT_PHASE_BLOCK_DESC;
		}
		else
		{
			cdrom[id].mode_select_phase = MODE_SELECT_PHASE_PAGE_HEADER;
		}
	}

	return 1;
}

int cdrom_mode_select_block_desc(uint8_t id)
{
	cdrom[id].current_page_pos++;
	if (cdrom[id].current_page_pos >= 8)
	{
		cdrom[id].current_page_pos = 0;
		cdrom[id].mode_select_phase = MODE_SELECT_PHASE_PAGE_HEADER;
	}
	return 1;
}

static void cdrom_invalid_field_pl(uint8_t id);

int cdrom_mode_select_page_header(uint8_t id, uint8_t val)
{
	if (cdrom[id].current_page_pos == 0)
	{
		cdrom[id].current_page_code = val & 0x3f;
		if (!(cdrom_mode_sense_page_flags[id] & (1LL << cdrom[id].current_page_code)))
		{
			cdrom_log("CD-ROM %i: Trying to modify an unimplemented page: %02X\n", id, cdrom[id].current_page_code);
			cdrom_mode_select_terminate(id, 1);
			cdrom_invalid_field_pl(id);
		}
		cdrom[id].current_page_pos++;
	}
	else if (cdrom[id].current_page_pos == 1)
	{
		cdrom[id].current_page_pos = 0;
		cdrom[id].current_page_len = val;
		cdrom[id].mode_select_phase = MODE_SELECT_PHASE_PAGE;
	}
	return 1;
}

int cdrom_mode_select_page(uint8_t id, uint8_t val)
{
	if (cdrom_mode_sense_pages_changeable[id][cdrom[id].current_page_code][cdrom[id].current_page_pos + 2] != 0xFF)
	{
		if (val != cdrom_mode_sense_pages_saved[id][cdrom[id].current_page_code][cdrom[id].current_page_pos + 2])
		{
			/* Trying to change an unchangeable value. */
			cdrom_log("CD-ROM %i: Trying to change an unchangeable value: [%02X][%02X] = %02X (new: %02X)\n", id, cdrom[id].current_page_code, cdrom[id].current_page_pos + 2, cdrom_mode_sense_pages_saved[id][cdrom[id].current_page_code][cdrom[id].current_page_pos + 2], val);
			cdrom_mode_select_terminate(id, 1);
			cdrom_invalid_field_pl(id);
			return 0;
		}
	}
	else
	{
		if (cdrom[id].current_page_code == 0xE)
		{
			if ((cdrom[id].current_page_pos == 6) || (cdrom[id].current_page_pos == 8))
			{
				if (val > 3)
				{
					/* Trying to set an unsupported audio channel. */
					cdrom_log("CD-ROM %i: Trying to set an unsupported value: [%02X][%02X] = %02X (new: %02X)\n", id, cdrom[id].current_page_code, cdrom[id].current_page_pos, cdrom_mode_sense_pages_saved[id][cdrom[id].current_page_code][cdrom[id].current_page_pos + 2], val);
					return 0;
				}
			}
		}
		cdrom_mode_sense_pages_saved[id][cdrom[id].current_page_code][cdrom[id].current_page_pos + 2] = val;
	}
	cdrom[id].current_page_pos++;
	if (cdrom[id].current_page_pos >= cdrom[id].current_page_len)
	{
		cdrom[id].current_page_pos = 0;
		cdrom[id].mode_select_phase = MODE_SELECT_PHASE_PAGE_HEADER;
	}
	return 1;
}

static void cdrom_command_complete(uint8_t id);

int cdrom_mode_select_write(uint8_t id, uint8_t val)
{
	int ret = 0;
	int ret2 = 0;

	if (id > CDROM_NUM)
	{
		cdrom_log("MODE SELECT: attempted write to wrong CD-ROM drive\n", val);
		return -6;
	}

	if (cdrom[id].total_length == 0)
	{
		cdrom_log("CD-ROM %i: MODE SELECT: attempted write when not initialized (%02X)\n", id, val);
		return -3;
	}

	cdrom[id].written_length++;

	switch (cdrom[id].mode_select_phase)
	{
		case MODE_SELECT_PHASE_IDLE:
			cdrom_log("CD-ROM %i: MODE SELECT idle (%02X)\n", id, val);
			ret = 1;
			break;
		case MODE_SELECT_PHASE_HEADER:
			cdrom_log("CD-ROM %i: MODE SELECT header (%02X)\n", id, val);
			ret = cdrom_mode_select_header(id, val);
			break;
		case MODE_SELECT_PHASE_BLOCK_DESC:
			cdrom_log("CD-ROM %i: MODE SELECT block descriptor (%02X)\n", id, val);
			ret = cdrom_mode_select_block_desc(id);
			break;
		case MODE_SELECT_PHASE_PAGE_HEADER:
			cdrom_log("CD-ROM %i: MODE SELECT page header (%02X)\n", id, val);
			ret = cdrom_mode_select_page_header(id, val);
			break;
		case MODE_SELECT_PHASE_PAGE:
			cdrom_log("CD-ROM %i: MODE SELECT page (%02X)\n", id, val);
			ret = cdrom_mode_select_page(id, val);
			if (cdrom[id].mode_select_phase == MODE_SELECT_PHASE_PAGE_HEADER)
			{
				if (cdrom[id].do_page_save && (cdrom_mode_sense_pages_default[id][cdrom[id].current_page_code][0] & 0x80))
				{
					cdrom_log("CD-ROM %i: Page %i finished, saving it...\n", id, cdrom[id].current_page_code);
					cdrom_mode_sense_save(id);
				}
			}
			break;
		default:
			cdrom_log("CD-ROM %i: MODE SELECT unknown phase (%02X)\n", id, val);
			ret = -4;
			break;
	}

	/* On termination, override the return value, but only if it is 1. */
	ret2 = cdrom_mode_select_terminate(id, 0);
	if (ret2)
	{
		cdrom_command_complete(id);
	}
	if (ret2 && (ret == 1))
	{
		ret = -5;
	}

	return ret;
}

uint8_t cdrom_read_capacity_cdb[12] = {0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static int cdrom_pass_through(uint8_t id, uint32_t *len, uint8_t *cdb, uint8_t *buffer);

int cdrom_read_capacity(uint8_t id, uint8_t *cdb, uint8_t *buffer, uint32_t *len)
{
	int ret = 0;
	int size = 0;

	if (cdrom_drives[id].handler->pass_through)
	{
		ret = cdrom_pass_through(id, len, cdb, buffer);
		if (!ret)
		{
			return 0;
		}
		if (*len == 65534)
		{
			*len = 8;
		}
	}
	else
	{
		size = cdrom_drives[id].handler->size(id) - 1;		/* IMPORTANT: What's returned is the last LBA block. */
		memset(buffer, 0, 8);
		buffer[0] = (size >> 24) & 0xff;
		buffer[1] = (size >> 16) & 0xff;
		buffer[2] = (size >> 8) & 0xff;
		buffer[3] = size & 0xff;
		buffer[6] = 8;				/* 2048 = 0x0800 */
		*len = 8;
	}
	return 1;
}

/*SCSI Mode Sense 6/10*/
uint8_t cdrom_mode_sense_read(uint8_t id, uint8_t page_control, uint8_t page, uint8_t pos)
{
	switch (page_control)
	{
		case 0:
		case 3:
			return cdrom_mode_sense_pages_saved[id][page][pos];
			break;
		case 1:
			return cdrom_mode_sense_pages_changeable[id][page][pos];
			break;
		case 2:
			return cdrom_mode_sense_pages_default[id][page][pos];
			break;
	}

	return 0;
}

uint32_t cdrom_mode_sense(uint8_t id, uint8_t *buf, uint32_t pos, uint8_t type, uint8_t block_descriptor_len)
{
	uint8_t page_control = (type >> 6) & 3;

	int i = 0;
	int j = 0;

	uint8_t msplen;

	type &= 0x3f;

	if (block_descriptor_len)
	{
		buf[pos++] = 1;		/* Density code. */
		buf[pos++] = 0;		/* Number of blocks (0 = all). */
		buf[pos++] = 0;
		buf[pos++] = 0;
		buf[pos++] = 0;		/* Reserved. */
		buf[pos++] = 0;		/* Block length (0x800 = 2048 bytes). */
		buf[pos++] = 8;
		buf[pos++] = 0;
	}

	for (i = 0; i < 0x40; i++)
	{
	        if ((type == GPMODE_ALL_PAGES) || (type == i))
	        {
			if (cdrom_mode_sense_page_flags[id] & (1LL << cdrom[id].current_page_code))
			{
				buf[pos++] = cdrom_mode_sense_read(id, page_control, i, 0);
				msplen = cdrom_mode_sense_read(id, page_control, i, 1);
				buf[pos++] = msplen;
				cdrom_log("CD-ROM %i: MODE SENSE: Page [%02X] length %i\n", id, i, msplen);
				for (j = 0; j < msplen; j++)
				{
					buf[pos++] = cdrom_mode_sense_read(id, page_control, i, 2 + j);
				}
			}
		}
	}

	return pos;
}

void cdrom_update_request_length(uint8_t id, int len, int block_len)
{
	/* For media access commands, make sure the requested DRQ length matches the block length. */
	switch (cdrom[id].current_cdb[0])
	{
		case 0x08:
		case 0x28:
		case 0xa8:
		case 0xb9:
		case 0xbe:
			if (cdrom[id].request_length < block_len)
			{
				cdrom[id].request_length = block_len;
			}
			/* Make sure we respect the limit of how many blocks we can transfer at once. */
			if (cdrom[id].requested_blocks > cdrom_drives[id].max_blocks_at_once)
			{
				cdrom[id].requested_blocks = cdrom_drives[id].max_blocks_at_once;
			}
			cdrom[id].block_total = (cdrom[id].requested_blocks * block_len);
			if (len > cdrom[id].block_total)
			{
				len = cdrom[id].block_total;
			}
			break;
		default:
			cdrom[id].packet_len = len;
			break;
	}
	/* If the DRQ length is odd, and the total remaining length is bigger, make sure it's even. */
	if ((cdrom[id].request_length & 1) && (cdrom[id].request_length < len))
	{
		cdrom[id].request_length &= 0xfffe;
	}
	/* If the DRQ length is smaller or equal in size to the total remaining length, set it to that. */
	if (len <= cdrom[id].request_length)
	{
		cdrom[id].request_length = len;
	}
	return;
}

static int cdrom_is_media_access(uint8_t id)
{
	switch (cdrom[id].current_cdb[0])
	{
		case 0x08:
		case 0x28:
		case 0xa8:
		case 0xb9:
		case 0xbe:
			return 1;
		default:
			return 0;
	}
}

static void cdrom_command_common(uint8_t id)
{
	cdrom[id].status = BUSY_STAT;
	cdrom[id].phase = 1;
	cdrom[id].pos = 0;
	if (cdrom[id].packet_status == CDROM_PHASE_COMPLETE)
	{
		cdrom[id].callback = 20LL * CDROM_TIME;
	}
	else if (cdrom[id].packet_status == CDROM_PHASE_DATA_IN)
	{
		if (cdrom[id].current_cdb[0] == 0x42)
		{
			cdrom_log("CD-ROM %i: READ SUBCHANNEL\n");
			cdrom[id].callback = 1000LL * CDROM_TIME;
		}
		else
		{
			cdrom[id].callback = 60LL * CDROM_TIME;
		}
	}
	else
	{
		cdrom[id].callback = 60LL * CDROM_TIME;
	}
}

static void cdrom_command_complete(uint8_t id)
{
	cdrom[id].packet_status = CDROM_PHASE_COMPLETE;
	cdrom_command_common(id);
}

static void cdrom_command_read(uint8_t id)
{
	cdrom[id].packet_status = CDROM_PHASE_DATA_IN;
	cdrom_command_common(id);
	cdrom[id].total_read = 0;
}

static void cdrom_command_read_dma(uint8_t id)
{
	cdrom[id].packet_status = CDROM_PHASE_DATA_IN_DMA;
	cdrom_command_common(id);
	cdrom[id].total_read = 0;
}

static void cdrom_command_write(uint8_t id)
{
	cdrom[id].packet_status = CDROM_PHASE_DATA_OUT;
	cdrom_command_common(id);
}

static void cdrom_command_write_dma(uint8_t id)
{
	cdrom[id].packet_status = CDROM_PHASE_DATA_OUT_DMA;
	cdrom_command_common(id);
}

static int cdrom_request_length_is_zero(uint8_t id)
{
	if ((cdrom[id].request_length == 0) && (cdrom_drives[id].bus_type < CDROM_BUS_SCSI))
	{
		return 1;
	}
	return 0;
}

static void cdrom_data_command_finish(uint8_t id, int len, int block_len, int alloc_len, int direction)
{
	cdrom_log("CD-ROM %i: Finishing command (%02X): %i, %i, %i, %i, %i\n", id, cdrom[id].current_cdb[0], len, block_len, alloc_len, direction, cdrom[id].request_length);
	cdrom[id].pos=0;
	if (alloc_len >= 0)
	{
		if (alloc_len < len)
		{
			len = alloc_len;
		}
	}
	if (cdrom_request_length_is_zero(id) || (len == 0) || (cdrom_current_mode(id) == 0))
	{
		if (cdrom_drives[id].bus_type != CDROM_BUS_SCSI)
		{
			cdrom[id].init_length = 0;
		}
		cdrom_command_complete(id);
	}
	else
	{
		if (cdrom_current_mode(id) == 2)
		{
			if (direction == 0)
			{
				if (cdrom_drives[id].bus_type != CDROM_BUS_SCSI)
				{
					cdrom[id].init_length = alloc_len;
				}
				cdrom_command_read_dma(id);
			}
			else
			{
				cdrom_command_write_dma(id);
			}
		}
		else
		{
			cdrom_update_request_length(id, len, block_len);
			if (direction == 0)
			{
				cdrom_command_read(id);
			}
			else
			{
				cdrom_command_write(id);
			}
		}
	}
	
	cdrom_log("CD-ROM %i: Status: %i, cylinder %i, packet length: %i, position: %i, phase: %i\n", id, cdrom[id].packet_status, cdrom[id].request_length, cdrom[id].packet_len, cdrom[id].pos, cdrom[id].phase);
}

static void cdrom_sense_clear(int id, int command)
{
	cdrom[id].previous_command = command;
	cdrom_sense_key = cdrom_asc = cdrom_ascq = 0;
}

static void cdrom_cmd_error(uint8_t id)
{
	SCSIPhase = SCSI_PHASE_STATUS;
	cdrom[id].error = ((cdrom_sense_key & 0xf) << 4) | ABRT_ERR;
	if (cdrom[id].unit_attention)
	{
		cdrom[id].error |= MCR_ERR;
	}
	cdrom[id].status = READY_STAT | ERR_STAT;
	cdrom[id].phase = 3;
	cdrom[id].packet_status = 0x80;
	cdrom[id].callback = 50LL * CDROM_TIME;
	cdrom_log("CD-ROM %i: ERROR: %02X/%02X/%02X\n", id, cdrom_sense_key, cdrom_asc, cdrom_ascq);
}

static void cdrom_unit_attention(uint8_t id)
{
	SCSIPhase = SCSI_PHASE_STATUS;
	cdrom[id].error = (SENSE_UNIT_ATTENTION << 4) | ABRT_ERR;
	if (cdrom[id].unit_attention)
	{
		cdrom[id].error |= MCR_ERR;
	}
	cdrom[id].status = READY_STAT | ERR_STAT;
	cdrom[id].phase = 3;
	cdrom[id].packet_status = 0x80;
	cdrom[id].callback = 50LL * CDROM_TIME;
	cdrom_log("CD-ROM %i: UNIT ATTENTION\n", id);
}

static void cdrom_not_ready(uint8_t id)
{
	cdrom_sense_key = SENSE_NOT_READY;
	cdrom_asc = ASC_MEDIUM_NOT_PRESENT;
	cdrom_ascq = 0;
	cdrom_cmd_error(id);
}

static void cdrom_invalid_lun(uint8_t id)
{
	cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
	cdrom_asc = ASC_INV_LUN;
	cdrom_ascq = 0;
	cdrom_cmd_error(id);
}

static void cdrom_illegal_opcode(uint8_t id)
{
	cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
	cdrom_asc = ASC_ILLEGAL_OPCODE;
	cdrom_ascq = 0;
	cdrom_cmd_error(id);
}

static void cdrom_lba_out_of_range(uint8_t id)
{
	cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
	cdrom_asc = ASC_LBA_OUT_OF_RANGE;
	cdrom_ascq = 0;
	cdrom_cmd_error(id);
}

static void cdrom_invalid_field(uint8_t id)
{
	cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
	cdrom_asc = ASC_INV_FIELD_IN_CMD_PACKET;
	cdrom_ascq = 0;
	cdrom_cmd_error(id);
	cdrom[id].status = 0x53;
}

static void cdrom_invalid_field_pl(uint8_t id)
{
	cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
	cdrom_asc = ASC_INV_FIELD_IN_PARAMETER_LIST;
	cdrom_ascq = 0;
	cdrom_cmd_error(id);
	cdrom[id].status = 0x53;
}

static void cdrom_illegal_mode(uint8_t id)
{
	cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
	cdrom_asc = ASC_ILLEGAL_MODE_FOR_THIS_TRACK;
	cdrom_ascq = 0;
	cdrom_cmd_error(id);
}

static void cdrom_incompatible_format(uint8_t id)
{
	cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
	cdrom_asc = ASC_INCOMPATIBLE_FORMAT;
	cdrom_ascq = 0;
	cdrom_cmd_error(id);
}

static void cdrom_data_phase_error(uint8_t id)
{
	cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
	cdrom_asc = ASC_DATA_PHASE_ERROR;
	cdrom_ascq = 0;
	cdrom_cmd_error(id);
}

static int cdrom_pass_through(uint8_t id, uint32_t *len, uint8_t *cdb, uint8_t *buffer)
{
	int ret = 0;
	uint8_t temp_cdb[16];

	memset(temp_cdb, 0, 16);

	if (cdb[0] == 8)
	{
		temp_cdb[0] = 0x28;
		temp_cdb[8] = cdb[4];
		temp_cdb[3] = cdb[1];
		temp_cdb[4] = cdb[2];
		temp_cdb[5] = cdb[3];
	}
	else
	{
		memcpy(temp_cdb, cdb, 16);
	}

	ret = cdrom_drives[id].handler->pass_through(id, temp_cdb, buffer, len);
	cdrom_log("CD-ROM %i: Data from pass through:  %02X %02X %02X %02X %02X %02X %02X %02X\n", id, buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7]);
	cdrom_log("CD-ROM %i: Returned value: %i\n", id, ret);

	if (!ret)
	{
		/* Command failed with OS error code, return illegal opcode. */
		cdrom_log("CD-ROM %i: Command failed with OS error code, return illegal opcode.\n", id);
		cdrom_illegal_opcode(id);
		return 0;
	}
	else
	{
		if ((cdrom_sense_key != 0) || (cdrom_asc != 0) || (cdrom_ascq != 0))
		{
			/* Command failed with sense, error with that sense. */
			cdrom_log("CD-ROM %i: Command failed with sense, error with that sense (%02X/%02X/%02X).\n", id, cdrom_sense_key, cdrom_asc, cdrom_ascq);
			cdrom_cmd_error(id);
			return 0;
		}
		else
		{
			/* Command was performed successfully. */
			cdrom_log("CD-ROM %i: Command was performed successfully.\n", id);
			return 1;
		}
	}
}

void cdrom_update_cdb(uint8_t *cdb, int lba_pos, int number_of_blocks)
{
	int temp = 0;

	switch(cdb[0])
	{
		case GPCMD_READ_6:
			cdb[1] = (lba_pos >> 16) & 0xff;
			cdb[2] = (lba_pos >> 8) & 0xff;
			cdb[3] = lba_pos & 0xff;
			break;

		case GPCMD_READ_10:
			cdb[2] = (lba_pos >> 24) & 0xff;
			cdb[3] = (lba_pos >> 16) & 0xff;
			cdb[4] = (lba_pos >> 8) & 0xff;
			cdb[5] = lba_pos & 0xff;
			cdb[7] = (number_of_blocks >> 8) & 0xff;
			cdb[8] = number_of_blocks & 0xff;
			break;

		case GPCMD_READ_12:
			cdb[2] = (lba_pos >> 24) & 0xff;
			cdb[3] = (lba_pos >> 16) & 0xff;
			cdb[4] = (lba_pos >> 8) & 0xff;
			cdb[5] = lba_pos & 0xff;
			cdb[6] = (number_of_blocks >> 24) & 0xff;
			cdb[7] = (number_of_blocks >> 16) & 0xff;
			cdb[8] = (number_of_blocks >> 8) & 0xff;
			cdb[9] = number_of_blocks & 0xff;
			break;
			
		case GPCMD_READ_CD_MSF:
			temp = cdrom_lba_to_msf_accurate(lba_pos);
			cdb[3] = (temp >> 16) & 0xff;
			cdb[4] = (temp >> 8) & 0xff;
			cdb[5] = temp & 0xff;

			temp = cdrom_lba_to_msf_accurate(lba_pos + number_of_blocks - 1);
			cdb[6] = (temp >> 16) & 0xff;
			cdb[7] = (temp >> 8) & 0xff;
			cdb[8] = temp & 0xff;
			break;			

		case GPCMD_READ_CD:
			cdb[2] = (lba_pos >> 24) & 0xff;
			cdb[3] = (lba_pos >> 16) & 0xff;
			cdb[4] = (lba_pos >> 8) & 0xff;
			cdb[5] = lba_pos & 0xff;
			cdb[6] = (number_of_blocks >> 16) & 0xff;
			cdb[7] = (number_of_blocks >> 8) & 0xff;
			cdb[8] = number_of_blocks & 0xff;
			break;
	}
}

#define cdbufferb cdrom[id].buffer

int cdrom_read_data(uint8_t id, int msf, int type, int flags, uint32_t *len)
{
	int ret = 0;
	int cdsize = 0;

	int i = 0;
	int temp_len = 0;

	int last_valid_data_pos = 0;

	if (cdrom_drives[id].handler->pass_through)
	{
		cdsize = cdrom_drives[id].handler->size(id);

		ret = cdrom_pass_through(id, len, cdrom[id].current_cdb, cdbufferb + cdrom[id].data_pos);
		cdrom[id].data_pos += *len;

		if (!ret)
		{
			return 0;
		}

		if (cdrom[id].sector_pos > (cdsize - 1))
		{
			/* cdrom_log("CD-ROM %i: Trying to read beyond the end of disc\n", id); */
			cdrom_lba_out_of_range(id);
			return 0;
		}

		cdrom[id].old_len = *len;
	}
	else
	{
		if (cdrom[id].sector_pos > (cdrom_drives[id].handler->size(id) - 1))
		{
			/* cdrom_log("CD-ROM %i: Trying to read beyond the end of disc\n", id); */
			cdrom_lba_out_of_range(id);
			return 0;
		}

		cdrom[id].old_len = 0;
		*len = 0;

		for (i = 0; i < cdrom[id].requested_blocks; i++)
		{
			ret = cdrom_drives[id].handler->readsector_raw(id, cdbufferb + cdrom[id].data_pos, cdrom[id].sector_pos + i, msf, type, flags, &temp_len);

			last_valid_data_pos = cdrom[id].data_pos;

			cdrom[id].data_pos += temp_len;
			cdrom[id].old_len += temp_len;

			*len += temp_len;

			if (!ret)
			{
				cdrom_illegal_mode(id);
				return 0;
			}
		}

		cdrom_log("CD-ROM %i: Data from raw sector read:  %02X %02X %02X %02X %02X %02X %02X %02X\n", id, cdbufferb[last_valid_data_pos + 0], cdbufferb[last_valid_data_pos + 1], cdbufferb[last_valid_data_pos + 2], cdbufferb[last_valid_data_pos + 3], cdbufferb[last_valid_data_pos + 4], cdbufferb[last_valid_data_pos + 5], cdbufferb[last_valid_data_pos + 6], cdbufferb[last_valid_data_pos + 7]);
	}

	return 1;
}

int cdrom_read_blocks(uint8_t id, uint32_t *len, int first_batch)
{
	int ret = 0;

	int msf = 0;
	
	int type = 0;
	int flags = 0;
	
	if (cdrom[id].current_cdb[0] == 0xb9)
	{
		msf = 1;
	}
	
	if ((cdrom[id].current_cdb[0] == 0xb9) || (cdrom[id].current_cdb[0] == 0xbe))
	{
		type = (cdrom[id].current_cdb[1] >> 2) & 7;
		flags = cdrom[id].current_cdb[9] || (((uint32_t) cdrom[id].current_cdb[10]) << 8);
	}
	else
	{
		type = 8;
		flags = 0x10;
	}

	cdrom[id].data_pos = 0;
	
	if (!cdrom[id].sector_len)
	{
		cdrom_command_complete(id);
		return -1;
	}

	cdrom_log("Reading %i blocks starting from %i...\n", cdrom[id].requested_blocks, cdrom[id].sector_pos);

	cdrom_update_cdb(cdrom[id].current_cdb, cdrom[id].sector_pos, cdrom[id].requested_blocks);

	ret = cdrom_read_data(id, msf, type, flags, len);

	cdrom_log("Read %i bytes of blocks...\n", *len);

	if (!ret || ((cdrom[id].old_len != *len) && !first_batch))
	{
		if ((cdrom[id].old_len != *len) && !first_batch)
		{
			cdrom_illegal_mode(id);
		}

		return 0;
	}

	cdrom[id].sector_pos += cdrom[id].requested_blocks;
	cdrom[id].sector_len -= cdrom[id].requested_blocks;

	return 1;
}

/*SCSI Get Configuration*/
uint8_t cdrom_set_profile(uint8_t *buf, uint8_t *index, uint16_t profile)
{
	uint8_t *buf_profile = buf + 12; /* start of profiles */

	buf_profile += ((*index) * 4); /* start of indexed profile */
	buf_profile[0] = (profile >> 8) & 0xff;
	buf_profile[1] = profile & 0xff;
	buf_profile[2] = ((buf_profile[0] == buf[6]) && (buf_profile[1] == buf[7]));

	/* each profile adds 4 bytes to the response */
	(*index)++;
	buf[11] += 4; /* Additional Length */

	return 4;
}

/*SCSI Read DVD Structure*/
static int cdrom_read_dvd_structure(uint8_t id, int format, const uint8_t *packet, uint8_t *buf)
{
	int layer = packet[6];
	uint64_t total_sectors;

	switch (format)
	{
        	case 0x00:	/* Physical format information */
			total_sectors = (uint64_t) cdrom_drives[id].handler->size(id);

	        if (layer != 0)
			{
				cdrom_invalid_field(id);
				return 0;
			}

                	total_sectors >>= 2;
			if (total_sectors == 0)
			{
				/* return -ASC_MEDIUM_NOT_PRESENT; */
				cdrom_not_ready(id);
				return 0;
			}

			buf[4] = 1;	/* DVD-ROM, part version 1 */
			buf[5] = 0xf;	/* 120mm disc, minimum rate unspecified */
			buf[6] = 1;	/* one layer, read-only (per MMC-2 spec) */
			buf[7] = 0;	/* default densities */

			/* FIXME: 0x30000 per spec? */
			buf[8] = buf[9] = buf[10] = buf[11] = 0; /* start sector */
			buf[12] = (total_sectors >> 24) & 0xff; /* end sector */
			buf[13] = (total_sectors >> 16) & 0xff;
			buf[14] = (total_sectors >> 8) & 0xff;
			buf[15] = total_sectors & 0xff;

			buf[16] = (total_sectors >> 24) & 0xff; /* l0 end sector */
			buf[17] = (total_sectors >> 16) & 0xff;
			buf[18] = (total_sectors >> 8) & 0xff;
			buf[19] = total_sectors & 0xff;

			/* Size of buffer, not including 2 byte size field */				
			buf[0] = ((2048 +2 ) >> 8) & 0xff;
			buf[1] = (2048 + 2) & 0xff;

			/* 2k data + 4 byte header */
			return (2048 + 4);

		case 0x01:	/* DVD copyright information */
			buf[4] = 0; /* no copyright data */
			buf[5] = 0; /* no region restrictions */

			/* Size of buffer, not including 2 byte size field */
			buf[0] = ((4 + 2) >> 8) & 0xff;
			buf[1] = (4 + 2) & 0xff;			

			/* 4 byte header + 4 byte data */
			return (4 + 4);

		case 0x03:	/* BCA information - invalid field for no BCA info */
			cdrom_invalid_field(id);
			return 0;

		case 0x04:	/* DVD disc manufacturing information */
				/* Size of buffer, not including 2 byte size field */
			buf[0] = ((2048 + 2) >> 8) & 0xff;
			buf[1] = (2048 + 2) & 0xff;

			/* 2k data + 4 byte header */
			return (2048 + 4);

		case 0xff:
			/*
			 * This lists all the command capabilities above.  Add new ones
			 * in order and update the length and buffer return values.
			 */

			buf[4] = 0x00; /* Physical format */
			buf[5] = 0x40; /* Not writable, is readable */
			buf[6] = ((2048 + 4) >> 8) & 0xff;
			buf[7] = (2048 + 4) & 0xff;

			buf[8] = 0x01; /* Copyright info */
			buf[9] = 0x40; /* Not writable, is readable */
			buf[10] = ((4 + 4) >> 8) & 0xff;
			buf[11] = (4 + 4) & 0xff;

			buf[12] = 0x03; /* BCA info */
			buf[13] = 0x40; /* Not writable, is readable */
			buf[14] = ((188 + 4) >> 8) & 0xff;
			buf[15] = (188 + 4) & 0xff;

			buf[16] = 0x04; /* Manufacturing info */
			buf[17] = 0x40; /* Not writable, is readable */
			buf[18] = ((2048 + 4) >> 8) & 0xff;
			buf[19] = (2048 + 4) & 0xff;

			/* Size of buffer, not including 2 byte size field */
			buf[6] = ((16 + 2) >> 8) & 0xff;
			buf[7] = (16 + 2) & 0xff;

			/* data written + 4 byte header */
			return (16 + 4);

		default: /* TODO: formats beyond DVD-ROM requires */
			cdrom_invalid_field(id);
			return 0;
	}
}

void cdrom_insert(uint8_t id)
{
	cdrom[id].unit_attention = 1;
}

/*SCSI Sense Initialization*/
void cdrom_sense_code_ok(uint8_t id)
{	
	cdrom_sense_key = SENSE_NONE;
	cdrom_asc = 0;
	cdrom_ascq = 0;
}

int cdrom_pre_execution_check(uint8_t id, uint8_t *cdb)
{
	int ready = 0;

	if (cdrom_drives[id].bus_type == CDROM_BUS_SCSI)
	{
		if (((cdrom[id].request_length >> 5) & 7) != cdrom_drives[id].scsi_device_lun)
		{
			cdrom_log("CD-ROM %i: Attempting to execute a unknown command targeted at SCSI LUN %i\n", id, ((cdrom[id].request_length >> 5) & 7));
			cdrom_invalid_lun(id);
			return 0;
		}
	}

	if (!(cdrom_command_flags[cdb[0]] & IMPLEMENTED))
	{
		cdrom_log("CD-ROM %i: Attempting to execute unknown command %02X over %s\n", id, cdb[0], (cdrom_drives[id].bus_type == CDROM_BUS_SCSI) ? "SCSI" : ((cdrom_drives[id].bus_type == CDROM_BUS_ATAPI_PIO_AND_DMA) ? "ATAPI PIO/DMA" : "ATAPI PIO"));

		cdrom_illegal_opcode(id);
		return 0;
	}

	if ((cdrom_drives[id].bus_type < CDROM_BUS_SCSI) && (cdrom_command_flags[cdb[0]] & SCSI_ONLY))
	{
		cdrom_log("CD-ROM %i: Attempting to execute SCSI-only command %02X over ATAPI\n", id, cdb[0]);
		cdrom_illegal_opcode(id);
		return 0;
	}

	if ((cdrom_drives[id].bus_type == CDROM_BUS_SCSI) && (cdrom_command_flags[cdb[0]] & ATAPI_ONLY))
	{
		cdrom_log("CD-ROM %i: Attempting to execute ATAPI-only command %02X over SCSI\n", id, cdb[0]);
		cdrom_illegal_opcode(id);
		return 0;
	}

	if ((cdrom_drives[id].handler->status(id) == CD_STATUS_PLAYING) || (cdrom_drives[id].handler->status(id) == CD_STATUS_PAUSED))
	{
		ready = 1;
		goto skip_ready_check;
	}

	if (cdrom_drives[id].handler->medium_changed(id))
	{
		/* cdrom_log("CD-ROM %i: Medium has changed...\n", id); */
		cdrom_insert(id);
	}

	ready = cdrom_drives[id].handler->ready(id);

skip_ready_check:
	if (!ready && cdrom[id].unit_attention)
	{
		/* If the drive is not ready, there is no reason to keep the
		   UNIT ATTENTION condition present, as we only use it to mark
		   disc changes. */
		cdrom[id].unit_attention = 0;
	}

	/* If the UNIT ATTENTION condition is set and the command does not allow
		execution under it, error out and report the condition. */
	if (cdrom[id].unit_attention == 1)
	{
		/* Only increment the unit attention phase if the command can not pass through it. */
		if (!(cdrom_command_flags[cdb[0]] & ALLOW_UA))
		{
			/* cdrom_log("CD-ROM %i: Unit attention now 2\n", id); */
			cdrom[id].unit_attention = 2;
			cdrom_log("CD-ROM %i: UNIT ATTENTION: Command %02X not allowed to pass through\n", id, cdb[0]);
			cdrom_unit_attention(id);
			return 0;
		}
	}
	else if (cdrom[id].unit_attention == 2)
	{
		if (cdb[0] != GPCMD_REQUEST_SENSE)
		{
			/* cdrom_log("CD-ROM %i: Unit attention now 0\n", id); */
			cdrom[id].unit_attention = 0;
		}
	}

	/* Unless the command is REQUEST SENSE, clear the sense. This will *NOT*
		the UNIT ATTENTION condition if it's set. */
	if (cdb[0] != GPCMD_REQUEST_SENSE)
	{
		cdrom_sense_clear(id, cdb[0]);
	}

	/* Next it's time for NOT READY. */
	if (!ready)
	{
		cdrom[id].media_status = MEC_MEDIA_REMOVAL;
	}
	else
	{
		cdrom[id].media_status = (cdrom[id].unit_attention) ? MEC_NEW_MEDIA : MEC_NO_CHANGE;
	}

	if ((cdrom_command_flags[cdb[0]] & CHECK_READY) && !ready)
	{
		cdrom_log("CD-ROM %i: Not ready (%02X)\n", id, cdb[0]);
		cdrom_not_ready(id);
		return 0;
	}

	cdrom_log("CD-ROM %i: Continuing with command %02X\n", id, cdb[0]);
		
	return 1;
}

void cdrom_clear_callback(uint8_t channel)
{
	uint8_t id = atapi_cdrom_drives[channel];

	if (id <= CDROM_NUM)
	{
		cdrom[id].callback = 0LL;
	}
}

static void cdrom_seek(uint8_t id, uint32_t pos)
{
        /* cdrom_log("CD-ROM %i: Seek %08X\n", id, pos); */
        cdrom[id].seek_pos   = pos;
	if (cdrom_drives[id].handler->stop)
	{
        	cdrom_drives[id].handler->stop(id);
	}
}

static void cdrom_rezero(uint8_t id)
{
	if (cdrom_drives[id].handler->stop)
	{
		cdrom_drives[id].handler->stop(id);
	}
	cdrom[id].sector_pos = cdrom[id].sector_len = 0;
	cdrom_seek(id, 0);
}

void cdrom_reset(uint8_t id)
{
	cdrom_rezero(id);
	cdrom[id].status = 0;
	cdrom[id].callback = 0LL;
	cdrom[id].packet_status = 0xff;
	cdrom[id].unit_attention = 0;
}

int cdrom_playing_completed(uint8_t id)
{
	cdrom[id].prev_status = cdrom[id].cd_status;
	cdrom[id].cd_status = cdrom_drives[id].handler->status(id);
	if (((cdrom[id].prev_status == CD_STATUS_PLAYING) || (cdrom[id].prev_status == CD_STATUS_PAUSED)) && ((cdrom[id].cd_status != CD_STATUS_PLAYING) && (cdrom[id].cd_status != CD_STATUS_PAUSED)))
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

void cdrom_request_sense(uint8_t id, uint8_t *buffer, uint8_t alloc_length)
{				
	/*Will return 18 bytes of 0*/
	if (alloc_length != 0)
	{
		memset(buffer, 0, alloc_length);
		memcpy(buffer, cdrom[id].sense, alloc_length);
	}

	buffer[0] = 0x70;

	if ((cdrom_sense_key > 0) && ((cdrom[id].cd_status < CD_STATUS_PLAYING) || (cdrom[id].cd_status == CD_STATUS_STOPPED)) && cdrom_playing_completed(id))
	{
		buffer[2]=SENSE_ILLEGAL_REQUEST;
		buffer[12]=ASC_AUDIO_PLAY_OPERATION;
		buffer[13]=ASCQ_AUDIO_PLAY_OPERATION_COMPLETED;
	}
	else if ((cdrom_sense_key == 0) && (cdrom[id].cd_status >= CD_STATUS_PLAYING) && (cdrom[id].cd_status != CD_STATUS_STOPPED))
	{
		buffer[2]=SENSE_ILLEGAL_REQUEST;
		buffer[12]=ASC_AUDIO_PLAY_OPERATION;
		buffer[13]=(cdrom[id].cd_status == CD_STATUS_PLAYING) ? ASCQ_AUDIO_PLAY_OPERATION_IN_PROGRESS : ASCQ_AUDIO_PLAY_OPERATION_PAUSED;
	}
	else
	{
		if (cdrom[id].unit_attention && (cdrom_sense_key == 0))
		{
			buffer[2]=SENSE_UNIT_ATTENTION;
			buffer[12]=ASC_MEDIUM_MAY_HAVE_CHANGED;
			buffer[13]=0;
		}
	}

	cdrom_log("CD-ROM %i: Reporting sense: %02X %02X %02X\n", id, buffer[2], buffer[12], buffer[13]);

	if (buffer[2] == SENSE_UNIT_ATTENTION)
	{
		/* If the last remaining sense is unit attention, clear
		   that condition. */
		cdrom[id].unit_attention = 0;
	}

	/* Clear the sense stuff as per the spec. */
	cdrom_sense_clear(id, GPCMD_REQUEST_SENSE);
}

void cdrom_request_sense_for_scsi(uint8_t id, uint8_t *buffer, uint8_t alloc_length)
{
	int ready = 0;

	if (cdrom_drives[id].handler->medium_changed(id))
	{
		/* cdrom_log("CD-ROM %i: Medium has changed...\n", id); */
		cdrom_insert(id);
	}

	ready = cdrom_drives[id].handler->ready(id);

	if (!ready && cdrom[id].unit_attention)
	{
		/* If the drive is not ready, there is no reason to keep the
		   UNIT ATTENTION condition present, as we only use it to mark
		   disc changes. */
		cdrom[id].unit_attention = 0;
	}

	/* Do *NOT* advance the unit attention phase. */

	cdrom_request_sense(id, buffer, alloc_length);
}

void cdrom_set_buf_len(uint8_t id, int32_t *BufLen, uint32_t *src_len)
{
	if (cdrom_drives[id].bus_type == CDROM_BUS_SCSI)
	{
		if (*BufLen == -1)
		{
			*BufLen = *src_len;
		}
		else
		{
			*BufLen = MIN(*src_len, *BufLen);
			*src_len = *BufLen;
		}
		cdrom_log("CD-ROM %i: Actual transfer length: %i\n", id, *BufLen);
	}
}

void cdrom_buf_alloc(uint8_t id, uint32_t len)
{
	cdrom_log("CD-ROM %i: Allocated buffer length: %i\n", id, len);
	cdbufferb = (uint8_t *) malloc(len);
}

void cdrom_buf_free(uint8_t id)
{
	if (cdbufferb) {
		cdrom_log("CD-ROM %i: Freeing buffer...\n", id);
		free(cdbufferb);
		cdbufferb = NULL;
	}
}

void cdrom_command(uint8_t id, uint8_t *cdb)
{
	uint32_t len;
	int msf;
	int pos=0;
	uint32_t max_len;
	uint32_t used_len;
	unsigned idx = 0;
	unsigned size_idx;
	unsigned preamble_len;
	int toc_format;
	uint32_t alloc_length;
	uint8_t index = 0;
	int block_desc = 0;
	int format = 0;
	int ret;
	int real_pos;
	int track = 0;
	char device_identify[9] = { '8', '6', 'B', '_', 'C', 'D', '0', '0', 0 };
	char device_identify_ex[15] = { '8', '6', 'B', '_', 'C', 'D', '0', '0', ' ', 'v', '1', '.', '0', '0', 0 };
	int32_t blen = 0;
	int32_t *BufLen;

#if 1
	int CdbLength;
#endif
	if (cdrom_drives[id].bus_type == CDROM_BUS_SCSI)
	{
		BufLen = &SCSIDevices[cdrom_drives[id].scsi_device_id][cdrom_drives[id].scsi_device_lun].BufferLength;
		cdrom[id].status &= ~ERR_STAT;
	}
	else
	{
		BufLen = &blen;
		cdrom[id].error = 0;
	}

	cdrom[id].packet_len = 0;
	cdrom[id].request_pos = 0;

	device_identify[7] = id + 0x30;

	device_identify_ex[7] = id + 0x30;
	device_identify_ex[10] = EMU_VERSION[0];
	device_identify_ex[12] = EMU_VERSION[2];
	device_identify_ex[13] = EMU_VERSION[3];
	
	cdrom[id].data_pos = 0;

	memcpy(cdrom[id].current_cdb, cdb, cdrom[id].cdb_len);

	cdrom[id].cd_status = cdrom_drives[id].handler->status(id);

	if (cdb[0] != 0)
	{
		cdrom_log("CD-ROM %i: Command 0x%02X, Sense Key %02X, Asc %02X, Ascq %02X, %i, Unit attention: %i\n", id, cdb[0], cdrom_sense_key, cdrom_asc, cdrom_ascq, ins, cdrom[id].unit_attention);
		cdrom_log("CD-ROM %i: Request length: %04X\n", id, cdrom[id].request_length);

#if 1
		for (CdbLength = 1; CdbLength < cdrom[id].cdb_len; CdbLength++)
		{
			cdrom_log("CD-ROM %i: CDB[%d] = %d\n", id, CdbLength, cdb[CdbLength]);
		}
#endif
	}
	
	msf = cdb[1] & 2;
	cdrom[id].sector_len = 0;

	SCSIPhase = SCSI_PHASE_STATUS;

	/* This handles the Not Ready/Unit Attention check if it has to be handled at this point. */
	if (cdrom_pre_execution_check(id, cdb) == 0)
	{
		return;
	}

	cdrom_buf_free(id);

	switch (cdb[0])
	{
		case GPCMD_TEST_UNIT_READY:
			SCSIPhase = SCSI_PHASE_STATUS;
			cdrom_command_complete(id);
			break;

		case GPCMD_REZERO_UNIT:
			if (cdrom_drives[id].handler->stop)
			{
				cdrom_drives[id].handler->stop(id);
			}
			cdrom[id].sector_pos = cdrom[id].sector_len = 0;
			cdrom_seek(id, 0);
			SCSIPhase = SCSI_PHASE_STATUS;
			break;

		case GPCMD_REQUEST_SENSE:
			/* If there's a unit attention condition and there's a buffered not ready, a standalone REQUEST SENSE
			   should forget about the not ready, and report unit attention straight away. */
			SCSIPhase = SCSI_PHASE_DATA_IN;
			max_len = cdb[4];
			cdrom_buf_alloc(id, 256);
			cdrom_set_buf_len(id, BufLen, &max_len);
			cdrom_request_sense(id, cdbufferb, max_len);
			cdrom_data_command_finish(id, 18, 18, cdb[4], 0);
			break;

		case GPCMD_SET_SPEED:
		case GPCMD_SET_SPEED_ALT:
			SCSIPhase = SCSI_PHASE_STATUS;
			cdrom_command_complete(id);
			break;

		case GPCMD_MECHANISM_STATUS:
			SCSIPhase = SCSI_PHASE_DATA_IN;
			len = (cdb[7] << 16) | (cdb[8] << 8) | cdb[9];

			cdrom_buf_alloc(id, 8);

			cdrom_set_buf_len(id, BufLen, &len);

 			memset(cdbufferb, 0, 8);
			cdbufferb[5] = 1;

			cdrom_data_command_finish(id, 8, 8, len, 0);
			break;

		case GPCMD_READ_TOC_PMA_ATIP:
			cdrom[id].toctimes++;

			SCSIPhase = SCSI_PHASE_DATA_IN;
			
			max_len = cdb[7];
			max_len <<= 8;
			max_len |= cdb[8];

			cdrom_buf_alloc(id, 65536);

			if (cdrom_drives[id].handler->pass_through)
			{
				ret = cdrom_pass_through(id, &len, cdrom[id].current_cdb, cdbufferb);
				if (!ret)
				{
					/* return; */
					cdrom_sense_key = cdrom_asc = cdrom_ascq = 0;
					goto cdrom_readtoc_fallback;
				}
				alloc_length = cdbufferb[0];
				alloc_length <<= 8;
				alloc_length |= cdbufferb[1];
				alloc_length += 2;
				len = MIN(alloc_length, len);

				cdrom_set_buf_len(id, BufLen, &len);
			}
			else
			{
cdrom_readtoc_fallback:
				toc_format = cdb[2] & 0xf;

				if (toc_format == 0)
				{
					toc_format = (cdb[9] >> 6) & 3;
				}

				switch (toc_format)
				{
					case 0: /*Normal*/
						len = cdrom_drives[id].handler->readtoc(id, cdbufferb, cdb[6], msf, max_len, 0);
						break;
					case 1: /*Multi session*/
						len = cdrom_drives[id].handler->readtoc_session(id, cdbufferb, msf, max_len);
						cdbufferb[0] = 0; cdbufferb[1] = 0xA;
						break;
					case 2: /*Raw*/
						len = cdrom_drives[id].handler->readtoc_raw(id, cdbufferb, max_len);
						break;
					default:
						cdrom_invalid_field(id);
						return;
				}
			}

			if (len > max_len)
			{
				len = max_len;

				cdbufferb[0] = ((len - 2) >> 8) & 0xff;
				cdbufferb[1] = (len - 2) & 0xff;
			}

			cdrom_set_buf_len(id, BufLen, &len);

			cdrom_data_command_finish(id, len, len, len, 0);
			/* cdrom_log("CD-ROM %i: READ_TOC_PMA_ATIP format %02X, length %i (%i)\n", id, toc_format, ide->cylinder, cdbufferb[1]); */
			return;
                
		case GPCMD_READ_CD_OLD:
			cdrom[id].current_cdb[0] = 0xbe;		/* IMPORTANT: Convert the command to new read CD for pass through purposes. */
		case GPCMD_READ_6:
		case GPCMD_READ_10:
		case GPCMD_READ_12:
		case GPCMD_READ_CD:
		case GPCMD_READ_CD_MSF:
			SCSIPhase = SCSI_PHASE_DATA_IN;
			alloc_length = 2048;

			switch(cdb[0])
			{
				case GPCMD_READ_6:
					cdrom[id].sector_len = cdb[4];
					cdrom[id].sector_pos = ((((uint32_t) cdb[1]) & 0x1f) << 16) | (((uint32_t) cdb[2]) << 8) | ((uint32_t) cdb[3]);
					msf = 0;
					break;
				case GPCMD_READ_10:
					cdrom[id].sector_len = (cdb[7] << 8) | cdb[8];
					cdrom[id].sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
					cdrom_log("CD-ROM %i: Length: %i, LBA: %i\n", id, cdrom[id].sector_len, cdrom[id].sector_pos);
					msf = 0;
					break;
				case GPCMD_READ_12:
					cdrom[id].sector_len = (((uint32_t) cdb[6]) << 24) | (((uint32_t) cdb[7]) << 16) | (((uint32_t) cdb[8]) << 8) | ((uint32_t) cdb[9]);
					cdrom[id].sector_pos = (((uint32_t) cdb[2]) << 24) | (((uint32_t) cdb[3]) << 16) | (((uint32_t) cdb[4]) << 8) | ((uint32_t) cdb[5]);
					msf = 0;
					break;
				case GPCMD_READ_CD_MSF:
					/* cdrom_log("CD-ROM %i: Read CD MSF: Start MSF %02X%02X%02X End MSF %02X%02X%02X Flags %02X\n", id, cdb[3], cdb[4], cdb[5], cdb[6], cdb[7], cdb[8], cdb[9]); */
					alloc_length = 2856;
					cdrom[id].sector_len = MSFtoLBA(cdb[6], cdb[7], cdb[8]);
					cdrom[id].sector_pos = MSFtoLBA(cdb[3], cdb[4], cdb[5]);

					cdrom[id].sector_len -= cdrom[id].sector_pos;
					cdrom[id].sector_len++;
					msf = 1;
					break;
				case GPCMD_READ_CD_OLD:
				case GPCMD_READ_CD:
					/* cdrom_log("CD-ROM %i: Read CD: Start LBA %02X%02X%02X%02X Length %02X%02X%02X Flags %02X\n", id, cdb[2], cdb[3], cdb[4], cdb[5], cdb[6], cdb[7], cdb[8], cdb[9]); */
					alloc_length = 2856;
					cdrom[id].sector_len = (cdb[6] << 16) | (cdb[7] << 8) | cdb[8];
					cdrom[id].sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];

					msf = 0;
					break;
			}

			if (!cdrom[id].sector_len)
			{
				SCSIPhase = SCSI_PHASE_STATUS;
				/* cdrom_log("CD-ROM %i: All done - callback set\n", id); */
				cdrom[id].packet_status = CDROM_PHASE_COMPLETE;
				cdrom[id].callback = 20LL * CDROM_TIME;
				break;
			}

			max_len = cdrom[id].sector_len;
			/* if (cdrom_drives[id].bus_type == CDROM_BUS_SCSI) */
			if (cdrom_current_mode(id) == 2)
			{
				cdrom[id].requested_blocks = max_len;
			}
			else
			{
				cdrom[id].requested_blocks = 1;
			}

			cdrom[id].packet_len = max_len * alloc_length;
			cdrom_buf_alloc(id, cdrom[id].packet_len);

			ret = cdrom_read_blocks(id, &alloc_length, 1);
			if (ret <= 0)
			{
				return;
			}

			cdrom[id].packet_len = alloc_length;
			cdrom_set_buf_len(id, BufLen, &cdrom[id].packet_len);

			if (cdrom[id].requested_blocks > 1)
			{
				cdrom_data_command_finish(id, alloc_length, alloc_length / cdrom[id].requested_blocks, alloc_length, 0);
			}
			else
			{
				cdrom_data_command_finish(id, alloc_length, alloc_length, alloc_length, 0);
			}
			cdrom[id].all_blocks_total = cdrom[id].block_total;
			if (cdrom[id].packet_status != CDROM_PHASE_COMPLETE)
			{
				ui_sb_update_icon(SB_CDROM | id, 1);
			}
			else
			{
				ui_sb_update_icon(SB_CDROM | id, 0);
			}
			return;

		case GPCMD_READ_HEADER:
			SCSIPhase = SCSI_PHASE_DATA_IN;

			alloc_length = ((cdb[7] << 8) | cdb[8]) << 3;
			cdrom_buf_alloc(id, 65536);

			if (cdrom_drives[id].handler->pass_through)
			{
				ret = cdrom_pass_through(id, &len, cdrom[id].current_cdb, cdbufferb);
				if (!ret)
				{
					return;
				}
			}
			else
			{
				cdrom[id].sector_len = (cdb[7] << 8) | cdb[8];
				cdrom[id].sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4]<<8) | cdb[5];
				if (msf)
				{
					real_pos = cdrom_lba_to_msf_accurate(cdrom[id].sector_pos);
				}
				else
				{
					real_pos = cdrom[id].sector_pos;
				}
				cdbufferb[0] = 1; /*2048 bytes user data*/
				cdbufferb[1] = cdbufferb[2] = cdbufferb[3] = 0;
				cdbufferb[4] = (real_pos >> 24);
				cdbufferb[5] = ((real_pos >> 16) & 0xff);
				cdbufferb[6] = ((real_pos >> 8) & 0xff);
				cdbufferb[7] = real_pos & 0xff;

				len = 8;
			}

			len = MIN(len, alloc_length);

			cdrom_set_buf_len(id, BufLen, &len);

			cdrom_data_command_finish(id, len, len, len, 0);
			return;

		case GPCMD_MODE_SENSE_6:
		case GPCMD_MODE_SENSE_10:
			SCSIPhase = SCSI_PHASE_DATA_IN;
		
			if (cdrom_drives[id].bus_type == CDROM_BUS_SCSI)
			{
				block_desc = ((cdb[1] >> 3) & 1) ? 0 : 1;
			}
			else
			{
				block_desc = 0;
			}

			if (cdb[0] == GPCMD_MODE_SENSE_6)
			{
				len = cdb[4];
				cdrom_buf_alloc(id, 256);
			}
			else
			{
				len = (cdb[8] | (cdb[7] << 8));
				cdrom_buf_alloc(id, 65536);
			}

			cdrom[id].current_page_code = cdb[2] & 0x3F;

			if (!(cdrom_mode_sense_page_flags[id] & (1LL << cdrom[id].current_page_code)))
			{
				cdrom_invalid_field(id);
				return;
			}
			
			memset(cdbufferb, 0, len);
			alloc_length = len;

			if (cdb[0] == GPCMD_MODE_SENSE_6)
			{
				len = cdrom_mode_sense(id, cdbufferb, 4, cdb[2], block_desc);
				if (len > alloc_length)
				{
					len = alloc_length;
				}
				cdbufferb[0] = len - 1;
				cdbufferb[1] = cdrom_drives[id].handler->media_type_id(id);
				if (block_desc)
				{
					cdbufferb[3] = 8;
				}
			}
			else
			{
				len = cdrom_mode_sense(id, cdbufferb, 8, cdb[2], block_desc);
				if (len > alloc_length)
				{
					len = alloc_length;
				}
				cdbufferb[0]=(len - 2) >> 8;
				cdbufferb[1]=(len - 2) & 255;
				cdbufferb[2] = cdrom_drives[id].handler->media_type_id(id);
				if (block_desc)
				{
					cdbufferb[6] = 0;
					cdbufferb[7] = 8;
				}
			}

			len = MIN(len, alloc_length);
			cdrom_set_buf_len(id, BufLen, &len);

			alloc_length = len;

			cdrom_log("CD-ROM %i: Reading mode page: %02X...\n", id, cdb[2]);

			cdrom_data_command_finish(id, len, len, alloc_length, 0);
			return;

		case GPCMD_MODE_SELECT_6:
		case GPCMD_MODE_SELECT_10:
			SCSIPhase = SCSI_PHASE_DATA_OUT;
		
			if (cdb[0] == GPCMD_MODE_SELECT_6)
			{
				len = cdb[4];
				cdrom_buf_alloc(id, 256);
			}
			else
			{
				len = (cdb[7] << 8) | cdb[8];
				cdrom_buf_alloc(id, 65536);
			}

			cdrom_set_buf_len(id, BufLen, &len);
			ret = cdrom_mode_select_init(id, cdb[0], len, cdb[1] & 1);

			cdrom_data_command_finish(id, len, len, len, 1);
			return;

		case GPCMD_GET_CONFIGURATION:
			SCSIPhase = SCSI_PHASE_DATA_IN;
		
			/* XXX: could result in alignment problems in some architectures */
			max_len = (cdb[7] << 8) | cdb[8];

			index = 0;

			/* only feature 0 is supported */
			if (cdb[2] != 0 || cdb[3] != 0)
			{
				cdrom_invalid_field(id);
				return;
			}

			cdrom_buf_alloc(id, 65536);
			memset(cdbufferb, 0, max_len);
			/*
			 * the number of sectors from the media tells us which profile
			 * to use as current.  0 means there is no media
			 */
			len = cdrom_drives[id].handler->size(id);
			if (len > CD_MAX_SECTORS)
			{
				cdbufferb[6] = (MMC_PROFILE_DVD_ROM >> 8) & 0xff;
				cdbufferb[7] = MMC_PROFILE_DVD_ROM & 0xff;
			}
			else if (len <= CD_MAX_SECTORS)
			{
				cdbufferb[6] = (MMC_PROFILE_CD_ROM >> 8) & 0xff;
				cdbufferb[7] = MMC_PROFILE_CD_ROM & 0xff;
			}
			cdbufferb[10] = 0x02 | 0x01; /* persistent and current */
			alloc_length = 12; /* headers: 8 + 4 */
			alloc_length += cdrom_set_profile(cdbufferb, &index, MMC_PROFILE_DVD_ROM);
			alloc_length += cdrom_set_profile(cdbufferb, &index, MMC_PROFILE_CD_ROM);
			cdbufferb[0] = ((alloc_length - 4) >> 24) & 0xff;
			cdbufferb[1] = ((alloc_length - 4) >> 16) & 0xff;
			cdbufferb[2] = ((alloc_length - 4) >> 8) & 0xff;
			cdbufferb[3] = (alloc_length - 4) & 0xff;           

			alloc_length = MIN(alloc_length, max_len);

			cdrom_set_buf_len(id, BufLen, &alloc_length);

			cdrom_data_command_finish(id, alloc_length, alloc_length, alloc_length, 0);
			break;

		case GPCMD_GET_EVENT_STATUS_NOTIFICATION:
			SCSIPhase = SCSI_PHASE_DATA_IN;

			cdrom_buf_alloc(id, 8 + sizeof(gesn_event_header));
		
			gesn_cdb = (void *) cdb;
			gesn_event_header = (void *) cdbufferb;

			/* It is fine by the MMC spec to not support async mode operations. */
			if (!(gesn_cdb->polled & 0x01))
			{
				/* asynchronous mode */
				/* Only polling is supported, asynchronous mode is not. */
				cdrom_invalid_field(id);
				return;
			}

			/* polling mode operation */

			/*
			 * These are the supported events.
			 *
			 * We currently only support requests of the 'media' type.
			 * Notification class requests and supported event classes are bitmasks,
			 * but they are built from the same values as the "notification class"
			 * field.
			 */
			gesn_event_header->supported_events = 1 << GESN_MEDIA;

			/*
			 * We use |= below to set the class field; other bits in this byte
			 * are reserved now but this is useful to do if we have to use the
			 * reserved fields later.
			 */
			gesn_event_header->notification_class = 0;

			/*
			 * Responses to requests are to be based on request priority.  The
			 * notification_class_request_type enum above specifies the
			 * priority: upper elements are higher prio than lower ones.
			 */
			if (gesn_cdb->class & (1 << GESN_MEDIA))
			{
				gesn_event_header->notification_class |= GESN_MEDIA;

				cdbufferb[4] = cdrom[id].media_status;	/* Bits 7-4 = Reserved, Bits 4-1 = Media Status */
				cdbufferb[5] = 1;			/* Power Status (1 = Active) */
				cdbufferb[6] = 0;
				cdbufferb[7] = 0;
				used_len = 8;
			}
			else
			{
				gesn_event_header->notification_class = 0x80; /* No event available */
				used_len = sizeof(*gesn_event_header);
			}
			gesn_event_header->len = used_len - sizeof(*gesn_event_header);

			memcpy(cdbufferb, gesn_event_header, 4);

			cdrom_set_buf_len(id, BufLen, &used_len);

			cdrom_data_command_finish(id, used_len, used_len, used_len, 0);
			break;

		case GPCMD_READ_DISC_INFORMATION:
			SCSIPhase = SCSI_PHASE_DATA_IN;
			
			max_len = cdb[7];
			max_len <<= 8;
			max_len |= cdb[8];

			cdrom_buf_alloc(id, 65536);

			if (cdrom_drives[id].handler->pass_through)
			{
				ret = cdrom_pass_through(id, &len, cdrom[id].current_cdb, cdbufferb);
				if (!ret)
				{
					return;
				}
				alloc_length = cdbufferb[0];
				alloc_length <<= 8;
				alloc_length |= cdbufferb[1];
				alloc_length += 2;
				if (alloc_length < len)
				{
					len = alloc_length;
				}
			}
			else
			{
				memset(cdbufferb, 0, 34);
				memset(cdbufferb, 1, 9);
				cdbufferb[0] = 0;
				cdbufferb[1] = 32;
				cdbufferb[2] = 0xe; /* last session complete, disc finalized */
				cdbufferb[7] = 0x20; /* unrestricted use */
				cdbufferb[8] = 0x00; /* CD-ROM */

				len=34;
			}

			cdrom_set_buf_len(id, BufLen, &len);

			cdrom_data_command_finish(id, len, len, len, 0);
			break;

		case GPCMD_READ_TRACK_INFORMATION:
			SCSIPhase = SCSI_PHASE_DATA_IN;
		
			max_len = cdb[7];
			max_len <<= 8;
			max_len |= cdb[8];

			cdrom_buf_alloc(id, 65536);

			track = ((uint32_t) cdb[2]) << 24;
			track |= ((uint32_t) cdb[3]) << 16;
			track |= ((uint32_t) cdb[4]) << 8;
			track |= (uint32_t) cdb[5];

			if (cdrom_drives[id].handler->pass_through)
			{
				ret = cdrom_pass_through(id, &len, cdrom[id].current_cdb, cdbufferb);
				if (!ret)
				{
					return;
				}
				alloc_length = cdbufferb[0];
				alloc_length <<= 8;
				alloc_length |= cdbufferb[1];
				alloc_length += 2;
				if (alloc_length < len)
				{
					len = alloc_length;
				}
			}
			else
			{
				if (((cdb[1] & 0x03) != 1) || (track != 1))
				{
					cdrom_invalid_field(id);
					return;
				}

				len = 36;

				memset(cdbufferb, 0, 36);
				cdbufferb[0] = 0;
				cdbufferb[1] = 34;
				cdbufferb[2] = 1; /* track number (LSB) */
				cdbufferb[3] = 1; /* session number (LSB) */
				cdbufferb[5] = (0 << 5) | (0 << 4) | (4 << 0); /* not damaged, primary copy, data track */
				cdbufferb[6] = (0 << 7) | (0 << 6) | (0 << 5) | (0 << 6) | (1 << 0); /* not reserved track, not blank, not packet writing, not fixed packet, data mode 1 */
				cdbufferb[7] = (0 << 1) | (0 << 0); /* last recorded address not valid, next recordable address not valid */
				cdbufferb[24] = (cdrom_drives[id].handler->size(id) >> 24) & 0xff; /* track size */
				cdbufferb[25] = (cdrom_drives[id].handler->size(id) >> 16) & 0xff; /* track size */
				cdbufferb[26] = (cdrom_drives[id].handler->size(id) >> 8) & 0xff; /* track size */
				cdbufferb[27] = cdrom_drives[id].handler->size(id) & 0xff; /* track size */

				if (len > max_len)
				{
					len = max_len;
					cdbufferb[0] = ((max_len - 2) >> 8) & 0xff;
					cdbufferb[1] = (max_len - 2) & 0xff;
				}
			}
		
			cdrom_set_buf_len(id, BufLen, &len);

			cdrom_data_command_finish(id, len, len, max_len, 0);
			break;

		case GPCMD_PLAY_AUDIO_10:
		case GPCMD_PLAY_AUDIO_12:
		case GPCMD_PLAY_AUDIO_MSF:
		case GPCMD_PLAY_AUDIO_TRACK_INDEX:
			SCSIPhase = SCSI_PHASE_STATUS;
		
			switch(cdb[0])
			{
				case GPCMD_PLAY_AUDIO_10:
					msf = 0;
					pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
					len = (cdb[7] << 8) | cdb[8];
					break;
				case GPCMD_PLAY_AUDIO_12:
					msf = 0;
					pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
					len = (cdb[6] << 24) | (cdb[7] << 16) | (cdb[8] << 8) | cdb[9];
					break;
				case GPCMD_PLAY_AUDIO_MSF:
					/* This is apparently deprecated in the ATAPI spec, and apparently
					   has been since 1995 (!). Hence I'm having to guess most of it. */
					msf = 1;
					pos = (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
					len = (cdb[6] << 16) | (cdb[7] << 8) | cdb[8];
					break;
				case GPCMD_PLAY_AUDIO_TRACK_INDEX:
					msf = 2;
					pos = (cdb[4] << 8) | cdb[5];
					len = (cdb[7] << 8) | cdb[8];
					break;
			}

			if ((cdrom_drive < 1) || (cdrom[id].cd_status <= CD_STATUS_DATA_ONLY) || !cdrom_drives[id].handler->is_track_audio(id, pos, msf))
			{
				cdrom_illegal_mode(id);
				break;
			}

			if (cdrom_drives[id].handler->playaudio)
			{
				cdrom_drives[id].handler->playaudio(id, pos, len, msf);
			}
			else
			{
				cdrom_illegal_mode(id);
				break;
			}

			cdrom_command_complete(id);
			break;

		case GPCMD_READ_SUBCHANNEL:
			SCSIPhase = SCSI_PHASE_DATA_IN;
		
			max_len = cdb[7];
			max_len <<= 8;
			max_len |= cdb[8];
			msf = (cdb[1] >> 1) & 1;

			cdrom_buf_alloc(id, 65536);

			cdrom_log("CD-ROM %i: Getting page %i (%s)\n", id, cdb[3], msf ? "MSF" : "LBA");
			if ((cdrom_drives[id].handler->pass_through) && (cdb[3] != 1))
			{
				ret = cdrom_pass_through(id, &len, cdrom[id].current_cdb, cdbufferb);
				if (!ret)
				{
					return;
				}
				switch(cdrom[id].cd_status)
				{
					case CD_STATUS_PLAYING:
						cdbufferb[1] = 0x11;
						break;
					case CD_STATUS_PAUSED:
						cdbufferb[1] = 0x12;
						break;
					case CD_STATUS_DATA_ONLY:
						cdbufferb[1] = 0x15;
						break;
					default:
						cdbufferb[1] = 0x13;
						break;
				}
				switch(cdb[3])
				{
					case 0:
						alloc_length = 4;
						break;
					case 1:
						alloc_length = 16;
						break;
					default:
						alloc_length = 24;
						break;
				}
				if (!(cdb[2] & 0x40) || (cdb[3] == 0))
				{
					len = 4;
				}
				else
				{
					len = alloc_length;
				}
			}
			else
			{
				if (cdb[3] > 3)
				{
					/* cdrom_log("CD-ROM %i: Read subchannel check condition %02X\n", id, cdb[3]); */
					cdrom_invalid_field(id);
					return;
				}

				switch(cdb[3])
				{
					case 0:
						alloc_length = 4;
						break;
					case 1:
						alloc_length = 16;
						break;
					default:
						alloc_length = 24;
						break;
				}

				memset(cdbufferb, 0, 24);
				pos = 0;
				cdbufferb[pos++] = 0;
				cdbufferb[pos++] = 0; /*Audio status*/
				cdbufferb[pos++] = 0; cdbufferb[pos++] = 0; /*Subchannel length*/
				cdbufferb[pos++] = cdb[3] & 3; /*Format code*/
				if (cdb[3] == 1)
				{
					cdbufferb[1] = cdrom_drives[id].handler->getcurrentsubchannel(id, &cdbufferb[5], msf);
					switch(cdrom[id].cd_status)
					{
						case CD_STATUS_PLAYING:
							cdbufferb[1] = 0x11;
							break;
						case CD_STATUS_PAUSED:
							cdbufferb[1] = 0x12;
							break;
						case CD_STATUS_DATA_ONLY:
							cdbufferb[1] = 0x15;
							break;
						default:
							cdbufferb[1] = 0x13;
							break;
					}
				}
				if (!(cdb[2] & 0x40) || (cdb[3] == 0))
				{
					len = 4;
				}
				else
				{
					len = alloc_length;
				}
			}

			cdrom_set_buf_len(id, BufLen, &len);

			cdrom_data_command_finish(id, len, len, len, 0);
			break;

		case GPCMD_READ_DVD_STRUCTURE:
			SCSIPhase = SCSI_PHASE_DATA_IN;

			alloc_length = (((uint32_t) cdb[6]) << 24) | (((uint32_t) cdb[7]) << 16) | (((uint32_t) cdb[8]) << 8) | ((uint32_t) cdb[9]);
			alloc_length = MIN(alloc_length, 256 * 512 + 4);

			cdrom_buf_alloc(id, alloc_length);

			if (cdrom_drives[id].handler->pass_through)
			{
				ret = cdrom_pass_through(id, &len, cdrom[id].current_cdb, cdbufferb);
				if (!ret)
				{
					return;
				}
				else
				{
					if (cdrom_drives[id].bus_type == CDROM_BUS_SCSI)
					{
						if (*BufLen == -1)
						{
							*BufLen = len;
						}
						else
						{
							*BufLen = MIN(len, *BufLen);
							len = *BufLen;
						}
					}
				}
			}
			else
			{
				len = (((uint32_t) cdb[6]) << 24) | (((uint32_t) cdb[7]) << 16) | (((uint32_t) cdb[8]) << 8) | ((uint32_t) cdb[9]);
				alloc_length = len;

 				if (cdb[7] < 0xff)
				{
					if (len <= CD_MAX_SECTORS)
					{
						cdrom_incompatible_format(id);
						return;
					}
					else
					{
						cdrom_invalid_field(id);
						return;
					}
				}

				memset(cdbufferb, 0, alloc_length);

				switch (cdb[7])
				{
					case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07:
					case 0x08: case 0x09: case 0x0a: case 0x0b: case 0x0c: case 0x0d: case 0x0e: case 0x0f:
					case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
					case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f:
					case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27:
					case 0x28: case 0x29: case 0x2a: case 0x2b: case 0x2c: case 0x2d: case 0x2e: case 0x2f:
					case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: case 0x36: case 0x37:
					case 0x38: case 0x39: case 0x3a: case 0x3b: case 0x3c: case 0x3d: case 0x3e: case 0x3f:
					case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
					case 0x48: case 0x49: case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f:
					case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
					case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f:
					case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: case 0x66: case 0x67:
					case 0x68: case 0x69: case 0x6a: case 0x6b: case 0x6c: case 0x6d: case 0x6e: case 0x6f:
					case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
					case 0x78: case 0x79: case 0x7a: case 0x7b: case 0x7c: case 0x7d: case 0x7e: case 0x7f:
					case 0xff:
						if (cdb[1] == 0)
						{
							ret = cdrom_read_dvd_structure(id, format, cdb, cdbufferb);

							cdrom_set_buf_len(id, BufLen, &alloc_length);

							if (ret)
							{
								cdrom_data_command_finish(id, alloc_length, alloc_length, len, 0);
							}
							return;
						}
						/* TODO: BD support, fall through for now */

					/* Generic disk structures */
					case 0x80: /* TODO: AACS volume identifier */
					case 0x81: /* TODO: AACS media serial number */
					case 0x82: /* TODO: AACS media identifier */
					case 0x83: /* TODO: AACS media key block */
					case 0x90: /* TODO: List of recognized format layers */
					case 0xc0: /* TODO: Write protection status */
					default:
						cdrom_invalid_field(id);
						return;
				}
			}
			break;

		case GPCMD_START_STOP_UNIT:
			SCSIPhase = SCSI_PHASE_STATUS;
		
			switch(cdb[4] & 3)
			{
				case 0:		/* Stop the disc. */
					if (cdrom_drives[id].handler->stop)
					{
						cdrom_drives[id].handler->stop(id);
					}
					break;
				case 1:		/* Start the disc and read the TOC. */
					cdrom_drives[id].handler->medium_changed(id);	/* This causes a TOC reload. */
					break;
				case 2:		/* Eject the disc if possible. */
					if (cdrom_drives[id].handler->stop)
					{
						cdrom_drives[id].handler->stop(id);
					}
					cdrom_eject(id);
					break;
				case 3:		/* Load the disc (close tray). */
					cdrom_reload(id);
					break;
			}

			cdrom_command_complete(id);
			break;
                
		case GPCMD_INQUIRY:
			SCSIPhase = SCSI_PHASE_DATA_IN;
		
			max_len = cdb[3];
			max_len <<= 8;
			max_len |= cdb[4];

			cdrom_buf_alloc(id, 65536);

			if (cdb[1] & 1)
			{
				preamble_len = 4;
				size_idx = 3;
					
				cdbufferb[idx++] = 05;
				cdbufferb[idx++] = cdb[2];
				cdbufferb[idx++] = 0;

				idx++;

				switch (cdb[2])
				{
					case 0x00:
						cdbufferb[idx++] = 0x00;
						cdbufferb[idx++] = 0x83;
						break;
					case 0x83:
						if (idx + 24 > max_len)
						{
							cdrom_data_phase_error(id);
							return;
						}

						cdbufferb[idx++] = 0x02;
						cdbufferb[idx++] = 0x00;
						cdbufferb[idx++] = 0x00;
						cdbufferb[idx++] = 20;
						ide_padstr8(cdbufferb + idx, 20, "53R141");	/* Serial */
						idx += 20;

						if (idx + 72 > cdb[4])
						{
							goto atapi_out;
						}
						cdbufferb[idx++] = 0x02;
						cdbufferb[idx++] = 0x01;
						cdbufferb[idx++] = 0x00;
						cdbufferb[idx++] = 68;
						ide_padstr8(cdbufferb + idx, 8, EMU_NAME); /* Vendor */
						idx += 8;
						ide_padstr8(cdbufferb + idx, 40, device_identify_ex); /* Product */
						idx += 40;
						ide_padstr8(cdbufferb + idx, 20, "53R141"); /* Product */
						idx += 20;
						break;
					default:
						cdrom_log("INQUIRY: Invalid page: %02X\n", cdb[2]);
						cdrom_invalid_field(id);
						return;
				}
			}
			else
			{
				preamble_len = 5;
				size_idx = 4;

				memset(cdbufferb, 0, 8);
				cdbufferb[0] = 5; /*CD-ROM*/
				cdbufferb[1] = 0x80; /*Removable*/
				cdbufferb[2] = (cdrom_drives[id].bus_type == CDROM_BUS_SCSI) ? 0x02 : 0x00; /*SCSI-2 compliant*/
				cdbufferb[3] = (cdrom_drives[id].bus_type == CDROM_BUS_SCSI) ? 0x02 : 0x21;
				cdbufferb[4] = 31;

				ide_padstr8(cdbufferb + 8, 8, EMU_NAME); /* Vendor */
				ide_padstr8(cdbufferb + 16, 16, device_identify); /* Product */
				ide_padstr8(cdbufferb + 32, 4, EMU_VERSION); /* Revision */
				idx = 36;
			}

atapi_out:
			cdbufferb[size_idx] = idx - preamble_len;
			len=idx;

			cdrom_set_buf_len(id, BufLen, &len);

			cdrom_data_command_finish(id, len, len, max_len, 0);
			break;

		case GPCMD_PREVENT_REMOVAL:
			SCSIPhase = SCSI_PHASE_STATUS;
			cdrom_command_complete(id);
			break;

		case GPCMD_PAUSE_RESUME_ALT:
		case GPCMD_PAUSE_RESUME:
			SCSIPhase = SCSI_PHASE_STATUS;
		
			if (cdb[8] & 1)
			{
				if (cdrom_drives[id].handler->resume)
				{
					cdrom_drives[id].handler->resume(id);
				}
				else
				{
					cdrom_illegal_mode(id);
					break;
				}
			}
			else
			{
				if (cdrom_drives[id].handler->pause)
				{
					cdrom_drives[id].handler->pause(id);
				}
				else
				{
					cdrom_illegal_mode(id);
					break;
				}
			}
			cdrom_command_complete(id);
			break;

		case GPCMD_SEEK_6:
		case GPCMD_SEEK_10:
			SCSIPhase = SCSI_PHASE_STATUS;
		
			switch(cdb[0])
			{
				case GPCMD_SEEK_6:
					pos = (cdb[2] << 8) | cdb[3];
					break;
				case GPCMD_SEEK_10:
					pos = (cdb[2] << 24) | (cdb[3]<<16) | (cdb[4]<<8) | cdb[5];
					break;
			}
			cdrom_seek(id, pos);
			cdrom_command_complete(id);
			break;

		case GPCMD_READ_CDROM_CAPACITY:
			SCSIPhase = SCSI_PHASE_DATA_IN;

			cdrom_buf_alloc(id, 8);

			if (cdrom_read_capacity(id, cdrom[id].current_cdb, cdbufferb, &len) == 0)
			{
				return;
			}
			
			cdrom_set_buf_len(id, BufLen, &len);

			cdrom_data_command_finish(id, len, len, len, 0);
			break;

		case GPCMD_STOP_PLAY_SCAN:
			SCSIPhase = SCSI_PHASE_STATUS;
		
			if (cdrom_drives[id].handler->stop)
			{
				cdrom_drives[id].handler->stop(id);
			}
			else
			{
				cdrom_illegal_mode(id);
				break;
			}
			cdrom_command_complete(id);
			break;

		default:
			cdrom_illegal_opcode(id);
			break;
	}

	/* cdrom_log("CD-ROM %i: Phase: %02X, request length: %i\n", cdrom[id].phase, cdrom[id].request_length); */
}

/* This is for block reads. */
int cdrom_block_check(uint8_t id)
{
	uint32_t alloc_length = 0;
	int ret = 0;

	/* If this is a media access command, and we hit the end of the block but not the entire length,
	   read the next block. */
	if (cdrom_is_media_access(id))
	{
		/* We have finished the current block. */
		cdrom_log("CD-ROM %i: %i bytes total read, %i bytes all total\n", id, cdrom[id].total_read, cdrom[id].all_blocks_total);
		if (cdrom[id].total_read >= cdrom[id].all_blocks_total)
		{
			cdrom_log("CD-ROM %i: %i bytes read, current block finished\n", id, cdrom[id].total_read);
			/* Read the next block. */
			ret = cdrom_read_blocks(id, &alloc_length, 0);
			if (ret == -1)
			{
				/* Return value is -1 - there are no further blocks to read. */
				cdrom_log("CD-ROM %i: %i bytes read, no further blocks to read\n", id, cdrom[id].total_read);
				cdrom[id].status = BUSY_STAT;
				cdrom_buf_free(id);
				return 1;
			}
			else if (ret == 0)
			{
				/* Return value is 0 - an error has occurred. */
				cdrom_log("CD-ROM %i: %i bytes read, error while reading blocks\n", id, cdrom[id].total_read);
				cdrom[id].status = BUSY_STAT | (cdrom[id].status & ERR_STAT);
				cdrom_buf_free(id);
				return 0;
			}
			else
			{
				/* Return value is 1 - sectors have been read successfully. */
				cdrom[id].pos = 0;
				cdrom[id].all_blocks_total += cdrom[id].block_total;
				cdrom_log("CD-ROM %i: %i bytes read, next block(s) read successfully, %i bytes are still left\n", id, cdrom[id].total_read, cdrom[id].all_blocks_total - cdrom[id].total_read);
				return 1;
			}
		}
		else
		{
			/* Blocks not exhausted, tell the host to check for buffer length. */
			cdrom_log("CD-ROM %i: Blocks not yet finished\n", id);
			return 1;
		}
	}
	else
	{
		/* Not a media access command, ALWAYS do the callback. */
		cdrom_log("CD-ROM %i: Not a media access command\n", id);
		return 1;
	}
}

/* This is the general ATAPI callback. */
void cdrom_callback(uint8_t id)		/* Callback for non-Read CD commands */
{
	int old_pos = 0;

	if (cdrom_drives[id].bus_type < CDROM_BUS_SCSI)
	{
		cdrom_log("CD-ROM %i: Lowering IDE IRQ\n", id);
		ide_irq_lower(&(ide_drives[cdrom_drives[id].ide_channel]));
	}
	
	cdrom[id].status = BUSY_STAT;

	if (cdrom[id].total_read >= cdrom[id].packet_len)
	{
		cdrom_log("CD-ROM %i: %i bytes read, command done\n", id, cdrom[id].total_read);

		cdrom[id].pos = cdrom[id].request_pos = 0;
		cdrom_command_complete(id);
	}
	else
	{
		cdrom_log("CD-ROM %i: %i bytes read, %i bytes are still left\n", id, cdrom[id].total_read, cdrom[id].packet_len - cdrom[id].total_read);

		/* Make sure to keep pos, and reset request_pos to 0. */
		/* Also make sure to not reset total_read. */
		old_pos = cdrom[id].pos;
		cdrom[id].packet_status = CDROM_PHASE_DATA_IN;
		cdrom_command_common(id);
		cdrom[id].pos = old_pos;
		cdrom[id].request_pos = 0;
	}
}

/* 0 = Continue transfer; 1 = Continue transfer, IRQ; -1 = Terminate transfer; -2 = Terminate transfer with error */
int cdrom_mode_select_return(uint8_t id, int ret)
{
	switch(ret)
	{
		case 0:
			/* Invalid field in parameter list. */
		case -6:
			/* Attempted to write to a non-existent CD-ROM drive (should never occur, but you never know). */
			cdrom_invalid_field_pl(id);
			return -2;
		case 1:
			/* Successful, more data needed. */
			if (cdrom[id].pos >= (cdrom[id].packet_len + 2))
			{
				cdrom[id].pos = 0;
				cdrom_command_write(id);
				return 1;
			}
			return 0;
		case 2:
			/* Successful, more data needed, second byte not yet processed. */
			return 0;
		case -3:
			/* Not initialized. */
		case -4:
			/* Unknown phase. */
			cdrom_illegal_opcode(id);
			return -2;
		case -5:
			/* Command terminated successfully. */
			/* cdrom_command_complete(id); */
			return -1;
		default:
			return -15;
	}
}

void cdrom_phase_callback(uint8_t id);

int cdrom_read_from_ide_dma(uint8_t channel)
{
	uint8_t id = atapi_cdrom_drives[channel];

	if (id > CDROM_NUM)
	{
		return 0;
	}

	if (ide_bus_master_write)
	{
		if (ide_bus_master_write(channel >> 1, cdbufferb, cdrom[id].packet_len))
		{
			cdrom_data_phase_error(id);
			cdrom_phase_callback(id);
			return 0;
		}
		else
		{
			return 1;
		}
	}

	return 0;
}

int cdrom_read_from_scsi_dma(uint8_t scsi_id, uint8_t scsi_lun)
{
	uint8_t id = scsi_cdrom_drives[scsi_id][scsi_lun];
	int32_t *BufLen = &SCSIDevices[scsi_id][scsi_lun].BufferLength;

	if (id > CDROM_NUM)
	{
		return 0;
	}

	cdrom_log("Reading from SCSI DMA: SCSI ID %02X, init length %i\n", scsi_id, *BufLen);
	memcpy(cdbufferb, SCSIDevices[scsi_id][scsi_lun].CmdBuffer, *BufLen);
	return 1;
}

int cdrom_read_from_dma(uint8_t id)
{
	int32_t *BufLen = &SCSIDevices[cdrom_drives[id].scsi_device_id][cdrom_drives[id].scsi_device_lun].BufferLength;

	int i = 0;
	int ret = 0;

	int in_data_length = 0;

	if (cdrom_drives[id].bus_type == CDROM_BUS_SCSI)
	{
		ret = cdrom_read_from_scsi_dma(cdrom_drives[id].scsi_device_id, cdrom_drives[id].scsi_device_lun);
	}
	else
	{
		ret = cdrom_read_from_ide_dma(cdrom_drives[id].ide_channel);
	}

	if (!ret)
	{
		return 0;
	}

	if (cdrom_drives[id].bus_type == CDROM_BUS_SCSI)
	{
		in_data_length = *BufLen;
		cdrom_log("CD-ROM %i: SCSI Input data length: %i\n", id, in_data_length);
	}
	else
	{
		in_data_length = cdrom[id].request_length;
		cdrom_log("CD-ROM %i: ATAPI Input data length: %i\n", id, in_data_length);
	}

	for (i = 0; i < in_data_length; i++)
	{
		ret = cdrom_mode_select_write(id, cdbufferb[i]);
		ret = cdrom_mode_select_return(id, ret);
		if (ret == -1)
		{
			return 1;
		}
		else if (ret == -2)
		{
			cdrom_phase_callback(id);
			return 0;
		}
	}

	return 0;
}

int cdrom_write_to_ide_dma(uint8_t channel)
{
	uint8_t id = atapi_cdrom_drives[channel];

	int transfer_length = 0;
	int cdbufferb_pos = 0;

	int bus_master_len = 0;
	int ret = 0;

	if (id > CDROM_NUM)
	{
		return 0;
	}

	transfer_length = cdrom[id].init_length;

	if (ide_bus_master_read)
	{
		while(transfer_length > 0)
		{
			/* cdrom_log("CD-ROM %i: ATAPI DMA on position: %08X...\n", id, cdbufferb + cdbufferb_pos); */
			bus_master_len = piix_bus_master_get_count(channel >> 1);
			ret = piix_bus_master_dma_read_ex(channel >> 1, cdbufferb + cdbufferb_pos);
			if (ret != 0)
			{
				break;
			}
			transfer_length -= bus_master_len;
			cdbufferb_pos += bus_master_len;
		}

		if (ret > 0)
		{
			/* cdrom_log("CD-ROM %i: ATAPI DMA error\n", id); */
			cdrom_data_phase_error(id);
			cdrom_phase_callback(id);
			return 0;
		}
		else
		{
			/* cdrom_log("CD-ROM %i: ATAPI DMA successful\n", id); */
			return 1;
		}
	}

	return 0;
}

int cdrom_write_to_scsi_dma(uint8_t scsi_id, uint8_t scsi_lun)
{
	uint8_t id = scsi_cdrom_drives[scsi_id][scsi_lun];
	int32_t *BufLen = &SCSIDevices[scsi_id][scsi_lun].BufferLength;

	if (id > CDROM_NUM)
	{
		return 0;
	}

	cdrom_log("Writing to SCSI DMA: SCSI ID %02X, init length %i\n", scsi_id, *BufLen);
	memcpy(SCSIDevices[scsi_id][scsi_lun].CmdBuffer, cdbufferb, *BufLen);
	cdrom_log("CD-ROM %i: Data from CD buffer:  %02X %02X %02X %02X %02X %02X %02X %02X\n", id, cdbufferb[0], cdbufferb[1], cdbufferb[2], cdbufferb[3], cdbufferb[4], cdbufferb[5], cdbufferb[6], cdbufferb[7]);
	cdrom_log("CD-ROM %i: Data from SCSI DMA :  %02X %02X %02X %02X %02X %02X %02X %02X\n", id, SCSIDevices[scsi_id][scsi_lun].CmdBuffer[0], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[1], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[2], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[3], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[4], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[5], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[6], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[7]);
	return 1;
}

int cdrom_write_to_dma(uint8_t id)
{
	int ret = 0;

	if (cdrom_drives[id].bus_type == CDROM_BUS_SCSI)
	{
		cdrom_log("Write to SCSI DMA: (%02X:%02X)\n", cdrom_drives[id].scsi_device_id, cdrom_drives[id].scsi_device_lun);
		ret = cdrom_write_to_scsi_dma(cdrom_drives[id].scsi_device_id, cdrom_drives[id].scsi_device_lun);
	}
	else
	{
		ret = cdrom_write_to_ide_dma(cdrom_drives[id].ide_channel);
	}

	if (!ret)
	{
		return 0;
	}

	return 1;
}

void cdrom_irq_raise(uint8_t id)
{
	if (cdrom_drives[id].bus_type < CDROM_BUS_SCSI)
	{
		ide_irq_raise(&(ide_drives[cdrom_drives[id].ide_channel]));
	}
}

/* If the result is 1, issue an IRQ, otherwise not. */
void cdrom_phase_callback(uint8_t id)
{
	switch(cdrom[id].packet_status)
	{
		case CDROM_PHASE_IDLE:
			cdrom_log("CD-ROM %i: CDROM_PHASE_IDLE\n", id);
			cdrom[id].pos=0;
			cdrom[id].phase = 1;
			cdrom[id].status = READY_STAT | DRQ_STAT | (cdrom[id].status & ERR_STAT);
			return;
		case CDROM_PHASE_COMMAND:
			cdrom_log("CD-ROM %i: CDROM_PHASE_COMMAND\n", id);
			cdrom[id].status = BUSY_STAT | (cdrom[id].status &ERR_STAT);
			memcpy(cdrom[id].atapi_cdb, (uint8_t *) cdrom[id].buffer, cdrom[id].cdb_len);
			cdrom_command(id, cdrom[id].atapi_cdb);
			return;
		case CDROM_PHASE_COMPLETE:
			cdrom_log("CD-ROM %i: CDROM_PHASE_COMPLETE\n", id);
			cdrom[id].status = READY_STAT;
			cdrom[id].phase = 3;
			cdrom[id].packet_status = 0xFF;
			cdrom_buf_free(id);
			ui_sb_update_icon(SB_CDROM | id, 0);
			cdrom_irq_raise(id);
			return;
		case CDROM_PHASE_DATA_OUT:
			cdrom_log("CD-ROM %i: CDROM_PHASE_DATA_OUT\n", id);
			cdrom[id].status = READY_STAT | DRQ_STAT | (cdrom[id].status & ERR_STAT);
			cdrom[id].phase = 0;
			cdrom_irq_raise(id);
			return;
		case CDROM_PHASE_DATA_OUT_DMA:
			cdrom_log("CD-ROM %i: CDROM_PHASE_DATA_OUT_DMA\n", id);
			cdrom_read_from_dma(id);
			cdrom[id].packet_status = CDROM_PHASE_COMPLETE;
			cdrom[id].status = READY_STAT;
			cdrom[id].phase = 3;
			ui_sb_update_icon(SB_CDROM | id, 0);
			cdrom_irq_raise(id);
			return;
		case CDROM_PHASE_DATA_IN:
			cdrom_log("CD-ROM %i: CDROM_PHASE_DATA_IN\n", id);
			cdrom[id].status = READY_STAT | DRQ_STAT | (cdrom[id].status & ERR_STAT);
			cdrom[id].phase = 2;
			cdrom_irq_raise(id);
			return;
		case CDROM_PHASE_DATA_IN_DMA:
			cdrom_log("CD-ROM %i: CDROM_PHASE_DATA_IN_DMA\n", id);
			cdrom_write_to_dma(id);
			cdrom[id].packet_status = CDROM_PHASE_COMPLETE;
			cdrom[id].status = READY_STAT;
			cdrom[id].phase = 3;
			ui_sb_update_icon(SB_CDROM | id, 0);
			cdrom_irq_raise(id);
			return;
		case CDROM_PHASE_ERROR:
			cdrom_log("CD-ROM %i: CDROM_PHASE_ERROR\n", id);
			cdrom[id].status = READY_STAT | ERR_STAT;
			cdrom[id].phase = 3;
			cdrom_buf_free(id);
			cdrom_irq_raise(id);
			ui_sb_update_icon(SB_CDROM | id, 0);
			return;
	}
}

/* Reimplement as 8-bit due to reimplementation of IDE data read and write. */
uint32_t cdrom_read(uint8_t channel, int length)
{
	uint16_t *cdbufferw;
	uint32_t *cdbufferl;

	uint8_t id = atapi_cdrom_drives[channel];

	uint32_t temp = 0;
	int ret = 0;

	if (id > CDROM_NUM)
	{
		return 0;
	}

	cdbufferw = (uint16_t *) cdbufferb;
	cdbufferl = (uint32_t *) cdbufferb;

	switch(length)
	{
		case 1:
			temp = cdbufferb[cdrom[id].pos];
			cdrom[id].pos++;
			cdrom[id].request_pos++;
			break;
		case 2:
			temp = cdbufferw[cdrom[id].pos >> 1];
			cdrom[id].pos += 2;
			cdrom[id].request_pos += 2;
			break;
		case 4:
			temp = cdbufferl[cdrom[id].pos >> 2];
			cdrom[id].pos += 4;
			cdrom[id].request_pos += 4;
			break;
		default:
			return 0;
	}

	if (cdrom[id].packet_status == CDROM_PHASE_DATA_IN)
	{
		cdrom[id].total_read += length;
		ret = cdrom_block_check(id);
		/* If the block check has returned 0, this means all the requested blocks have been read, therefore the command has finished. */
		if (ret)
		{
			cdrom_log("CD-ROM %i: Return value is 1 (request length: %i)\n", id, cdrom[id].request_length);
			if (cdrom[id].request_pos >= cdrom[id].request_length)
			{
				/* Time for a DRQ. */
				cdrom_log("CD-ROM %i: Issuing read callback\n", id);
				cdrom_callback(id);
			}
			else
			{
				cdrom_log("CD-ROM %i: Doing nothing\n", id);
			}
		}
		else
		{
			cdrom_log("CD-ROM %i: Return value is 0\n", id);
		}
		cdrom_log("CD-ROM %i: Returning: %02X (buffer position: %i, request position: %i, total: %i)\n", id, temp, cdrom[id].pos, cdrom[id].request_pos, cdrom[id].total_read);
		return temp;
	}
	else
	{
		cdrom_log("CD-ROM %i: Returning zero (buffer position: %i, request position: %i, total: %i)\n", id, cdrom[id].pos, cdrom[id].request_pos, cdrom[id].total_read);
		return 0;
	}
}

/* Reimplement as 8-bit due to reimplementation of IDE data read and write. */
void cdrom_write(uint8_t channel, uint32_t val, int length)
{
	uint8_t i = 0;
	uint16_t *cdbufferw;
	uint32_t *cdbufferl;

	uint8_t old_pos = 0;

	uint8_t id = atapi_cdrom_drives[channel];

	int ret = 0;

	if (id > CDROM_NUM)
	{
		return;
	}

	cdbufferw = (uint16_t *) cdbufferb;
	cdbufferl = (uint32_t *) cdbufferb;

	old_pos = cdrom[id].pos;

	switch(length)
	{
		case 1:
			cdbufferb[cdrom[id].pos] = val & 0xff;
			cdrom[id].pos++;
			break;
		case 2:
			cdbufferw[cdrom[id].pos >> 1] = val & 0xffff;
			cdrom[id].pos += 2;
			break;
		case 4:
			cdbufferl[cdrom[id].pos >> 2] = val;
			cdrom[id].pos += 4;
			break;
		default:
			return;
	}

	if (cdrom[id].packet_status == CDROM_PHASE_DATA_OUT)
	{
		for (i = 0; i < length; i++)
		{
			ret = cdrom_mode_select_write(id, cdbufferb[old_pos + i]);
			cdrom_mode_select_return(id, ret);
		}
		return;
	}
	else if (cdrom[id].packet_status == CDROM_PHASE_IDLE)
	{
		if (cdrom[id].pos >= cdrom[id].cdb_len)
		{
			cdrom[id].pos=0;
			cdrom[id].status = BUSY_STAT;
			cdrom[id].packet_status = CDROM_PHASE_COMMAND;
			cdrom_buf_free(id);
			timer_process();
			cdrom_phase_callback(id);
			timer_update_outstanding();
		}
		return;
	}
}

void cdrom_hard_reset(void)
{
    int i = 0;

    for (i=0; i<CDROM_NUM; i++) {
	if (cdrom_drives[i].host_drive == 200) {
		image_reset(i);
	}
	else if ((cdrom_drives[i].host_drive >= 'A') && (cdrom_drives[i].host_drive <= 'Z'))
	{
		ioctl_reset(i);
	}
    }
}


/* Peform a master init on the entire module. */
void
cdrom_global_init(void)
{
    int c;

    /* Clear the global data. */
    memset(cdrom, 0x00, sizeof(cdrom));
    memset(cdrom_drives, 0x00, sizeof(cdrom_drives));

    /* Initialize the host devices, if any. */
    cdrom_init_host_drives();

    /* Set all drives to NULL mode. */
    for (c=0; c<CDROM_NUM; c++)
	cdrom_null_open(c, cdrom_drives[c].host_drive);
}


void
cdrom_global_reset(void)
{
    int c;

    for (c=0; c<CDROM_NUM; c++) {
	if (cdrom_drives[c].bus_type) {
		SCSIReset(cdrom_drives[c].scsi_device_id, cdrom_drives[c].scsi_device_lun);
	}

pclog("CDROM global_reset drive=%d host=%02x\n", c, cdrom_drives[c].host_drive);
	if (cdrom_drives[c].host_drive == 200) {
		image_open(c, cdrom_image[c].image_path);
	} else
	if ((cdrom_drives[c].host_drive>='A') && (cdrom_drives[c].host_drive <= 'Z')) {
		ioctl_open(c, cdrom_drives[c].host_drive);
	} else {
		cdrom_null_open(c, cdrom_drives[c].host_drive);
	}
    }
}


void
cdrom_close(uint8_t id)
{
    switch (cdrom_drives[id].host_drive) {
	case 0:
		null_close(id);
		break;

	case 200:
		image_close(id);
		break;

	default:
		ioctl_close(id);
		break;
    }
}
