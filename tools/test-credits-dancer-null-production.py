"""#677: the credits survive a dancer birth that fails.

CS_Thread opcode 0x24 hands CS_Thread_Init's result to CS_Credits_NewDancer.
CS_Thread_Init returns NULL when the dancer's model is not in the loaded level
or a pool is full; retail then reads through address 0 without faulting, native
crashed (SEH 0xc0000005 at CS_Credits.c, the Cortex epilogue). This pins that
native keeps no dancer and the epilogue text still advances, and that the ghost
wait (opcode 0x25) treats no dancer as caught up instead of reading through it.
It cannot run the engine: the credits need a loaded level. The cause, the hub
hint hold skipping the credits level swap, is covered by
tools/test-hub-hint-hold.c. Run from the repository root.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
credits = (root / "game/233/CS_Credits.c").read_text(encoding="utf-8")
thread = (root / "game/233/CS_Thread.c").read_text(encoding="utf-8")


def body(text, head):
    at = text.index(head)
    return text[at:text.index("\n}\n", at) + 3]


dancer = body(credits, "void CS_Credits_NewDancer(struct Thread *dancerTh, int dancerModelID)\n{")
ghosts = body(credits, "int CS_Credits_NewCreditGhosts(void)\n{")
tick = body(credits, "void CS_Credits_ThTick(void)\n{")

# NewDancer: the old dancer is still killed, a NULL thread leaves no dancer
# instance, the instance is only read from a live thread, native only.
guard = dancer.index("if (dancerTh == NULL)")
assert dancer.index("oldDancerThread->flags |= 0x800;") < guard
assert dancer.index("creditsBSS.DancerThread = dancerTh;") < guard
assert "#if defined(CTR_NATIVE)" in dancer[guard - 600:guard]
assert "creditsBSS.dancerInst_invisible = NULL;" in dancer[guard:guard + 200]
deref = dancer.index("creditsBSS.dancerInst_invisible = dancerTh->inst;")
assert guard < dancer.index("else", guard) < deref
assert dancer.count("dancerTh->") == 1
# The epilogue text is set after the guard, whatever the birth did.
text = dancer.index("creditsObj->epilogue_topString = ptrStrings[")
assert deref < text and dancer.index("creditsObj->countdown = 360;") > deref
assert "creditsObj->epilogue_nextString = CS_Credits_GetNextString(creditsObj->epilogue_topString);" in dancer[text:]

# Ghost wait: no dancer means the trail has caught up; checked before the read.
g = ghosts.index("if (creditsBSS.dancerInst_invisible == NULL)")
assert "return 1;" in ghosts[g:g + 80]
assert g < ghosts.index("model = creditsBSS.dancerInst_invisible->model;")
assert "#if defined(CTR_NATIVE)" in ghosts[:g]

# The credits tick already skips the ghost trail without a dancer.
assert "if (danceInst != NULL)" in tick

# Opcode 0x24 still hands the birth to NewDancer (the guard lives there).
assert "dancerThread = (struct Thread *)CS_Thread_Init(dancerModelID, R233.s_g_dancer, initData, 0, 0);" in thread
call = thread.index("dancerThread = (struct Thread *)CS_Thread_Init(dancerModelID, R233.s_g_dancer, initData, 0, 0);")
assert "CS_Credits_NewDancer(dancerThread, (int)opcodeMetaShorts[6]);" in thread[call:call + 300]
print("PASS: credits dancer NULL guard production text")
