#include <common.h>

// NOTE(aalhendi): ASM-verified NTSC-U 926 0x8002a3d4-0x8002a400
void DECOMP_cseq_opcode07(struct SongSeq *seq)
{
	u8 *note = seq->currNote;
	seq->LR = note[1];
	DECOMP_cseq_opcode_from06and07(seq);
}
