"""A custom Arcade race holds the room (Rulebook, Custom Arcade rule 4).

Runs the two send functions a race can reach against a stubbed socket, and pins the frame
order that keeps received items and their effects out of the race. The race's
own lifetime (a retry keeps it, leaving ends it at the load) is covered by
test-custom-exit-window.c. No game, no server: only play can prove the race.
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
net = (root / 'ap/ap_net.cpp').read_text()


def function(source, signature):
    start = source.index(signature)
    open_brace = source.index('\n{', start)
    close = source.index('\n}\n', open_brace)
    return source[start:close + 3]


body = '\n'.join(function(net, 'extern "C" ' + name) for name in (
    'void ap_net_hold_checks(int hold)',
    'void ap_net_send_location(long long location_code)',
    'void ap_net_send_goal(void)'))

fixture = """
#include <cassert>
#include <cstdint>
static bool g_hold_checks, g_rejected, g_connected = true;
static int sent, goals, journal, retained;
struct APClient { enum class ClientStatus { GOAL }; void StatusUpdate(ClientStatus) { goals++; } };
static APClient client, *g_ap = &client;
struct Held { void hold(int64_t) { retained++; } } g_held_checks;
#define AP_NET_GUARD(name, expr) expr
static void ap_solo_journal_append(int64_t) { journal++; }
static bool ap_net_try_send_location(int64_t) { sent++; return true; }
""" + body + """
int main()
{
    ap_net_send_location(1); ap_net_send_goal();
    assert(sent == 1 && goals == 1 && journal == 1);
    ap_net_hold_checks(1);
    ap_net_send_location(2); ap_net_send_goal();
    assert(sent == 1 && goals == 1 && journal == 1 && retained == 0);
    ap_net_hold_checks(0);
    ap_net_send_location(3); ap_net_send_goal();
    assert(sent == 2 && goals == 2 && journal == 2);
    return 0;
}
"""
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'hold.cpp').write_text(fixture)
    subprocess.run(['g++', '-std=c++17', 'hold.cpp', '-o', 'hold'], cwd=path, check=True)
    subprocess.run([str(path / 'hold')], check=True)

# Every sender goes through the held function; a new one must be looked at.
senders = {source.name for source in (root / 'ap').glob('*.c')
           if re.search(r'\bap_net_send_location\(', source.read_text())}
assert senders == {'ap_arcade_crystals.c', 'ap_hooks_location_events.c', 'ap_hooks_race.c'}, senders

frame = function((root / 'ap/ap_hooks_frame.c').read_text(), 'static void ap_onframe_body')
assert frame.index('ap_net_hold_checks(AP_ArcadeHoldsRoom())') < frame.index('AP_NetTick(gGT);')
assert re.search(r'if \(!AP_ArcadeHoldsRoom\(\)\)\n\s*AP_TrapTick\(gGT\);', frame)
effects = re.search(r'if \(!AP_ArcadeHoldsRoom\(\)\)\n\s*\{(.*?)\n\s*\}', frame, re.S).group(1)
for effect in ('AP_WumpaTick', 'AP_UsefulTick', 'AP_TiziTick', 'AP_ReserveTopUpTick', 'AP_TurboGrantTick'):
    assert effect + '(gGT)' in effects, effect

tick = function((root / 'ap/ap_hooks_net.c').read_text(), 'static void AP_NetTick')
hold = tick.index('if (AP_ArcadeHoldsRoom())')
for later in ('ap_net_take_recv_reset', 'AP_ReconcilePodiumFromTrophies', 'ap_net_drain_items', 'AP_FeedEndDrain'):
    assert hold < tick.index(later), later
print('arcade room hold: ok')
