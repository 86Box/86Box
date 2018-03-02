
/* The TC8521 is a 4-bit RTC, so each memory location can only hold a single
 * BCD digit. Hence everything has 'ones' and 'tens' digits. */
enum TC8521_ADDR
{
	/* Page 0 registers */
	TC8521_SECOND1,
	TC8521_SECOND10,
	TC8521_MINUTE1,
	TC8521_MINUTE10,
	TC8521_HOUR1,
	TC8521_HOUR10,
	TC8521_WEEKDAY,
	TC8521_DAY1,
	TC8521_DAY10,
	TC8521_MONTH1,
	TC8521_MONTH10,
	TC8521_YEAR1,
	TC8521_YEAR10,
	TC8521_PAGE,	/* PAGE register */
	TC8521_TEST,	/* TEST register */
	TC8521_RESET,	/* RESET register */
	/* Page 1 registers */
	TC8521_24HR     = 0x1A,
	TC8521_LEAPYEAR	= 0x1B
};


void tc8521_tick();
void tc8521_update(uint8_t *nvrram, int reg);
void tc8521_get(uint8_t *nvrram);
void tc8521_internal_set_nvrram(uint8_t *nvrram);
void tc8521_internal_sync(uint8_t *nvrram);
