"""Optional real-TCP opponent for inspecting one graphical client.

Create a room in the game, then run: python tests/manual_peer.py ROOM [--port PORT]
The peer waits for your submission and always attacks (test driver, not the AI).
"""
import argparse
import select
import time
from integration import Peer

parser = argparse.ArgumentParser()
parser.add_argument("room")
parser.add_argument("--port", type=int, default=28765)
args = parser.parse_args()
peer = Peer(args.port)
peer.send("join_room", room=args.room)
last_ping = time.monotonic()
try:
    while True:
        if time.monotonic() - last_ping >= 0.5:
            peer.send("ping")
            last_ping = time.monotonic()
        if not select.select([peer.sock], [], [], 0.1)[0]:
            continue
        message = peer.read()
        kind = message["type"]
        if kind != "pong":
            print(message, flush=True)
        if kind == "opponent_submitted":
            peer.send("submit", roundId=message["roundId"], action="attack")
        elif kind == "match_finished":
            peer.send("rematch", agree=True)
        elif kind == "error":
            raise RuntimeError(message["message"])
        elif kind in ("opponent_left", "room_left"):
            break
except (EOFError, KeyboardInterrupt, ConnectionError):
    pass
finally:
    peer.close()
