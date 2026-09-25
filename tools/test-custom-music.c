// cc -Wall -Wextra -Werror -I include tools/test-custom-music.c platform/native_custom_music.c -o /tmp/test-custom-music
//
// Saphi .sca parser and admission check for custom-race music (box authoring
// build, include/platform/native_custom_music.h). Uses a synthetic fixture
// built here; no third-party audio. With a file argument it parses that file
// against the retail HOWL table size (528 slots) and prints a summary, for
// checking real Saphi downloads by hand.
#include <platform/native_custom_music.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks, failures;
#define CHECK(x) do { checks++; if (!(x)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)

#define SLOTS 528

static unsigned char fx[16384];
static size_t fxSize;

static void put16(unsigned char *p, unsigned int v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
static void put32(unsigned char *p, unsigned int v) { put16(p, v & 0xffff); put16(p + 2, v >> 16); }

static size_t chunk(size_t at, const char *tag, const unsigned char *data, size_t size)
{
	memcpy(fx + at, tag, 4);
	put32(fx + at + 4, (unsigned int)size);
	memcpy(fx + at + 8, data, size);
	at += 8 + size;
	while (at & 3) fx[at++] = 0;
	return at;
}

// Three samples (ids 5, 9, 12; 2, 4 and 6 units = 96 bytes of data), a CSEQ
// with one instrument, one drum and three songs, and a META with an odd length.
static const unsigned int kIDs[3] = {5, 9, 12}, kSizes[3] = {2, 4, 6};
static void build(void)
{
	unsigned char bank[CTR_SCA_SECTOR + 96], cseq[64], sizes[6];
	const char *meta = "{\"name\":\"Fixture\",\"author\":\"Test\"}";
	size_t at = 4;
	unsigned int i;

	memset(fx, 0, sizeof fx);
	memcpy(fx, "SCA\x01", 4);
	memset(bank, 0, sizeof bank);
	put16(bank, 3);
	for (i = 0; i < 3; i++)
	{
		put16(bank + 2 + 2 * i, kIDs[i]);
		put16(sizes + 2 * i, kSizes[i]);
	}
	for (i = 0; i < 96; i++) bank[CTR_SCA_SECTOR + i] = (unsigned char)(i + 1);
	memset(cseq, 0xee, sizeof cseq); // padding past songSize, as in real files
	put32(cseq, 48);
	cseq[4] = 1;
	cseq[5] = 1;
	put16(cseq + 6, 3);
	at = chunk(at, "BANK", bank, sizeof bank);
	at = chunk(at, "CSEQ", cseq, sizeof cseq);
	at = chunk(at, "SIZE", sizes, sizeof sizes);
	at = chunk(at, "META", (const unsigned char *)meta, strlen(meta));
	fxSize = at;
}

static size_t find(const char *tag)
{
	size_t i;
	for (i = 4; i + 4 <= fxSize; i++)
		if (!memcmp(fx + i, tag, 4)) return i;
	return 0;
}

static int parse(struct CustomMusicSca *out)
{
	char error[128];
	return CustomMusic_Parse(fx, fxSize, SLOTS, out, error, sizeof error);
}

static int refused_with(const char *expected)
{
	struct CustomMusicSca out;
	char error[128];
	int ok = CustomMusic_Parse(fx, fxSize, SLOTS, &out, error, sizeof error);
	if (ok) return 0;
	if (out.bank || out.numSamples) return 0;
	if (!strstr(error, expected)) { printf("  refusal was \"%s\"\n", error); return 0; }
	return 1;
}

static int summarize(const char *path)
{
	FILE *f = fopen(path, "rb");
	static unsigned char buffer[CTR_SCA_MAX_BYTES + 1];
	struct CustomMusicSca sca;
	char error[128];
	size_t size;
	if (!f) { perror(path); return 1; }
	size = fread(buffer, 1, sizeof buffer, f);
	fclose(f);
	if (!CustomMusic_Parse(buffer, size, SLOTS, &sca, error, sizeof error)) { printf("%s: refused: %s\n", path, error); return 1; }
	printf("%s: %u samples, %u sample bytes, bank %zu bytes, song %zu bytes, %u songs\n", path, sca.numSamples,
	       sca.sampleUnits * 8, sca.bankSize, sca.cseqSize, (unsigned int)(sca.cseq[6] | (sca.cseq[7] << 8)));
	return 0;
}

int main(int argc, char **argv)
{
	struct CustomMusicSca sca;
	unsigned char sectors[3 * CTR_SCA_SECTOR];
	uint16_t table[SLOTS * 2];
	int16_t driver[CTR_SCA_SECTOR / 2];
	const int16_t *later[2];
	unsigned int needed, i;
	char error[128];
	size_t at;

	if (argc > 1)
	{
		int bad = 0;
		for (i = 1; i < (unsigned int)argc; i++) bad |= summarize(argv[i]);
		return bad;
	}

	// The fixture parses and points at the right bytes.
	build();
	CHECK(parse(&sca));
	CHECK(sca.numSamples == 3 && sca.sampleUnits == 12);
	CHECK(sca.bankSize == CTR_SCA_SECTOR + 96 && sca.bank == fx + find("BANK") + 8);
	CHECK(sca.cseq == fx + find("CSEQ") + 8 && sca.cseqSize == 48);
	for (i = 0; i < 3; i++)
		CHECK(CustomMusic_SampleID(&sca, i) == kIDs[i] && CustomMusic_SampleSize(&sca, i) == kSizes[i]);
	CHECK(CustomMusic_SampleID(&sca, 3) == 0 && CustomMusic_SampleSize(&sca, 3) == 0);
	CHECK(CustomMusic_SampleID(NULL, 0) == 0);

	// Sector reads as the engine does them: header sector, then sample data
	// zero-padded to whole sectors; nothing past the bank.
	memset(sectors, 0xaa, sizeof sectors);
	CHECK(CustomMusic_ReadBankSectors(&sca, 0, 1, sectors));
	CHECK(sectors[0] == 3 && sectors[1] == 0 && sectors[2] == 5 && sectors[CTR_SCA_SECTOR - 1] == 0);
	memset(sectors, 0xaa, sizeof sectors);
	CHECK(CustomMusic_ReadBankSectors(&sca, 1, 2, sectors));
	CHECK(sectors[0] == 1 && sectors[95] == 96 && sectors[96] == 0 && sectors[2 * CTR_SCA_SECTOR - 1] == 0);
	CHECK(!CustomMusic_ReadBankSectors(&sca, 2, 1, sectors));
	CHECK(!CustomMusic_ReadBankSectors(&sca, 0, 0, sectors));
	CHECK(!CustomMusic_ReadBankSectors(NULL, 0, 1, sectors));

	// An unknown chunk is skipped; an unpadded last chunk is accepted.
	build();
	at = chunk(fxSize, "XTRA", (const unsigned char *)"abc", 3);
	fxSize = at - 1;
	CHECK(parse(&sca));

	// Refusals, each leaving the output zeroed.
	build(); fx[3] = 2; CHECK(refused_with("Not an SCA version 1"));
	build(); fx[0] = 'X'; CHECK(refused_with("Not an SCA version 1"));
	build(); fxSize = 3; CHECK(refused_with("size out of range"));
	build(); CHECK(!CustomMusic_Parse(fx, CTR_SCA_MAX_BYTES + 1, SLOTS, &sca, error, sizeof error));
	build(); fxSize = find("META") + 6; CHECK(refused_with("Truncated SCA chunk header"));
	build(); put32(fx + find("META") + 4, 4096); CHECK(refused_with("runs past the file"));
	build(); memcpy(fx + find("META"), "BANK", 4); CHECK(refused_with("Duplicate SCA chunk"));
	build(); memcpy(fx + find("SIZE"), "ZISE", 4); CHECK(refused_with("needs BANK, CSEQ and SIZE"));
	build(); memcpy(fx + find("CSEQ"), "QESC", 4); CHECK(refused_with("needs BANK, CSEQ and SIZE"));
	build(); put16(fx + find("BANK") + 8, 0); CHECK(refused_with("sample count out of range"));
	build(); put16(fx + find("BANK") + 8, CTR_SCA_MAX_SAMPLES + 1); CHECK(refused_with("sample count out of range"));
	build(); put16(fx + find("BANK") + 8 + 2, SLOTS); CHECK(refused_with("outside the HOWL table"));
	build(); put16(fx + find("BANK") + 8 + 4, 5); CHECK(refused_with("repeats a sample id"));
	build(); put16(fx + find("BANK") + 8, 2); CHECK(refused_with("SIZE does not match"));
	build(); put16(fx + find("SIZE") + 8 + 2, 0); CHECK(refused_with("zero size"));
	build(); put16(fx + find("SIZE") + 8 + 4, 8); CHECK(refused_with("shorter than SIZE"));
	build(); put16(fx + find("SIZE") + 8 + 2, 3); CHECK(refused_with("not whole ADPCM blocks"));
	build(); put32(fx + find("CSEQ") + 8, 65); CHECK(refused_with("CSEQ size out of range"));
	build(); put32(fx + find("CSEQ") + 8, 4); CHECK(refused_with("CSEQ size out of range"));
	build(); put16(fx + find("CSEQ") + 8 + 6, 0); CHECK(refused_with("has no song"));
	build(); fx[find("CSEQ") + 8 + 4] = 4; CHECK(refused_with("tables exceed"));
	{
		// A bank chunk with only the header sector has no samples.
		static const unsigned char head[CTR_SCA_SECTOR] = {1, 0, 5, 0};
		unsigned char cseq[16] = {16, 0, 0, 0, 0, 0, 1, 0};
		unsigned char sizes[2] = {2, 0};
		memset(fx, 0, sizeof fx);
		memcpy(fx, "SCA\x01", 4);
		at = chunk(4, "BANK", head, sizeof head);
		at = chunk(at, "CSEQ", cseq, sizeof cseq);
		fxSize = chunk(at, "SIZE", sizes, sizeof sizes);
		CHECK(refused_with("no sample data"));
	}
	build(); CHECK(!CustomMusic_Parse(fx, fxSize, SLOTS, NULL, error, sizeof error));
	CHECK(!CustomMusic_Parse(NULL, 16, SLOTS, &sca, error, sizeof error) && sca.bank == NULL);

	// SPU ceiling: the authoring engine refuses a bank ending at or past 0xfe000
	// (1 MiB SPU memory less the retail 8 KB top reserve).
	CHECK(CTR_SCA_SPU_LIMIT == 0xfe000);
	CHECK(CustomMusic_SpuFits(0x202, (CTR_SCA_SPU_LIMIT / 8) - 0x202 - 1));
	CHECK(!CustomMusic_SpuFits(0x202, (CTR_SCA_SPU_LIMIT / 8) - 0x202));
	CHECK(!CustomMusic_SpuFits(0xffffffffu, 0xffffffffu));

	// Admission. Table: slot 5 already on the SPU (bank 0) with the .sca's
	// size, everything else unloaded with retail size 10.
	build();
	CHECK(parse(&sca));
	for (i = 0; i < SLOTS; i++) { table[2 * i] = 0; table[2 * i + 1] = 10; }
	table[2 * 5] = 0x300; table[2 * 5 + 1] = 2;
	memset(driver, 0, sizeof driver);
	driver[0] = 2; driver[1] = 100; driver[2] = 101; // 20 units, no shared slot
	later[0] = driver;
	CHECK(CustomMusic_Admit(&sca, table, SLOTS, 0x1000, later, 1, &needed, error, sizeof error));
	CHECK(needed == 0x1000 + 12 + 20 && error[0] == 0);
	CHECK(CustomMusic_Admit(&sca, table, SLOTS, 0x1000, NULL, 0, &needed, error, sizeof error) && needed == 0x1000 + 12);
	// A loaded slot the .sca would resize.
	table[2 * 5 + 1] = 3;
	CHECK(!CustomMusic_Admit(&sca, table, SLOTS, 0x1000, later, 1, &needed, error, sizeof error));
	CHECK(strstr(error, "already loaded") != NULL && needed == 0);
	table[2 * 5 + 1] = 2;
	// A driver bank sharing a slot: fine with the same size, refused otherwise.
	driver[0] = 3; driver[3] = 9;
	table[2 * 9 + 1] = 4;
	CHECK(CustomMusic_Admit(&sca, table, SLOTS, 0x1000, later, 1, &needed, error, sizeof error) && needed == 0x1000 + 12 + 24);
	table[2 * 9 + 1] = 10;
	CHECK(!CustomMusic_Admit(&sca, table, SLOTS, 0x1000, later, 1, &needed, error, sizeof error));
	CHECK(strstr(error, "driver bank uses") != NULL);
	driver[0] = 2;
	// Too big for the SPU once the driver bank is counted (the Arcade case).
	CHECK(CustomMusic_Admit(&sca, table, SLOTS, (CTR_SCA_SPU_LIMIT / 8) - 12 - 20 - 1, later, 1, &needed, error, sizeof error));
	CHECK(!CustomMusic_Admit(&sca, table, SLOTS, (CTR_SCA_SPU_LIMIT / 8) - 12 - 20, later, 1, &needed, error, sizeof error));
	CHECK(strstr(error, "do not fit in SPU memory") != NULL && needed == (CTR_SCA_SPU_LIMIT / 8));
	// A second later bank (a character bank) is counted too.
	later[1] = driver;
	CHECK(!CustomMusic_Admit(&sca, table, SLOTS, (CTR_SCA_SPU_LIMIT / 8) - 12 - 40, later, 2, &needed, error, sizeof error));
	// Malformed driver headers and inputs.
	driver[0] = -1;
	CHECK(!CustomMusic_Admit(&sca, table, SLOTS, 0x1000, later, 1, &needed, error, sizeof error));
	driver[0] = 1; driver[1] = SLOTS;
	CHECK(!CustomMusic_Admit(&sca, table, SLOTS, 0x1000, later, 1, &needed, error, sizeof error));
	driver[1] = 100;
	CHECK(!CustomMusic_Admit(&sca, table, 12, 0x1000, later, 1, &needed, error, sizeof error));
	CHECK(!CustomMusic_Admit(NULL, table, SLOTS, 0x1000, later, 1, &needed, error, sizeof error));
	CHECK(!CustomMusic_Admit(&sca, table, SLOTS, 0x1000, NULL, 1, &needed, error, sizeof error));
	later[0] = NULL;
	CHECK(!CustomMusic_Admit(&sca, table, SLOTS, 0x1000, later, 1, &needed, error, sizeof error));

	printf("custom music: %d checks, %d failures\n", checks, failures);
	return failures != 0;
}
