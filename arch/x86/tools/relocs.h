#ifndef RELOCS_H
#define RELOCS_H

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <elf.h>
#ifdef __APPLE__
#include <sys/_endian.h>

#define	R_386_NONE		0	/* relocation type */
#define	R_386_32		1
#define	R_386_PC32		2
#define	R_386_GOT32		3
#define	R_386_PLT32		4
#define	R_386_COPY		5
#define	R_386_GLOB_DAT		6
#define	R_386_JMP_SLOT		7
#define	R_386_RELATIVE		8
#define	R_386_GOTOFF		9
#define	R_386_GOTPC		10
#define	R_386_32PLT		11
#define	R_386_TLS_GD_PLT	12
#define	R_386_TLS_LDM_PLT	13
#define	R_386_TLS_TPOFF		14
#define	R_386_TLS_IE		15
#define	R_386_TLS_GOTIE		16
#define	R_386_TLS_LE		17
#define	R_386_TLS_GD		18
#define	R_386_TLS_LDM		19
#define	R_386_16		20
#define	R_386_PC16		21
#define	R_386_8			22
#define	R_386_PC8		23
#define	R_386_UNKNOWN24		24
#define	R_386_UNKNOWN25		25
#define	R_386_UNKNOWN26		26
#define	R_386_UNKNOWN27		27
#define	R_386_UNKNOWN28		28
#define	R_386_UNKNOWN29		29
#define	R_386_UNKNOWN30		30
#define	R_386_UNKNOWN31		31
#define	R_386_TLS_LDO_32	32
#define	R_386_UNKNOWN33		33
#define	R_386_UNKNOWN34		34
#define	R_386_TLS_DTPMOD32	35
#define	R_386_TLS_DTPOFF32	36
#define	R_386_UNKNOWN37		37
#define	R_386_NUM		38

#define R_X86_64_NONE		0	/* No reloc */
#define R_X86_64_64		1	/* Direct 64 bit  */
#define R_X86_64_PC32		2	/* PC relative 32 bit signed */
#define R_X86_64_GOT32		3	/* 32 bit GOT entry */
#define R_X86_64_PLT32		4	/* 32 bit PLT address */
#define R_X86_64_COPY		5	/* Copy symbol at runtime */
#define R_X86_64_GLOB_DAT	6	/* Create GOT entry */
#define R_X86_64_JUMP_SLOT	7	/* Create PLT entry */
#define R_X86_64_RELATIVE	8	/* Adjust by program base */
#define R_X86_64_GOTPCREL	9	/* 32 bit signed pc relative
					   offset to GOT */
#define R_X86_64_32		10	/* Direct 32 bit zero extended */
#define R_X86_64_32S		11	/* Direct 32 bit sign extended */
#define R_X86_64_16		12	/* Direct 16 bit zero extended */
#define R_X86_64_PC16		13	/* 16 bit sign extended pc relative */
#define R_X86_64_8		14	/* Direct 8 bit sign extended  */
#define R_X86_64_PC8		15	/* 8 bit sign extended pc relative */

#define R_X86_64_NUM		16

#else
#include <byteswap.h>
#define USE_BSD
#include <endian.h>
#endif
#include <regex.h>
#include <tools/le_byteshift.h>

void die(char *fmt, ...) __attribute__((noreturn));

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

enum symtype {
	S_ABS,
	S_REL,
	S_SEG,
	S_LIN,
	S_NSYMTYPES
};

void process_32(FILE *fp, int use_real_mode, int as_text,
		int show_absolute_syms, int show_absolute_relocs,
		int show_reloc_info);
void process_64(FILE *fp, int use_real_mode, int as_text,
		int show_absolute_syms, int show_absolute_relocs,
		int show_reloc_info);
#endif /* RELOCS_H */
