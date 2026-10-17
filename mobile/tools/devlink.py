#!/usr/bin/env python3
"""Developer-link receiver for OksaSDR.

  devlink.py serve --out DIR      listen on 0.0.0.0:47100 for the app; write
                                  DIR/devlink.log and DIR/shot-*.jpg
  devlink.py send '{"cmd":"screenshot"}'   send one command, print the reply
                                  (add "device":"android" or "ios74" to pick
                                  one of several linked devices)

The app connects out (Log page > Developer link > this PC's address). A
local control port (127.0.0.1:47101) forwards commands from `send`. The app
only accepts receive/display commands: screenshot, state, set (allow-listed
properties), tune, mode, band, zoom, drawer. It cannot be made to transmit.
"""
import argparse
import asyncio
import base64
import datetime
import json
import os
import sys

APP_PORT = 47100
CONTROL_PORT = 47101
LINE_LIMIT = 32 * 1024 * 1024


class Link:
    """Several devices may be linked at once (an iPad and an Android phone,
    say). Each log line carries the device's tag, e.g. [ios74] or [android5]
    (OS and build, from the app's hello); a command goes to the device named
    in its "device" field, or to the one that connected last."""

    def __init__(self, out):
        self.out = out
        self.apps = {}  # writer -> tag
        self.order = []  # writers, most recent last
        self.waiting = {}
        self.next_id = 1
        os.makedirs(out, exist_ok=True)
        self.log = open(os.path.join(out, "devlink.log"), "a", buffering=1, encoding="utf-8")

    def note(self, text, tag=None):
        stamp = datetime.datetime.now().strftime("%H:%M:%S")
        prefix = f"[{tag}] " if tag else ""
        self.log.write(f"--- {stamp} {prefix}{text}\n")

    async def app(self, reader, writer):
        peer = writer.get_extra_info("peername")
        self.apps[writer] = str(peer[0]) if peer else "app"
        self.order.append(writer)
        self.note(f"app connected from {peer}", self.apps[writer])
        try:
            while line := await reader.readline():
                try:
                    msg = json.loads(line)
                except ValueError:
                    continue
                kind = msg.get("t")
                tag = self.apps.get(writer, "app")
                if kind == "log":
                    self.log.write(f"[{tag}] " + msg.get("line", "") + "\n")
                elif kind == "hello":
                    system = (msg.get("os") or "app").split()[0].lower()
                    self.apps[writer] = f"{system}{msg.get('build', '')}"
                    self.note(f"hello {json.dumps(msg, ensure_ascii=False)}", self.apps[writer])
                elif kind == "reply":
                    if "jpeg" in msg:
                        ext = "png" if msg.get("format") == "png" else "jpg"
                        name = datetime.datetime.now().strftime(f"shot-{tag}-%H%M%S.{ext}")
                        path = os.path.join(self.out, name)
                        with open(path, "wb") as f:
                            f.write(base64.b64decode(msg.pop("jpeg")))
                        with open(os.path.join(self.out, f"latest.{ext}"), "wb") as f:
                            f.write(open(path, "rb").read())
                        msg["path"] = path
                    msg["device"] = tag
                    fut = self.waiting.pop(msg.get("id"), None)
                    if fut and not fut.done():
                        fut.set_result(msg)
        finally:
            self.note("app disconnected", self.apps.pop(writer, None))
            if writer in self.order:
                self.order.remove(writer)

    def target(self, device):
        if device:
            for writer in reversed(self.order):
                if device in self.apps.get(writer, ""):
                    return writer
            return None
        return self.order[-1] if self.order else None

    async def control(self, reader, writer):
        line = await reader.readline()
        try:
            cmd = json.loads(line)
        except ValueError:
            writer.write(b'{"error":"bad json"}\n')
            await writer.drain()
            writer.close()
            return
        target = self.target(cmd.pop("device", None))
        if not target:
            reply = {"error": "app not connected", "devices": list(self.apps.values())}
        else:
            cmd["id"] = self.next_id
            self.next_id += 1
            fut = asyncio.get_running_loop().create_future()
            self.waiting[cmd["id"]] = fut
            target.write((json.dumps(cmd) + "\n").encode())
            await target.drain()
            try:
                reply = await asyncio.wait_for(fut, 20)
            except asyncio.TimeoutError:
                self.waiting.pop(cmd["id"], None)
                reply = {"error": "timeout"}
        writer.write((json.dumps(reply, ensure_ascii=False) + "\n").encode())
        await writer.drain()
        writer.close()


async def serve(out):
    link = Link(out)
    # A screenshot reply is one JSON line of base64 (a detailed 3D view runs
    # well past asyncio's 64 KiB default line limit).
    app = await asyncio.start_server(link.app, "0.0.0.0", APP_PORT, limit=LINE_LIMIT)
    ctl = await asyncio.start_server(link.control, "127.0.0.1", CONTROL_PORT)
    link.note(f"listening on {APP_PORT}")
    async with app, ctl:
        await asyncio.gather(app.serve_forever(), ctl.serve_forever())


async def send(text):
    reader, writer = await asyncio.open_connection("127.0.0.1", CONTROL_PORT)
    writer.write(text.encode() + b"\n")
    await writer.drain()
    print((await reader.readline()).decode().strip())
    writer.close()


def main():
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest="what", required=True)
    s = sub.add_parser("serve")
    s.add_argument("--out", required=True)
    c = sub.add_parser("send")
    c.add_argument("json")
    a = p.parse_args()
    if a.what == "serve":
        asyncio.run(serve(a.out))
    else:
        asyncio.run(send(a.json))


if __name__ == "__main__":
    sys.exit(main())
