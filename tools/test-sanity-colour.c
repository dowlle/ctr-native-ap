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
            assert(!(tag & ~0x3c00u));
            assert((tag >> 10) >= 1 && (tag >> 10) <= 9);
            assert(!(seen & (1u << (tag >> 10))));
            seen |= 1u << (tag >> 10);
        }
    for (int logic = 0; logic < 3; ++logic) {
        assert(AP_SanityLook(1, logic) == AP_PAD_LOOK_DONE);
        assert(AP_SanityLook(0, logic) == (logic ? AP_PAD_LOOK_OPEN : AP_PAD_LOOK_CLOSED));
    }
    puts("sanity recolour: state precedence, unknown logic, palette and nine unique primitive tags pass");
    return 0;
}
