#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <86box/io.h>
#include <86box/mca.h>


void    (*mca_card_write[8])(int addr, uint8_t val, void *priv);
uint8_t  (*mca_card_read[8])(int addr, void *priv);
uint8_t (*mca_card_feedb[8])(void *priv);
void    (*mca_card_reset[8])(void *priv);
void           *mca_priv[8];

static int mca_index;
static int mca_nr_cards;


void mca_init(int nr_cards)
{
	int c;

	for (c = 0; c < 8; c++) {
		mca_card_read[c] = NULL;
		mca_card_write[c] = NULL;
		mca_card_reset[c] = NULL;
		mca_priv[c] = NULL;
	}

	mca_index = 0;
	mca_nr_cards = nr_cards;
}

void mca_set_index(int index)
{
	mca_index = index;
}

uint8_t mca_read(uint16_t port)
{
	if (mca_index >= mca_nr_cards)
		return 0xff;
	if (!mca_card_read[mca_index])
		return 0xff;
	return mca_card_read[mca_index](port, mca_priv[mca_index]);
}

uint8_t mca_read_index(uint16_t port, int index)
{
	if (mca_index >= mca_nr_cards)
		return 0xff;
	if (!mca_card_read[index])
		return 0xff;
	return mca_card_read[index](port, mca_priv[index]);
}

int mca_get_nr_cards(void)
{
	return mca_nr_cards;
}

void mca_write(uint16_t port, uint8_t val)
{
	if (mca_index >= mca_nr_cards)
		return;
	if (mca_card_write[mca_index])
		mca_card_write[mca_index](port, val, mca_priv[mca_index]);
}

uint8_t mca_feedb(void)
{
	if (mca_card_feedb[mca_index])
		return !!(mca_card_feedb[mca_index](mca_priv[mca_index]));
	else
		return 0;
}

void mca_reset(void)
{
	int c;

	for (c = 0; c < 8; c++) {
		if (mca_card_reset[c])
			mca_card_reset[c](mca_priv[c]);
	}
}


void mca_add(uint8_t (*read)(int addr, void *priv), void (*write)(int addr, uint8_t val, void *priv), uint8_t (*feedb)(void *priv), void (*reset)(void *priv), void *priv)
{
	int c;

        for (c = 0; c < mca_nr_cards; c++) {
		if (!mca_card_read[c] && !mca_card_write[c]) {
			mca_card_read[c] = read;
			mca_card_write[c] = write;
			mca_card_feedb[c] = feedb;
			mca_card_reset[c] = reset;
			mca_priv[c] = priv;
			return;
		}
	}
}
