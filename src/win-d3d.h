/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#ifdef __cplusplus
extern "C" {
#endif
        void d3d_init(HWND h);
        void d3d_close();
        void d3d_reset();
        void d3d_resize(int x, int y);
#ifdef __cplusplus
}
#endif
