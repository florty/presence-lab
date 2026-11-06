"""Bridge: ESPHome native API on the sensor -> Server-Sent Events + static page for the browser.

The API key stays here; the browser only ever sees decoded values.
"""
import asyncio, json, logging, math, os, time
from collections import deque
from pathlib import Path

from aioesphomeapi import APIClient
from aiohttp import web

HOST = os.environ["ESPHOME_HOST"]
KEY = os.environ["ESPHOME_KEY"]
WEB = Path(__file__).parent / "web"
HISTORY_SECS = 900
STALE_SECS = 30
# The radar's target flag drops out for a fraction of a second while someone sits still, so "empty"
# only counts once it has lasted this long. Tune here if leaving the room feels too slow to show.
EMPTY_HOLD_SECS = 15

log = logging.getLogger("bridge")
state = {
    "connected": False, "presence": None, "targets": None, "distance_cm": None, "heart": None,
    "breath": None, "lux": None, "movement": None, "wifi": None, "uptime": None,
    "points": [], "points_at": None, "firmware": None, "presence_since": None, "since_exact": False,
}
history = deque(maxlen=HISTORY_SECS)  # [epoch_s, heart, breath, distance_cm, presence]
version = 0
raw_presence = None
raw_empty_since = None

FIELDS = {
    "person_information": "presence", "target_number": "targets",
    "distance_to_detection_object": "distance_cm", "real-time_heart_rate": "heart",
    "real-time_respiratory_rate": "breath", "seeed_mr60bha2_illuminance": "lux",
    "movement": "movement", "wifi_signal": "wifi", "uptime": "uptime",
}


def clean(v):
    return None if isinstance(v, float) and math.isnan(v) else v


def put(field, value):
    global version
    if state[field] != value:
        state[field] = value
        version += 1


def set_presence(value, connected_at):
    if value != state["presence"]:
        # The first reading after a bridge start doesn't tell us when it really began.
        put("since_exact", state["presence"] is not None and time.time() - connected_at > 10)
        put("presence_since", round(time.time()))
        put("presence", value)


def on_presence(value, connected_at):
    global raw_presence, raw_empty_since
    raw_presence = value
    if value is False:
        raw_empty_since = raw_empty_since or time.time()
        if state["presence"] is None:
            set_presence(False, connected_at)
    else:
        raw_empty_since = None
        set_presence(value, connected_at)


def settle_presence():
    """Accept "empty" once the radar has reported it for EMPTY_HOLD_SECS without interruption."""
    if raw_presence is False and state["presence"] is True and time.time() - raw_empty_since >= EMPTY_HOLD_SECS:
        set_presence(False, 0)
        put("targets", 0.0)


async def sensor_loop():
    while True:
        client = APIClient(HOST, 6053, None, noise_psk=KEY)
        stopped = asyncio.Event()
        last_rx = connected_at = time.time()

        async def on_stop(expected):
            stopped.set()

        try:
            await client.connect(on_stop=on_stop, login=True)
            info = await client.device_info()
            ents, _ = await client.list_entities_services()
            names = {e.key: e.object_id for e in ents}

            def on_state(s):
                nonlocal last_rx
                last_rx = time.time()
                name = names.get(s.key)
                if name == "radar_points":
                    try:
                        put("points", json.loads(s.state)[1])
                        put("points_at", round(time.time(), 2))
                    except (ValueError, IndexError, TypeError):
                        pass
                elif name in FIELDS:
                    missing = getattr(s, "missing_state", False)
                    value = None if missing else clean(s.state)
                    if name == "person_information":
                        on_presence(value, connected_at)
                    elif name == "target_number":
                        # Keep the last count through a dropout; settle_presence clears it when really empty.
                        if value or state["presence"] is not True:
                            put("targets", value)
                    else:
                        put(FIELDS[name], value)

            client.subscribe_states(on_state)
            put("firmware", info.esphome_version)
            put("connected", True)
            log.info("connected to %s (%s)", info.name, info.esphome_version)
            while not stopped.is_set():
                if time.time() - last_rx > STALE_SECS:
                    log.warning("no data for %ss, reconnecting", STALE_SECS)
                    break
                try:
                    await asyncio.wait_for(stopped.wait(), 5)
                except asyncio.TimeoutError:
                    pass
        except Exception as e:  # network errors are routine on this Wi-Fi link
            log.warning("sensor connection failed: %s", e)
        put("connected", False)
        try:
            await client.disconnect()
        except Exception:
            pass
        await asyncio.sleep(3)


async def history_loop():
    while True:
        await asyncio.sleep(1)
        settle_presence()
        live = state["connected"]
        history.append([int(time.time())] + [state[k] if live else None for k in ("heart", "breath", "distance_cm", "presence")])


async def api_state(_):
    return web.json_response({"state": state, "history": list(history), "now": time.time()},
                             headers={"Cache-Control": "no-store"})


async def events(request):
    resp = web.StreamResponse(headers={"Content-Type": "text/event-stream", "Cache-Control": "no-store",
                                       "X-Accel-Buffering": "no"})
    await resp.prepare(request)
    seen = -1
    last_sent = 0.0
    try:
        while True:
            now = time.time()
            # Resend every 3 s even if unchanged, so the page can tell a quiet sensor from a dead link.
            if version != seen or now - last_sent > 3:
                seen = version
                last_sent = now
                await resp.write(f"data: {json.dumps(state)}\n\n".encode())
            await asyncio.sleep(0.1)
    except (ConnectionResetError, asyncio.CancelledError):
        pass
    return resp


async def index(_):
    return web.FileResponse(WEB / "index.html", headers={"Cache-Control": "no-store"})


async def start_tasks(app):
    app["tasks"] = [asyncio.create_task(sensor_loop()), asyncio.create_task(history_loop())]


def main():
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    logging.getLogger("aioesphomeapi").setLevel(logging.ERROR)
    app = web.Application()
    app.on_startup.append(start_tasks)
    app.add_routes([web.get("/", index), web.get("/api/state", api_state), web.get("/events", events)])
    web.run_app(app, port=8096, access_log=None, print=None)


if __name__ == "__main__":
    main()
