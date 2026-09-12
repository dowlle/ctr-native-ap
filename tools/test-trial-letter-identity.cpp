#include "../ap/ap_lettersanity.h"
#include <cassert>
#include <cstdio>

int main()
{
    int tested = 0;
    for (long long code = 35020999LL; code <= 35021396LL; ++code)
    {
        int slot = -99, letter = -99;
        int found = AP_CustomLetterItemToIdentityPure(code, &slot, &letter);
        if (code >= 35021000LL && code <= 35021395LL)
        {
            assert(found == 1);
            assert(slot == 1 + (code - 35021000LL) / 3);
            assert(letter == (code - 35021000LL) % 3);
            assert(AP_CustomLetterVerifyIndexPure(code) == 200 + code - 35021000LL);
        }
        else {
            assert(!found && slot == -99 && letter == -99);
            assert(AP_CustomLetterVerifyIndexPure(code) == -1);
        }
    }
    for (long code = 35012499L; code <= 35012554L; ++code)
    {
        int index = AP_LetterLocationToItemIndexPure(code);
        if (code >= 35012500L && code <= 35012547L)
            assert(index == 139 + code - 35012500L);
        else if (code >= 35012548L && code <= 35012553L)
            assert(index == 194 + code - 35012548L);
        else assert(index == -1);
    }
    for (int index = -1; index <= 220; ++index)
    {
        int level = -99, letter = -99;
        const int found = AP_LetterItemIndexToIdentityPure(index, &level, &letter);
        if (index >= 139 && index <= 186)
        {
            assert(found == 1);
            assert(level == AP_LetterItemRowToLevelIDPure((index - 139) / 3));
            assert(letter == (index - 139) % 3);
        }
        else if (index >= 194 && index <= 199)
        {
            assert(found == 1);
            assert(level == 16 + (index - 194) / 3);
            assert(letter == (index - 194) % 3);
        }
        else
        {
            assert(found == 0);
            assert(level == -99 && letter == -99);
        }
        ++tested;
    }
    std::printf("PASS: %d item indexes, frozen retail mapping, six trial letters, nonletter rejection\n", tested);
}
