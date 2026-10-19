"""Real subprocess + real TCP tests. Python standard library only; no mock sockets."""
import argparse
import json
import pathlib
import queue
import socket
import struct
import subprocess
import tempfile
import threading
import time


def frame(message):
    body = json.dumps(message, separators=(",", ":")).encode("utf-8")
    return struct.pack("!I", len(body)) + body


class Peer:
    def __init__(self, port):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=3)
        self.sock.settimeout(3)
        self.inbox = []
        self.take("welcome")

    def close(self):
        self.sock.close()

    def send(self, kind, **fields):
        self.sock.sendall(frame(dict(type=kind, **fields)))

    def exact(self, count):
        result = b""
        while len(result) < count:
            data = self.sock.recv(count - len(result))
            if not data:
                raise EOFError("server closed socket")
            result += data
        return result

    def read(self):
        length = struct.unpack("!I", self.exact(4))[0]
        assert 0 < length <= 65536
        return json.loads(self.exact(length))

    def take(self, kind):
        for index, message in enumerate(self.inbox):
            if message["type"] == kind:
                return self.inbox.pop(index)
        deadline = time.monotonic() + 4
        while time.monotonic() < deadline:
            message = self.read()
            if message["type"] == kind:
                return message
            self.inbox.append(message)
        raise AssertionError(f"did not receive {kind}; inbox={self.inbox}")

    def error(self, code):
        assert self.take("error")["code"] == code

    def expect_closed(self):
        try:
            assert self.sock.recv(1) == b""
        except (ConnectionResetError, ConnectionAbortedError):
            pass


def run(server, probe, config_path):
    config = json.loads(config_path.read_text(encoding="utf-8"))
    config["server"].update(address="127.0.0.1", port=0)
    config["timing"].update(selectionMs=650, revealMs=60, displayMs=80,
                            waitingRoomMs=1300, heartbeatMs=500, heartbeatTimeoutMs=4000)
    with tempfile.TemporaryDirectory(prefix="duel-integration-") as temp:
        path = pathlib.Path(temp) / "game.json"
        path.write_text(json.dumps(config), encoding="utf-8")
        process = subprocess.Popen([str(server), "--config", str(path)], stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True)
        peers = []
        try:
            ready = queue.Queue()
            threading.Thread(target=lambda: ready.put(process.stdout.readline()), daemon=True).start()
            line = ready.get(timeout=8).strip()
            assert line.startswith("READY "), line
            port = int(line.split()[1])
            probe_result = subprocess.run([str(probe), str(port)], capture_output=True, text=True, timeout=20)
            assert probe_result.returncode == 0, probe_result.stdout + probe_result.stderr
            print(probe_result.stdout.strip())

            def peer():
                p = Peer(port)
                peers.append(p)
                return p

            a, b, stranger = peer(), peer(), peer()
            # Split one frame into individual header bytes and tiny body fragments.
            data = frame({"type": "create_room"})
            for byte in data[:4]:
                a.sock.sendall(bytes([byte]))
                time.sleep(0.002)
            for index in range(4, len(data), 3):
                a.sock.sendall(data[index:index + 3])
            code = a.take("room_created")["room"]
            assert len(code) == 6 and code.isdecimal()
            a.send("join_room", room=code)
            a.error("already_in_room")
            b.send("join_room", room="12x456")
            b.error("invalid_message")
            missing = "000000" if code != "000000" else "999999"
            b.send("join_room", room=missing)
            b.error("room_not_found")
            b.send("join_room", room=code)
            assert b.take("room_joined")["seat"] == 1
            first = a.take("match_start")
            assert first == b.take("match_start")
            assert first["rules"] == config["rules"]
            stranger.send("join_room", room=code)
            stranger.error("room_started")
            rid = first["roundId"]
            a.send("submit", roundId=rid + 1, action="attack")
            a.error("wrong_round")
            a.send("submit", roundId=rid, action="ultimate")
            a.error("illegal_action")
            a.send("submit", roundId=rid, action="attack", playerId=1)
            a.error("invalid_message")
            a.send("submit", roundId=rid, action="attack")
            assert a.take("submit_ack")["automatic"] is False
            ready_message = b.take("opponent_submitted")
            assert ready_message == {"type": "opponent_submitted", "roundId": rid}
            assert not any("actions" in m or "action" in m for m in b.inbox)
            a.send("submit", roundId=rid, action="charge")
            a.error("already_submitted")
            b.send("submit", roundId=rid, action="attack")
            result = a.take("round_result")
            assert result == b.take("round_result")
            assert result["state"] == [{"hp": 4, "energy": 0}] * 2
            a.send("submit", roundId=rid, action="attack")
            a.error("wrong_phase")
            # Coalesce ping + submit into one TCP write; finish the actual match.
            for expected_hp in (3, 2, 1, 0):
                start = a.take("round_start")
                assert start == b.take("round_start")
                rid = start["roundId"]
                a.sock.sendall(frame({"type": "ping"}) + frame({"type": "submit", "roundId": rid, "action": "attack"}))
                b.send("submit", roundId=rid, action="attack")
                result = a.take("round_result")
                assert result == b.take("round_result")
                assert [s["hp"] for s in result["state"]] == [expected_hp] * 2
            assert a.take("match_finished")["outcome"] == "draw"
            assert b.take("match_finished")["outcome"] == "draw"
            a.send("rematch", agree=True)
            assert a.take("rematch_state")["accepted"] == [True, False]
            assert b.take("rematch_state")["accepted"] == [True, False]
            b.send("rematch", agree=True)
            restart = a.take("match_start")
            assert restart == b.take("match_start")
            assert restart["round"] == 1 and restart["roundId"] > rid
            assert restart["state"] == [{"hp": 5, "energy": 0}] * 2
            # Do not submit either side: the server clock selects Defend for both.
            timed = a.take("round_result")
            assert timed == b.take("round_result")
            assert timed["actions"] == ["defend", "defend"]
            assert timed["damage"] == [0, 0]
            a.send("submit", roundId=restart["roundId"], action="attack")
            a.error("wrong_phase")
            a.send("leave_room")
            a.take("room_left")
            b.take("opponent_left")
            stranger.send("join_room", room=code)
            stranger.error("room_not_found")
            # A waiting room is reclaimed without requiring a disconnect.
            a.send("create_room")
            expired_code = a.take("room_created")["room"]
            assert a.take("opponent_left")["reason"] == "Waiting room expired"
            b.send("join_room", room=expired_code)
            b.error("room_not_found")
            # Disconnect during a match cancels it; never reports a normal win.
            b.send("create_room")
            disconnect_code = b.take("room_created")["room"]
            c = peer()
            c.send("join_room", room=disconnect_code)
            b.take("match_start")
            c.take("match_start")
            c.close()
            assert b.take("opponent_left")["reason"] == "Opponent disconnected"
            # Header length is checked before any body allocation/read.
            for bad in (struct.pack("!I", 65537), struct.pack("!I", 0),
                        struct.pack("!I", 5) + b"{oops",
                        struct.pack("!I", 81) + b"[" * 40 + b"0" + b"]" * 40):
                malformed = peer()
                malformed.sock.sendall(bad)
                malformed.expect_closed()
            # Server survives malformed clients and recognizes non-heartbeating peers.
            idle = peer()
            idle.sock.settimeout(6)
            idle.expect_closed()
            survivor = peer()
            survivor.send("ping")
            survivor.take("pong")
            assert process.poll() is None
            print("PASS real TCP: framing, validation, secrecy, 5-round draw, rematch, timeout, leave, disconnect, cleanup, heartbeat")
        finally:
            for p in peers:
                p.close()
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
            stderr = process.stderr.read()
            if stderr:
                print(stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--server", type=pathlib.Path, required=True)
    parser.add_argument("--probe", type=pathlib.Path, required=True)
    parser.add_argument("--config", type=pathlib.Path, required=True)
    arguments = parser.parse_args()
    run(arguments.server.resolve(), arguments.probe.resolve(), arguments.config.resolve())
