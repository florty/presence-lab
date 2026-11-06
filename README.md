# Presence lab

Live 2D page for the Office MR60BHA2 radar, read directly over the ESPHome native API.

- Page: http://10.49.10.194:8096/ (LAN only, no login, shows vital signs)
- `bridge/server.py` connects to the sensor and streams decoded values; `web/index.html` is the page
- `firmware/` is the lab firmware that adds the `radar_points` point-cloud text sensor
- Start: `docker compose up -d --build`. Needs `.env` with `ESPHOME_HOST` and `ESPHOME_KEY` (gitignored)
- Flash: `.venv-esphome/bin/esphome upload firmware/presence-office.yaml --device 10.49.5.97`

While this runs the sensor is decoupled from Home Assistant. Restore steps are in the vault note
`Homelab/iot/Presence-Office.md`. With the original firmware the page still works, without positions.
