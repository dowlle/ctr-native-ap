// Real-apclientpp reconnect catch-up benchmark against tools/fake-ap-server.py.
//
// Build from the repository root after ap/vendor/fetch-deps.sh:
//   g++ -std=c++17 -O1 -m32 -DCTR_AP -DAP_NO_SCHEMA -DASIO_STANDALONE \
//     -D_WEBSOCKETPP_CPP11_THREAD_=1 -D_WEBSOCKETPP_CPP11_STL_=1 \
//     -Iap/vendor/apclientpp -Iap/vendor/wswrap/include -Iap/vendor/websocketpp \
//     -Iap/vendor/asio/include -Iap/vendor/json/include -Iap \
//     tools/test-reconnect-catchup.cpp ap/ap_net.cpp -lssl -lcrypto -lz -pthread \
//     -o /tmp/test-reconnect-catchup
// Run a server first, then: /tmp/test-reconnect-catchup ws://127.0.0.1:PORT COUNT

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include "../ap/ap_net.h"
#include "../ap/ap_seedcfg.h"

extern "C" void AP_LogLine(const char *) {}
void ap_seedcfg_parse_json(const nlohmann::json &) {}

int main(int argc, char **argv)
{
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s ws://host:port item-count\n", argv[0]);
        return 2;
    }
    const int wanted = std::atoi(argv[2]);
    if (wanted <= 0 || ap_net_init("catchup-harness", "Crash Team Racing", argv[1]) != 0)
        return 2;
    ap_net_connect_slot("Harness", "");

    using clock = std::chrono::steady_clock;
    double worst_ms = 0.0;
    double poll_total_ms = 0.0;
    int poll_calls = 0;
    int received = 0;
    const auto deadline = clock::now() + std::chrono::seconds(10);
    while (received < wanted && clock::now() < deadline) {
        const auto start = clock::now();
        ap_net_poll();
        const double elapsed = std::chrono::duration<double, std::milli>(clock::now() - start).count();
        if (elapsed > worst_ms)
            worst_ms = elapsed;
        poll_total_ms += elapsed;
        poll_calls++;
        long long items[64];
        int n;
        while ((n = ap_net_drain_items(items, 64)) > 0)
            received += n;
        if (received < wanted)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ap_net_shutdown();
    std::printf("RESULT items=%d received=%d polls=%d worst_poll_ms=%.3f total_poll_ms=%.3f\n",
                wanted, received, poll_calls, worst_ms, poll_total_ms);
    return received == wanted ? 0 : 1;
}
