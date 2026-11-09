#ifndef AP_WUMPA_SHOP_H
#define AP_WUMPA_SHOP_H

// The Wumpa Shop and the Wumpa goal (#249): pure state and decisions.
//
// BANKING. In an Adventure race, hold ten and bank overflow immediately.
// Hits reduce only held fruit. A finish banks remaining held fruit once;
// exit, restart and a received DeathLink loss exclude the starting fruit.
// Outside a race, all received AP fruit banks at once. A banked
// fruit adds once to the LIFETIME total, which the Wumpa goal counts and which
// never goes down. The wallet balance is lifetime minus the prices of the
// distinct purchases (design note, "Wallet accounting"). Banking runs when
// the seed has a valid `wumpa_shop` block or `goal_wumpa` above 0.
//
// SHOP. Five hubs, `slots` slots each, codes 35028600 + 40 * hub + (n - 1) in
// the apworld's hub order N. Sanity Beach, Gem Stone Valley, Lost Ruins,
// Glacier Park, Citadel City. A slot goes on sale once the lifetime total
// reaches its `unlock_banked`. A server-checked slot that was never bought
// here (Collect, release) shows SOLD and costs nothing.
//
// Header-only and free of engine types so tools/test-wumpa-bank.c and
// tools/test-wumpa-shop-seedcfg.cpp drive the same functions production runs.

#include <stdio.h>

#define AP_WUMPA_SHOP_HUBS 5
#define AP_WUMPA_SHOP_MAX_SLOTS 25
#define AP_WUMPA_SHOP_CODE_BASE 35028600LL
#define AP_WUMPA_SHOP_HUB_STRIDE 40
#define AP_WUMPA_SHOP_BLOCK_VERSION 1
#define AP_WUMPA_KART_MAX 10
// Bounds a hostile room cannot push past (prices and unlocks are seed data).
#define AP_WUMPA_SHOP_PRICE_MAX 100000
#define AP_WUMPA_SHOP_UNLOCK_MAX 1000000
#define AP_WUMPA_GOAL_MAX 5000

// Config state.
#define AP_WUMPA_SHOP_ABSENT  0
#define AP_WUMPA_SHOP_VALID   1
#define AP_WUMPA_SHOP_REFUSED (-1)

typedef struct AP_WumpaShopSlot
{
	long long code;
	int price;
	int unlock;
} AP_WumpaShopSlot;

typedef struct AP_WumpaShopConfig
{
	int state;      // AP_WUMPA_SHOP_*
	int slots;      // per hub, 1..25 when valid
	int goal;       // goal_wumpa, 0 = off, -1 = unreadable (never met)
	int hints;      // wumpa_shop_hints
	AP_WumpaShopSlot slot[AP_WUMPA_SHOP_HUBS][AP_WUMPA_SHOP_MAX_SLOTS];
} AP_WumpaShopConfig;

// Engine level id of each hub in the apworld's hub order.
static inline int AP_WumpaShopHubLevel(int hub)
{
	static const int levels[AP_WUMPA_SHOP_HUBS] = {26, 25, 27, 28, 29};
	return hub >= 0 && hub < AP_WUMPA_SHOP_HUBS ? levels[hub] : -1;
}

// Hub index of an engine level id, or -1 when the level is not a hub.
static inline int AP_WumpaShopHubOfLevel(int level)
{
	int h;
	for (h = 0; h < AP_WUMPA_SHOP_HUBS; h++)
		if (AP_WumpaShopHubLevel(h) == level)
			return h;
	return -1;
}

static inline long long AP_WumpaShopCode(int hub, int n)
{
	return AP_WUMPA_SHOP_CODE_BASE + (long long)AP_WUMPA_SHOP_HUB_STRIDE * hub + (n - 1);
}

static inline int AP_WumpaBankingActive(const AP_WumpaShopConfig *c)
{
	return c != 0 && (c->goal > 0 || c->state == AP_WUMPA_SHOP_VALID);
}

static inline int AP_WumpaShopUsable(const AP_WumpaShopConfig *c)
{
	return c != 0 && c->state == AP_WUMPA_SHOP_VALID && c->slots > 0;
}

// Race modes whose fruit banks (decision 22, 2026-10-28): Adventure races,
// relic races included (they carry RELIC_RACE without ADVENTURE_MODE), never
// Battle mode. Arcade, Time Trial and the menus bank nothing. The Arcade and
// Time Trial flags are refused outright: the main-menu reset clears
// ADVENTURE_MODE but not RELIC_RACE (MM_MenuFlow.c), so a stale relic flag
// must not open banking in the next Arcade or Time Trial race.
#define AP_WUMPA_MODE_ADVENTURE  0x80000u
#define AP_WUMPA_MODE_RELIC      0x4000000u
#define AP_WUMPA_MODE_BATTLE     0x20u
#define AP_WUMPA_MODE_ARCADE     0x400000u
#define AP_WUMPA_MODE_TIME_TRIAL 0x20000u
static inline int AP_WumpaBankRaceMode(unsigned int gameMode1)
{
	return (gameMode1 & (AP_WUMPA_MODE_ADVENTURE | AP_WUMPA_MODE_RELIC)) != 0 &&
	       (gameMode1 & (AP_WUMPA_MODE_BATTLE | AP_WUMPA_MODE_ARCADE | AP_WUMPA_MODE_TIME_TRIAL)) == 0;
}

// Fruit a positive change banks: what would pass the kart's cap of 10.
static inline int AP_WumpaBankOverflow(int held, int delta)
{
	int over;
	if (delta <= 0)
		return 0;
	if (held < 0)
		held = 0;
	over = held + delta - AP_WUMPA_KART_MAX;
	return over > 0 ? over : 0;
}

// The held-at-finish payout, once per race attempt. A new attempt (observed
// countdown, which a pause-menu restart also produces) arms it; the finish
// edge or the attempt's abandonment takes it, whichever comes first. A forced
// DeathLink loss, an exit to the map and a restart take the abandoned payout
// below instead of the full count (ruled 2026-11-04).
typedef struct AP_WumpaFinishLatch
{
	int armed;
} AP_WumpaFinishLatch;

static inline void AP_WumpaFinishArm(AP_WumpaFinishLatch *l) { l->armed = 1; }

static inline int AP_WumpaFinishTake(AP_WumpaFinishLatch *l, int held, int forcedLoss)
{
	int armed = l->armed;
	l->armed = 0;
	if (!armed || forcedLoss || held <= 0)
		return 0;
	return held;
}

// The abandoned-attempt payout (ruled 2026-11-04): the fruit picked up in the
// race minus hits taken, which is the surviving count minus the fruit the
// attempt started with (the kart's own start plus Progressive Starting Wumpa),
// so a restart loop can never bank starting fruit. count < 0 means the attempt
// never touched its fruit. Takes the same latch, so an exit followed by the
// forced reload, or a finish followed by RETRY, pays once.
static inline int AP_WumpaLeaveTake(AP_WumpaFinishLatch *l, int count, int start)
{
	int armed = l->armed;
	l->armed = 0;
	if (!armed || count < 0)
		return 0;
	if (start < 0)
		start = 0;
	return count > start ? count - start : 0;
}

// What a shop row shows and whether Cross buys it.
#define AP_WUMPA_SLOT_FOR_SALE 0 // on sale and affordable
#define AP_WUMPA_SLOT_TOO_POOR 1 // on sale, balance short
#define AP_WUMPA_SLOT_LOCKED   2 // lifetime below its unlock
#define AP_WUMPA_SLOT_SOLD     3 // bought here, or checked by the server
#define AP_WUMPA_SLOT_PENDING  4 // purchase sent, waiting for the room

static inline int AP_WumpaSlotState(const AP_WumpaShopSlot *s, long long lifetime,
                                    long long balance, int bought, int checked,
                                    int pending)
{
	if (bought || checked)
		return AP_WUMPA_SLOT_SOLD;
	if (pending)
		return AP_WUMPA_SLOT_PENDING;
	if (lifetime < s->unlock)
		return AP_WUMPA_SLOT_LOCKED;
	if (balance < s->price)
		return AP_WUMPA_SLOT_TOO_POOR;
	return AP_WUMPA_SLOT_FOR_SALE;
}

// The in-game tracker's per-hub shop line (decision 21, 2026-10-28): slots
// not yet sold, how many of them are on sale (unlocked), and how many of
// those the balance covers now. `states` holds AP_WumpaSlotState results.
typedef struct AP_WumpaShopTally
{
	int left, open, affordable;
} AP_WumpaShopTally;

static inline AP_WumpaShopTally AP_WumpaShopTallyStates(const int *states, int n)
{
	AP_WumpaShopTally t = {0, 0, 0};
	int k;
	for (k = 0; k < n; k++)
	{
		if (states[k] == AP_WUMPA_SLOT_SOLD)
			continue;
		t.left++;
		if (states[k] != AP_WUMPA_SLOT_LOCKED)
			t.open++;
		if (states[k] == AP_WUMPA_SLOT_FOR_SALE)
			t.affordable++;
	}
	return t;
}

static inline int AP_WumpaShopTrackerLine(char *out, int cap, AP_WumpaShopTally t)
{
	if (t.left == 0)
		return snprintf(out, (size_t)cap, "WUMPA SHOP: SOLD OUT");
	return snprintf(out, (size_t)cap, "WUMPA SHOP: %d SLOT%s LEFT, %d OPEN, %d AFFORDABLE",
	                t.left, t.left == 1 ? "" : "S", t.open, t.affordable);
}

// The log line of a received Wumpa Fruit item (AP_WumpaReceive): `held` fruit
// waits for the kart (`pending` in all), `banked` went past 10 into the Wumpa
// bank, `discarded` went past 10 with banking off. "banked" is only ever the
// Wumpa bank (2026-11-02).
static inline int AP_WumpaReceiveLine(char *out, int cap, int held, int pending, int banked,
                                      int discarded, int max)
{
	if (held > 0 && banked > 0)
		return snprintf(out, (size_t)cap, "[AP WUMPA] held for the kart +%d (pending %d), banked +%d past %d\n",
		                held, pending, banked, max);
	if (banked > 0)
		return snprintf(out, (size_t)cap, "[AP WUMPA] banked +%d past %d (kart full)\n", banked, max);
	if (discarded == 0)
		return snprintf(out, (size_t)cap, "[AP WUMPA] held for the kart +%d (pending %d)\n", held, pending);
	if (held == 0)
		return snprintf(out, (size_t)cap, "[AP WUMPA] discarded +%d (kart full at %d)\n", discarded, max);
	return snprintf(out, (size_t)cap, "[AP WUMPA] held for the kart +%d, discarded +%d (kart full at %d)\n",
	                held, discarded, max);
}

// The hub counter (decision 20, 2026-10-28): spendable balance and lifetime
// banked total, labelled COLLECTED (ruling of 2026-11-02). FONT_SMALL is 13 px a
// glyph on the 512 px wide screen, so 39 glyphs fit; six-digit numbers take
// 32 here and 34 in the shop header.
static inline int AP_WumpaHubLine(char *out, int cap, long long balance, long long lifetime)
{
	int n = snprintf(out, (size_t)cap, "WUMPA %lld  COLLECTED %lld", balance, lifetime);
	if (n <= 39) return n;
	return snprintf(out, (size_t)cap, "WUMPA %.4g COLLECTED %.4g", (double)balance, (double)lifetime);
}

// The shop page header: the same two numbers.
static inline int AP_WumpaShopHeaderLine(char *out, int cap, long long balance, long long lifetime)
{
	int n = snprintf(out, (size_t)cap, "BALANCE %lld  COLLECTED %lld", balance, lifetime);
	if (n <= 35) return n;
	// The panel is 464 pixels wide: at most 35 small glyphs. Preserve exact
	// totals where possible; unusually large totals use significant digits.
	n = snprintf(out, (size_t)cap, "BALANCE %lld COLLECTED %lld", balance, lifetime);
	if (n <= 35) return n;
	n = snprintf(out, (size_t)cap, "BALANCE %.3g COLLECTED %.3g", (double)balance, (double)lifetime);
	if (n <= 35) return n;
	return snprintf(out, (size_t)cap, "BALANCE %.2g COLLECTED %.2g", (double)balance, (double)lifetime);
}

// Goal arm: off, or the lifetime total reached the target. An unreadable
// target (-1) is never met, so a bad room value cannot complete the goal.
static inline int AP_WumpaGoalMet(int goal, long long lifetime)
{
	if (goal < 0)
		return 0;
	return goal == 0 || lifetime >= goal;
}

#endif
