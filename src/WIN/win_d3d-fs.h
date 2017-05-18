/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#ifdef __cplusplus
extern "C" {
#endif
        int d3d_fs_init(HWND h);
        void d3d_fs_close();
        void d3d_fs_reset();
        void d3d_fs_resize(int x, int y);
#ifdef __cplusplus
}
#endif
