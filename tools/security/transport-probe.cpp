// Loopback regression fixture for the production WebSocket transport.
#include <wswrap.hpp>
#include <chrono>
#include <thread>
#include <cstdio>
int main(int argc, char **argv) {
    if (argc != 3) return 2;
    bool opened = false, failed = false;
    wswrap::WS connection(argv[1], [&] { opened = true; }, [] {},
        [](const std::string&) {}, wswrap::WS::onerror_ex_handler(
        [&](const std::string&) { failed = true; }), argv[2]);
    auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!opened && !failed && std::chrono::steady_clock::now() < end) {
        connection.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::printf("opened=%d failed=%d\n", opened, failed);
    return opened ? 0 : failed ? 1 : 2;
}
