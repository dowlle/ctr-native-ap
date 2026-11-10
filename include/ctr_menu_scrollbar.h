#ifndef CTR_MENU_SCROLLBAR_H
#define CTR_MENU_SCROLLBAR_H

// Geometry of the slim scroll bar on the right edge of a menu list (A4-36).
// A list that shows `visible` of `total` rows from row `top` has a thumb whose
// height is the visible fraction of the track and whose position follows `top`.
// Pure integer maths so tools/test-menu-scrollbar.c can check it without the
// engine; the drawing is CTR_MenuDrawScrollBar in game/230/MM_ConfigMenu.c.

// Returns 0 when the list does not scroll (nothing to draw). Otherwise fills
// the thumb's offset from the track top and its height; the thumb always lies
// inside the track, sits at the top for top = 0 and at the bottom for the last
// window, and never gets thinner than minH.
static inline int CTR_ScrollBarThumb(int total, int visible, int top, int trackH, int minH, int *thumbY, int *thumbH)
{
	int maxTop, h;

	if (visible <= 0 || total <= visible || trackH <= 0)
		return 0;
	maxTop = total - visible;
	if (top < 0)
		top = 0;
	if (top > maxTop)
		top = maxTop;
	h = (int)((long long)trackH * visible / total);
	if (h < minH)
		h = minH;
	if (h > trackH)
		h = trackH;
	*thumbH = h;
	*thumbY = (int)((long long)(trackH - h) * top / maxTop);
	return 1;
}

#endif
