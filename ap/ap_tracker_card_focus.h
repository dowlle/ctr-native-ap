#ifndef AP_TRACKER_CARD_FOCUS_H
#define AP_TRACKER_CARD_FOCUS_H

typedef struct {
	int column, row;
} AP_TrackerCardPosition;

static inline int AP_TrackerCardAt(const AP_TrackerCardPosition *cards, int count,
                                   int column, int row)
{
	for (int i = 0; i < count; i++)
		if (cards[i].column == column && cards[i].row == row)
			return i;
	return -1;
}

/* Layout assigns consecutive rows in each column. Focus keeps its node
 * index so the same card still owns its map pin, checks and activities. */
static inline void AP_TrackerCardMove(const AP_TrackerCardPosition *cards, int count,
                                      int dx, int dy, int *focus, int *pinned)
{
	if (*focus < 0 || *focus >= count)
		return;
	AP_TrackerCardPosition from = cards[*focus];
	int next = AP_TrackerCardAt(cards, count, from.column + dx, from.row + dy);
	if (next >= 0 && next != *focus) {
		*focus = next;
		*pinned = 0;
	}
}

#endif
