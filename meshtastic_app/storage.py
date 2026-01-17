#!/usr/bin/env python3
"""
Data Storage for Meshtastic Master Controller

Simple SQLite-based storage for slave data batches.
Stores records with absolute timestamps for easy date-based queries.
"""

import sqlite3
import time
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional, Tuple

from .models import MAX_DATA_BATCHES


# =============================================================================
# Constants
# =============================================================================

# Default database file
DEFAULT_DB_PATH = "master_data.db"

# Maximum records to return in a single query (NASA Rule 2)
MAX_QUERY_RESULTS = 10000


# =============================================================================
# Data Classes
# =============================================================================

@dataclass
class StoredRecord:
    """A single stored record with absolute timestamp."""
    id: int
    slave_id: str
    batch_id: int
    timestamp: int  # Absolute Unix timestamp
    data: bytes
    received_at: int


# =============================================================================
# Storage Manager
# =============================================================================

class DataStorage:
    """
    SQLite-based storage for slave data batches.

    Records are stored with absolute timestamps (batch_time + delta)
    for easy querying by date range.
    """

    def __init__(self, db_path: str = DEFAULT_DB_PATH):
        """
        Initialize storage.

        Args:
            db_path: Path to SQLite database file.
        """
        self.db_path = db_path
        self._init_db()

    def _init_db(self):
        """Initialize database schema."""
        with sqlite3.connect(self.db_path) as conn:
            conn.execute("""
                CREATE TABLE IF NOT EXISTS records (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    slave_id TEXT NOT NULL,
                    batch_id INTEGER NOT NULL,
                    timestamp INTEGER NOT NULL,
                    data BLOB NOT NULL,
                    received_at INTEGER NOT NULL
                )
            """)

            # Index for efficient date queries
            conn.execute("""
                CREATE INDEX IF NOT EXISTS idx_slave_timestamp
                ON records(slave_id, timestamp)
            """)

            # Index for batch lookups
            conn.execute("""
                CREATE INDEX IF NOT EXISTS idx_batch
                ON records(slave_id, batch_id)
            """)

            conn.commit()

    def store_batch(
        self,
        slave_id: str,
        batch_id: int,
        batch_timestamp: int,
        records: List[Tuple[int, bytes]],  # [(delta_seconds, data), ...]
    ) -> int:
        """
        Store a batch of records.

        Args:
            slave_id: Source slave node ID.
            batch_id: Unique batch identifier.
            batch_timestamp: Unix timestamp when batch was created.
            records: List of (delta_seconds, data) tuples.

        Returns:
            Number of records stored.
        """
        if not records:
            return 0

        received_at = int(time.time())

        # Convert deltas to absolute timestamps
        rows = [
            (slave_id, batch_id, batch_timestamp + delta, data, received_at)
            for delta, data in records
        ]

        with sqlite3.connect(self.db_path) as conn:
            conn.executemany(
                """
                INSERT INTO records (slave_id, batch_id, timestamp, data, received_at)
                VALUES (?, ?, ?, ?, ?)
                """,
                rows
            )
            conn.commit()

        return len(rows)

    def get_records_by_day(
        self,
        slave_id: str,
        year: int,
        month: int,
        day: int,
    ) -> List[StoredRecord]:
        """
        Get all records for a slave on a specific day.

        Args:
            slave_id: Slave node ID.
            year: Year (e.g., 2025).
            month: Month (1-12).
            day: Day (1-31).

        Returns:
            List of records ordered by timestamp.
        """
        import calendar
        from datetime import datetime

        # Calculate day boundaries in UTC
        start = datetime(year, month, day, 0, 0, 0)
        start_ts = int(start.timestamp())
        end_ts = start_ts + 86400  # +24 hours

        return self.get_records_by_range(slave_id, start_ts, end_ts)

    def get_records_by_range(
        self,
        slave_id: str,
        start_timestamp: int,
        end_timestamp: int,
        limit: int = MAX_QUERY_RESULTS,
    ) -> List[StoredRecord]:
        """
        Get records for a slave within a time range.

        Args:
            slave_id: Slave node ID.
            start_timestamp: Start Unix timestamp (inclusive).
            end_timestamp: End Unix timestamp (exclusive).
            limit: Maximum records to return.

        Returns:
            List of records ordered by timestamp.
        """
        with sqlite3.connect(self.db_path) as conn:
            conn.row_factory = sqlite3.Row
            cursor = conn.execute(
                """
                SELECT id, slave_id, batch_id, timestamp, data, received_at
                FROM records
                WHERE slave_id = ? AND timestamp >= ? AND timestamp < ?
                ORDER BY timestamp
                LIMIT ?
                """,
                (slave_id, start_timestamp, end_timestamp, limit)
            )

            return [
                StoredRecord(
                    id=row["id"],
                    slave_id=row["slave_id"],
                    batch_id=row["batch_id"],
                    timestamp=row["timestamp"],
                    data=row["data"],
                    received_at=row["received_at"],
                )
                for row in cursor
            ]

    def get_latest_records(
        self,
        slave_id: str,
        limit: int = 100,
    ) -> List[StoredRecord]:
        """
        Get the most recent records for a slave.

        Args:
            slave_id: Slave node ID.
            limit: Maximum records to return.

        Returns:
            List of records ordered by timestamp (newest first).
        """
        with sqlite3.connect(self.db_path) as conn:
            conn.row_factory = sqlite3.Row
            cursor = conn.execute(
                """
                SELECT id, slave_id, batch_id, timestamp, data, received_at
                FROM records
                WHERE slave_id = ?
                ORDER BY timestamp DESC
                LIMIT ?
                """,
                (slave_id, limit)
            )

            return [
                StoredRecord(
                    id=row["id"],
                    slave_id=row["slave_id"],
                    batch_id=row["batch_id"],
                    timestamp=row["timestamp"],
                    data=row["data"],
                    received_at=row["received_at"],
                )
                for row in cursor
            ]

    def get_all_slave_ids(self) -> List[str]:
        """Get list of all slave IDs with stored data."""
        with sqlite3.connect(self.db_path) as conn:
            cursor = conn.execute(
                "SELECT DISTINCT slave_id FROM records ORDER BY slave_id"
            )
            return [row[0] for row in cursor]

    def get_record_count(self, slave_id: str = None) -> int:
        """
        Get total record count.

        Args:
            slave_id: Optional slave ID to filter by.

        Returns:
            Number of records.
        """
        with sqlite3.connect(self.db_path) as conn:
            if slave_id:
                cursor = conn.execute(
                    "SELECT COUNT(*) FROM records WHERE slave_id = ?",
                    (slave_id,)
                )
            else:
                cursor = conn.execute("SELECT COUNT(*) FROM records")
            return cursor.fetchone()[0]

    def get_date_range(self, slave_id: str) -> Optional[Tuple[int, int]]:
        """
        Get the timestamp range for a slave's data.

        Args:
            slave_id: Slave node ID.

        Returns:
            Tuple of (min_timestamp, max_timestamp) or None if no data.
        """
        with sqlite3.connect(self.db_path) as conn:
            cursor = conn.execute(
                """
                SELECT MIN(timestamp), MAX(timestamp)
                FROM records
                WHERE slave_id = ?
                """,
                (slave_id,)
            )
            row = cursor.fetchone()
            if row[0] is None:
                return None
            return (row[0], row[1])

    def delete_old_records(self, before_timestamp: int) -> int:
        """
        Delete records older than a timestamp.

        Args:
            before_timestamp: Delete records before this Unix timestamp.

        Returns:
            Number of records deleted.
        """
        with sqlite3.connect(self.db_path) as conn:
            cursor = conn.execute(
                "DELETE FROM records WHERE timestamp < ?",
                (before_timestamp,)
            )
            conn.commit()
            return cursor.rowcount

    def vacuum(self):
        """Reclaim disk space after deletions."""
        with sqlite3.connect(self.db_path) as conn:
            conn.execute("VACUUM")

    def delete_record(self, record_id: int) -> bool:
        """
        Delete a specific record by ID.

        Args:
            record_id: Database record ID.

        Returns:
            True if record was deleted, False if not found.
        """
        with sqlite3.connect(self.db_path) as conn:
            cursor = conn.execute(
                "DELETE FROM records WHERE id = ?",
                (record_id,)
            )
            conn.commit()
            return cursor.rowcount > 0

    def delete_records_by_batch(self, slave_id: str, batch_id: int) -> int:
        """
        Delete all records from a specific batch.

        Args:
            slave_id: Slave node ID.
            batch_id: Batch ID to delete.

        Returns:
            Number of records deleted.
        """
        with sqlite3.connect(self.db_path) as conn:
            cursor = conn.execute(
                "DELETE FROM records WHERE slave_id = ? AND batch_id = ?",
                (slave_id, batch_id)
            )
            conn.commit()
            return cursor.rowcount

    def delete_records_by_day(
        self,
        slave_id: str,
        year: int,
        month: int,
        day: int,
    ) -> int:
        """
        Delete all records for a slave on a specific day.

        Args:
            slave_id: Slave node ID.
            year: Year (e.g., 2025).
            month: Month (1-12).
            day: Day (1-31).

        Returns:
            Number of records deleted.
        """
        from datetime import datetime

        start = datetime(year, month, day, 0, 0, 0)
        start_ts = int(start.timestamp())
        end_ts = start_ts + 86400

        return self.delete_records_by_range(slave_id, start_ts, end_ts)

    def delete_records_by_range(
        self,
        slave_id: str,
        start_timestamp: int,
        end_timestamp: int,
    ) -> int:
        """
        Delete records for a slave within a time range.

        Args:
            slave_id: Slave node ID.
            start_timestamp: Start Unix timestamp (inclusive).
            end_timestamp: End Unix timestamp (exclusive).

        Returns:
            Number of records deleted.
        """
        with sqlite3.connect(self.db_path) as conn:
            cursor = conn.execute(
                """
                DELETE FROM records
                WHERE slave_id = ? AND timestamp >= ? AND timestamp < ?
                """,
                (slave_id, start_timestamp, end_timestamp)
            )
            conn.commit()
            return cursor.rowcount

    def delete_all_slave_records(self, slave_id: str) -> int:
        """
        Delete all records for a slave.

        Args:
            slave_id: Slave node ID.

        Returns:
            Number of records deleted.
        """
        with sqlite3.connect(self.db_path) as conn:
            cursor = conn.execute(
                "DELETE FROM records WHERE slave_id = ?",
                (slave_id,)
            )
            conn.commit()
            return cursor.rowcount
