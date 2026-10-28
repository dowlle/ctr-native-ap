// gcc -m32 -std=c++17 -O2 -DCTR_CUSTOM_TRACKS -DCTR_CUSTOM_PACKAGES -DUTF8PROC_STATIC -I include -I . -I ap/vendor/json/include -I vendor/utf8proc
// -x c++ tools/probe-xdelta-import.cpp platform/native_xdelta_import.cpp platform/native_custom_package.cpp platform/native_custom_package_files.cpp
// -x c platform/native_xdelta_decode.c platform/native_custom_package_owned.c platform/native_custom_content_verify.c platform/native_custom_offline.c platform/native_custom_unicode.c vendor/utf8proc/utf8proc.c -x none
// -lstdc++ -lm -lpthread -o /tmp/probe-xdelta-import
//
// Corpus check for the xdelta importer: scan a real patch against a real disc
// and print the review list with LEV/VRM hashes, the pass count, the time and
// the peak memory. Local use only: patches and discs never go into CI or a
// repository. With a third argument it also installs the offered tracks into
// that assets folder.
//
//   /tmp/probe-xdelta-import <patch.xdelta> <disc.bin> [assets]
#include <platform/native_xdelta_import.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sys/resource.h>
#include <sys/stat.h>

int main(int argc, char **argv)
{
    if (argc < 3) { std::fprintf(stderr, "usage: %s <patch> <disc> [assets]\n", argv[0]); return 2; }
    std::string scratch = argc > 3 ? std::string(argv[3]) + "/tracks" : "/tmp";
    if (argc > 3) { mkdir(argv[3], 0755); mkdir(scratch.c_str(), 0755); }
    XdeltaImportRequest request = {argv[1], argv[2], nullptr, scratch.c_str(), 0};
    XdeltaImportScan *scan = nullptr;
    char error[512];
    volatile int progress = 0;
    auto start = std::chrono::steady_clock::now();
    int ok = XdeltaImport_Scan(&request, &scan, &progress, nullptr, error, sizeof error);
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    if (!ok) { std::printf("REFUSED: %s (%.1f s)\n", error, seconds); return 1; }
    const XdeltaImportSummary *s = XdeltaImport_Summary(scan);
    std::printf("patch %s %llu bytes\ndisc %s\npasses %u, %.1f s, peak RSS %ld KiB\n", s->patchSha256, s->patchBytes,
                s->discSha256, s->passes, seconds, usage.ru_maxrss);
    if (s->descriptorSha256[0]) std::printf("descriptor %s\n", s->descriptorSha256);
    if (s->descriptorNote[0]) std::printf("%s\n", s->descriptorNote);
    unsigned offered = 0;
    for (unsigned i = 0; i < s->count; i++)
    {
        const XdeltaImportItem &it = s->items[i];
        if (it.offered)
        {
            offered++;
            std::printf("OFFER %3d %-28s %-26s lev %s vrm %s pin %s\n", it.bigfileIndex, it.title, it.tags, it.levSha256, it.vrmSha256, it.pin);
        }
        else std::printf("SKIP  %3d %-28s %s\n", it.bigfileIndex, it.title, it.reason);
    }
    std::printf("%u offered\n", offered);
    if (argc > 3)
    {
        std::vector<unsigned char> selected(s->count, 1);
        int n = XdeltaImport_Install(scan, selected.data(), argv[3], error, sizeof error);
        std::printf("installed %d%s%s\n", n, n < 0 ? ": " : "", n < 0 ? error : "");
    }
    XdeltaImport_Free(&scan);
    return 0;
}
