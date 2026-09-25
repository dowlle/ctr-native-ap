#include <platform/native_custom_music.h>
#include <stdio.h>
#include <string.h>

// Box authoring build only (CTR_CUSTOM_PACKAGES): parser for Saphi .sca
// containers. See include/platform/native_custom_music.h for the layout.

static unsigned int read_u16(const unsigned char *p) { return (unsigned int)p[0] | ((unsigned int)p[1] << 8); }
static uint32_t read_u32(const unsigned char *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int refuse(struct CustomMusicSca *out, char *error, size_t errorSize, const char *reason)
{
	if (out) memset(out, 0, sizeof *out);
	if (error && errorSize) snprintf(error, errorSize, "%s", reason);
	return 0;
}

int CustomMusic_Parse(const void *data, size_t size, unsigned int spuSlots,
                      struct CustomMusicSca *out, char *error, size_t errorSize)
{
	const unsigned char *bytes = (const unsigned char *)data;
	const unsigned char *bank = NULL, *cseq = NULL, *sizes = NULL;
	size_t bankSize = 0, cseqChunk = 0, sizesSize = 0, offset = 4;
	unsigned int numSamples, i, units = 0;
	unsigned int numLong, numDrums, numSongs;
	size_t songSize, tables;

	if (!out) return refuse(out, error, errorSize, "No output");
	if (!bytes || size < 4 || size > CTR_SCA_MAX_BYTES) return refuse(out, error, errorSize, "SCA size out of range");
	if (memcmp(bytes, "SCA\x01", 4) != 0) return refuse(out, error, errorSize, "Not an SCA version 1 file");

	while (offset < size)
	{
		const unsigned char *tag;
		size_t chunk;
		if (size - offset < 8) return refuse(out, error, errorSize, "Truncated SCA chunk header");
		tag = bytes + offset;
		chunk = read_u32(bytes + offset + 4);
		if (chunk > size - offset - 8) return refuse(out, error, errorSize, "SCA chunk runs past the file");
		if (!memcmp(tag, "BANK", 4) || !memcmp(tag, "CSEQ", 4) || !memcmp(tag, "SIZE", 4))
		{
			const unsigned char **slot = !memcmp(tag, "BANK", 4) ? &bank : !memcmp(tag, "CSEQ", 4) ? &cseq : &sizes;
			size_t *slotSize = slot == &bank ? &bankSize : slot == &cseq ? &cseqChunk : &sizesSize;
			if (*slot) return refuse(out, error, errorSize, "Duplicate SCA chunk");
			*slot = bytes + offset + 8;
			*slotSize = chunk;
		}
		offset += 8 + chunk;
		// Chunks start on 4-byte boundaries; the last one may end unpadded.
		offset = (offset + 3) & ~(size_t)3;
	}
	if (!bank || !cseq || !sizes) return refuse(out, error, errorSize, "SCA needs BANK, CSEQ and SIZE");

	// BANK: the retail bank header sector, then sample data.
	if (bankSize <= CTR_SCA_SECTOR) return refuse(out, error, errorSize, "SCA bank has no sample data");
	numSamples = read_u16(bank);
	if (numSamples == 0 || numSamples > CTR_SCA_MAX_SAMPLES) return refuse(out, error, errorSize, "SCA bank sample count out of range");
	for (i = 0; i < numSamples; i++)
	{
		unsigned int id = read_u16(bank + 2 + 2 * i);
		unsigned int j;
		if (id >= spuSlots) return refuse(out, error, errorSize, "SCA sample id outside the HOWL table");
		for (j = 0; j < i; j++)
			if (read_u16(bank + 2 + 2 * j) == id) return refuse(out, error, errorSize, "SCA bank repeats a sample id");
	}

	// SIZE: one spuSize per sample; the sample data must cover their sum.
	if (sizesSize != 2 * (size_t)numSamples) return refuse(out, error, errorSize, "SCA SIZE does not match the bank");
	for (i = 0; i < numSamples; i++)
	{
		unsigned int units1 = read_u16(sizes + 2 * i);
		if (units1 == 0) return refuse(out, error, errorSize, "SCA sample has zero size");
		// Whole 16-byte ADPCM blocks: the authoring build's SPU addresses are
		// in 16-byte units (native_spu_memory.h).
		if (units1 & 1) return refuse(out, error, errorSize, "SCA sample size is not whole ADPCM blocks");
		units += units1;
	}
	if ((size_t)units * 8 > bankSize - CTR_SCA_SECTOR) return refuse(out, error, errorSize, "SCA sample data shorter than SIZE");

	// CSEQ: header (songSize, long and drum counts, song count), 12-byte
	// instruments, 8-byte drums, s16 song offsets, all inside songSize.
	if (cseqChunk < 8) return refuse(out, error, errorSize, "SCA CSEQ too short");
	songSize = read_u32(cseq);
	numLong = cseq[4];
	numDrums = cseq[5];
	numSongs = read_u16(cseq + 6);
	if (songSize < 8 || songSize > cseqChunk || songSize > CTR_SCA_MAX_SONG_BYTES)
		return refuse(out, error, errorSize, "SCA CSEQ size out of range");
	if (numSongs == 0) return refuse(out, error, errorSize, "SCA CSEQ has no song");
	tables = 8 + 12 * (size_t)numLong + 8 * (size_t)numDrums + 2 * (size_t)numSongs;
	if (tables >= songSize) return refuse(out, error, errorSize, "SCA CSEQ tables exceed its size");

	out->bank = bank;
	out->bankSize = bankSize;
	out->cseq = cseq;
	out->cseqSize = songSize;
	out->sizes = sizes;
	out->numSamples = numSamples;
	out->sampleUnits = units;
	if (error && errorSize) error[0] = 0;
	return 1;
}

unsigned int CustomMusic_SampleID(const struct CustomMusicSca *sca, unsigned int index)
{
	return sca && index < sca->numSamples ? read_u16(sca->bank + 2 + 2 * index) : 0;
}

unsigned int CustomMusic_SampleSize(const struct CustomMusicSca *sca, unsigned int index)
{
	return sca && index < sca->numSamples ? read_u16(sca->sizes + 2 * index) : 0;
}

int CustomMusic_ReadBankSectors(const struct CustomMusicSca *sca, unsigned int firstSector,
                                unsigned int numSector, void *destination)
{
	size_t start, want, have;
	if (!sca || !sca->bank || !destination || numSector == 0) return 0;
	start = (size_t)firstSector * CTR_SCA_SECTOR;
	if (start >= sca->bankSize) return 0;
	want = (size_t)numSector * CTR_SCA_SECTOR;
	have = sca->bankSize - start;
	if (have > want) have = want;
	memcpy(destination, sca->bank + start, have);
	memset((unsigned char *)destination + have, 0, want - have);
	return 1;
}

int CustomMusic_SpuFits(unsigned int startUnits, unsigned int totalUnits)
{
	unsigned long long end = ((unsigned long long)startUnits + totalUnits) * 8;
	return end < CTR_SCA_SPU_LIMIT;
}

static int sca_index_of(const struct CustomMusicSca *sca, unsigned int id)
{
	unsigned int i;
	for (i = 0; i < sca->numSamples; i++)
		if (CustomMusic_SampleID(sca, i) == id) return (int)i;
	return -1;
}

int CustomMusic_Admit(const struct CustomMusicSca *sca, const uint16_t *spuTable, unsigned int spuSlots,
                      unsigned int startUnits, const int16_t *const *laterBanks, unsigned int laterCount,
                      unsigned int *neededUnits, char *error, size_t errorSize)
{
	unsigned long long total;
	unsigned int i, b;

	if (neededUnits) *neededUnits = 0;
	if (!sca || !sca->bank || !spuTable || (laterCount && !laterBanks))
		return refuse(NULL, error, errorSize, "No SCA or HOWL table");

	for (i = 0; i < sca->numSamples; i++)
	{
		unsigned int id = CustomMusic_SampleID(sca, i);
		if (id >= spuSlots) return refuse(NULL, error, errorSize, "SCA sample id outside the HOWL table");
		if (spuTable[2 * id] != 0 && spuTable[2 * id + 1] != CustomMusic_SampleSize(sca, i))
			return refuse(NULL, error, errorSize, "SCA resizes a sample that is already loaded");
	}

	total = (unsigned long long)startUnits + sca->sampleUnits;
	for (b = 0; b < laterCount; b++)
	{
		const int16_t *bank = laterBanks[b];
		int count;
		if (!bank) return refuse(NULL, error, errorSize, "Missing driver bank header");
		count = bank[0];
		if (count < 0 || count > (int)CTR_SCA_MAX_SAMPLES) return refuse(NULL, error, errorSize, "Driver bank header out of range");
		for (i = 0; i < (unsigned int)count; i++)
		{
			int id = bank[1 + i];
			int shared;
			if (id < 0 || (unsigned int)id >= spuSlots) return refuse(NULL, error, errorSize, "Driver bank id outside the HOWL table");
			shared = sca_index_of(sca, (unsigned int)id);
			if (shared >= 0 && CustomMusic_SampleSize(sca, (unsigned int)shared) != spuTable[2 * id + 1])
				return refuse(NULL, error, errorSize, "SCA resizes a sample a driver bank uses");
			total += spuTable[2 * id + 1];
		}
	}

	if (total > 0xffffffffull) total = 0xffffffffull;
	if (neededUnits) *neededUnits = (unsigned int)total;
	if (!CustomMusic_SpuFits((unsigned int)total, 0))
		return refuse(NULL, error, errorSize, "SCA and the driver banks do not fit in SPU memory");
	if (error && errorSize) error[0] = 0;
	return 1;
}
