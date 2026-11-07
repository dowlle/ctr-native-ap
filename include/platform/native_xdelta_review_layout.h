#ifndef NATIVE_XDELTA_REVIEW_LAYOUT_H
#define NATIVE_XDELTA_REVIEW_LAYOUT_H

struct XdeltaReviewLayout
{
    int reasonY, reasonLines, panelHeight, showStatus;
    int titleLines, tagY;
};

/* The footer starts at 196. Long refusal text ends before the enlarged
   panel's bottom at 188; jobs and transient messages keep the old layout. */
static inline struct XdeltaReviewLayout XdeltaReview_Layout(int offered, int installing, int message, int compactTitle)
{
    struct XdeltaReviewLayout layout = {112, 2, 108, 1, 2, 98};
    if (!offered && !installing && !message)
    {
        layout.reasonLines = 6;
        layout.panelHeight = 124;
        layout.showStatus = 0;
        if (compactTitle)
        {
            layout.titleLines = 1;
            layout.tagY = 84;
            layout.reasonY = 98;
            layout.reasonLines = 7;
        }
    }
    return layout;
}
#endif
