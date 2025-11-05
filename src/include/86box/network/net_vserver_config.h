/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          Shared identity and account settings for the virtual server.
 */
#ifndef NETWORK_NET_VSERVER_CONFIG_H
#define NETWORK_NET_VSERVER_CONFIG_H

#define VSERVER_SERVER_NAME "86BOXHOST"
/* Last octet for the server in SLiRP's /24 network. */
#define VSERVER_SLIRP_HOST_ID 4
/* The current legacy server presents one name as both domain and workgroup. */
#define VSERVER_DOMAIN_NAME "WORKGROUP"
#define VSERVER_WORKGROUP_NAME VSERVER_DOMAIN_NAME
#define VSERVER_SERVER_COMMENT "86Box"
#define VSERVER_SERVER_VERSION_MAJOR 4
#define VSERVER_SERVER_VERSION_MINOR 0

#define VSERVER_NATIVE_OS_NAME VSERVER_SERVER_COMMENT
#define VSERVER_NATIVE_LANMAN_NAME VSERVER_SERVER_COMMENT " SMB"
#define VSERVER_SLIRP_HOSTNAME VSERVER_SERVER_COMMENT
#define VSERVER_PPP_SERVER_NAME VSERVER_SERVER_COMMENT
#define VSERVER_PPTP_VENDOR_NAME VSERVER_SERVER_COMMENT

/* This is the sole authenticated account; other logons become guests. */
#define VSERVER_LOGON_USERNAME "86Box"
#define VSERVER_LOGON_PASSWORD "86Box"
#define VSERVER_LOGON_GROUP_NAME "Domain Users"
#define VSERVER_LOGON_SCRIPT ""
#define VSERVER_OTHER_DOMAINS ""

#define VSERVER_SHARE_NAME "SHARED"
#define VSERVER_SHARE_REMARK VSERVER_SERVER_COMMENT " shared folder"
#define VSERVER_LOGON_SHARE_NAME "NETLOGON"
#define VSERVER_LOGON_SHARE_REMARK "Logon server share"
#define VSERVER_IPC_SHARE_NAME "IPC$"
#define VSERVER_IPC_SHARE_REMARK "Remote IPC"

#define VSERVER_FTP_TEST_FILE_NAME "ftp-test.txt"
#define VSERVER_FTP_TEST_FILE_CONTENT "86Box read-only FTP test file.\r\n"
#ifndef VSERVER_TFTP_BOOT_FILE_NAME
#    define VSERVER_TFTP_BOOT_FILE_NAME "tftp-test.txt"
#endif
#define VSERVER_TFTP_TEST_FILE_NAME VSERVER_TFTP_BOOT_FILE_NAME
#define VSERVER_TFTP_TEST_FILE_CONTENT "86Box read-only TFTP test file.\r\n"

#endif /* NETWORK_NET_VSERVER_CONFIG_H */
