from __future__ import annotations

from collections import deque
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
import logging
import queue
import re
import threading
from typing import Any

from protocol import WireSample, light_percent


@dataclass(frozen=True)
class Sample:
    number: int
    board_id: int
    session_id: str
    sequence: int
    uptime_ms: int
    timestamp: str
    received_at: str
    temperature: float | None
    humidity: float | None
    light_adc: int | None
    light_percent: float | None
    co2_ppm: int | None
    formaldehyde_mg_m3: float | None
    valid: int
    sharing: bool


class MySQLRepository:
    def __init__(self, settings: dict[str, Any]) -> None:
        try:
            import pymysql
        except ImportError as exc:
            raise RuntimeError("请先执行 python -m pip install -r requirements.txt") from exc
        self._driver = pymysql
        self._settings = dict(settings)
        self._database = self._settings.pop("database", "stm32_lab5")
        if not re.fullmatch(r"[A-Za-z0-9_]{1,64}", self._database):
            raise ValueError("数据库名只允许字母、数字和下划线")
        self._connection: Any = None
        self._lock = threading.Lock()
        self._connect()

    def _connect(self) -> None:
        self._connection = self._driver.connect(
            **self._settings, charset="utf8mb4", autocommit=True,
            connect_timeout=3, read_timeout=3, write_timeout=3,
        )
        with self._connection.cursor() as cursor:
            cursor.execute(f"CREATE DATABASE IF NOT EXISTS `{self._database}` CHARACTER SET utf8mb4")
        self._connection.select_db(self._database)
        with self._connection.cursor() as cursor:
            cursor.execute("""
                CREATE TABLE IF NOT EXISTS sensor_samples (
                    id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
                    number BIGINT UNSIGNED NOT NULL,
                    board_id TINYINT UNSIGNED NOT NULL,
                    session_id CHAR(32) NOT NULL,
                    sequence BIGINT UNSIGNED NOT NULL,
                    uptime_ms BIGINT UNSIGNED NOT NULL,
                    collected_at DATETIME(6) NOT NULL,
                    received_at DATETIME(6) NOT NULL,
                    temperature DECIMAL(6,2) NULL,
                    humidity DECIMAL(5,2) NULL,
                    light_adc SMALLINT UNSIGNED NULL,
                    light_percent DECIMAL(5,2) NULL,
                    co2_ppm SMALLINT UNSIGNED NULL,
                    formaldehyde_mg_m3 DECIMAL(6,2) NULL,
                    valid TINYINT UNSIGNED NOT NULL,
                    sharing BOOLEAN NOT NULL,
                    UNIQUE KEY sample_identity (board_id, session_id, sequence),
                    KEY board_time (board_id, collected_at),
                    KEY collection_time (collected_at)
                ) ENGINE=InnoDB
            """)
            # Migrate a laboratory database created by the two-board version.
            for column, definition in (
                ("co2_ppm", "SMALLINT UNSIGNED NULL AFTER light_percent"),
                ("formaldehyde_mg_m3", "DECIMAL(6,2) NULL AFTER co2_ppm"),
            ):
                cursor.execute("SHOW COLUMNS FROM sensor_samples LIKE %s", (column,))
                if cursor.fetchone() is None:
                    cursor.execute(f"ALTER TABLE sensor_samples ADD COLUMN {column} {definition}")

    def _ready(self) -> None:
        if self._connection is None:
            self._connect()
        else:
            try:
                self._connection.ping()
            except self._driver.MySQLError:
                self._connection.close()
                self._connection = None
                self._connect()
            self._connection.select_db(self._database)

    def save(self, sample: Sample) -> None:
        with self._lock:
            self._ready()
            with self._connection.cursor() as cursor:
                cursor.execute("""
                    INSERT INTO sensor_samples
                    (number, board_id, session_id, sequence, uptime_ms, collected_at,
                     received_at, temperature, humidity, light_adc, light_percent,
                     co2_ppm, formaldehyde_mg_m3, valid, sharing)
                    VALUES (%s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s)
                    ON DUPLICATE KEY UPDATE sequence=VALUES(sequence)
                """, (
                    sample.number, sample.board_id, sample.session_id, sample.sequence,
                    sample.uptime_ms, self._utc(sample.timestamp), self._utc(sample.received_at),
                    sample.temperature, sample.humidity, sample.light_adc, sample.light_percent,
                    sample.co2_ppm, sample.formaldehyde_mg_m3, sample.valid, sample.sharing,
                ))

    @staticmethod
    def _utc(value: str) -> datetime:
        return datetime.fromisoformat(value).astimezone(timezone.utc).replace(tzinfo=None)

    def recent(self, limit: int, board_id: int | None = None) -> list[Sample]:
        with self._lock:
            self._ready()
            with self._connection.cursor(self._driver.cursors.DictCursor) as cursor:
                condition = " WHERE board_id=%s" if board_id is not None else ""
                parameters = (board_id, limit) if board_id is not None else (limit,)
                cursor.execute("SELECT * FROM sensor_samples" + condition + " ORDER BY id DESC LIMIT %s", parameters)
                rows = cursor.fetchall()
        return [self._to_sample(row) for row in reversed(rows)]

    def history(self, board_id: int | None, page: int, page_size: int) -> tuple[list[Sample], int]:
        """Return one newest-first page, while keeping each page chronological for charts."""
        if page < 1 or page_size < 1:
            raise ValueError("页码和每页条数必须大于零")
        condition = " WHERE board_id=%s" if board_id is not None else ""
        parameters: tuple[Any, ...] = (board_id,) if board_id is not None else ()
        offset = (page - 1) * page_size
        with self._lock:
            self._ready()
            with self._connection.cursor(self._driver.cursors.DictCursor) as cursor:
                cursor.execute("SELECT COUNT(*) AS total FROM sensor_samples" + condition, parameters)
                total = int(cursor.fetchone()["total"])
                cursor.execute(
                    "SELECT * FROM sensor_samples" + condition + " ORDER BY id DESC LIMIT %s OFFSET %s",
                    parameters + (page_size, offset),
                )
                rows = cursor.fetchall()
        return [self._to_sample(row) for row in reversed(rows)], total

    @staticmethod
    def _to_sample(row: dict[str, Any]) -> Sample:
        return Sample(
            number=row["number"], board_id=row["board_id"], session_id=row["session_id"],
            sequence=row["sequence"], uptime_ms=row["uptime_ms"],
            timestamp=row["collected_at"].replace(tzinfo=timezone.utc).isoformat(timespec="milliseconds"),
            received_at=row["received_at"].replace(tzinfo=timezone.utc).isoformat(timespec="milliseconds"),
            temperature=float(row["temperature"]) if row["temperature"] is not None else None,
            humidity=float(row["humidity"]) if row["humidity"] is not None else None,
            light_adc=row["light_adc"],
            light_percent=float(row["light_percent"]) if row["light_percent"] is not None else None,
            co2_ppm=row.get("co2_ppm"),
            formaldehyde_mg_m3=(float(row["formaldehyde_mg_m3"])
                                if row.get("formaldehyde_mg_m3") is not None else None),
            valid=row["valid"], sharing=bool(row["sharing"]),
        )

    def close(self) -> None:
        with self._lock:
            if self._connection is not None:
                self._connection.close()


class SampleStore:
    def __init__(self, repository: MySQLRepository | None = None, limit: int = 400,
                 light_dark: int = 4095, light_bright: int = 0) -> None:
        self.repository = repository
        self._samples: deque[Sample] = deque(repository.recent(limit) if repository else [], maxlen=limit)
        self._next_number = max((sample.number for sample in self._samples), default=0) + 1
        self._received = self._invalid_frames = self._saved = self._dropped = 0
        self._database_error = ""
        self._light_dark, self._light_bright = light_dark, light_bright
        light_percent(0, light_dark, light_bright)
        self._lock = threading.Lock()
        self._pending: queue.Queue[Sample] = queue.Queue(maxsize=4096)
        self._stop = threading.Event()
        self._worker: threading.Thread | None = None
        if repository:
            self._worker = threading.Thread(target=self._save_loop, name="mysql-writer", daemon=True)
            self._worker.start()

    def add(self, wire: WireSample, session_id: str, collected_at: datetime) -> Sample:
        with self._lock:
            sample = Sample(
                number=self._next_number, board_id=wire.board_id, session_id=session_id,
                sequence=wire.sequence, uptime_ms=wire.uptime_ms,
                timestamp=collected_at.isoformat(timespec="milliseconds"),
                received_at=datetime.now(timezone.utc).isoformat(timespec="milliseconds"),
                temperature=wire.temperature_centi / 100 if wire.valid & 1 else None,
                humidity=wire.humidity_centi / 100 if wire.valid & 1 else None,
                light_adc=wire.light_adc if wire.valid & 2 else None,
                light_percent=light_percent(wire.light_adc, self._light_dark, self._light_bright) if wire.valid & 2 else None,
                co2_ppm=wire.co2_ppm if wire.valid & 4 else None,
                formaldehyde_mg_m3=(wire.formaldehyde_mg_m3_centi / 100
                                     if wire.valid & 8 else None),
                valid=wire.valid, sharing=wire.sharing,
            )
            self._samples.append(sample)
            self._next_number += 1
            self._received += 1
            if self.repository:
                try:
                    self._pending.put_nowait(sample)
                except queue.Full:
                    self._dropped += 1
                    logging.error("MySQL 缓冲区已满，板 %d 第 %d 条数据未入库", wire.board_id, wire.sequence)
            return sample

    def _save_loop(self) -> None:
        while not self._stop.is_set():
            try:
                sample = self._pending.get(timeout=0.2)
            except queue.Empty:
                continue
            while not self._stop.is_set():
                try:
                    assert self.repository is not None
                    self.repository.save(sample)
                except Exception as exc:
                    with self._lock:
                        self._database_error = str(exc)
                    logging.error("MySQL 写入失败，保留数据重试: %s", exc)
                    self._stop.wait(1)
                else:
                    with self._lock:
                        self._saved += 1
                        self._database_error = ""
                    self._pending.task_done()
                    break

    def snapshot(self, board_id: int | None = None) -> list[dict[str, Any]]:
        with self._lock:
            return [asdict(sample) for sample in self._samples
                    if board_id is None or sample.board_id == board_id]

    def history(self, board_id: int | None, page: int, page_size: int) -> dict[str, Any]:
        if page < 1 or page_size < 1:
            raise ValueError("页码和每页条数必须大于零")
        if self.repository is not None:
            samples, total = self.repository.history(board_id, page, page_size)
        else:
            with self._lock:
                filtered = [asdict(sample) for sample in self._samples
                            if board_id is None or sample.board_id == board_id]
            total = len(filtered)
            end = max(0, total - (page - 1) * page_size)
            start = max(0, end - page_size)
            samples = filtered[start:end]
        return {
            "samples": [asdict(sample) if isinstance(sample, Sample) else sample for sample in samples],
            "page": page,
            "page_size": page_size,
            "total": total,
            "page_count": (total + page_size - 1) // page_size,
        }

    def record_invalid(self) -> None:
        with self._lock:
            self._invalid_frames += 1

    def status(self) -> dict[str, Any]:
        with self._lock:
            return {"received": self._received, "invalid_frames": self._invalid_frames,
                    "database": "mysql" if self.repository else "memory",
                    "saved": self._saved, "pending": self._pending.unfinished_tasks,
                    "dropped": self._dropped, "database_error": self._database_error}

    def close(self) -> None:
        deadline = threading.Event()
        for attempt in range(50):
            if not self._pending.unfinished_tasks:
                break
            deadline.wait(0.1)
        if self._pending.unfinished_tasks:
            logging.error("退出时仍有 %d 条数据未入库", self._pending.unfinished_tasks)
        self._stop.set()
        if self._worker:
            self._worker.join(timeout=4)
        if self.repository:
            self.repository.close()
