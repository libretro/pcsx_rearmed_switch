/***************************************************************************
 *   Copyright (C) 2007 Ryan Schultz, PCSX-df Team, PCSX team              *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02111-1307 USA.           *
 ***************************************************************************/

/*
* PSX memory functions.
*/

// TODO: Implement caches & cycle penalty.

#include "psxmem.h"
#include "psxmem_map.h"
#include "r3000a.h"
#include "psxhw.h"
#include "debug.h"

#include "memmap.h"

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

void *(*psxMapHook)(unsigned long addr, size_t size, int is_fixed,
		enum psxMapTag tag);
void (*psxUnmapHook)(void *ptr, size_t size, enum psxMapTag tag);

void *psxMap(unsigned long addr, size_t size, int is_fixed,
		enum psxMapTag tag)
{
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	int try_ = 0;
	unsigned long mask;
	void *req, *ret;

retry:
	if (psxMapHook != NULL) {
		ret = psxMapHook(addr, size, 0, tag);
		if (ret == NULL)
			return NULL;
	}
	else {
		/* avoid MAP_FIXED, it overrides existing mappings.. */
		/* if (is_fixed)
			flags |= MAP_FIXED; */

		req = (void *)addr;
		ret = mmap(req, size, PROT_READ | PROT_WRITE, flags, -1, 0);
		if (ret == MAP_FAILED)
			return NULL;
	}

	if (addr != 0 && ret != (void *)addr) {
		SysMessage("psxMap: warning: wanted to map @%08x, got %p\n",
			addr, ret);

		if (is_fixed) {
			psxUnmap(ret, size, tag);
			return NULL;
		}

		if (((addr ^ (unsigned long)ret) & ~0xff000000l) && try_ < 2)
		{
			psxUnmap(ret, size, tag);

			// try to use similarly aligned memory instead
			// (recompiler needs this)
			mask = try_ ? 0xffff : 0xffffff;
			addr = ((unsigned long)ret + mask) & ~mask;
			try_++;
			goto retry;
		}
	}

	return ret;
}

void psxUnmap(void *ptr, size_t size, enum psxMapTag tag)
{
	if (psxUnmapHook != NULL) {
		psxUnmapHook(ptr, size, tag);
		return;
	}

	if (ptr)
		munmap(ptr, size);
}

s8 *psxM = NULL; // Kernel & User Memory (2 Meg)
s8 *psxP = NULL; // Parallel Port (64K)
s8 *psxR = NULL; // BIOS ROM (512K)
s8 *psxH = NULL; // Scratch Pad (1K) & Hardware Registers (8K)

u8 **psxMemWLUT = NULL;
u8 **psxMemRLUT = NULL;

/*  Playstation Memory Map (from Playstation doc by Joshua Walker)
0x0000_0000-0x0000_ffff		Kernel (64K)
0x0001_0000-0x001f_ffff		User Memory (1.9 Meg)

0x1f00_0000-0x1f00_ffff		Parallel Port (64K)

0x1f80_0000-0x1f80_03ff		Scratch Pad (1024 bytes)

0x1f80_1000-0x1f80_2fff		Hardware Registers (8K)

0x1fc0_0000-0x1fc7_ffff		BIOS (512K)

0x8000_0000-0x801f_ffff		Kernel and User Memory Mirror (2 Meg) Cached
0x9fc0_0000-0x9fc7_ffff		BIOS Mirror (512K) Cached

0xa000_0000-0xa01f_ffff		Kernel and User Memory Mirror (2 Meg) Uncached
0xbfc0_0000-0xbfc7_ffff		BIOS Mirror (512K) Uncached
*/

int psxMemInit() {
	int i;

	psxMemRLUT = (u8 **)malloc(0x10000 * sizeof(void *));
	psxMemWLUT = (u8 **)malloc(0x10000 * sizeof(void *));
	memset(psxMemRLUT, 0, 0x10000 * sizeof(void *));
	memset(psxMemWLUT, 0, 0x10000 * sizeof(void *));

	psxM = psxMap(0x80000000, 0x00210000, 1, MAP_TAG_RAM);
#ifndef RAM_FIXED
	if (psxM == NULL)
		psxM = psxMap(0x77000000, 0x00210000, 0, MAP_TAG_RAM);
#endif
	if (psxM == NULL) {
		SysMessage(_("mapping main RAM failed"));
		return -1;
	}

	psxP = &psxM[0x200000];
	psxH = psxMap(0x1f800000, 0x10000, 0, MAP_TAG_OTHER);
	psxR = psxMap(0x1fc00000, 0x80000, 0, MAP_TAG_OTHER);

	if (psxMemRLUT == NULL || psxMemWLUT == NULL || 
	    psxR == NULL || psxP == NULL || psxH == NULL) {
		SysMessage(_("Error allocating memory!"));
		psxMemShutdown();
		return -1;
	}

// MemR
	for (i = 0; i < 0x80; i++) psxMemRLUT[i + 0x0000] = (u8 *)&psxM[(i & 0x1f) << 16];

	memcpy(psxMemRLUT + 0x8000, psxMemRLUT, 0x80 * sizeof(void *));
	memcpy(psxMemRLUT + 0xa000, psxMemRLUT, 0x80 * sizeof(void *));

	psxMemRLUT[0x1f00] = (u8 *)psxP;
	psxMemRLUT[0x1f80] = (u8 *)psxH;

	for (i = 0; i < 0x08; i++) psxMemRLUT[i + 0x1fc0] = (u8 *)&psxR[i << 16];

	memcpy(psxMemRLUT + 0x9fc0, psxMemRLUT + 0x1fc0, 0x08 * sizeof(void *));
	memcpy(psxMemRLUT + 0xbfc0, psxMemRLUT + 0x1fc0, 0x08 * sizeof(void *));

// MemW
	for (i = 0; i < 0x80; i++) psxMemWLUT[i + 0x0000] = (u8 *)&psxM[(i & 0x1f) << 16];

	memcpy(psxMemWLUT + 0x8000, psxMemWLUT, 0x80 * sizeof(void *));
	memcpy(psxMemWLUT + 0xa000, psxMemWLUT, 0x80 * sizeof(void *));

	psxMemWLUT[0x1f00] = (u8 *)psxP;
	psxMemWLUT[0x1f80] = (u8 *)psxH;

	return 0;
}

void psxMemReset() {
	FILE *f = NULL;
	char bios[1024];

	memset(psxM, 0, 0x00200000);
	memset(psxP, 0xff, 0x00010000);

	if (strcmp(Config.Bios, "HLE") != 0) {
		sprintf(bios, "%s/%s", Config.BiosDir, Config.Bios);
		f = fopen(bios, "rb");

		if (f == NULL) {
			SysMessage(_("Could not open BIOS:\"%s\". Enabling HLE Bios!\n"), bios);
			memset(psxR, 0, 0x80000);
			Config.HLE = TRUE;
		} else {
			fread(psxR, 1, 0x80000, f);
			fclose(f);
			Config.HLE = FALSE;
		}
	} else Config.HLE = TRUE;
}

void psxMemShutdown() {
	psxUnmap(psxM, 0x00210000, MAP_TAG_RAM); psxM = NULL;
	psxUnmap(psxH, 0x10000, MAP_TAG_OTHER); psxH = NULL;
	psxUnmap(psxR, 0x80000, MAP_TAG_OTHER); psxR = NULL;

	free(psxMemRLUT); psxMemRLUT = NULL;
	free(psxMemWLUT); psxMemWLUT = NULL;
}

static int writeok = 1;

u8 psxMemRead8(u32 mem) {
	char *p;
	u32 t;

	t = mem >> 16;
	if (t == 0x1f80 || t == 0x9f80 || t == 0xbf80) {
		if ((mem & 0xffff) < 0x400)
			return psxHu8(mem);
		else
			return psxHwRead8(mem);
	} else {
		p = (char *)(psxMemRLUT[t]);
		if (p != NULL) {
			if (Config.Debug)
				DebugCheckBP((mem & 0xffffff) | 0x80000000, R1);
			return *(u8 *)(p + (mem & 0xffff));
		} else {
#ifdef PSXMEM_LOG
			PSXMEM_LOG("err lb %8.8lx\n", mem);
#endif
			return 0;
		}
	}
}

u16 psxMemRead16(u32 mem) {
	char *p;
	u32 t;

	t = mem >> 16;
	if (t == 0x1f80 || t == 0x9f80 || t == 0xbf80) {
		if ((mem & 0xffff) < 0x400)
			return psxHu16(mem);
		else
			return psxHwRead16(mem);
	} else {
		p = (char *)(psxMemRLUT[t]);
		if (p != NULL) {
			if (Config.Debug)
				DebugCheckBP((mem & 0xffffff) | 0x80000000, R2);
			return SWAPu16(*(u16 *)(p + (mem & 0xffff)));
		} else {
#ifdef PSXMEM_LOG
			PSXMEM_LOG("err lh %8.8lx\n", mem);
#endif
			return 0;
		}
	}
}

u32 psxMemRead32(u32 mem) {
	char *p;
	u32 t;

	t = mem >> 16;
	if (t == 0x1f80 || t == 0x9f80 || t == 0xbf80) {
		if ((mem & 0xffff) < 0x400)
			return psxHu32(mem);
		else
			return psxHwRead32(mem);
	} else {
		p = (char *)(psxMemRLUT[t]);
		if (p != NULL) {
			if (Config.Debug)
				DebugCheckBP((mem & 0xffffff) | 0x80000000, R4);
			return SWAPu32(*(u32 *)(p + (mem & 0xffff)));
		} else {
#ifdef PSXMEM_LOG
			if (writeok) { PSXMEM_LOG("err lw %8.8lx\n", mem); }
#endif
			return 0;
		}
	}
}

void psxMemWrite8(u32 mem, u8 value) {
	char *p;
	u32 t;

	t = mem >> 16;
	if (t == 0x1f80 || t == 0x9f80 || t == 0xbf80) {
		if ((mem & 0xffff) < 0x400)
			psxHu8(mem) = value;
		else
			psxHwWrite8(mem, value);
	} else {
		p = (char *)(psxMemWLUT[t]);
		if (p != NULL) {
			if (Config.Debug)
				DebugCheckBP((mem & 0xffffff) | 0x80000000, W1);
			*(u8 *)(p + (mem & 0xffff)) = value;
#ifdef PSXREC
			psxCpu->Clear((mem & (~3)), 1);
#endif
		} else {
#ifdef PSXMEM_LOG
			PSXMEM_LOG("err sb %8.8lx\n", mem);
#endif
		}
	}
}

void psxMemWrite16(u32 mem, u16 value) {
	char *p;
	u32 t;

	t = mem >> 16;
	if (t == 0x1f80 || t == 0x9f80 || t == 0xbf80) {
		if ((mem & 0xffff) < 0x400)
			psxHu16ref(mem) = SWAPu16(value);
		else
			psxHwWrite16(mem, value);
	} else {
		p = (char *)(psxMemWLUT[t]);
		if (p != NULL) {
			if (Config.Debug)
				DebugCheckBP((mem & 0xffffff) | 0x80000000, W2);
			*(u16 *)(p + (mem & 0xffff)) = SWAPu16(value);
#ifdef PSXREC
			psxCpu->Clear((mem & (~3)), 1);
#endif
		} else {
#ifdef PSXMEM_LOG
			PSXMEM_LOG("err sh %8.8lx\n", mem);
#endif
		}
	}
}

void psxMemWrite32(u32 mem, u32 value) {
	char *p;
	u32 t;

//	if ((mem&0x1fffff) == 0x71E18 || value == 0x48088800) SysPrintf("t2fix!!\n");
	t = mem >> 16;
	if (t == 0x1f80 || t == 0x9f80 || t == 0xbf80) {
		if ((mem & 0xffff) < 0x400)
			psxHu32ref(mem) = SWAPu32(value);
		else
			psxHwWrite32(mem, value);
	} else {
		p = (char *)(psxMemWLUT[t]);
		if (p != NULL) {
			if (Config.Debug)
				DebugCheckBP((mem & 0xffffff) | 0x80000000, W4);
			*(u32 *)(p + (mem & 0xffff)) = SWAPu32(value);
#ifdef PSXREC
			psxCpu->Clear(mem, 1);
#endif
		} else {
			if (mem != 0xfffe0130) {
#ifdef PSXREC
				if (!writeok)
					psxCpu->Clear(mem, 1);
#endif

#ifdef PSXMEM_LOG
				if (writeok) { PSXMEM_LOG("err sw %8.8lx\n", mem); }
#endif
			} else {
				int i;

				switch (value) {
					case 0x800: case 0x804:
						if (writeok == 0) break;
						writeok = 0;
						memset(psxMemWLUT + 0x0000, 0, 0x80 * sizeof(void *));
						memset(psxMemWLUT + 0x8000, 0, 0x80 * sizeof(void *));
						memset(psxMemWLUT + 0xa000, 0, 0x80 * sizeof(void *));
						break;
					case 0x00: case 0x1e988:
						if (writeok == 1) break;
						writeok = 1;
						for (i = 0; i < 0x80; i++) psxMemWLUT[i + 0x0000] = (void *)&psxM[(i & 0x1f) << 16];
						memcpy(psxMemWLUT + 0x8000, psxMemWLUT, 0x80 * sizeof(void *));
						memcpy(psxMemWLUT + 0xa000, psxMemWLUT, 0x80 * sizeof(void *));
						break;
					default:
#ifdef PSXMEM_LOG
						PSXMEM_LOG("unk %8.8lx = %x\n", mem, value);
#endif
						break;
				}
			}
		}
	}
}

void *psxMemPointer(u32 mem) {
	char *p;
	u32 t;

	t = mem >> 16;
	if (t == 0x1f80 || t == 0x9f80 || t == 0xbf80) {
		if ((mem & 0xffff) < 0x400)
			return (void *)&psxH[mem];
		else
			return NULL;
	} else {
		p = (char *)(psxMemWLUT[t]);
		if (p != NULL) {
			return (void *)(p + (mem & 0xffff));
		}
		return NULL;
	}
}
