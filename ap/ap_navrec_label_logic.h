#ifndef AP_NAVREC_LABEL_LOGIC_H
#define AP_NAVREC_LABEL_LOGIC_H

// Depth bands proven by Penguin-MODSK's Bot_Trackrom overhead-name renderer,
// expressed as a freestanding helper so boundary behavior stays testable.
static int AP_NavRec_LabelWidthForDepth(int depth)
{
	if (depth < 150)
		return 0;
	if (depth < 190)
		return 13;
	if (depth < 234)
		return 12;
	if (depth < 331)
		return 11;
	if (depth < 475)
		return 10;
	if (depth < 906)
		return 9;
	return 7;
}

#endif // AP_NAVREC_LABEL_LOGIC_H
