"""FastAPI application factory for FleetSuite.

``create_app()`` wires the registry, the broadcast hub, and the services
together in a lifespan, mounts the REST API + the single ``/ws`` socket, and
serves the built Vue SPA from ``web/static``. Blocking library calls (serial
I/O, pio, git) are dispatched to a thread so the event loop stays responsive.
"""

from __future__ import annotations

import asyncio
import logging
from pathlib import Path

from fastapi import APIRouter, Body, FastAPI, HTTPException, Request, WebSocket
from fastapi.responses import JSONResponse, StreamingResponse
from fastapi.staticfiles import StaticFiles

from meshtastic_mcp import (
    admin,
    boards,
    fixtures,
    flash as flash_lib,
    info as mt_info,
    log_query,
)

from .db import repo_builds as rb
from .db import repo_cameras as rc
from .db import repo_devices as rd
from .db import repo_flash as rf
from .db import repo_runs as rr
from .db.database import Database, default_db_path
from .services import (
    builder,
    camera_stream,
    control,
    datadog,
    discovery,
    firmware,
    identity,
    native,
    power,
    serial_monitor,
    test_runner,
)
from .services.control import ControlBusy
from .services.power import AmbiguousPort, NoPort
from .ws.hub import Connection, Hub

log = logging.getLogger("meshtastic_mcp.web")

STATIC_DIR = Path(__file__).parent / "static"


def _busy_guard(exc: ControlBusy) -> HTTPException:
    return HTTPException(status_code=409, detail=str(exc))


def create_app() -> FastAPI:
    app = FastAPI(title="FleetSuite", version="0.1.0")

    # --- lifespan: own the db + services for the process lifetime ----------
    @app.on_event("startup")
    async def _startup() -> None:
        db = await Database(default_db_path()).connect()
        hub = Hub()
        hub.bind_loop(asyncio.get_running_loop())

        app.state.db = db
        app.state.hub = hub
        app.state.orch = builder.BuildOrchestrator(db, hub)
        app.state.runner = test_runner.TestRunner(db, hub)
        app.state.forwarder = datadog.DDForwarder(db, hub)
        await app.state.forwarder.reload()
        app.state.serialmon = serial_monitor.SerialMonitor(db, hub)
        # Discovery auto-enriches devices, suspending their serial monitor for
        # the connect — so it needs the monitor handle.
        app.state.discovery = discovery.DeviceDiscovery(
            db, hub, serialmon=app.state.serialmon
        )
        app.state.discovery.start()
        log.info("FleetSuite started — registry at %s", db.path)

    @app.on_event("shutdown")
    async def _shutdown() -> None:
        disc = getattr(app.state, "discovery", None)
        if disc:
            await disc.stop()
        sm = getattr(app.state, "serialmon", None)
        if sm:
            await sm.shutdown()
        db = getattr(app.state, "db", None)
        if db:
            await db.close()

    api = APIRouter(prefix="/api")
    _mount_devices(api)
    _mount_cameras(api)
    _mount_firmware(api)
    _mount_builds(api)
    _mount_datadog(api)
    _mount_tests(api)
    _mount_native(api)
    _mount_boards(api)
    _mount_hubs(api)
    app.include_router(api)
    _mount_ws(app)

    @app.exception_handler(ControlBusy)
    async def _busy(_req: Request, exc: ControlBusy):
        return JSONResponse(status_code=409, content={"detail": str(exc)})

    if STATIC_DIR.is_dir():
        app.mount("/", StaticFiles(directory=str(STATIC_DIR), html=True), name="spa")
    else:
        log.warning("no built SPA at %s — run `npm run build` in web-ui/", STATIC_DIR)

    return app


# --- helpers ---------------------------------------------------------------
async def _device_or_404(db: Database, serial: str) -> dict:
    row = await rd.get(db, serial)
    if row is None:
        raise HTTPException(status_code=404, detail=f"unknown device: {serial}")
    return row


def _gate_idle() -> None:
    try:
        control._ensure_idle()
    except ControlBusy as exc:
        raise _busy_guard(exc)


async def _port_action(request: Request, serial: str, fn, *args):
    """Run a blocking port-bound library call, suspending any live serial
    monitor for the device so the USB port is free, then resuming it."""
    _gate_idle()
    sm = request.app.state.serialmon
    await sm.suspend(serial)
    try:
        return await asyncio.to_thread(fn, *args)
    finally:
        await sm.resume(serial)


# --- devices ---------------------------------------------------------------
def _mount_devices(api: APIRouter) -> None:
    @api.get("/devices")
    async def list_devices(request: Request):
        return await rd.list_all(request.app.state.db)

    @api.patch("/devices/{serial}")
    async def patch_device(serial: str, request: Request, body: dict = Body(...)):
        db, hub = request.app.state.db, request.app.state.hub
        await _device_or_404(db, serial)
        if "friendly_name" in body:
            dev = await rd.set_friendly_name(db, serial, body["friendly_name"])
        else:
            dev = await rd.get(db, serial)
        await hub.publish("device.update", dev)
        return dev

    @api.put("/devices/{serial}/env")
    async def set_env(serial: str, request: Request, body: dict = Body(...)):
        db, hub = request.app.state.db, request.app.state.hub
        await _device_or_404(db, serial)
        env = body.get("env")
        # A provided env pins it; clearing it releases the pin to auto-detect.
        dev = await rd.set_env(db, serial, env, locked=env is not None)
        await hub.publish("device.update", dev)
        return dev

    @api.post("/devices/{serial}/refresh")
    async def refresh(serial: str, request: Request):
        db, hub = request.app.state.db, request.app.state.hub
        row = await _device_or_404(db, serial)
        port = row.get("current_port")
        info = await _port_action(request, serial, mt_info.device_info, port)
        hw_model = info.get("hw_model")
        env = identity.env_for_hw_model(hw_model) if hw_model else None
        dev = await rd.update_enrichment(
            db,
            serial,
            node_num=info.get("my_node_num"),
            env=env,
            hw_model=hw_model,
            firmware_version=info.get("firmware_version"),
            region=info.get("region"),
        )
        await hub.publish("device.update", dev)
        return {"device": dev}

    @api.get("/devices/{serial}/flash-stats")
    async def flash_stats(serial: str, request: Request):
        return await rf.comparison(request.app.state.db, serial)

    @api.post("/devices/{serial}/flash")
    async def flash_device(serial: str, request: Request):
        db, hub = request.app.state.db, request.app.state.hub
        row = await _device_or_404(db, serial)
        env = control.env_for_device(row)
        port = row.get("current_port")
        if not env or not port:
            raise HTTPException(status_code=400, detail="no env/port resolved")
        loop = asyncio.get_running_loop()
        start = loop.time()
        result = await _port_action(
            request, serial, lambda: flash_lib.flash(env, port, confirm=True)
        )
        duration = round(loop.time() - start, 2)
        ok = result.get("exit_code") == 0
        fw = firmware.firmware_ref()
        await rf.record(
            db,
            device_serial=serial,
            env=env,
            fw_sha=fw.get("sha"),
            from_artifact=False,
            duration_s=duration,
            ok=ok,
        )
        if ok:
            await rd.record_flashed(db, serial, branch=fw.get("branch"), sha=fw.get("sha"))
            await hub.publish("device.update", await rd.get(db, serial))
        return {"ok": ok, "duration_s": duration, **result}

    @api.post("/devices/{serial}/reboot")
    async def reboot(serial: str, request: Request):
        row = await _device_or_404(request.app.state.db, serial)
        return await _port_action(request, serial, admin.reboot, row.get("current_port"), True, 5)

    @api.post("/devices/{serial}/factory-reset")
    async def factory_reset(serial: str, request: Request):
        row = await _device_or_404(request.app.state.db, serial)
        return await _port_action(
            request, serial, admin.factory_reset, row.get("current_port"), True
        )

    @api.post("/devices/{serial}/send-text")
    async def send_text(serial: str, request: Request, body: dict = Body(...)):
        row = await _device_or_404(request.app.state.db, serial)
        text = body.get("text", "")
        return await _port_action(
            request, serial, admin.send_text, text, None, 0, False, row.get("current_port")
        )

    @api.post("/devices/{serial}/inject-nodedb")
    async def inject_nodedb(serial: str, request: Request, body: dict = Body(...)):
        row = await _device_or_404(request.app.state.db, serial)
        size = int(body.get("size", 500))
        return await _port_action(request, serial, _inject, size, row.get("current_port"))

    @api.get("/devices/{serial}/config")
    async def get_config(serial: str, request: Request, section: str | None = None):
        row = await _device_or_404(request.app.state.db, serial)
        return await _port_action(
            request, serial, admin.get_config, section, row.get("current_port")
        )

    @api.put("/devices/{serial}/config")
    async def set_config(serial: str, request: Request, body: dict = Body(...)):
        row = await _device_or_404(request.app.state.db, serial)
        path = body.get("path")
        if not path:
            raise HTTPException(status_code=400, detail="missing config path")
        return await _port_action(
            request, serial, admin.set_config, path, body.get("value"), row.get("current_port")
        )

    @api.get("/devices/{serial}/packets")
    async def device_packets(
        serial: str, request: Request, start: str = "-30m", max: int = 100
    ):
        await _device_or_404(request.app.state.db, serial)
        # Recorder packets are mesh-wide, not keyed by USB port — return the
        # recent window so the per-device tab has live traffic to show.
        window = await asyncio.to_thread(
            lambda: log_query.packets_window(start, "now", max=max)
        )
        return {"packets": window.get("packets", [])}

    @api.get("/devices/{serial}/test-results")
    async def device_test_results(serial: str, request: Request, limit: int = 100):
        rows = await rr.results_for_device(request.app.state.db, serial)
        return rows[:limit]

    # --- per-device USB power (uhubctl) ----------------------------------
    @api.put("/devices/{serial}/hub-port")
    async def set_hub_port(serial: str, request: Request, body: dict = Body(...)):
        db, hub = request.app.state.db, request.app.state.hub
        await _device_or_404(db, serial)
        loc = body.get("location")
        port = body.get("port")
        dev = await rd.set_hub_port(
            db, serial, location=loc, port=int(port) if port is not None else None
        )
        await hub.publish("device.update", dev)
        return dev

    @api.post("/devices/{serial}/locate")
    async def locate_device(serial: str, request: Request):
        db, hub = request.app.state.db, request.app.state.hub
        await _device_or_404(db, serial)
        try:
            res = await power.locate(db, serial)
        except RuntimeError as exc:
            raise HTTPException(status_code=400, detail=str(exc))
        if res["located"]:
            await hub.publish("device.update", res["device"])
        return res

    @api.post("/devices/{serial}/power/{action}")
    async def power_action(serial: str, action: str, request: Request):
        db, hub = request.app.state.db, request.app.state.hub
        await _device_or_404(db, serial)
        if action not in ("on", "off", "cycle"):
            raise HTTPException(status_code=404, detail="unknown power action")
        _gate_idle()
        # Free the port from any live serial monitor before toggling VBUS.
        await request.app.state.serialmon.suspend(serial)
        try:
            result = await power.power_device(db, serial, action)
        except AmbiguousPort as exc:
            raise HTTPException(
                status_code=409,
                detail={"error": str(exc), "candidates": exc.candidates},
            )
        except (NoPort, ValueError) as exc:
            raise HTTPException(status_code=400, detail=str(exc))
        except RuntimeError as exc:  # uhubctl errors (permissions, hub gone)
            raise HTTPException(status_code=502, detail=str(exc))
        finally:
            if action != "off":
                await request.app.state.serialmon.resume(serial)
        return result


def _inject(size: int, port: str | None) -> dict:
    return fixtures.push_fake_nodedb(
        size, target="hardware", port=port, confirm=True, reboot_after=True
    )


# --- cameras ---------------------------------------------------------------
def _mount_cameras(api: APIRouter) -> None:
    @api.get("/cameras")
    async def list_cameras(request: Request):
        return await rc.list_all(request.app.state.db)

    @api.post("/cameras")
    async def add_camera(request: Request, body: dict = Body(...)):
        db, hub = request.app.state.db, request.app.state.hub
        cid = await rc.add(
            db, name=body.get("name", "camera"), device_index=str(body.get("device_index", "0"))
        )
        cam = await rc.get(db, cid)
        await hub.publish("camera.update", cam)
        return cam

    @api.delete("/cameras/{cid}", status_code=204)
    async def remove_camera(cid: int, request: Request):
        db, hub = request.app.state.db, request.app.state.hub
        await rc.remove(db, cid)
        await hub.publish("camera.update", {"id": cid, "deleted": True})

    @api.post("/cameras/{cid}/assign")
    async def assign_camera(cid: int, request: Request, body: dict = Body(...)):
        db, hub = request.app.state.db, request.app.state.hub
        cam = await rc.assign(db, cid, body.get("device_serial"))
        if cam is None:
            raise HTTPException(status_code=404, detail="unknown camera")
        await hub.publish("camera.update", cam)
        return cam

    @api.post("/cameras/{cid}/rotation")
    async def rotate_camera(cid: int, request: Request, body: dict = Body(...)):
        db, hub = request.app.state.db, request.app.state.hub
        cam = await rc.set_rotation(db, cid, int(body.get("rotation", 0)))
        if cam is None:
            raise HTTPException(status_code=404, detail="unknown camera")
        await hub.publish("camera.update", cam)
        return cam

    @api.post("/cameras/{cid}/mirror")
    async def mirror_camera(cid: int, request: Request, body: dict = Body(...)):
        db, hub = request.app.state.db, request.app.state.hub
        cam = await rc.set_mirror(db, cid, bool(body.get("mirror", False)))
        if cam is None:
            raise HTTPException(status_code=404, detail="unknown camera")
        await hub.publish("camera.update", cam)
        return cam

    @api.get("/cameras/{cid}/status")
    async def camera_status(cid: int, request: Request):
        cam = await rc.get(request.app.state.db, cid)
        if cam is None:
            raise HTTPException(status_code=404, detail="unknown camera")
        return await asyncio.to_thread(camera_stream.probe, str(cam.get("device_index")))

    @api.get("/cameras/{cid}/stream.mjpg")
    async def camera_stream_ep(cid: int, request: Request):
        cam = await rc.get(request.app.state.db, cid)
        if cam is None:
            raise HTTPException(status_code=404, detail="unknown camera")
        probe = await asyncio.to_thread(camera_stream.probe, str(cam.get("device_index")))
        if not probe["ok"]:
            raise HTTPException(status_code=503, detail=probe["error"])
        return StreamingResponse(
            camera_stream.mjpeg(str(cam.get("device_index"))),
            media_type=f"multipart/x-mixed-replace; boundary={camera_stream.BOUNDARY}",
        )


# --- firmware --------------------------------------------------------------
def _mount_firmware(api: APIRouter) -> None:
    @api.get("/firmware")
    async def get_firmware():
        return await asyncio.to_thread(firmware.firmware_ref)


# --- builds ----------------------------------------------------------------
def _mount_builds(api: APIRouter) -> None:
    @api.get("/builds")
    async def list_builds(request: Request):
        db = request.app.state.db
        return {
            "docker": await asyncio.to_thread(builder.docker_available),
            "builds": await rb.list_all(db),
        }

    @api.post("/builds")
    async def enqueue_builds(request: Request, body: dict = Body(default={})):
        db = request.app.state.db
        orch = request.app.state.orch
        fw = await asyncio.to_thread(firmware.firmware_ref)
        if not fw.get("available"):
            raise HTTPException(status_code=400, detail="no firmware checkout")
        sha, branch = fw["sha"], fw.get("branch")

        envs = body.get("envs")
        if not envs:
            # Prebuild the envs every online, env-resolved device needs.
            envs = sorted(
                {control.env_for_device(d) for d in await rd.online_with_env(db)}
                - {None}
            )
        if not envs:
            return []
        return await orch.enqueue(
            list(envs), sha=sha, branch=branch, force=bool(body.get("force"))
        )


# --- datadog ---------------------------------------------------------------
def _mount_datadog(api: APIRouter) -> None:
    @api.get("/datadog")
    async def get_datadog(request: Request):
        return request.app.state.forwarder.status()

    @api.put("/datadog")
    async def put_datadog(request: Request, body: dict = Body(...)):
        db = request.app.state.db
        fwd = request.app.state.forwarder
        cfg = await datadog.load_config(db)
        for key in ("enabled", "site", "scrub", "collector", "host", "ship_debug"):
            if key in body:
                setattr(cfg, key, body[key])
        # Only overwrite the key if a (non-empty) one was supplied.
        if body.get("api_key"):
            cfg.api_key = body["api_key"]
        await datadog.save_config(db, cfg)
        await fwd.reload()
        status = fwd.status()
        await request.app.state.hub.publish("datadog.update", status)
        return status

    @api.post("/datadog/test")
    async def test_datadog(request: Request):
        fwd = request.app.state.forwarder
        await fwd.reload()
        return await asyncio.to_thread(fwd.test_key)


# --- tests -----------------------------------------------------------------
def _mount_tests(api: APIRouter) -> None:
    @api.get("/tests/status")
    async def tests_status():
        return test_runner.status()

    @api.get("/tests/runs")
    async def tests_runs(request: Request):
        return await rr.list_runs(request.app.state.db)

    @api.post("/tests/start")
    async def tests_start(request: Request, body: dict = Body(default={})):
        runner = request.app.state.runner
        try:
            return await runner.start(list(body.get("args", [])))
        except RuntimeError as exc:
            raise HTTPException(status_code=409, detail=str(exc))

    @api.post("/tests/stop", status_code=204)
    async def tests_stop(request: Request):
        await request.app.state.runner.stop()


# --- native ----------------------------------------------------------------
def _mount_native(api: APIRouter) -> None:
    @api.get("/native")
    async def native_info(request: Request):
        return await native.info(request.app.state.db)

    @api.post("/native")
    async def native_create(request: Request, body: dict = Body(...)):
        db, hub = request.app.state.db, request.app.state.hub
        name = (body.get("name") or "").strip()
        if not name:
            raise HTTPException(status_code=400, detail="missing name")
        try:
            dev = await native.create(db, name=name, tcp_port=int(body.get("tcp_port", 4403)))
        except RuntimeError as exc:
            raise HTTPException(status_code=400, detail=str(exc))
        await hub.publish("device.update", dev)
        return dev

    @api.post("/native/{name}/{action}")
    async def native_lifecycle(name: str, action: str, request: Request):
        db, hub = request.app.state.db, request.app.state.hub
        if action not in ("start", "stop", "restart"):
            raise HTTPException(status_code=404, detail="unknown action")
        try:
            dev = await native.lifecycle(db, name, action)
        except RuntimeError as exc:
            raise HTTPException(status_code=400, detail=str(exc))
        await hub.publish("device.update", dev)
        return dev

    @api.delete("/native/{name}", status_code=204)
    async def native_delete(name: str, request: Request):
        db, hub = request.app.state.db, request.app.state.hub
        await native.remove(db, name)
        await hub.publish("device.update", {"serial_number": f"native:{name}", "deleted": True})


# --- boards ----------------------------------------------------------------
def _mount_boards(api: APIRouter) -> None:
    @api.get("/boards")
    async def list_boards(query: str | None = None, architecture: str | None = None):
        return await asyncio.to_thread(
            boards.list_boards, architecture, False, query, None
        )


# --- hubs (uhubctl) --------------------------------------------------------
def _mount_hubs(api: APIRouter) -> None:
    @api.get("/hubs")
    async def list_hubs():
        if not power.available():
            return {"available": False, "hubs": []}
        try:
            hubs = await asyncio.to_thread(power.list_hubs)
        except RuntimeError as exc:  # uhubctl present but failed (permissions)
            return {"available": True, "hubs": [], "error": str(exc)}
        return {"available": True, "hubs": hubs}


# --- websocket -------------------------------------------------------------
def _mount_ws(app: FastAPI) -> None:
    @app.websocket("/ws")
    async def ws(websocket: WebSocket):
        await websocket.accept()
        hub: Hub = websocket.app.state.hub
        sm = websocket.app.state.serialmon
        conn = Connection(send=websocket.send_json)
        hub.add(conn)
        # Track this peer's live serial monitors so we can release them on drop.
        serials: set[str] = set()
        try:
            while True:
                msg = await websocket.receive_json()
                action = msg.get("action")
                topic = msg.get("topic")
                if action == "subscribe" and topic:
                    hub.subscribe(conn, topic)
                    if topic.startswith("serial.") and topic not in serials:
                        serials.add(topic)
                        await sm.acquire(topic[len("serial.") :])
                elif action == "unsubscribe" and topic:
                    hub.unsubscribe(conn, topic)
                    if topic in serials:
                        serials.discard(topic)
                        await sm.release(topic[len("serial.") :])
        except Exception:  # noqa: BLE001 - normal on client disconnect
            pass
        finally:
            hub.remove(conn)
            for topic in serials:
                await sm.release(topic[len("serial.") :])
