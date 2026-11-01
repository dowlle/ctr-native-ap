// g++ -m32 -std=c++17 -Wall -Wextra -DCTR_AP -I ap -I ap/vendor/json/include -o /tmp/test-wumpa-shop-ledger tools/test-wumpa-shop-ledger.cpp && /tmp/test-wumpa-shop-ledger
//
// #249: the Wumpa Shop wallet (ap/ap_wumpa_ledger.h). Balance from banked and
// purchases, banking before the first reply and while offline, reconnect
// without double counting, the purchase reservation against the reply's
// original_value, resending a paid but unchecked purchase, and the refusals.
#include <cstdio>
#include <set>
#include "ap_wumpa_ledger.h"

static int failures;
#define CHECK(label, expr) do { bool ok_ = (expr); std::printf("%s  %s\n", ok_ ? "ok  " : "FAIL", label); failures += !ok_; } while (0)

using json = nlohmann::json;

static APWumpaLedger ready(long long banked, json shop = nullptr)
{
    APWumpaLedger l;
    l.connect("ws://room", "seed", 0, 1);
    l.bankRetrieved(banked ? json(banked) : json(nullptr));
    l.shopRetrieved(shop);
    return l;
}

int main()
{
    {
        APWumpaLedger l;
        l.connect("ws://room", "seed", 0, 1);
        CHECK("keys scoped by seed, team and slot", l.bankKey == "ctr_wumpa_banked_v1:4:seed:0:1" &&
                                                    l.shopKey == "ctr_wumpa_shop_v1:4:seed:0:1");
        l.credit(5);
        CHECK("credit before the reply is shown", l.shownLifetime() == 5 && !l.bankKnown());
        l.bankRetrieved(json(40));
        CHECK("first reply adds early credits to the server total", l.lifetime == 45 && l.bankWantsSend());
        l.bankSent = true;
        l.bankReply(json(45));
        CHECK("max reply confirms", l.server == 45 && !l.bankWantsSend());
        l.credit(10);
        l.disconnected();
        l.credit(3); // offline
        l.connect("ws://room", "seed", 0, 1);
        l.bankRetrieved(json(45)); // the 10 never reached the server
        CHECK("offline banking kept, nothing counted twice", l.lifetime == 58);
        l.disconnected();
        l.connect("ws://room", "seed", 0, 1);
        l.bankRetrieved(json(58)); // the max write landed, its reply was lost
        CHECK("a landed write is not added again", l.lifetime == 58);
        l.connect("ws://room", "other-seed", 0, 1);
        CHECK("another seed starts from zero", l.shownLifetime() == 0 && l.bought.empty());
    }
    {
        APWumpaLedger l = ready(100, json{{"35028600", 30}});
        CHECK("balance = lifetime - prices", l.balance() == 70);
        l.credit(10);
        CHECK("banking raises lifetime and balance alike", l.lifetime == 110 && l.balance() == 80);
        CHECK("refuse: sold", l.tryBuy(35028600, 10, 0, false) == APWumpaLedger::BUY_SOLD);
        CHECK("refuse: a Collected slot is sold without a charge",
              l.tryBuy(35028601, 10, 0, true) == APWumpaLedger::BUY_SOLD && l.balance() == 80);
        CHECK("refuse: locked below its unlock", l.tryBuy(35028602, 10, 200, false) == APWumpaLedger::BUY_LOCKED);
        CHECK("refuse: too poor", l.tryBuy(35028603, 81, 0, false) == APWumpaLedger::BUY_TOO_POOR);
        CHECK("buy: accepted", l.tryBuy(35028604, 80, 0, false) == APWumpaLedger::BUY_OK);
        CHECK("refuse: one purchase at a time", l.tryBuy(35028605, 1, 0, false) == APWumpaLedger::BUY_BUSY);
        json before = {{"35028600", 30}};
        json after = {{"35028600", 30}, {"35028604", 80}};
        CHECK("reply: confirmed", l.shopReply(after, before) == APWumpaLedger::REPLY_CONFIRMED);
        CHECK("spending leaves lifetime alone", l.lifetime == 110 && l.balance() == 0);
    }
    {
        // Two clients of one slot, both at balance 50, buy different 40-fruit slots.
        APWumpaLedger a = ready(50), b = ready(50);
        CHECK("race: both accept locally", a.tryBuy(35028600, 40, 0, false) == APWumpaLedger::BUY_OK &&
                                            b.tryBuy(35028601, 40, 0, false) == APWumpaLedger::BUY_OK);
        json one = {{"35028600", 40}}, both = {{"35028600", 40}, {"35028601", 40}};
        CHECK("race: the first write stands", a.shopReply(one, json(nullptr)) == APWumpaLedger::REPLY_CONFIRMED);
        CHECK("race: the second sees the first in original_value and rolls back",
              b.shopReply(both, one) == APWumpaLedger::REPLY_ROLLBACK && b.rollbacks.size() == 1);
        b.rollbackSent(35028601);
        CHECK("race: after the undo the second wallet is consistent", !b.isBought(35028601) && b.balance() == 10);
    }
    {
        // Two clients buy the same slot: kept once, charged once.
        APWumpaLedger a = ready(50);
        a.tryBuy(35028600, 40, 0, false);
        json one = {{"35028600", 40}};
        CHECK("same slot twice: confirmed, not rolled back", a.shopReply(one, one) == APWumpaLedger::REPLY_CONFIRMED &&
                                                             a.balance() == 10);
    }
    {
        // Crash after the write, before the check: the next connect resends it.
        APWumpaLedger l = ready(90, json{{"35028600", 20}, {"35028641", 30}});
        std::set<long long> checked = {35028600};
        auto unsent = l.unsentPaid([&](long long c) { return checked.count(c) != 0; });
        CHECK("resend: a paid but unchecked slot is sent again", unsent.size() == 1 && unsent[0] == 35028641);
        CHECK("resend: and costs nothing more", l.balance() == 40);
    }
    {
        // The seed's prices decide what a stored entry costs.
        APWumpaLedger l;
        l.connect("ws://room", "seed", 0, 1);
        l.configure({{35028600, 20}, {35028601, 30}});
        l.bankRetrieved(json(100));
        l.shopRetrieved(json{{"35028600", 99999}, {"35099999", 50}});
        CHECK("wallet: an inflated price counts at the seed price, an unknown code counts nothing",
              l.balance() == 80);
    }
    {
        // Crash after an outspent purchase, before its undo: the entry is
        // still in the wallet and its check was never sent.
        APWumpaLedger l;
        l.connect("ws://room", "seed", 0, 1);
        l.configure({{35028600, 40}, {35028601, 40}});
        l.bankRetrieved(json(50));
        l.shopRetrieved(json{{"35028600", 40}, {"35028601", 40}});
        CHECK("overspent: detected, nothing undone", l.overspent() && l.balance() == -30 &&
                                                       l.isBought(35028600) && l.isBought(35028601));
        l.credit(30);
        CHECK("overspent: banking that pays for both ends it", !l.overspent());
        APWumpaLedger ok = ready(100, json{{"35028600", 40}});
        CHECK("overspent: a covered wallet is not", !ok.overspent());
    }
    {
        APWumpaLedger l = ready(100);
        l.tryBuy(35028600, 10, 0, false);
        CHECK("another client's write leaves our purchase pending",
              l.shopReply(json{{"35028700", 5}}, json(nullptr)) == APWumpaLedger::REPLY_NONE && l.pendingCode == 35028600);
        CHECK("and updates the wallet", l.balance() == 95);
    }
    {
        APWumpaLedger l;
        l.connect("ws://room", "seed", 0, 1);
        CHECK("refuse: wallet not read yet", l.tryBuy(35028600, 1, 0, false) == APWumpaLedger::BUY_NOT_READY);
        l.bankRetrieved(json("garbage"));
        l.shopRetrieved(json::array());
        CHECK("refuse: a foreign value keeps the wallet unknown", !l.bankKnown() && !l.shopKnown());
    }

    std::printf("%s\n", failures ? "FAILED" : "all passed");
    return failures != 0;
}
