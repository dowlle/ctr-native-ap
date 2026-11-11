// cc -std=c11 -Wall -Wextra -Iap tools/test-sanity-colour.c -o /tmp/test-sanity-colour
#include <assert.h>
#include <stdio.h>
#include "ap_sanity_colour.h"
int main(void) {
    unsigned seen = 0;
    int looks[] = {AP_PAD_LOOK_OPEN, AP_PAD_LOOK_CLOSED, AP_PAD_LOOK_DONE};
    assert(AP_SANITY_VISIBILITY >= 0 && AP_SANITY_VISIBILITY <= 2);
    assert(AP_PAD_RED_RGB == 0xB85648u);
    for (int state = 0; state < 3; ++state)
        for (int variant = 0; variant < 3; ++variant) {
            unsigned tag = AP_SanityTag(looks[state], variant);
            assert(!(tag & ~NATIVE_SANITY_TPAGE_MASK));
            assert(tag & NATIVE_SANITY_TPAGE_MARKER);
            assert((NativeSanityDecodeTag(tag)) >= 1 && (NativeSanityDecodeTag(tag)) <= 9);
            assert(!(seen & (1u << (NativeSanityDecodeTag(tag)))));
            seen |= 1u << (NativeSanityDecodeTag(tag));
            unsigned dark = AP_SanityRecolourRGB(0x32080808, NativeSanityDecodeTag(tag));
            unsigned light = AP_SanityRecolourRGB(0x32808080, NativeSanityDecodeTag(tag));
            assert((dark >> 24) == 0x32 && (light >> 24) == 0x32);
            for (int channel = 0; channel < 3; ++channel)
                assert(((dark >> (channel * 8)) & 255) < ((light >> (channel * 8)) & 255));
            if (state == 0) assert(((light >> 8) & 255) > (light & 255));
            if (state == 1) assert((light & 255) > ((light >> 8) & 255));
            if (state == 2 && variant == 2) {
                unsigned grey = AP_SanityRecolourRGB(0x3200a0ff, NativeSanityDecodeTag(tag));
                assert((grey & 255) == ((grey >> 8) & 255));
                assert((grey & 255) == ((grey >> 16) & 255));
            }
        }
    for (int logic = 0; logic < 3; ++logic) {
        assert(AP_SanityLook(1, logic) == AP_PAD_LOOK_DONE);
        assert(AP_SanityLook(0, logic) == (logic ? AP_PAD_LOOK_OPEN : AP_PAD_LOOK_CLOSED));
    }
    assert(AP_SanityRecolourRGB(0x32abcdef, 0) == 0x32abcdef);
    assert(AP_SanityRecolourRGB(0x32abcdef, 10) == 0x32abcdef);
    puts("sanity recolour: state precedence, unknown logic, palette and nine unique primitive tags pass");
    return 0;
}
