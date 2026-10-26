#include <stdlib.h>
#include <dlfcn.h>
#include <86box/plat_dynld.h>

void *
dynld_module(const char *name, dllimp_t *table)
{
    dllimp_t *imp;
    void *modhandle = dlopen(name, RTLD_LAZY | RTLD_GLOBAL);

    if (modhandle) {
        for (imp = table; imp->name != NULL; imp++) {
            if ((*(void **) imp->func = dlsym(modhandle, imp->name)) == NULL) {
                dlclose(modhandle);
                return NULL;
            }
        }
    }

    return modhandle;
}

void
dynld_close(void *handle)
{
    if (handle == NULL)
        return;

    dlclose(handle);
}
