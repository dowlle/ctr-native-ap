// cc -m32 -DCTR_NATIVE -DBUILD=926 -DCTR_CUSTOM_PACKAGES -I tools/harness-support/sdl3_stub -I include -I .
//   tools/test-spu-memory.c -o /tmp/test-spu-memory -lm
//
// The 1 MiB SPU sound memory of the box authoring build
// (include/platform/native_spu_memory.h). Compiles the engine's real bank
// allocator (game/HOWL/HOWL_Bank.c) and the real SPU emulation
// (platform/native_audio.c, SDL stubbed out) as the authoring build does and
// checks:
//   - retail banks land at the same byte addresses as the retail 8-byte-unit
//     allocator would put them, and bank placement past 512 KB works,
//   - the allocator's new ceiling (0xfe000) and its address units in u16,
//   - address masking, transfers and voice bounds above 512 KB,
//   - a voice plays a sample above 512 KB exactly as it plays it low,
//   - the reverb never touches SPU memory,
//   - savestate snapshots carry 1 MiB, and an old 512 KB snapshot is refused
//     without touching the live state.
#include <common.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <platform/native_assets.h>
#include <platform/native_disc_image.h>

struct sData sdata_static; // common.h binds sdata to it

// --- what the allocator calls: disc reads, the mempack, the libspu transfer ---

#define FAKE_SLOTS 64
#define FAKE_BANKS 8
static unsigned char s_hwl[FAKE_BANKS * 0x800 * 400]; // bank n at sector s_bankSector[n]
static u16 s_bankSector[FAKE_BANKS];
static struct HowlHeader s_header;
static struct SpuAddrEntry s_table[FAKE_SLOTS];
static unsigned char s_mempack[2 * 1024 * 1024] __attribute__((aligned(4)));

int LOAD_HowlSectorChainStart(CdlFILE *file, void *dst, int firstSector, int numSector)
{
	(void)file;
	memcpy(dst, s_hwl + (size_t)firstSector * 0x800, (size_t)numSector * 0x800);
	return 1;
}
int LOAD_HowlSectorChainEnd(void) { return 1; }
int MEMPACK_PushState(void) { return 1; }
void MEMPACK_PopState(void) {}
void *MEMPACK_AllocMem(int size) { (void)size; return s_mempack; }
void *MEMPACK_ReallocMem(int size) { return size <= (int)sizeof s_mempack ? s_mempack : NULL; }

#include "game/HOWL/HOWL_Bank.c"

// --- the SPU emulation, with the asset and disc readers it links against ---

int NativeAssets_ResolvePath(const char *relativePath, char *dst, size_t dstSize) { (void)relativePath; (void)dst; (void)dstSize; return 0; }
int NativeAssets_ReadBytes(const char *path, int readMode, struct NativeAssetsByteBuffer *bytes) { (void)path; (void)readMode; (void)bytes; return 0; }
int NativeDiscImage_FindFile(const char *path, struct NativeDiscImageFile *fileOut) { (void)path; (void)fileOut; return 0; }
int NativeDiscImage_ReadRawSectors(const struct NativeDiscImageFile *file, u32 sector, u32 sectorCount, void *dst)
{
	(void)file; (void)sector; (void)sectorCount; (void)dst;
	return 0;
}

#include "platform/native_audio.c"

unsigned int SpuSetTransferStartAddr(unsigned int addr) { return NativeAudio_SpuSetTransferStartAddr(addr); }
unsigned int SpuWrite(unsigned char *addr, unsigned int size) { return NativeAudio_SpuWrite(addr, size); }
int SpuIsTransferCompleted(int flag) { (void)flag; return 1; }

static int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

// --- fake KART.HWL: banks of ADPCM samples, sizes in 8-byte units as on disc ---

// Slot sizes (8-byte units) and the banks that load them, in load order:
// bank 0 (common), a large level bank, a driver bank, a character bank, and
// one that crosses the new ceiling. Slot 1 is shared by banks 0 and 2, as
// retail slot 87 is.
static const u16 kSlotSize[FAKE_SLOTS] = {
	0, 0x400, 0x600, 0x200, // bank 0: slots 1..3 (24 KB), from 0x1010
	0x4000, 0x3000, 0x1000, // level bank: slots 4..6 (256 KB)
	0x1800, 0x1800, 0x1000, // driver bank: slots 7..9 plus shared slot 1 (136 KB)
	0x3000, 0x2000,         // character bank: slots 10..11 (160 KB), 0x69010 to 0x91010
	0x8000, 0x5a00,         // from 0x91010 would end at 0xfe010, past the ceiling
};
static const int kBankSlots[5][5] = {
	{3, 1, 2, 3},
	{3, 4, 5, 6},
	{4, 7, 8, 9, 1},
	{2, 10, 11},
	{2, 12, 13},
};

static unsigned char sample_byte(int slot, u32 offset)
{
	return (unsigned char)(slot * 37 + offset * 11 + (offset >> 8));
}

static void build_hwl(void)
{
	int b, i, sector = 0;
	memset(s_hwl, 0, sizeof s_hwl);
	for (b = 0; b < 5; b++)
	{
		s16 *head = (s16 *)(s_hwl + (size_t)sector * 0x800);
		u32 at = 0;
		s_bankSector[b] = (u16)sector;
		head[0] = (s16)kBankSlots[b][0];
		for (i = 0; i < kBankSlots[b][0]; i++)
		{
			int slot = kBankSlots[b][1 + i];
			u32 bytes = (u32)kSlotSize[slot] * 8, k;
			head[1 + i] = (s16)slot;
			for (k = 0; k < bytes; k++)
				s_hwl[(size_t)(sector + 1) * 0x800 + at + k] = sample_byte(slot, k);
			at += bytes;
		}
		sector += 1 + (int)((at + 0x7ff) / 0x800);
	}
	memset(&s_header, 0, sizeof s_header);
	s_header.numSpuAddrs = FAKE_SLOTS;
	s_header.numBanks = FAKE_BANKS;
	for (i = 0; i < FAKE_SLOTS; i++)
	{
		s_table[i].spuAddr = 0; // on disc every spuAddr is 0
		s_table[i].spuSize = kSlotSize[i];
	}
	memset(&sdata_static, 0, sizeof sdata_static);
	sdata->boolAudioEnabled = 1;
	sdata->ptrHowlHeader = &s_header;
	sdata->howl_spuAddrs = s_table;
	sdata->howl_bankOffsets = s_bankSector;
	Bank_ResetAllocator();
}

static int load_bank(int bankID)
{
	struct Bank music;
	int guard = 0;
	memset(&music, 0, sizeof music);
	if (!Bank_Load(bankID, &music)) return 0;
	while (!Bank_AssignSpuAddrs())
		if (++guard > 16) return 0;
	return 1;
}

static u32 slot_byte_addr(int slot)
{
	return (u32)CTR_SPU_BYTES_AT((u32)s_table[slot].spuAddr);
}

static int slot_on_spu(int slot)
{
	u32 at = slot_byte_addr(slot), bytes = (u32)kSlotSize[slot] * 8, k;
	for (k = 0; k < bytes; k++)
		if (s_audio.spu.memory[at + k] != sample_byte(slot, k)) return 0;
	return 1;
}

// One ADPCM block: filter 0, shift 0, samples 1..7 repeated; flags end+repeat
// on the last block so the voice loops on itself.
static void put_adpcm_blocks(u32 at, int count)
{
	int b, i;
	for (b = 0; b < count; b++)
	{
		unsigned char *blk = &s_audio.spu.memory[at + (u32)b * 16];
		blk[0] = 0x00;
		blk[1] = (unsigned char)((b == 0 ? 0x04 : 0) | (b == count - 1 ? 0x03 : 0));
		for (i = 2; i < 16; i++) blk[i] = (unsigned char)(((i & 7) << 4) | ((i + 3) & 7));
	}
}

static void play_voice_at(u32 addr, s16 *out, int frames)
{
	SpuVoiceAttr attr;
	memset(&attr, 0, sizeof attr);
	attr.voice = SPU_VOICECH(0);
	attr.mask = SPU_VOICE_WDSA | SPU_VOICE_VOLL | SPU_VOICE_VOLR | SPU_VOICE_PITCH | SPU_VOICE_ADSR_AR | SPU_VOICE_ADSR_SL |
	            SPU_VOICE_ADSR_SR | SPU_VOICE_ADSR_RR;
	attr.addr = addr;
	attr.volume.left = 0x3fff;
	attr.volume.right = 0x3fff;
	attr.pitch = 0x1000;
	attr.ar = 0;
	attr.sl = 0xf;
	attr.sr = 0x7f;
	attr.rr = 0;
	NativeAudio_SpuSetKey(0, SPU_VOICECH(0));
	NativeAudio_SpuSetVoiceAttr(&attr);
	NativeAudio_SpuSetKey(1, SPU_VOICECH(0));
	NativeAudio_RenderFrames(out, frames);
	NativeAudio_SpuSetKey(0, SPU_VOICECH(0));
}

int main(void)
{
	static s16 low[2 * 512], high[2 * 512];
	static unsigned char snap[sizeof(struct NativeAudioSnapshot)];
	u32 expect;
	int i, nonzero;

	// Sizes: 1 MiB here, 512 KB retail; u16 address units reach both.
	CHECK(CTR_SPU_BYTES == 1024 * 1024 && NATIVE_AUDIO_SPU_MEMSIZE == 1024 * 1024);
	CHECK(sizeof(s_audio.spu.memory) == 1024 * 1024);
	CHECK(CTR_SPU_CEILING == 0xfe000 && CTR_SPU_RETAIL_CEILING == 0x7e000);
	CHECK(CTR_SPU_WIDE_BYTES - CTR_SPU_WIDE_CEILING == CTR_SPU_RETAIL_BYTES - CTR_SPU_RETAIL_CEILING);
	CHECK(CTR_SPU_UNITS(CTR_SPU_WIDE_BYTES - 16) <= 0xffff);
	CHECK(((CTR_SPU_RETAIL_BYTES - 8) >> 3) <= 0xffff);
	CHECK(CTR_SPU_UNITS(CTR_SPU_WIDE_CEILING) <= 0xffff); // a loaded bank's u16 min + max (Bank_ClearInRange)
	CHECK(CTR_SPU_UNITS(0x1010) == 0x101 && CTR_SPU_BYTES_AT(0x101) == 0x1010);
	CHECK(CTR_SPU_SIZE_UNITS(0x7ffe) == 0x3fff);

	CHECK(NativeAudio_SpuInit());
	NativeAudio_SetDeterministicRenderMode(1);

	// Allocator: bank 0, level, driver and character banks load back to back
	// from 0x1010 at the byte addresses the retail 8-byte allocator computes,
	// the driver bank's shared slot keeps bank 0's copy, and the character
	// bank lands past 512 KB.
	build_hwl();
	CHECK(sdata->audioAllocPtr == 0x101);
	CHECK(load_bank(0) && load_bank(1) && load_bank(2) && load_bank(3));
	expect = 0x1010;
	for (i = 0; i < 3; i++)
	{
		int b = i, k;
		for (k = 0; k < kBankSlots[b][0]; k++)
		{
			int slot = kBankSlots[b][1 + k];
			if (b == 2 && slot == 1) { CHECK(slot_byte_addr(1) == 0x1010); expect += kSlotSize[1] * 8; continue; }
			CHECK(slot_byte_addr(slot) == expect);
			expect += kSlotSize[slot] * 8;
		}
	}
	CHECK(slot_byte_addr(10) == expect && expect < 0x80000 && slot_byte_addr(11) >= 0x80000);
	for (i = 1; i <= 11; i++) CHECK(slot_on_spu(i));
	CHECK(expect == 0x69010);
	CHECK((u32)CTR_SPU_BYTES_AT((u32)sdata->audioAllocPtr) == expect + (0x3000 + 0x2000) * 8);
	CHECK(sdata->bank[3].min == CTR_SPU_UNITS(expect) && sdata->bank[3].max == CTR_SPU_UNITS((0x3000 + 0x2000) * 8));

	// A bank that would end at or past 0xfe000 is not transferred, as retail
	// skips one past 0x7e000.
	memset(&s_audio.spu.memory[slot_byte_addr(11) + kSlotSize[11] * 8], 0x5a, 16);
	CHECK(load_bank(4));
	CHECK(s_audio.spu.memory[slot_byte_addr(11) + kSlotSize[11] * 8] == 0x5a);
	CHECK(Bank_DestroyLast());

	// Destroying the character bank frees its range and rewinds the allocator.
	CHECK(Bank_DestroyLast());
	CHECK(s_table[10].spuAddr == 0 && s_table[11].spuAddr == 0 && s_table[7].spuAddr != 0);
	CHECK((u32)CTR_SPU_BYTES_AT((u32)sdata->audioAllocPtr) == expect);

	// SPU emulation above 512 KB: masking, transfer bounds, voice bounds.
	CHECK(NativeAudio_WrapSpuAddr(0x80010) == 0x80010);
	CHECK(NativeAudio_WrapSpuAddr(0xffff0) == 0xffff0);
	CHECK(NativeAudio_WrapSpuAddr(0x100010) == 0x10);
	CHECK(NativeAudio_SpuSetTransferStartAddr(0xf0000) == 1);
	{
		unsigned char blob[32];
		memset(blob, 0x77, sizeof blob);
		CHECK(NativeAudio_SpuWrite(blob, sizeof blob) == sizeof blob && s_audio.spu.memory[0xf001f] == 0x77);
		CHECK(NativeAudio_SpuSetTransferStartAddr(0xffff0) == 1 && NativeAudio_SpuWrite(blob, sizeof blob) == 0);
		CHECK(NativeAudio_SpuSetTransferStartAddr(0x100001) == 0);
	}
	{
		struct NativeAudioVoiceState v;
		memset(&v, 0, sizeof v);
		v.active = 1;
		v.attr.addr = 0xc0000; v.attr.loop_addr = 0xc0000;
		v.stream.currentAddr = 0xfffff0; v.stream.repeatAddr = 0xc0000;
		CHECK(!NativeAudio_ValidateVoiceSnapshot(&v));
		v.stream.currentAddr = 0xffff0;
		CHECK(NativeAudio_ValidateVoiceSnapshot(&v));
		v.attr.addr = 0x100000;
		CHECK(!NativeAudio_ValidateVoiceSnapshot(&v));
	}

	// A voice plays a sample at 0xc0000 exactly as the same sample at 0x10000.
	memset(s_audio.spu.memory, 0, sizeof s_audio.spu.memory);
	put_adpcm_blocks(0x10000, 4);
	put_adpcm_blocks(0xc0000, 4);
	NativeAudio_ClearOutputQueue();
	play_voice_at(0x10000, low, 512);
	NativeAudio_ClearOutputQueue();
	play_voice_at(0xc0000, high, 512);
	for (i = 0, nonzero = 0; i < 2 * 512; i++) nonzero |= low[i] != 0;
	CHECK(nonzero);
	CHECK(memcmp(low, high, sizeof low) == 0);

	// Reverb keeps its own buffer: running it leaves SPU memory alone.
	{
		static unsigned char before[1024 * 1024];
		SpuReverbAttr rev;
		memcpy(before, s_audio.spu.memory, sizeof before);
		memset(&rev, 0, sizeof rev);
		rev.mask = SPU_REV_MODE | SPU_REV_DEPTHL | SPU_REV_DEPTHR;
		rev.mode = SPU_REV_MODE_HALL;
		rev.depth.left = 0x3fff;
		rev.depth.right = 0x3fff;
		NativeAudio_SpuSetReverbModeParam(&rev);
		NativeAudio_SpuSetReverb(1);
		NativeAudio_SpuSetReverbVoice(1, SPU_VOICECH(0));
		play_voice_at(0xc0000, high, 512);
		CHECK(memcmp(before, s_audio.spu.memory, sizeof before) == 0);
		NativeAudio_SpuSetReverb(0);
	}

	// Savestates: the snapshot carries all 1 MiB, round-trips, and an old
	// 512 KB snapshot is refused before anything is restored.
	CHECK(NativeAudio_GetStateSize() == (int)sizeof(struct NativeAudioSnapshot));
	CHECK(sizeof(((struct NativeAudioSnapshot *)0)->spuSampleMem) == 1024 * 1024);
	s_audio.spu.memory[0xfabcd] = 0x42;
	CHECK(NativeAudio_CaptureState(snap, (int)sizeof snap));
	CHECK(((struct NativeAudioSnapshot *)snap)->spuSampleMem[0xfabcd] == 0x42);
	s_audio.spu.memory[0xfabcd] = 0x00;
	CHECK(NativeAudio_RestoreState(snap, (int)sizeof snap) && s_audio.spu.memory[0xfabcd] == 0x42);
	{
		const int oldSize = (int)sizeof(struct NativeAudioSnapshot) - (CTR_SPU_WIDE_BYTES - CTR_SPU_RETAIL_BYTES);
		s_audio.spu.memory[0xfabcd] = 0x13;
		CHECK(!NativeAudio_RestoreState(snap, oldSize));
		((struct NativeAudioSnapshot *)snap)->size = (u32)oldSize;
		CHECK(!NativeAudio_RestoreState(snap, (int)sizeof snap));
		CHECK(s_audio.spu.memory[0xfabcd] == 0x13);
	}

	printf("spu memory: %d checks, %d failures\n", checks, failures);
	return failures != 0;
}
