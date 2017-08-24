extern double PITCONST;

extern void pit_init(void);
extern void pit_ps2_init(void);
extern void pit_reset(PIT *pit);
extern void pit_set_gate(PIT *pit, int channel, int gate);
extern void pit_set_using_timer(PIT *pit, int t, int using_timer);
extern void pit_set_out_func(PIT *pit, int t, void (*func)(int new_out, int old_out));
extern void pit_clock(PIT *pit, int t);


extern void pit_null_timer(int new_out, int old_out);
extern void pit_irq0_timer(int new_out, int old_out);
extern void pit_irq0_timer_pcjr(int new_out, int old_out);
extern void pit_refresh_timer_xt(int new_out, int old_out);
extern void pit_refresh_timer_at(int new_out, int old_out);
extern void pit_speaker_timer(int new_out, int old_out);
