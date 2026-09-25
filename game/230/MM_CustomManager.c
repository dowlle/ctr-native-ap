/* Track Manager (Options > Custom Content) for the box authoring build.
   Included by MM_ConfigMenu.c inside CTR_CUSTOM_PACKAGES only. */
#include <platform/native_custom_revision.h>
static int s_saphiVersionTrack;
static size_t s_saphiBrowseSelection;
/* Shared game-font manager: Browse and Installed, no race launch actions. */
static int MM_CustomSearchMatch(const char *text, const char *query)
{
    if (!*query) return 1;
    for (; *text; text++)
    {
        const char *a = text, *b = query;
        while (*a && *b)
        {
            int ac = *a >= 'A' && *a <= 'Z' ? *a + 32 : (unsigned char)*a;
            int bc = *b >= 'A' && *b <= 'Z' ? *b + 32 : (unsigned char)*b;
            if (ac != bc) break;
            a++; b++;
        }
        if (!*b) return 1;
    }
    return 0;
}

static int MM_CustomSourcePreferred(const struct CustomSaphiRevision *a, const struct CustomSaphiRevision *b)
{
    if (!!a->disabledReason[0] != !!b->disabledReason[0]) return !a->disabledReason[0];
    if (a->current != b->current) return a->current;
    int order=CustomRevision_Compare(a->version,b->version);
    if (order) return order>0;
    return (a->modeTags & 1) && !(b->modeTags & 1);
}
static const struct CustomTrackLibraryEntry *MM_CustomSourceInstalled(const struct CustomSaphiRevision *source, int *older)
{
    const struct CustomTrackLibraryEntry *exact=NULL;
    *older=0;
    for(size_t i=0;i<s_contentLibrary.count;i++)
    {
        const struct CustomTrackLibraryEntry *r=&s_contentLibrary.entries[i];
        if(!r->installed || r->local.trackID!=source->trackID) continue;
        if(r->local.levID==source->lev.id && r->local.vrmID==source->vrm.id && !strcmp(r->version,source->version)) exact=r;
        else if(CustomRevision_Compare(source->version,r->version)>0) *older=1;
    }
    return exact;
}

static void MM_ConfigProc_CustomContent(struct RectMenu *menu, uint32_t *ot, struct GamepadBuffer *pad)
{
    size_t visible[CTR_LIBRARY_MAX], count = 0, first;
    const struct CustomSaphiRevision *source = NULL;
    const struct CustomTrackLibraryEntry *installed = NULL;
    int tapped = pad->buttonsTapped;
    int enterVersions = 0;
    int rawPad = Platform_InputRawGamepadButtons();
    int rawTapped = rawPad & ~s_contentSearchPadPrev;
    char line[256];
    (void)menu;
    s_contentSearchPadPrev = rawPad;
    MM_CustomLibrary_Tick(0);
    if (s_saphiRefreshBusy)
    {
        struct CustomSaphiCatalogue *fresh = NULL;
        int result = CustomSaphi_PollRefresh(&fresh, s_saphiMessage, sizeof s_saphiMessage);
        if (result == 1 || result == -2)
        {
            s_saphiRefreshBusy = 0;
            if (result == 1)
            {
                CustomSaphi_Free(&s_saphiCatalogue);
                s_saphiCatalogue = fresh;
            }
        }
    }
    if (s_saphiDownloading)
    {
        int result = CustomSaphi_PollInstall(s_contentInstallPin, s_saphiMessage, sizeof s_saphiMessage);
        if (result == 1 || result == -2)
        {
            s_saphiDownloading = 0;
            if (result == 1)
            {
                s_contentStoreWanted = 1;
                snprintf(s_saphiMessage, sizeof s_saphiMessage, "Installed. Files verified.");
            }
            else fprintf(stderr, "[Saphi] install: %s\n", s_saphiMessage);
        }
    }
    if (s_contentSearchEditing)
    {
        if (!NativeText_Result())
        {
            if (rawTapped & CONN_PAD_CANCEL) NativeText_Resolve(2);
            else if (rawTapped & CONN_PAD_COMMIT) NativeText_Resolve(1);
        }
        if (NativeText_Result())
        {
            if (NativeText_Result() == 2) strcpy(s_saphiQuery, s_contentSearchBackup);
            NativeText_End(); s_contentSearchEditing = 0;
        }
        tapped = 0;
    }
    if ((!s_saphiLoaded || (tapped & BTN_L2)) && !s_saphiRefreshBusy)
    {
        s_saphiLoaded = 1;
        s_saphiRefreshBusy = CustomSaphi_StartRefresh();
        if (!s_saphiRefreshBusy) snprintf(s_saphiMessage,sizeof s_saphiMessage,"Could not refresh Saphi. L2: Retry.");
        if (tapped & BTN_L2) s_contentStoreWanted = 1;
    }
    if (s_saphiVersionTrack && (tapped & (BTN_TRIANGLE | BTN_START)))
    {
        s_saphiVersionTrack=0;s_saphiSelection[0]=s_saphiBrowseSelection;tapped=0;
    }
    if (tapped & (BTN_L1 | BTN_R1))
    {
        if(s_saphiVersionTrack) s_saphiSelection[0]=s_saphiBrowseSelection;
        s_saphiVersionTrack=0;s_contentLibraryTab = !s_contentLibraryTab;
    }
    if (tapped & BTN_R2)
    {
        strcpy(s_contentSearchBackup, s_saphiQuery);
        NativeText_Begin(s_saphiQuery, sizeof s_saphiQuery, 28, 180, 456, 12, 0);
        s_contentSearchEditing = 1;
        tapped = 0;
    }
    if (!s_contentLibraryTab)
    {
        for (size_t i = 0; i < CustomSaphi_Count(s_saphiCatalogue) && count < CTR_LIBRARY_MAX; i++)
        {
            const struct CustomSaphiRevision *r = CustomSaphi_Row(s_saphiCatalogue, i);
            if (s_saphiVersionTrack)
            {
                if(r->trackID==s_saphiVersionTrack) visible[count++]=i;
                continue;
            }
            if (!(MM_CustomSearchMatch(r->title,s_saphiQuery) || MM_CustomSearchMatch(r->author,s_saphiQuery))) continue;
            size_t at=0;
            for(;at<count;at++) if(CustomSaphi_Row(s_saphiCatalogue,visible[at])->trackID==r->trackID) break;
            if(at==count) visible[count++]=i;
            else if(MM_CustomSourcePreferred(r,CustomSaphi_Row(s_saphiCatalogue,visible[at]))) visible[at]=i;
        }
    }
    else
    {
        for (size_t i = 0; i < s_contentLibrary.count && count < CTR_LIBRARY_MAX; i++)
        {
            const struct CustomTrackLibraryEntry *r = &s_contentLibrary.entries[i];
            if (r->installed && (MM_CustomSearchMatch(r->title,s_saphiQuery) || MM_CustomSearchMatch(r->author,s_saphiQuery) ||
                MM_CustomSearchMatch(r->version,s_saphiQuery)))
            {
                size_t at=0;
                for(;at<count;at++) if(r->local.uuid[0] && !strcmp(r->local.uuid,s_contentLibrary.entries[visible[at]].local.uuid)) break;
                if(at==count) visible[count++]=i;
                else if(CustomRevision_Compare(r->version,s_contentLibrary.entries[visible[at]].version)>0) visible[at]=i;
            }
        }
    }
    size_t *selected = &s_saphiSelection[s_contentLibraryTab];
    if (*selected >= count) *selected = 0;
    if (count && (tapped & (BTN_UP | BTN_DOWN)))
    {
        *selected = (*selected + ((tapped & BTN_UP) ? count - 1 : 1)) % count;
        if (!s_saphiDownloading) s_saphiMessage[0] = 0;
        OtherFX_Play(0,1);
    }
    if (count)
    {
        if (!s_contentLibraryTab) source = CustomSaphi_Row(s_saphiCatalogue, visible[*selected]);
        else installed = &s_contentLibrary.entries[visible[*selected]];
    }
    if(source && !s_saphiVersionTrack && (tapped & BTN_SQUARE))
    {
        enterVersions=source->trackID;
        s_saphiMessage[0]=0;tapped=0;
    }
    int hasOlder=0;
    const struct CustomTrackLibraryEntry *sourceInstalled=source ? MM_CustomSourceInstalled(source,&hasOlder):NULL;
    if ((tapped & (BTN_CROSS | BTN_CIRCLE)) && !MM_CustomLibrary_Busy())
    {
        if (sourceInstalled) { s_contentStoreWanted=1; snprintf(s_saphiMessage,sizeof s_saphiMessage,"Installed. Files verified."); }
        else if (source && !source->disabledReason[0])
        {
            s_saphiDownloading = CustomSaphi_StartInstall(source,NativeAssets_GetAssetDir());
            snprintf(s_saphiMessage,sizeof s_saphiMessage,"%s",s_saphiDownloading ? "Downloading and checking selected revision..." : "Could not start download.");
        }
        else if (installed) { s_contentStoreWanted = 1; snprintf(s_saphiMessage,sizeof s_saphiMessage,"Checking installed files..."); }
    }
    DecalFont_DrawLineOT("TRACK MANAGER",256,24,FONT_BIG,JUSTIFY_CENTER | ORANGE,ot);
    MM_CustomText_Draw("L1",42,52,28,1,WHITE,ot);
    MM_CustomText_Draw("Browse",146,52,150,1,JUSTIFY_CENTER | (!s_contentLibraryTab ? ORANGE : WHITE),ot);
    MM_CustomText_Draw("Installed",365,52,156,1,JUSTIFY_CENTER | (s_contentLibraryTab ? ORANGE : WHITE),ot);
    MM_CustomText_Draw("R1",456,52,28,1,WHITE,ot);
    first = (*selected / 7) * 7;
    for (size_t row = first; row < first + 7 && row < count; row++)
    {
        const char *title = !s_contentLibraryTab ? CustomSaphi_Row(s_saphiCatalogue,visible[row])->title : s_contentLibrary.entries[visible[row]].title;
        RECT box = {24,68 + (int)(row-first)*15,260,14};
        if(s_saphiVersionTrack && !s_contentLibraryTab)
        {
            const struct CustomSaphiRevision *r=CustomSaphi_Row(s_saphiCatalogue,visible[row]);
            snprintf(line,sizeof line,"v%.32s %s",r->version,(r->modeTags&1)?"Arcade":(r->modeTags&2)?"TT":"Other");
            title=line;
        }
        MM_CustomText_Draw(title,32,box.y+2,244,1,row==*selected ? ORANGE : WHITE,ot);
        if (row==*selected) CTR_Box_DrawClearBox(&box,&sdata->menuRowHighlight_Normal,TRANS_50_DECAL,ot);
    }
    if (!count) MM_CustomText_Draw(s_saphiRefreshBusy && !s_contentLibraryTab ? "Loading Saphi..." :
        *s_saphiQuery ? "No search matches" : s_contentLibraryTab ? "No installed tracks" : "Catalogue unavailable",32,84,244,3,WHITE,ot);
    if (source || installed)
    {
        MM_CustomText_Draw(source ? source->title : installed->title,305,70,176,2,ORANGE,ot);
        snprintf(line,sizeof line,"v%.32s",source ? source->version : installed->version);
        MM_CustomText_Draw(line,305,98,176,1,WHITE,ot);
        MM_CustomText_Draw(source ? ((source->modeTags&1) ? "Arcade tagged" : (source->modeTags&2) ? "Time Trial" : "Other modes") :
            installed->local.arcade ? "Arcade" : "No Arcade",305,112,176,1,WHITE,ot);
        MM_CustomText_Draw(s_saphiDownloading ? "Downloading..." : source ?
            sourceInstalled ? "* Recheck" : source->disabledReason[0] ? "Unavailable" : hasOlder ? "* Update" : "* Download" : "* Recheck",305,130,176,1,ORANGE,ot);
        const char *status = source && source->disabledReason[0] ? source->disabledReason :
            sourceInstalled ? "Installed" : hasOlder && !s_saphiVersionTrack ? "Update available" : s_saphiMessage[0] ? s_saphiMessage : source ?
            (source->modeTags & 1) ? "Arcade tagged" : (source->modeTags & 2) ? "Time Trial tagged" : "Other modes" : "Files verified";
        MM_CustomText_Draw(status,305,144,176,1,WHITE,ot);
        if(source) MM_CustomText_Draw(s_saphiVersionTrack ? "Back: Tracks" : "[ Versions",305,159,176,1,ORANGE,ot);
    }
    snprintf(line,sizeof line,"R2 Search: %s%s",s_saphiQuery,s_contentSearchEditing ? "_" : "");
    MM_CustomText_Draw(line,256,180,456,1,JUSTIFY_CENTER | WHITE,ot);
    snprintf(line,sizeof line,"%u/%u   L2 Refresh   Back: Options",count ? (unsigned)*selected+1 : 0,(unsigned)count);
    MM_CustomText_Draw(line,256,196,456,1,JUSTIFY_CENTER | WHITE,ot);
    RECT listBox={20,64,268,108}, detailBox={297,64,190,108};
    RECTMENU_DrawInnerRect(&listBox,0,ot);RECTMENU_DrawInnerRect(&detailBox,0,ot);
    if(enterVersions) { s_saphiVersionTrack=enterVersions;s_saphiBrowseSelection=*selected;*selected=0; }
}
