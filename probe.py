"""Connect to the sensor over the ESPHome native API and print entities + a short stream of states."""
import asyncio, os, sys, time
from aioesphomeapi import APIClient
for l in open(".env"):
    k, _, v = l.strip().partition("=")
    os.environ.setdefault(k, v)
SECS = float(sys.argv[1]) if len(sys.argv) > 1 else 20
async def main():
    c = APIClient(os.environ["ESPHOME_HOST"], 6053, None, noise_psk=os.environ["ESPHOME_KEY"])
    await c.connect(login=True)
    info = await c.device_info()
    print("device", info.name, "esphome", info.esphome_version, "project", info.project_version)
    ents, _ = await c.list_entities_services()
    names = {e.key: e.object_id for e in ents}
    print("entities:", sorted(names.values()))
    n = {}
    t0 = time.time()
    def on(s):
        name = names.get(s.key, s.key)
        n[name] = n.get(name, 0) + 1
        print(f"{time.time()-t0:5.1f}s {name} = {getattr(s,'state',None)}")
    c.subscribe_states(on)
    await asyncio.sleep(SECS)
    print("rates/s:", {k: round(v / SECS, 2) for k, v in sorted(n.items())})
    await c.disconnect()
asyncio.run(main())
