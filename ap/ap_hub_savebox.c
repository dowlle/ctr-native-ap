/* The hub save box never outlives its hub (A4-58). The decisions and the two
 * gaps they close are in ap_hub_savebox_logic.h; this unit asks the engine and
 * acts. Single unity member after ap_fasttravel.c and ap_wumpa_shop_ui.c. */
#include "ap_hub_savebox_logic.h"
#include "ap_fasttravel.h"
#include "ap_tracker.h" /* AP_TrackerLaunchPending */

static int AP_HSBIsBoxMenu(const struct RectMenu *m)
{
	return m != NULL && (m == &data.menuGreenLoadSave || m == &data.menuFourAdvProfiles);
}

int AP_HubSaveBoxMayOpenNow(void)
{
	return AP_HubSaveBoxMayOpen(sdata->Loading.stage == LOAD_IDLE, AP_FastTravelPending(),
	                            AP_TrackerLaunchPending(), AP_CharSwap_PickerOpen());
}

void AP_HubSaveBoxCloseForLoad(void)
{
	struct GameTracker *gt = sdata->gGT;
	/* A4-60c: the hub behind name entry is a menu screen. Its profile page is
	 * the main menu's, which loads the adventure through these same menus. */
	int fromHub = gt != NULL && gt->levelID >= GEM_STONE_VALLEY && gt->levelID <= CITADEL_CITY &&
	              !AP_HubNameEntry_Active();
	int a = AP_HubSaveBoxLoadActions(fromHub, AP_HSBIsBoxMenu(sdata->ptrActiveMenu),
	                                 AP_HSBIsBoxMenu(sdata->ptrDesiredMenu), sdata->ptrLoadSaveObj != 0,
	                                 AP_WumpaShopPageOpen());
	char line[160];
	if (a == 0)
		return;
	if (a & AP_HSB_CLOSE_SHOP)
		AP_WumpaShopClosePage();
	if (a & AP_HSB_CLEAR_DESIRED)
		sdata->ptrDesiredMenu = NULL;
	if (a & AP_HSB_CLEAR_ACTIVE)
	{
		sdata->ptrActiveMenu->state |= NEEDS_TO_CLOSE;
		sdata->ptrActiveMenu = NULL;
	}
	/* The icon thread and its instances live in pools the load rebuilds:
	 * kill them now, while they are still this hub's. */
	if (a & AP_HSB_DESTROY_ICONS)
		SelectProfile_Destroy();
	snprintf(line, sizeof line, "[AP HUB] save box closed for the load of level %d (menu %d, next menu %d, icons %d, shop page %d)\n",
	         sdata->Loading.Lev_ID_To_Load, !!(a & AP_HSB_CLEAR_ACTIVE), !!(a & AP_HSB_CLEAR_DESIRED),
	         !!(a & AP_HSB_DESTROY_ICONS), !!(a & AP_HSB_CLOSE_SHOP));
	AP_LogLine(line);
}
