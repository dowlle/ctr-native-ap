#ifndef NATIVE_CUSTOM_MUSIC_H
#define NATIVE_CUSTOM_MUSIC_H
// Custom-track music for the box authoring build (CTR_CUSTOM_PACKAGES only).
//
// Project Saphi ships a track's audio as a separate .sca file. The container
// wraps two retail HOWL structures the engine already plays:
//
//   "SCA" 0x01                      magic and version
//   chunks: 4-byte tag, u32 size, data, next chunk at the next 4-byte boundary
//     BANK  a retail level sound bank as stored in KART.HWL: one 0x800 header
//           sector (s16 numSamples, s16 sample slot ids), then PS1 SPU ADPCM
//           sample data back to back. The data may end mid-sector.
//     CSEQ  a retail CSEQ pack (struct CseqHeader, instruments, drums, songs)
//     SIZE  one u16 per bank sample, in bank order: the spuSize (8-byte units)
//           the engine must use for that slot while this bank is loaded
//     META  JSON name and author, display only
//
// The layout is derived from Saphi's files and checked against every current
// .sca in the Saphi catalogue (research note, 2026-09-25). No public spec
// exists. Anything that does not match is refused and the race keeps the
// default music.
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define CTR_SCA_SECTOR 0x800
// Retail HOWL limits the parser enforces (namespace_Howl.h, HOWL_Load.c).
#define CTR_SCA_MAX_BYTES (1024 * 1024)
#define CTR_SCA_MAX_SAMPLES ((CTR_SCA_SECTOR - 2) / 2)
#define CTR_SCA_MAX_SONG_BYTES 0x10000
// SPU transfer ceiling the engine checks in Bank_AssignSpuAddrs (bytes). This
// code exists only in the authoring build, which has the 1 MiB SPU memory.
#include <platform/native_spu_memory.h>
#define CTR_SCA_SPU_LIMIT CTR_SPU_WIDE_CEILING

struct CustomMusicSca
{
	const unsigned char *bank; // BANK chunk data: header sector, then samples
	size_t bankSize;
	const unsigned char *cseq; // CSEQ chunk data, starting at struct CseqHeader
	size_t cseqSize;           // CseqHeader.songSize, not the padded chunk size
	const unsigned char *sizes; // SIZE chunk: u16 little-endian per sample
	unsigned int numSamples;
	unsigned int sampleUnits; // sum of SIZE entries, in 8-byte units
};

// Validate an .sca held in memory and point out into it (no copy). spuSlots
// is the HOWL header's numSpuAddrs; every sample id must be below it. Checks:
// magic and version, chunk bounds, one each of BANK, CSEQ and SIZE, bank
// header, SIZE length with every size a whole number of 16-byte blocks,
// sample data covering the SIZE total, and a CSEQ whose
// header, instruments, drums and song offset table fit its songSize.
// Returns 1 on success; on refusal *out is zeroed and error names the reason.
int CustomMusic_Parse(const void *data, size_t size, unsigned int spuSlots,
                      struct CustomMusicSca *out, char *error, size_t errorSize);
unsigned int CustomMusic_SampleID(const struct CustomMusicSca *sca, unsigned int index);
unsigned int CustomMusic_SampleSize(const struct CustomMusicSca *sca, unsigned int index);

// Copy numSector sectors of the BANK, starting at sector firstSector, the way
// the engine reads a bank from KART.HWL. Bytes past the chunk end read as
// zero. Returns 0 if the range starts past the bank or is empty.
int CustomMusic_ReadBankSectors(const struct CustomMusicSca *sca, unsigned int firstSector,
                                unsigned int numSector, void *destination);

// The engine's SPU check (Bank_AssignSpuAddrs stage 2) applied to a run of
// banks loaded back to back from startUnits: every bank must end below
// CTR_SCA_SPU_LIMIT. Units are 8 bytes. Because banks only append, checking
// the total end is the same as checking each bank.
int CustomMusic_SpuFits(unsigned int startUnits, unsigned int totalUnits);

// Decide whether a parsed .sca may replace the level bank of this race.
// spuTable is the engine's howl_spuAddrs (u16 spuAddr, u16 spuSize per slot,
// spuSlots entries) before any patch. startUnits is where the level bank
// starts, in 8-byte units (sdata->audioAllocPtr converted from the engine's
// address units, native_spu_memory.h). laterBanks are the header sectors (s16
// numSamples, s16 ids) of the banks this race loads after it: bank 54 and any
// character bank. Refuses when:
//   - a slot already on the SPU (bank 0) would get a different size,
//   - a later bank shares a slot whose size the .sca changes,
//   - the level bank plus the later banks would pass the SPU ceiling, where
//     the engine would silently skip a driver bank.
// neededUnits (optional) receives startUnits plus everything loaded.
int CustomMusic_Admit(const struct CustomMusicSca *sca, const uint16_t *spuTable, unsigned int spuSlots,
                      unsigned int startUnits, const int16_t *const *laterBanks, unsigned int laterCount,
                      unsigned int *neededUnits, char *error, size_t errorSize);

#ifdef __cplusplus
}
#endif
#endif
