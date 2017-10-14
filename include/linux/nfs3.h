/*
 * NFSv3 protocol definitions
 */
#ifndef _LINUX_NFS3_H
#define _LINUX_NFS3_H

#include <uapi/linux/nfs3.h>
#if defined(NFS_VAAI)	// 2012/12/04 Cindy Jen add for NFS VAAI
/* Flags for vstorage() call */
#define NFS3_VSTORAGE_PRIM_CLONEFILE        0x0001
#define NFS3_VSTORAGE_PRIM_RESVSPACE        0x0002
#define NFS3_VSTORAGE_PRIM_STATX            0x0004

enum nfs3_vstorageop {
        NFS3_VSTORAGEOP_REGISTER = 0,
        NFS3_VSTORAGEOP_RESERVESPACE = 1,
        NFS3_VSTORAGEOP_EXTENDEDSTAT = 2,
        NFS3_VSTORAGEOP_CLONEFILE = 3
};
#endif

#define NFS3_VERSION		3
#define NFS3PROC_NULL		0
#define NFS3PROC_GETATTR	1
#define NFS3PROC_SETATTR	2
#define NFS3PROC_LOOKUP		3
#define NFS3PROC_ACCESS		4
#define NFS3PROC_READLINK	5
#define NFS3PROC_READ		6
#define NFS3PROC_WRITE		7
#define NFS3PROC_CREATE		8
#define NFS3PROC_MKDIR		9
#define NFS3PROC_SYMLINK	10
#define NFS3PROC_MKNOD		11
#define NFS3PROC_REMOVE		12
#define NFS3PROC_RMDIR		13
#define NFS3PROC_RENAME		14
#define NFS3PROC_LINK		15
#define NFS3PROC_READDIR	16
#define NFS3PROC_READDIRPLUS	17
#define NFS3PROC_FSSTAT		18
#define NFS3PROC_FSINFO		19
#define NFS3PROC_PATHCONF	20
#define NFS3PROC_COMMIT		21
#if defined(NFS_VAAI)	// 2012/12/04 Cindy Jen add for NFS VAAI
#define NFS3PROC_VSTORAGE       22
#endif


/* Number of 32bit words in post_op_attr */
#define NFS3_POST_OP_ATTR_WORDS		22

#endif /* _LINUX_NFS3_H */
