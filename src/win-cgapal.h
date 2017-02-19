extern PALETTE cgapal;
extern PALETTE cgapal_mono[6];

extern uint32_t pal_lookup[256];

#ifdef __cplusplus
extern "C" {
#endif
void cgapal_rebuild();
#ifdef __cplusplus
}
#endif
