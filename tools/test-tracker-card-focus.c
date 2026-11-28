#include <assert.h>
#include <stdio.h>

#include "../ap/ap_tracker_card_focus.h"

enum { UP, DOWN, LEFT, RIGHT, DIRECTIONS };
static const int dx[] = {0, 0, -1, 1};
static const int dy[] = {-1, 1, 0, 0};

/* Card IDs are positions in the captured grid, ordered down the left then
 * down the right. Names are display data, so repeated titles are allowed. */
static const char *const roomCards[][6] = {
	{"Haven Stadium", "Coco Park 2025", "No Entry",
	 "Ripper Roo", "Country Speedway", "Inferno Island"},
	{"Arabian Heights Day", "Dingo Canyon", "No Entry",
	 "No Entry", "Papu Papu", "No Entry"},
	{"Papu's Pyramid", "Sewer Speedway", "Dragon Mines",
	 "Ripper Roo", "Slide Coliseum", "Green Gem Cup"},
	{"Blue Gem Cup", "Roo's Tubes", "Blizzard Bluff",
	 "Turbo Track", "Papu Papu", "Yellow Gem Cup"},
};

static const AP_TrackerCardPosition positions[] = {
	{0, 0}, {0, 1}, {0, 2}, {1, 0}, {1, 1}, {1, 2},
};
static const int expected[][DIRECTIONS] = {
	{0, 1, 0, 3}, {0, 2, 1, 4}, {1, 2, 2, 5},
	{3, 4, 0, 3}, {3, 5, 1, 4}, {4, 5, 2, 5},
};

static void check_move(const AP_TrackerCardPosition *cards, int count, int start,
                       int direction, int expectedFocus)
{
	int focus = start, pinned = 1;
	AP_TrackerCardMove(cards, count, dx[direction], dy[direction], &focus, &pinned);
	assert(focus == expectedFocus);
	assert(pinned == (expectedFocus == start));
}

static void check_room_hub(const char *const *titles, const int *order)
{
	AP_TrackerCardPosition cards[6];
	int index[6];
	for (int i = 0; i < 6; i++) {
		cards[i] = positions[order[i]];
		index[order[i]] = i;
	}
	assert(AP_TrackerCardAt(cards, 6, 0, 0) == index[0]);

	for (int start = 0; start < 6; start++)
		for (int direction = 0; direction < DIRECTIONS; direction++) {
			int target = expected[start][direction];
			int focus = index[start], pinned = 1;
			AP_TrackerCardMove(cards, 6, dx[direction], dy[direction], &focus, &pinned);
			if (focus != index[target]) {
				fprintf(stderr, "%s (card %d), direction %d: expected %s (card %d)\n",
				        titles[start], start, direction, titles[target], target);
				assert(focus == index[target]);
			}
			assert(pinned == (target == start));
		}
}

static void check_other_hubs(void)
{
	/* In both rooms Glacier Park and Citadel City have three cards per
	 * column, and Gem Stone Valley has four. Use interleaved storage order. */
	for (int room = 0; room < 2; room++)
		for (int hub = 0; hub < 3; hub++) {
			const int rows = hub == 0 ? 4 : 3;
			AP_TrackerCardPosition cards[8];
			const int left = room, right = 1 - room;
			for (int row = 0; row < rows; row++) {
				cards[2 * row + left] = (AP_TrackerCardPosition){0, row};
				cards[2 * row + right] = (AP_TrackerCardPosition){1, row};
			}
			assert(AP_TrackerCardAt(cards, 2 * rows, 0, 0) == left);
			for (int card = 0; card < 2 * rows; card++) {
				check_move(cards, 2 * rows, card, UP, card < 2 ? card : card - 2);
				check_move(cards, 2 * rows, card, DOWN, card / 2 == rows - 1 ? card : card + 2);
				check_move(cards, 2 * rows, card, LEFT, 2 * (card / 2) + left);
				check_move(cards, 2 * rows, card, RIGHT, 2 * (card / 2) + right);
			}
		}
}

int main(void)
{
	/* Pad storage order from TR7's original cursor laps, then permuted. */
	static const int orders[][6] = {{0, 5, 1, 4, 2, 3}, {1, 0, 2, 3, 5, 4}};
	static const int permuted[] = {4, 2, 5, 0, 3, 1};
	for (int roomHub = 0; roomHub < 4; roomHub++) {
		check_room_hub(roomCards[roomHub], orders[roomHub % 2]);
		check_room_hub(roomCards[roomHub], permuted);
	}
	check_other_hubs();

	/* One fewer card in the right column. The left bottom card must
	 * not jump diagonally and the right bottom card must stop at row 2. */
	check_move(positions, 5, 2, RIGHT, 2);
	check_move(positions, 5, 4, DOWN, 4);
	check_move(positions, 5, 2, UP, 1);
	for (int direction = 0; direction < DIRECTIONS; direction++) {
		check_move(positions, 1, 0, direction, 0);
		check_move(NULL, 0, 0, direction, 0);
	}
	assert(AP_TrackerCardAt(NULL, 0, 0, 0) == -1);
	puts("tracker card focus: 363 direction checks passed");
	return 0;
}
