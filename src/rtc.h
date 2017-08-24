#define BCD(X) (((X) % 10) | (((X) / 10) << 4))
#define DCB(X) ((((X) & 0xF0) >> 4) * 10 + ((X) & 0x0F))

enum RTC_ADDR
{
        RTC_SECONDS,
        RTC_ALARMSECONDS,
        RTC_MINUTES,
        RTC_ALARMMINUTES,
        RTC_HOURS,
        RTC_ALARMHOURS,
        RTC_DOW,
        RTC_DOM,
        RTC_MONTH,
        RTC_YEAR,
        RTC_REGA,
        RTC_REGB,
        RTC_REGC,
        RTC_REGD
};

/*      The century register at location 32h is a BCD register designed to automatically load the BCD value 20 as the year register changes from 99 to 00.
        The MSB of this register is not affected when the load of 20 occurs, and remains at the value written by the user. */
#define RTC_CENTURY 0x32

/*      When the 12-hour format is selected, the higher-order bit of the hours byte represents PM when it is logic 1. */
#define RTC_AMPM 0x80

/*      Register A bitflags */
enum RTC_RA_BITS 
{
/*      Rate Selector (RS0)

        These four rate-selection bits select one of the 13 taps on the 15-stage divider or disable the divider output.
        The tap selected can be used to generate an output square wave (SQW pin) and/or a periodic interrupt.
        The user can do one of the following:
        - Enable the interrupt with the PIE bit;
        - Enable the SQW output pin with the SQWE bit;
        - Enable both at the same time and the same rate; or
        - Enable neither.

        Table 3 lists the periodic interrupt rates and the square wave frequencies that can be chosen with the RS bits.
        These four read/write bits are not affected by !RESET. */
        RTC_RS = 0xF,
/*      DV0

        These three bits are used to turn the oscillator on or off and to reset the countdown chain.
        A pattern of 010 is the only combination of bits that turn the oscillator on and allow the RTC to keep time.
        A pattern of 11x enables the oscillator but holds the countdown chain in reset.
        The next update occurs at 500ms after a pattern of 010 is written to DV0, DV1, and DV2. */
        RTC_DV0 = 0x70,
/*      Update-In-Progress (UIP)

        This bit is a status flag that can be monitored. When the UIP bit is a 1, the update transfer occurs soon.
        When UIP is a 0, the update transfer does not occur for at least 244us.
        The time, calendar, and alarm information in RAM is fully available for access when the UIP bit is 0.
        The UIP bit is read-only and is not affected by !RESET.
        Writing the SET bit in Register B to a 1 inhibits any update transfer and clears the UIP status bit. */
        RTC_UIP = 0x80
};

/*      Register B bitflags */
enum RTC_RB_BITS 
{
/*      Daylight Saving Enable (DSE)

        This bit is a read/write bit that enables two daylight saving adjustments when DSE is set to 1.
        On the first Sunday in April (or the last Sunday in April in the MC146818A), the time increments from 1:59:59 AM to 3:00:00 AM.
        On the last Sunday in October when the time first reaches 1:59:59 AM, it changes to 1:00:00 AM.
        When DSE is enabled, the internal logic test for the first/last Sunday condition at midnight.
        If the DSE bit is not set when the test occurs, the daylight saving function does not operate correctly.
        These adjustments do not occur when the DSE bit is 0. This bit is not affected by internal functions or !RESET. */
        RTC_DSE = 0x1,
/*      24/12

        The 24/12 control bit establishes the format of the hours byte. A 1 indicates the 24-hour mode and a 0 indicates the 12-hour mode.
        This bit is read/write and is not affected by internal functions or !RESET. */
        RTC_2412 = 0x2,
/*      Data Mode (DM)

        This bit indicates whether time and calendar information is in binary or BCD format.
        The DM bit is set by the program to the appropriate format and can be read as required.
        This bit is not modified by internal functions or !RESET. A 1 in DM signifies binary data, while a 0 in DM specifies BCD data. */
        RTC_DM = 0x4,
/*      Square-Wave Enable (SQWE)

        When this bit is set to 1, a square-wave signal at the frequency set by the rate-selection bits RS3-RS0 is driven out on the SQW pin.
        When the SQWE bit is set to 0, the SQW pin is held low. SQWE is a read/write bit and is cleared by !RESET.
        SQWE is low if disabled, and is high impedance when VCC is below VPF. SQWE is cleared to 0 on !RESET. */
        RTC_SQWE = 0x8,
/*      Update-Ended Interrupt Enable (UIE)

        This bit is a read/write bit that enables the update-end flag (UF) bit in Register C to assert !IRQ.
        The !RESET pin going low or the SET bit going high clears the UIE bit.
        The internal functions of the device do not affect the UIE bit, but is cleared to 0 on !RESET. */
        RTC_UIE = 0x10,
/*      Alarm Interrupt Enable (AIE)

        This bit is a read/write bit that, when set to 1, permits the alarm flag (AF) bit in Register C to assert !IRQ.
        An alarm interrupt occurs for each second that the three time bytes equal the three alarm bytes, including a don't-care alarm code of binary 11XXXXXX.
        The AF bit does not initiate the !IRQ signal when the AIE bit is set to 0.
        The internal functions of the device do not affect the AIE bit, but is cleared to 0 on !RESET. */
        RTC_AIE = 0x20,
/*      Periodic Interrupt Enable (PIE)

        The PIE bit is a read/write bit that allows the periodic interrupt flag (PF) bit in Register C to drive the !IRQ pin low.
        When the PIE bit is set to 1, periodic interrupts are generated by driving the !IRQ pin low at a rate specified by the RS3-RS0 bits of Register A.
        A 0 in the PIE bit blocks the !IRQ output from being driven by a periodic interrupt, but the PF bit is still set at the periodic rate.
        PIE is not modified by any internal device functions, but is cleared to 0 on !RESET. */
        RTC_PIE = 0x40,
/*      SET

        When the SET bit is 0, the update transfer functions normally by advancing the counts once per second.
        When the SET bit is written to 1, any update transfer is inhibited, and the program can initialize the time and calendar bytes without an update
        occurring in the midst of initializing. Read cycles can be executed in a similar manner. SET is a read/write bit and is not affected by !RESET or
        internal functions of the device. */
        RTC_SET = 0x80
};

/*      Register C bitflags */
enum RTC_RC_BITS 
{
/*      Unused

        These bits are unused in Register C. These bits always read 0 and cannot be written. */
        RTC_RC = 0xF,
/*      Update-Ended Interrupt Flag (UF)

        This bit is set after each update cycle. When the UIE bit is set to 1, the 1 in UF causes the IRQF bit to be a 1, which asserts the !IRQ pin.
        This bit can be cleared by reading Register C or with a !RESET. */
        RTC_UF = 0x10,
/*      Alarm Interrupt Flag (AF)

        A 1 in the AF bit indicates that the current time has matched the alarm time.
        If the AIE bit is also 1, the !IRQ pin goes low and a 1 appears in the IRQF bit. This bit can be cleared by reading Register C or with a !RESET. */
        RTC_AF = 0x20,
/*      Periodic Interrupt Flag (PF)

        This bit is read-only and is set to 1 when an edge is detected on the selected tap of the divider chain.
        The RS3 through RS0 bits establish the periodic rate. PF is set to 1 independent of the state of the PIE bit.
        When both PF and PIE are 1s, the !IRQ signal is active and sets the IRQF bit. This bit can be cleared by reading Register C or with a !RESET. */
        RTC_PF = 0x40,
/*      Interrupt Request Flag (IRQF)

        The interrupt request flag (IRQF) is set to a 1 when one or more of the following are true:
        - PF == PIE == 1
        - AF == AIE == 1
        - UF == UIE == 1

        Any time the IRQF bit is a 1, the !IRQ pin is driven low.
        All flag bits are cleared after Register C is read by the program or when the !RESET pin is low. */
        RTC_IRQF = 0x80
};

/*      Register D bitflags */
enum RTC_RD_BITS 
{
/*      Unused

        The remaining bits of Register D are not usable. They cannot be written and they always read 0. */
        RTC_RD = 0x7F,
/*      Valid RAM and Time (VRT)

        This bit indicates the condition of the battery connected to the VBAT pin. This bit is not writeable and should always be 1 when read.
        If a 0 is ever present, an exhausted internal lithium energy source is indicated and both the contents of the RTC data and RAM data are questionable.
        This bit is unaffected by !RESET. */
        RTC_VRT = 0x80
};

extern void rtc_tick(void);
extern void time_update(char *nvrram, int reg);
extern void time_get(char *nvrram);
extern void time_internal_set_nvrram(char *nvrram);
extern void time_internal_sync(char *nvrram);
