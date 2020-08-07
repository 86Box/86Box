/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef LIBSLIRP_VERSION_H_
#define LIBSLIRP_VERSION_H_

#ifdef __cplusplus
extern "C" {
#endif

#define SLIRP_MAJOR_VERSION 4
#define SLIRP_MINOR_VERSION 3
#define SLIRP_MICRO_VERSION 1
#define SLIRP_VERSION_STRING "4.3.1-git-86Box"

#define SLIRP_CHECK_VERSION(major,minor,micro)                          \
    (SLIRP_MAJOR_VERSION > (major) ||                                   \
     (SLIRP_MAJOR_VERSION == (major) && SLIRP_MINOR_VERSION > (minor)) || \
     (SLIRP_MAJOR_VERSION == (major) && SLIRP_MINOR_VERSION == (minor) && \
      SLIRP_MICRO_VERSION >= (micro)))

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIBSLIRP_VERSION_H_ */
