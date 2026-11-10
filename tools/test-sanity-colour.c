// cc -std=c11 -Wall -Wextra -Iap tools/test-sanity-colour.c -o /tmp/test-sanity-colour
#include <assert.h>
#include <stdio.h>
#include "ap_sanity_colour.h"
int main(void)
{
    int variant, logic, checked;
    assert(AP_SANITY_VISIBILITY >= 0 && AP_SANITY_VISIBILITY <= 2);
    assert(AP_PAD_RED_RGB == 0xB85648u);
    assert(AP_SanityHaloBudget(-1) == 0);
    assert(AP_SanityHaloBudget(1023) == 0);
    assert(AP_SanityHaloBudget(2047) == 0);
    assert(AP_SanityHaloBudget(2048) == 1);
    assert(AP_SanityHaloBudget(8192) == 4);
    assert(AP_SanityHaloBudget(1000000) == 32);
    assert(AP_SanityHaloFilterColour(AP_PAD_LOOK_OPEN, 1) == 0x50001eu);
    assert(AP_SanityHaloFilterColour(AP_PAD_LOOK_CLOSED, 1) == 0x3c3c00u);
    assert(AP_SanityHaloFilterColour(AP_PAD_LOOK_DONE, 1) == 0);
    for (checked = 0; checked <= 1; ++checked)
        for (logic = 0; logic <= 2; ++logic) {
            int look = AP_SanityLook(checked, logic);
            assert(look == (checked ? AP_PAD_LOOK_DONE : logic ? AP_PAD_LOOK_OPEN : AP_PAD_LOOK_CLOSED));
            assert(AP_SanityTint(look) == (AP_PadLookRGB(look) << 4));
            for (variant = 0; variant < 3; ++variant) {
                unsigned glow = AP_SanityHaloColour(look, variant);
                assert(glow > 0 && glow < 0x1000000u);
                if (variant) {
                    assert(AP_SanityBlend(variant) > AP_SanityBlend(variant - 1));
                    assert(glow > AP_SanityHaloColour(look, variant - 1));
                }
            }
        }
    puts("sanity colour: completion, logic fallback, pad palette and all three intensities pass");
    return 0;
}
