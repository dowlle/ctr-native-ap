// gcc -m32 -std=gnu11 -Wall -Wextra -Werror -DCTR_CUSTOM_TRACKS -I include tools/test-custom-road-identity.c platform/native_custom_content_verify.c -o /tmp/test-custom-road-identity
#include <platform/native_custom_content_verify.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { Mesh = 0x190, Quad = 0x1b0, Vertices = 0x210, Map = 0x250 };

static void put32(unsigned char *at, uint32_t value)
{
    for (unsigned byte = 0; byte < 4; ++byte) at[byte] = value >> (8 * byte);
}

static int checks, failures;
#define CHECK(condition) do { ++checks; if (!(condition)) { ++failures; fprintf(stderr, "line %d: %s\n", __LINE__, #condition); } } while (0)

int main(void)
{
    unsigned char lev[Map + 20] = {0};
    unsigned char *body = lev + 4;
    put32(lev, Map);
    put32(body, Mesh);
    put32(body + Mesh, 1);
    put32(body + Mesh + 4, 3);
    put32(body + Mesh + 0xc, Quad);
    put32(body + Mesh + 0x10, Vertices);
    put32(body + Map, 12);
    put32(body + Map + 8, Mesh + 0xc);
    put32(body + Map + 12, Mesh + 0x10);
    body[Quad + 0x13] = 0x10;
    body[Quad + 2] = 1;
    body[Quad + 4] = 2;
    // Points (-1, 0, 0) and (2, 0, 0), with a duplicate of the first point.
    body[Vertices] = body[Vertices + 1] = 0xff;
    body[Vertices + 16] = 2;
    body[Vertices + 32] = body[Vertices + 33] = 0xff;
    char original[65], new_hash[65];
    CHECK(CustomContentVerify_RoadHash(lev, sizeof lev, original));
    CHECK(!strcmp(original, "af6096cc3b0e2b8ad504649090962c9418028c25ac583d5708427cc22313e21e"));
    body[Quad] = 1;
    body[Quad + 2] = 0;
    body[Quad + 4] = 0;
    body[0xc] = 17;
    body[Vertices + 8] = 30;
    CHECK(CustomContentVerify_RoadHash(lev, sizeof lev, new_hash) && !strcmp(original, new_hash));
    body[Vertices + 16]++;
    CHECK(CustomContentVerify_RoadHash(lev, sizeof lev, new_hash) && strcmp(original, new_hash));
    body[Quad + 2] = 3;
    CHECK(!CustomContentVerify_RoadHash(lev, sizeof lev, new_hash) && !new_hash[0]);
    body[Quad + 2] = 0;
    body[Quad + 0x13] = 0;
    CHECK(!CustomContentVerify_RoadHash(lev, sizeof lev, new_hash));
    CHECK(!CustomContentVerify_RoadHash(lev, sizeof lev - 1, new_hash));
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
