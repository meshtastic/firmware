#!/usr/bin/env python3
"""
REST API for Meshtastic Master Controller

This module provides a FastAPI-based REST API for accessing slave information,
telemetry, and sending commands. It's designed to be used by web applications
to display slave data.

Endpoints:
    GET  /api/status                     - Master controller status
    GET  /api/slaves                     - List all slaves
    GET  /api/slaves/{node_id}           - Get specific slave details
    GET  /api/slaves/{node_id}/telemetry - Get slave telemetry data
    GET  /api/slaves/{node_id}/status    - Get slave extended status report
    GET  /api/slaves/{node_id}/batches   - Get slave data batches
    POST /api/slaves/{node_id}/command   - Send command to slave

Usage:
    from meshtastic_app import MasterController, MasterConfig
    from meshtastic_app.api import create_api, run_api_server

    config = MasterConfig.from_yaml("config.yaml")
    master = MasterController(config)
    master.initialize()

    # Create API with master reference
    app = create_api(master)

    # Run API server (blocking)
    run_api_server(app, host="0.0.0.0", port=8080)
"""

import asyncio
import logging
import time
from dataclasses import asdict
from datetime import datetime
from typing import Any, Dict, List, Optional

try:
    from fastapi import FastAPI, HTTPException, Query
    from fastapi.middleware.cors import CORSMiddleware
    from pydantic import BaseModel, Field
except ImportError:
    raise ImportError(
        "FastAPI and Pydantic are required for the API module.\n"
        "Install with: pip3 install fastapi uvicorn pydantic"
    )

from .protocol import CommandType, SlaveStatus
from .models import (
    API_MAX_LIST_ITEMS,
    API_MAX_BATCHES_PER_REQUEST,
    API_DEFAULT_HOST,
    API_DEFAULT_PORT,
)


def _get_version() -> str:
    """Get package version (lazy import to avoid circular dependency)."""
    from . import __version__
    return __version__


# =============================================================================
# Pydantic Models for API Responses
# =============================================================================

class TelemetryResponse(BaseModel):
    """Telemetry data for a slave."""
    battery_level: int = Field(..., ge=0, le=100, description="Battery percentage")
    voltage: float = Field(..., ge=0, description="Voltage in volts")
    channel_utilization: float = Field(..., ge=0, le=100, description="Channel utilization %")
    air_util_tx: float = Field(..., ge=0, le=100, description="Air utilization TX %")
    temperature: Optional[float] = Field(None, description="Temperature in Celsius")
    humidity: Optional[float] = Field(None, ge=0, le=100, description="Humidity %")
    pressure: Optional[float] = Field(None, description="Barometric pressure in hPa")
    last_telemetry_time: float = Field(..., description="Unix timestamp of last telemetry")


class MemoryMetrics(BaseModel):
    """Memory metrics from extended status."""
    free_heap_kb: int = Field(..., ge=0, description="Free heap memory in KB")
    free_fram_kb: int = Field(..., ge=0, description="Free FRAM in KB (0 if not available)")
    free_flash_kb: int = Field(..., ge=0, description="Free flash storage in KB")
    total_fram_kb: int = Field(..., ge=0, description="Total FRAM capacity in KB")
    total_flash_kb: int = Field(..., ge=0, description="Total flash capacity in KB")


class ExtendedStatusResponse(BaseModel):
    """Extended status report from a slave."""
    uptime: int = Field(..., ge=0, description="Uptime in seconds")
    status: str = Field(..., description="Status code (OK, LOW_BATTERY, etc.)")
    battery_percent: int = Field(..., ge=0, le=100, description="Battery percentage")
    voltage_mv: int = Field(..., ge=0, description="Voltage in millivolts")
    memory: MemoryMetrics = Field(..., description="Memory metrics")
    pending_data_bytes: int = Field(..., ge=0, description="Pending data to send")
    error_count: int = Field(..., ge=0, description="Cumulative error count")


class DataBatchResponse(BaseModel):
    """A data batch from a slave."""
    batch_id: int = Field(..., description="Unique batch ID")
    record_count: int = Field(..., ge=0, description="Number of records")
    record_size: int = Field(..., ge=0, description="Size of each record in bytes")
    records_hex: List[str] = Field(..., description="Records as hex strings")


class StoredRecordResponse(BaseModel):
    """A stored record from the database."""
    id: int = Field(..., description="Database record ID")
    slave_id: str = Field(..., description="Source slave node ID")
    batch_id: int = Field(..., description="Original batch ID")
    timestamp: int = Field(..., description="Absolute Unix timestamp")
    timestamp_iso: str = Field(..., description="ISO formatted timestamp")
    data: str = Field(..., description="Record data as UTF-8 string")
    data_hex: str = Field(..., description="Record data as hex string")


class DayRecordsResponse(BaseModel):
    """Records for a specific day."""
    slave_id: str = Field(..., description="Slave node ID")
    date: str = Field(..., description="Date in YYYY-MM-DD format")
    record_count: int = Field(..., ge=0, description="Number of records")
    records: List[StoredRecordResponse] = Field(..., description="Records ordered by timestamp")


class SlaveResponse(BaseModel):
    """Full slave information."""
    node_id: str = Field(..., description="Node ID (e.g., !28979058)")
    long_name: str = Field("", description="Device long name")
    short_name: str = Field("", description="Device short name")
    first_seen: float = Field(..., description="Unix timestamp when first discovered")
    last_seen: float = Field(..., description="Unix timestamp of last communication")
    is_online: bool = Field(..., description="Whether slave is currently online")
    telemetry: TelemetryResponse = Field(..., description="Current telemetry data")
    telemetry_count: int = Field(..., ge=0, description="Total telemetry messages received")
    batch_count: int = Field(..., ge=0, description="Total data batches received")
    error_count: int = Field(..., ge=0, description="Total errors for this slave")
    has_extended_status: bool = Field(..., description="Whether extended status is available")


class SlaveListResponse(BaseModel):
    """List of slaves with summary."""
    total: int = Field(..., ge=0, description="Total number of slaves")
    online: int = Field(..., ge=0, description="Number of online slaves")
    offline: int = Field(..., ge=0, description="Number of offline slaves")
    slaves: List[SlaveResponse] = Field(..., description="List of slaves")


class MasterStatusResponse(BaseModel):
    """Master controller status."""
    node_id: str = Field(..., description="Master node ID")
    state: str = Field(..., description="Controller state")
    total_slaves: int = Field(..., ge=0, description="Total tracked slaves")
    online_slaves: int = Field(..., ge=0, description="Online slave count")
    offline_slaves: int = Field(..., ge=0, description="Offline slave count")
    total_telemetry_received: int = Field(..., ge=0, description="Total telemetry messages")
    total_batches_received: int = Field(..., ge=0, description="Total data batches")
    total_errors: int = Field(..., ge=0, description="Total errors")
    position_broadcasting: bool = Field(..., description="Whether position broadcast is enabled")
    private_channel: int = Field(..., ge=0, description="Private channel index")
    private_port: int = Field(..., ge=0, description="Private port number")


class CommandRequest(BaseModel):
    """Request to send a command to a slave."""
    command: str = Field(..., description="Command type (REBOOT, SLEEP, WAKE, etc.)")
    params: List[str] = Field(default=[], description="Command parameters as hex strings")


class CommandResponse(BaseModel):
    """Response from sending a command."""
    success: bool = Field(..., description="Whether command was sent")
    message: str = Field(..., description="Status message")


class HealthResponse(BaseModel):
    """API health check response."""
    status: str = Field(..., description="Health status")
    timestamp: str = Field(..., description="Current server time")
    master_connected: bool = Field(..., description="Whether master is connected")


# =============================================================================
# API Factory
# =============================================================================

def create_api(master_controller) -> FastAPI:
    """
    Create a FastAPI application with master controller reference.

    Args:
        master_controller: MasterController instance.

    Returns:
        Configured FastAPI application.
    """
    app = FastAPI(
        title="Meshtastic Master Controller API",
        description="REST API for monitoring and controlling Meshtastic slave nodes",
        version=_get_version(),
        docs_url="/api/docs",
        redoc_url="/api/redoc",
        openapi_url="/api/openapi.json",
    )

    # Add CORS middleware for web access
    app.add_middleware(
        CORSMiddleware,
        allow_origins=["*"],  # Configure appropriately for production
        allow_credentials=True,
        allow_methods=["*"],
        allow_headers=["*"],
    )

    # Store master reference
    app.state.master = master_controller
    logger = logging.getLogger("API")

    # -------------------------------------------------------------------------
    # Helper Functions
    # -------------------------------------------------------------------------

    def get_master():
        """Get master controller from app state."""
        return app.state.master

    def slave_to_response(slave) -> SlaveResponse:
        """Convert SlaveNode to API response."""
        return SlaveResponse(
            node_id=slave.node_id,
            long_name=slave.long_name,
            short_name=slave.short_name,
            first_seen=slave.first_seen,
            last_seen=slave.last_seen,
            is_online=slave.is_online,
            telemetry=TelemetryResponse(
                battery_level=slave.battery_level,
                voltage=slave.voltage,
                channel_utilization=slave.channel_utilization,
                air_util_tx=slave.air_util_tx,
                temperature=slave.temperature,
                humidity=slave.humidity,
                pressure=slave.pressure,
                last_telemetry_time=slave.last_telemetry_time,
            ),
            telemetry_count=slave.telemetry_count,
            batch_count=slave.batch_count,
            error_count=slave.error_count,
            has_extended_status=slave.last_status is not None,
        )

    def status_to_response(status) -> ExtendedStatusResponse:
        """Convert SlaveStatusReport to API response."""
        return ExtendedStatusResponse(
            uptime=status.uptime,
            status=status.status.name,
            battery_percent=status.battery_percent,
            voltage_mv=status.voltage_mv,
            memory=MemoryMetrics(
                free_heap_kb=status.free_heap_kb,
                free_fram_kb=status.free_fram_kb,
                free_flash_kb=status.free_flash_kb,
                total_fram_kb=status.total_fram_kb,
                total_flash_kb=status.total_flash_kb,
            ),
            pending_data_bytes=status.pending_data_bytes,
            error_count=status.error_count,
        )

    def batch_to_response(batch) -> DataBatchResponse:
        """Convert DataBatch to API response."""
        return DataBatchResponse(
            batch_id=batch.batch_id,
            record_count=len(batch.records),
            record_size=batch.record_size,
            records_hex=[r.hex() for r in batch.records],
        )

    # -------------------------------------------------------------------------
    # Health Endpoint
    # -------------------------------------------------------------------------

    @app.get("/api/health", response_model=HealthResponse, tags=["Health"])
    async def health_check():
        """Check API and master controller health."""
        master = get_master()
        from .master import MasterState

        return HealthResponse(
            status="healthy",
            timestamp=datetime.now().isoformat(),
            master_connected=master.state == MasterState.READY,
        )

    # -------------------------------------------------------------------------
    # Master Status Endpoint
    # -------------------------------------------------------------------------

    @app.get("/api/status", response_model=MasterStatusResponse, tags=["Master"])
    async def get_master_status():
        """Get master controller status and statistics."""
        master = get_master()
        stats = master.get_stats()

        return MasterStatusResponse(
            node_id=stats["master_node_id"],
            state=stats["state"],
            total_slaves=stats["total_slaves"],
            online_slaves=stats["online_slaves"],
            offline_slaves=stats["offline_slaves"],
            total_telemetry_received=stats["total_telemetry_received"],
            total_batches_received=stats["total_batches_received"],
            total_errors=stats["total_errors"],
            position_broadcasting=master.config.position_broadcast_enabled,
            private_channel=master.config.private_channel_index,
            private_port=master.config.private_port_num,
        )

    # -------------------------------------------------------------------------
    # Slave List Endpoints
    # -------------------------------------------------------------------------

    @app.get("/api/slaves", response_model=SlaveListResponse, tags=["Slaves"])
    async def list_slaves(
        online_only: bool = Query(False, description="Only return online slaves"),
        limit: int = Query(API_MAX_LIST_ITEMS, ge=1, le=API_MAX_LIST_ITEMS, description="Max slaves to return"),
        offset: int = Query(0, ge=0, description="Offset for pagination"),
    ):
        """
        List all tracked slaves.

        Returns slave summary with telemetry data.
        """
        master = get_master()
        master.update_slave_status()  # Refresh online status

        if online_only:
            slaves = master.get_online_slaves()
        else:
            slaves = master.get_slaves()

        # Apply pagination (NASA Rule 2: bounded results)
        total = len(slaves)
        slaves = slaves[offset : offset + limit]

        online_count = sum(1 for s in master.get_slaves() if s.is_online)

        return SlaveListResponse(
            total=total,
            online=online_count,
            offline=total - online_count if not online_only else 0,
            slaves=[slave_to_response(s) for s in slaves],
        )

    @app.get("/api/slaves/{node_id}", response_model=SlaveResponse, tags=["Slaves"])
    async def get_slave(node_id: str):
        """
        Get detailed information for a specific slave.

        Args:
            node_id: Slave node ID (e.g., !28979058)
        """
        master = get_master()
        slave = master.get_slave(node_id)

        if not slave:
            raise HTTPException(status_code=404, detail=f"Slave {node_id} not found")

        return slave_to_response(slave)

    # -------------------------------------------------------------------------
    # Slave Telemetry Endpoint
    # -------------------------------------------------------------------------

    @app.get("/api/slaves/{node_id}/telemetry", response_model=TelemetryResponse, tags=["Telemetry"])
    async def get_slave_telemetry(node_id: str):
        """
        Get current telemetry for a slave.

        Returns battery, voltage, channel utilization, and environment metrics.
        """
        master = get_master()
        telemetry = master.get_slave_telemetry(node_id)

        if telemetry is None:
            raise HTTPException(status_code=404, detail=f"Slave {node_id} not found")

        return TelemetryResponse(**telemetry)

    # -------------------------------------------------------------------------
    # Slave Extended Status Endpoint
    # -------------------------------------------------------------------------

    @app.get("/api/slaves/{node_id}/status", response_model=ExtendedStatusResponse, tags=["Status"])
    async def get_slave_status(node_id: str):
        """
        Get extended status report for a slave.

        Returns power metrics, memory stats, and operational status.
        Extended status is only available if the slave has sent a STATUS message
        via the custom protocol.
        """
        master = get_master()
        slave = master.get_slave(node_id)

        if not slave:
            raise HTTPException(status_code=404, detail=f"Slave {node_id} not found")

        if not slave.last_status:
            raise HTTPException(
                status_code=404,
                detail=f"No extended status available for {node_id}. "
                       f"Slave must send STATUS via custom protocol."
            )

        return status_to_response(slave.last_status)

    # -------------------------------------------------------------------------
    # Slave Data Batches Endpoint
    # -------------------------------------------------------------------------

    @app.get("/api/slaves/{node_id}/batches", response_model=List[DataBatchResponse], tags=["Data"])
    async def get_slave_batches(
        node_id: str,
        limit: int = Query(10, ge=1, le=API_MAX_BATCHES_PER_REQUEST, description="Max batches to return"),
    ):
        """
        Get data batches received from a slave.

        Batches are returned newest first.
        """
        master = get_master()
        batches = master.get_slave_batches(node_id, limit=limit)

        if batches is None:
            raise HTTPException(status_code=404, detail=f"Slave {node_id} not found")

        return [batch_to_response(b) for b in batches]

    # -------------------------------------------------------------------------
    # Command Endpoint
    # -------------------------------------------------------------------------

    @app.post("/api/slaves/{node_id}/command", response_model=CommandResponse, tags=["Commands"])
    async def send_command(node_id: str, request: CommandRequest):
        """
        Send a command to a slave.

        Available commands:
        - REBOOT: Reboot the slave device
        - SLEEP: Put slave into sleep mode
        - WAKE: Wake slave from sleep
        - SET_INTERVAL: Set data send interval
        - CLEAR_DATA: Clear pending data
        - SEND_DATA: Request slave to send data
        - SET_MODE: Set operational mode
        """
        master = get_master()

        # Validate slave exists
        slave = master.get_slave(node_id)
        if not slave:
            raise HTTPException(status_code=404, detail=f"Slave {node_id} not found")

        # Parse command type
        try:
            command = CommandType[request.command.upper()]
        except KeyError:
            valid_commands = [c.name for c in CommandType]
            raise HTTPException(
                status_code=400,
                detail=f"Invalid command: {request.command}. "
                       f"Valid commands: {valid_commands}"
            )

        # Convert hex params to bytes
        params = []
        for hex_param in request.params:
            try:
                params.append(bytes.fromhex(hex_param))
            except ValueError:
                raise HTTPException(
                    status_code=400,
                    detail=f"Invalid hex parameter: {hex_param}"
                )

        # Send command
        success = master.send_command(node_id, command, params)

        if success:
            return CommandResponse(
                success=True,
                message=f"Command {command.name} sent to {node_id}",
            )
        else:
            return CommandResponse(
                success=False,
                message=f"Failed to send command. Master state: {master.state.value}",
            )

    # -------------------------------------------------------------------------
    # Request Status/Data Endpoints
    # -------------------------------------------------------------------------

    @app.post("/api/slaves/{node_id}/request-status", response_model=CommandResponse, tags=["Commands"])
    async def request_slave_status(node_id: str):
        """Request a slave to send its extended status report."""
        master = get_master()

        slave = master.get_slave(node_id)
        if not slave:
            raise HTTPException(status_code=404, detail=f"Slave {node_id} not found")

        success = master.request_status(node_id)

        return CommandResponse(
            success=success,
            message="Status request sent" if success else "Failed to send request",
        )

    @app.post("/api/slaves/{node_id}/request-data", response_model=CommandResponse, tags=["Commands"])
    async def request_slave_data(node_id: str):
        """Request a slave to send its pending data batches."""
        master = get_master()

        slave = master.get_slave(node_id)
        if not slave:
            raise HTTPException(status_code=404, detail=f"Slave {node_id} not found")

        success = master.request_data(node_id)

        return CommandResponse(
            success=success,
            message="Data request sent" if success else "Failed to send request",
        )

    # -------------------------------------------------------------------------
    # Broadcast Endpoints
    # -------------------------------------------------------------------------

    @app.post("/api/broadcast/heartbeat", response_model=CommandResponse, tags=["Broadcast"])
    async def broadcast_heartbeat():
        """Broadcast a heartbeat to all slaves."""
        master = get_master()
        success = master.send_heartbeat()

        return CommandResponse(
            success=success,
            message="Heartbeat broadcast sent" if success else "Failed to broadcast",
        )

    @app.post("/api/broadcast/command", response_model=CommandResponse, tags=["Broadcast"])
    async def broadcast_command(request: CommandRequest):
        """Broadcast a command to all slaves."""
        master = get_master()

        try:
            command = CommandType[request.command.upper()]
        except KeyError:
            valid_commands = [c.name for c in CommandType]
            raise HTTPException(
                status_code=400,
                detail=f"Invalid command: {request.command}. Valid: {valid_commands}"
            )

        params = []
        for hex_param in request.params:
            try:
                params.append(bytes.fromhex(hex_param))
            except ValueError:
                raise HTTPException(
                    status_code=400,
                    detail=f"Invalid hex parameter: {hex_param}"
                )

        success = master.broadcast_command(command, params)

        return CommandResponse(
            success=success,
            message=f"Command {command.name} broadcast" if success else "Broadcast failed",
        )

    # -------------------------------------------------------------------------
    # Stored Data Endpoints (SQLite database)
    # -------------------------------------------------------------------------

    def record_to_response(rec) -> StoredRecordResponse:
        """Convert StoredRecord to API response."""
        from datetime import datetime
        return StoredRecordResponse(
            id=rec.id,
            slave_id=rec.slave_id,
            batch_id=rec.batch_id,
            timestamp=rec.timestamp,
            timestamp_iso=datetime.utcfromtimestamp(rec.timestamp).isoformat() + "Z",
            data=rec.data.decode("utf-8", errors="replace"),
            data_hex=rec.data.hex(),
        )

    @app.get("/api/data/{slave_id}/day/{year}/{month}/{day}", response_model=DayRecordsResponse, tags=["Data"])
    async def get_records_by_day(
        slave_id: str,
        year: int = Query(..., ge=2020, le=2100, description="Year"),
        month: int = Query(..., ge=1, le=12, description="Month"),
        day: int = Query(..., ge=1, le=31, description="Day"),
    ):
        """
        Get all stored records for a slave on a specific day.

        Records are returned in chronological order, suitable for
        displaying a day's data from beginning to end.
        """
        master = get_master()
        records = master.storage.get_records_by_day(slave_id, year, month, day)

        return DayRecordsResponse(
            slave_id=slave_id,
            date=f"{year:04d}-{month:02d}-{day:02d}",
            record_count=len(records),
            records=[record_to_response(r) for r in records],
        )

    @app.get("/api/data/{slave_id}/latest", response_model=List[StoredRecordResponse], tags=["Data"])
    async def get_latest_records(
        slave_id: str,
        limit: int = Query(100, ge=1, le=1000, description="Max records to return"),
    ):
        """
        Get the most recent stored records for a slave.

        Records are returned newest first.
        """
        master = get_master()
        records = master.storage.get_latest_records(slave_id, limit)

        return [record_to_response(r) for r in records]

    @app.get("/api/data/{slave_id}/range", response_model=List[StoredRecordResponse], tags=["Data"])
    async def get_records_by_range(
        slave_id: str,
        start: int = Query(..., description="Start Unix timestamp (inclusive)"),
        end: int = Query(..., description="End Unix timestamp (exclusive)"),
        limit: int = Query(1000, ge=1, le=10000, description="Max records to return"),
    ):
        """
        Get stored records for a slave within a time range.

        Records are returned in chronological order.
        """
        master = get_master()
        records = master.storage.get_records_by_range(slave_id, start, end, limit)

        return [record_to_response(r) for r in records]

    @app.get("/api/data/slaves", response_model=List[str], tags=["Data"])
    async def get_data_slaves():
        """Get list of all slave IDs with stored data."""
        master = get_master()
        return master.storage.get_all_slave_ids()

    @app.get("/api/data/{slave_id}/stats", tags=["Data"])
    async def get_data_stats(slave_id: str):
        """Get storage statistics for a slave."""
        master = get_master()
        count = master.storage.get_record_count(slave_id)
        date_range = master.storage.get_date_range(slave_id)

        from datetime import datetime
        result = {
            "slave_id": slave_id,
            "record_count": count,
            "first_record": None,
            "last_record": None,
        }

        if date_range:
            result["first_record"] = {
                "timestamp": date_range[0],
                "iso": datetime.utcfromtimestamp(date_range[0]).isoformat() + "Z",
            }
            result["last_record"] = {
                "timestamp": date_range[1],
                "iso": datetime.utcfromtimestamp(date_range[1]).isoformat() + "Z",
            }

        return result

    # -------------------------------------------------------------------------
    # Data Deletion Endpoints
    # -------------------------------------------------------------------------

    class DeleteResponse(BaseModel):
        """Response for delete operations."""
        success: bool = Field(..., description="Whether deletion was successful")
        deleted_count: int = Field(..., ge=0, description="Number of records deleted")
        message: str = Field(..., description="Status message")

    @app.delete("/api/data/{slave_id}/record/{record_id}", response_model=DeleteResponse, tags=["Data"])
    async def delete_record(slave_id: str, record_id: int):
        """
        Delete a specific record by ID.

        This permanently removes the record from the database.
        """
        master = get_master()
        deleted = master.storage.delete_record(record_id)

        if deleted:
            return DeleteResponse(
                success=True,
                deleted_count=1,
                message=f"Record {record_id} deleted",
            )
        else:
            raise HTTPException(status_code=404, detail=f"Record {record_id} not found")

    @app.delete("/api/data/{slave_id}/batch/{batch_id}", response_model=DeleteResponse, tags=["Data"])
    async def delete_batch(slave_id: str, batch_id: int):
        """
        Delete all records from a specific batch.

        This permanently removes all records associated with the batch.
        """
        master = get_master()
        count = master.storage.delete_records_by_batch(slave_id, batch_id)

        return DeleteResponse(
            success=count > 0,
            deleted_count=count,
            message=f"Deleted {count} records from batch {batch_id}" if count > 0 else "No records found",
        )

    @app.delete("/api/data/{slave_id}/day/{year}/{month}/{day}", response_model=DeleteResponse, tags=["Data"])
    async def delete_records_by_day(
        slave_id: str,
        year: int,
        month: int,
        day: int,
    ):
        """
        Delete all records for a slave on a specific day.

        This permanently removes all records for the specified date.
        """
        master = get_master()
        count = master.storage.delete_records_by_day(slave_id, year, month, day)

        return DeleteResponse(
            success=count > 0,
            deleted_count=count,
            message=f"Deleted {count} records for {year:04d}-{month:02d}-{day:02d}",
        )

    @app.delete("/api/data/{slave_id}/range", response_model=DeleteResponse, tags=["Data"])
    async def delete_records_by_range(
        slave_id: str,
        start: int = Query(..., description="Start Unix timestamp (inclusive)"),
        end: int = Query(..., description="End Unix timestamp (exclusive)"),
    ):
        """
        Delete records for a slave within a time range.

        This permanently removes all records in the specified time range.
        """
        master = get_master()
        count = master.storage.delete_records_by_range(slave_id, start, end)

        return DeleteResponse(
            success=count > 0,
            deleted_count=count,
            message=f"Deleted {count} records between {start} and {end}",
        )

    @app.delete("/api/data/{slave_id}", response_model=DeleteResponse, tags=["Data"])
    async def delete_all_slave_data(slave_id: str):
        """
        Delete ALL records for a slave.

        WARNING: This permanently removes all stored data for the slave.
        Use with caution.
        """
        master = get_master()
        count = master.storage.delete_all_slave_records(slave_id)

        return DeleteResponse(
            success=count > 0,
            deleted_count=count,
            message=f"Deleted all {count} records for {slave_id}",
        )

    @app.post("/api/data/vacuum", response_model=CommandResponse, tags=["Data"])
    async def vacuum_database():
        """
        Reclaim disk space after deletions.

        Run this after deleting large amounts of data to reduce database file size.
        """
        master = get_master()
        master.storage.vacuum()

        return CommandResponse(
            success=True,
            message="Database vacuum completed",
        )

    return app


# =============================================================================
# Server Runner
# =============================================================================

def run_api_server(
    app: FastAPI,
    host: str = API_DEFAULT_HOST,
    port: int = API_DEFAULT_PORT,
    log_level: str = "info",
):
    """
    Run the API server (blocking).

    Args:
        app: FastAPI application instance.
        host: Host to bind to.
        port: Port to bind to.
        log_level: Logging level.
    """
    try:
        import uvicorn
    except ImportError:
        raise ImportError("uvicorn is required. Install with: pip3 install uvicorn")

    uvicorn.run(app, host=host, port=port, log_level=log_level)


async def run_api_server_async(
    app: FastAPI,
    host: str = API_DEFAULT_HOST,
    port: int = API_DEFAULT_PORT,
    log_level: str = "info",
):
    """
    Run the API server asynchronously.

    Use this when running the API alongside the master controller.

    Args:
        app: FastAPI application instance.
        host: Host to bind to.
        port: Port to bind to.
        log_level: Logging level.
    """
    try:
        import uvicorn
    except ImportError:
        raise ImportError("uvicorn is required. Install with: pip3 install uvicorn")

    config = uvicorn.Config(app, host=host, port=port, log_level=log_level)
    server = uvicorn.Server(config)
    await server.serve()
