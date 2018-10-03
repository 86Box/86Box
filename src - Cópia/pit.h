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
    int		wp,
		rm[3],
		wm[3];
    uint16_t	rl[3];
    int		thit[3];
    int		delay[3];
    int		rereadlatch[3];
    int		gate[3];
    int		out[3];
    int64_t	running[3];
    int		enabled[3];
    int		newcount[3];
    int		count[3];
    int		using_timer[3];
    int		initial[3];
    int		latched[3];
    int		disabled[3];

    uint8_t	read_status[3];
    int		do_read_status[3];

    PIT_nr	pit_nr[3];

    void	(*set_out_funcs[3])(int new_out, int old_out);
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
extern void	pit_set_gate(PIT *pit, int channel, int gate);
extern void	pit_set_using_timer(PIT *pit, int t, int using_timer);
extern void	pit_set_out_func(PIT *pit, int t, void (*func)(int new_out, int old_out));
extern void	pit_clock(PIT *pit, int t);

extern void     setrtcconst(float clock);

extern void     setpitclock(float clock);
extern float    pit_timer0_freq(void);

extern void	pit_null_timer(int new_out, int old_out);
extern void	pit_irq0_timer(int new_out, int old_out);
extern void	pit_irq0_timer_pcjr(int new_out, int old_out);
extern void	pit_refresh_timer_xt(int new_out, int old_out);
extern void	pit_refresh_timer_at(int new_out, int old_out);
extern void	pit_speaker_timer(int new_out, int old_out);


#endif	/*EMU_PIT_H*/
