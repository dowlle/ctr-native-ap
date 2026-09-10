#define AP_NO_SCHEMA
#include <apclient.hpp>
#include <cstdio>
int main() {
    DefaultDataPackageStore store;
    int failures = 0;
    auto check = [&](const std::string& game, const std::string& checksum, bool expected) {
        nlohmann::json data = {{"checksum", checksum}, {"test", true}};
        bool saved = store.save(game, data);
        if (saved != expected) ++failures;
        nlohmann::json loaded;
        if (saved && (!store.load(game, checksum, loaded) || loaded != data)) ++failures;
    };
    check("Crash Team Racing", "fixture", true);
    check("Crash Team Racing", "", true);
    for (const auto& name : {".", "..", "../", "..\\", ".. ", "...", " ", "\n", ""})
        check(name, "fixture", false);
    check(std::string("game\0suffix", 11), "fixture", false);
    for (const auto& name : {"CON", "con.json", "CON .json", "PrN", "AUX", "NUL", "COM1", "Lpt9",
                             "CONIN$", "CONOUT$", "CONERR$"}) {
        check(name, "fixture", false);
        check("Crash Team Racing", name, false);
    }
    check("Console Game", "fixture", true);
    check("COM10", "fixture", true);
    for (const auto& checksum : {"../escape", "..", ".", "trailing.", "trailing ", "\n"})
        check("Crash Team Racing", checksum, false);
    std::printf("cache failures=%d\n", failures);
    return failures ? 1 : 0;
}
