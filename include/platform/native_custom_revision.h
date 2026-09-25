#ifndef NATIVE_CUSTOM_REVISION_H
#define NATIVE_CUSTOM_REVISION_H
#include <string.h>
/* Natural ordering for source version labels; never changes an exact seed pin.
   Numeric runs compare by magnitude without integer overflow. */
static inline int CustomRevision_Compare(const char *a, const char *b)
{
    while (*a && *b)
    {
        if (*a>='0' && *a<='9' && *b>='0' && *b<='9')
        {
            const char *ae=a,*be=b,*az,*bz;
            while (*ae>='0' && *ae<='9') ae++;
            while (*be>='0' && *be<='9') be++;
            az=a; bz=b; while(az<ae && *az=='0')az++; while(bz<be && *bz=='0')bz++;
            if (ae-az!=be-bz) return ae-az>be-bz ? 1:-1;
            int c=memcmp(az,bz,(size_t)(ae-az)); if(c)return c;
            a=ae;b=be;continue;
        }
        if (*a!=*b) return (unsigned char)*a-(unsigned char)*b;
        a++;b++;
    }
    if (!*a && *b=='-') return 1;
    if (!*b && *a=='-') return -1;
    return (unsigned char)*a-(unsigned char)*b;
}
#endif
