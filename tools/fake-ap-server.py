#!/usr/bin/env python3
"""Minimal Archipelago websocket server for test-reconnect-catchup.cpp."""

import argparse
import asyncio
import json
import time

import websockets


def packet(cmd):
    return json.dumps([cmd], separators=(",", ":"))


async def serve_once(port, item_count, packet_count):
    delivered = asyncio.Event()

    async def handler(ws):
        await ws.send(packet({
            "cmd": "RoomInfo",
            "time": time.time(),
            "version": {"major": 0, "minor": 6, "build": 2, "class": "Version"},
            "generator_version": {"major": 0, "minor": 6, "build": 2, "class": "Version"},
            "seed_name": "catchup-harness",
            "games": [],
            "datapackage_versions": {},
            "datapackage_checksums": {},
            "password": False,
            "permissions": {},
        }))
        while True:
            incoming = json.loads(await ws.recv())
            if any(command.get("cmd") == "Connect" for command in incoming):
                break
        await ws.send(packet({
            "cmd": "Connected",
            "team": 0,
            "slot": 1,
            "players": [{"team": 0, "slot": 1, "alias": "Harness", "name": "Harness"}],
            "slot_info": {"1": {"name": "Harness", "game": "Crash Team Racing", "type": 0, "group_members": []}},
            "slot_data": {},
            "checked_locations": [],
            "missing_locations": [],
        }))
        per_packet = item_count // packet_count
        index = 0
        for packet_index in range(packet_count):
            count = per_packet if packet_index + 1 < packet_count else item_count - index
            items = [
                {"item": 35000000 + i, "location": 35010000 + i,
                 "player": 1, "flags": i % 5}
                for i in range(index, index + count)
            ]
            await ws.send(packet({"cmd": "ReceivedItems", "index": index, "items": items}))
            index += count
        delivered.set()
        await asyncio.sleep(1)

    async with websockets.serve(handler, "127.0.0.1", port, compression=None, max_size=None):
        print(f"READY port={port} items={item_count} packets={packet_count}", flush=True)
        await asyncio.wait_for(delivered.wait(), timeout=10)
        await asyncio.sleep(1)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--items", type=int, required=True)
    parser.add_argument("--packets", type=int, required=True)
    args = parser.parse_args()
    if args.items <= 0 or args.packets <= 0 or args.packets > args.items:
        parser.error("items and packets must be positive, packets <= items")
    asyncio.run(serve_once(args.port, args.items, args.packets))


if __name__ == "__main__":
    main()
