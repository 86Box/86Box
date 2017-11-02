#ifndef EMU_PIT_H
# define EMU_PIT_H


typedef struct {
    int64_t	nr;
    struct PIT	*pit;
} PIT_nr;

typedef struct PIT {
    uint32_t	l[3];
    int64_t	c[3];
    uint8_t	m[3];
    uint8_t	ctrl,
		ctrls[3];
    int64_t	wp,
		rm[3],
		wm[3];
    uint16_t	rl[3];
    int64_t	thit[3];
    int64_t	delay[3];
    int64_t	rereadlatch[3];
    int64_t	gate[3];
    int64_t	out[3];
    int64_t	running[3];
    int64_t	enabled[3];
    int64_t	newcount[3];
    int64_t	count[3];
    int64_t	using_timer[3];
    int64_t	initial[3];
    int64_t	latched[3];
    int64_t	disabled[3];

    uint8_t	read_status[3];
    int64_t	do_read_status[3];

    PIT_nr	pit_nr[3];

    void	(*set_out_funcs[3])(int64_t new_out, int64_t old_out);
} PIT;


extern PIT	pit,
		pit2;
extern double	PITCONST;
extern float	CGACONST,
		MDACONST,
		VGACONST1,
		VGACONST2,
		RTCCONST;


extern void	pit_init(void);
extern void	pit_ps2_init(void);
extern void	pit_reset(PIT *pit);
extern void	pit_set_gate(PIT *pit, int64_t channel, int64_t gate);
extern void	pit_set_using_timer(PIT *pit, int64_t t, int64_t using_timer);
extern void	pit_set_out_func(PIT *pit, int64_t t, void (*func)(int64_t new_out, int64_t old_out));
extern void	pit_clock(PIT *pit, int64_t t);

extern void     setpitclock(float clock);
extern float    pit_timer0_freq(void);

extern void	pit_null_timer(int64_t new_out, int64_t old_out);
extern void	pit_irq0_timer(int64_t new_out, int64_t old_out);
extern void	pit_irq0_timer_pcjr(int64_t new_out, int64_t old_out);
extern void	pit_refresh_timer_xt(int64_t new_out, int64_t old_out);
extern void	pit_refresh_timer_at(int64_t new_out, int64_t old_out);
extern void	pit_speaker_timer(int64_t new_out, int64_t old_out);


#endif	/*EMU_PIT_H*/
