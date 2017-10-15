extern double PITCONST;

extern void pit_init(void);
extern void pit_ps2_init(void);
extern void pit_reset(PIT *pit);
extern void pit_set_gate(PIT *pit, int64_t channel, int64_t gate);
extern void pit_set_using_timer(PIT *pit, int64_t t, int64_t using_timer);
extern void pit_set_out_func(PIT *pit, int64_t t, void (*func)(int64_t new_out, int64_t old_out));
extern void pit_clock(PIT *pit, int64_t t);


extern void pit_null_timer(int64_t new_out, int64_t old_out);
extern void pit_irq0_timer(int64_t new_out, int64_t old_out);
extern void pit_irq0_timer_pcjr(int64_t new_out, int64_t old_out);
extern void pit_refresh_timer_xt(int64_t new_out, int64_t old_out);
extern void pit_refresh_timer_at(int64_t new_out, int64_t old_out);
extern void pit_speaker_timer(int64_t new_out, int64_t old_out);
