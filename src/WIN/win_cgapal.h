extern PALETTE cgapal;
extern PALETTE cgapal_mono[6];

extern uint32_t pal_lookup[256];

#ifdef __cplusplus
extern "C" {
#endif
void cgapal_rebuild();
void destroy_bitmap(BITMAP *b);
#ifdef __cplusplus
}
#endif
