/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
extern double PITCONST;
extern float cpuclock;
void pit_init();
void pit_reset();
void pit_set_gate(int channel, int gate);
void pit_set_using_timer(int t, int using_timer);
void pit_set_out_func(int t, void (*func)(int new_out, int old_out));
void pit_clock(int t);


void pit_null_timer(int new_out, int old_out);
void pit_irq0_timer(int new_out, int old_out);
void pit_irq0_timer_pcjr(int new_out, int old_out);
void pit_refresh_timer_xt(int new_out, int old_out);
void pit_refresh_timer_at(int new_out, int old_out);
void pit_speaker_timer(int new_out, int old_out);
