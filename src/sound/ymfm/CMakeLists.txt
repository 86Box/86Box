#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          CMake build script.
#

add_library(ymfm STATIC
    ymfm_adpcm.cpp
    ymfm_misc.cpp
    ymfm_opl.cpp
    ymfm_opm.cpp
    ymfm_opn.cpp
    ymfm_opq.cpp
    ymfm_opz.cpp
    ymfm_pcm.cpp
    ymfm_ssg.cpp
)
