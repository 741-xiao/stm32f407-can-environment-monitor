

import argparse
from dataclasses import asdict, dataclass, field
from datetime import datetime, timedelta, timezone
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import logging
import os
from pathlib import Path
import socket
import socketserver
import threading
import tomllib
from typing import Any
from urllib.parse import parse_qs, urlsplit
import uuid

from protocol import WireSample, integer, parse_frame
from storage import MySQLRepository, SampleStore

ROOT = Path(__file__).resolve().parent
DEVELOPER_NAME = "肖肃"
STUDENT_ID = "202440700102"


@dataclass
class BoardClient:
    board_id: int
    connection: socket.socket
    anchor_uptime: int
    anchor_time: datetime = field(default_factory=lambda: datetime.now(timezone.utc))
    session_id: str = field(default_factory=lambda: uuid.uuid4().hex)
    sequence: int | None = None
    sharing: bool = False
    latest: str | None = None
    latest_received: datetime | None = None

    def send(self, line: str) -> None:
        try:
            self.connection.sendall((line.rstrip("\r\n") + "\r\n").encode("ascii"))
        except OSError:
            try:
                self.connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass


class BoardHub:
    def __init__(self, store: SampleStore) -> None:
        self.store = store
        self._clients: dict[int, BoardClient] = {}
        self._lock = threading.Lock()

    def register(self, board_id: int, connection: socket.socket, uptime_ms: int) -> BoardClient:
        with self._lock:
            if board_id in self._clients:
                raise ValueError(f"板号 {board_id} 已连接，请检查三块板的 APP_BOARD_ID")
            client = BoardClient(board_id, connection, uptime_ms)
            self._clients[board_id] = client
            # Board 1 is the collector. Its display is mirrored to boards 2/3;
            # the PC forwards only collector data so all OLEDs show one sample.
            if board_id != 1:
                collector = self._clients.get(1)
                client.send(f"CTRL,1,{int(collector.sharing) if collector else 0}")
                if collector and collector.sharing and collector.latest and collector.latest_received:
                    if (datetime.now(timezone.utc) - collector.latest_received).total_seconds() < 8:
                        client.send(collector.latest)
            logging.info("板 %d 已注册，连接会话 %s", board_id, client.session_id)
            return client

    def control(self, client: BoardClient, sharing: bool) -> None:
        with self._lock:
            client.sharing = sharing
            if client.board_id == 1:
                for receiver in (2, 3):
                    peer = self._clients.get(receiver)
                    if peer:
                        peer.send(f"CTRL,1,{int(sharing)}")

    def sample(self, client: BoardClient, line: str, wire: WireSample) -> None:
        with self._lock:
            if wire.board_id != client.board_id:
                raise ValueError("数据板号与连接身份不符")
            if client.sequence is not None:
                delta_sequence = (wire.sequence - client.sequence) & 0xFFFFFFFF
                if not 0 < delta_sequence < 0x80000000:
                    raise ValueError("重复或倒序采样序号")
            delta_ms = ((wire.uptime_ms - client.anchor_uptime + 0x80000000) & 0xFFFFFFFF) - 0x80000000
            collected_at = client.anchor_time + timedelta(milliseconds=delta_ms)
            if abs((collected_at - datetime.now(timezone.utc)).total_seconds()) > 60:
                raise ValueError("采集时刻偏差超过 60 秒，请重新连接")
            client.sequence = wire.sequence
            client.anchor_uptime, client.anchor_time = wire.uptime_ms, collected_at
            client.sharing = wire.sharing
            client.latest, client.latest_received = line, datetime.now(timezone.utc)
            self.store.add(wire, client.session_id, collected_at)
            if client.board_id == 1:
                for receiver in (2, 3):
                    peer = self._clients.get(receiver)
                    if peer:
                        peer.send(line if wire.sharing else "CTRL,1,0")

    def disconnect(self, client: BoardClient) -> None:
        with self._lock:
            if self._clients.get(client.board_id) is not client:
                return
            del self._clients[client.board_id]
            if client.board_id == 1:
                for receiver in (2, 3):
                    peer = self._clients.get(receiver)
                    if peer:
                        peer.send("CTRL,1,0")
        logging.info("板 %d 已断开", client.board_id)

    def status(self) -> list[dict[str, Any]]:
        with self._lock:
            now = datetime.now(timezone.utc)
            return [{"board_id": board_id, "connected": board_id in self._clients,
                     "sharing": self._clients[board_id].sharing if board_id in self._clients else False,
                     "fresh": bool(board_id in self._clients and self._clients[board_id].latest_received and
                                   (now - self._clients[board_id].latest_received).total_seconds() < 8)}
                    for board_id in (1, 2, 3)]

    def close(self) -> None:
        with self._lock:
            clients = list(self._clients.values())
            self._clients.clear()
        for client in clients:
            try:
                client.connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass


class SensorTCPHandler(socketserver.StreamRequestHandler):
    def handle(self) -> None:
        hub: BoardHub = self.server.hub
        client = None
        self.request.settimeout(10)
        self.request.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
        try:
            while True:
                raw = self.rfile.readline(128)
                if not raw:
                    break
                if not raw.endswith(b"\n"):
                    raise ValueError("网络帧过长或不完整")
                try:
                    line = raw.decode("ascii").strip()
                    fields = line.split(",")
                    if client is None:
                        if len(fields) != 3 or fields[0] != "HELLO":
                            raise ValueError("连接首帧必须为 HELLO,板号,运行毫秒")
                        client = hub.register(integer(fields[1], 1, 3), self.request, integer(fields[2], 0, 0xFFFFFFFF))
                    elif fields[0] == "CTRL" and len(fields) == 3:
                        if integer(fields[1], 1, 3) != client.board_id:
                            raise ValueError("控制板号与连接身份不符")
                        hub.control(client, bool(integer(fields[2], 0, 1)))
                    else:
                        hub.sample(client, line, parse_frame(line))
                except (ValueError, UnicodeDecodeError) as exc:
                    hub.store.record_invalid()
                    logging.warning("忽略来自 %s 的无效帧: %s", self.client_address[0], exc)
                    if client is None:
                        break
        except (OSError, ValueError) as exc:
            logging.info("TCP 连接结束: %s", exc)
        finally:
            if client:
                hub.disconnect(client)


class SensorTCPServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, address: tuple[str, int], hub: BoardHub) -> None:
        self.hub = hub
        super().__init__(address, SensorTCPHandler)


class WebHandler(BaseHTTPRequestHandler):
    def _send(self, body: bytes, content_type: str, status: HTTPStatus = HTTPStatus.OK) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:
        server: WebServer = self.server
        url = urlsplit(self.path)
        assets = {"/": ("index.html", "text/html; charset=utf-8"),
                  "/dashboard.js": ("dashboard.js", "text/javascript; charset=utf-8"),
                  "/dashboard.css": ("dashboard.css", "text/css; charset=utf-8")}
        if url.path in assets:
            filename, content_type = assets[url.path]
            self._send((ROOT / "web" / filename).read_bytes(), content_type)
            return
        if url.path in ("/api/data", "/api/history", "/api/status"):
            try:
                query = parse_qs(url.query)
                board_id = integer(query["board"][0], 1, 3) if "board" in query else None
                payload = {"developer_name": server.developer_name, "student_id": server.student_id,
                           "status": server.store.status(), "boards": server.hub.status()}
                if url.path == "/api/history":
                    page = integer(query.get("page", ["1"])[0], 1, 1000000)
                    page_size = integer(query.get("page_size", ["40"])[0], 1, 200)
                    payload.update(server.store.history(board_id, page, page_size))
                elif url.path == "/api/data":
                    payload["samples"] = server.store.snapshot(board_id)
                self._send(json.dumps(payload, ensure_ascii=False).encode("utf-8"), "application/json; charset=utf-8")
            except ValueError as exc:
                self._send(json.dumps({"error": str(exc)}, ensure_ascii=False).encode("utf-8"),
                           "application/json; charset=utf-8", HTTPStatus.BAD_REQUEST)
            except Exception as exc:
                logging.error("历史记录读取失败: %s", exc)
                self._send(b'{"error":"MySQL unavailable"}', "application/json", HTTPStatus.SERVICE_UNAVAILABLE)
            return
        self._send(b"404 Not Found", "text/plain", HTTPStatus.NOT_FOUND)

    def log_message(self, message: str, *args: object) -> None:
        logging.debug("HTTP %s", message % args)


class WebServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address: tuple[str, int], hub: BoardHub,
                 developer_name: str = DEVELOPER_NAME, student_id: str = STUDENT_ID) -> None:
        self.hub, self.store = hub, hub.store
        self.developer_name, self.student_id = developer_name, student_id
        super().__init__(address, WebHandler)


def main() -> None:
    parser = argparse.ArgumentParser(description="三 STM32 WiFi 采集、列表、图表与 MySQL 存储")
    parser.add_argument("--config", type=Path, default=ROOT / "server.toml")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--data-port", type=int, default=5107)
    parser.add_argument("--web-port", type=int, default=8080)
    parser.add_argument("--memory-only", action="store_true", help="仅用于无 MySQL 的通信演示，不保存数据")
    parser.add_argument("--developer-name", default=DEVELOPER_NAME)
    parser.add_argument("--student-id", default=STUDENT_ID)
    parser.add_argument("--debug", action="store_true")
    args = parser.parse_args()
    logging.basicConfig(level=logging.DEBUG if args.debug else logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    settings = tomllib.loads(args.config.read_text(encoding="utf-8")) if args.config.exists() else {}
    mysql_settings = {"host": "127.0.0.1", "port": 3306, "user": "root",
                      "password": os.environ.get("MYSQL_PASSWORD", ""), "database": "stm32_lab5"}
    mysql_settings.update(settings.get("mysql", {}))
    store = None
    tcp_server = web_server = hub = repository = None
    running_servers = []
    try:
        repository = None if args.memory_only else MySQLRepository(mysql_settings)
        light = settings.get("light", {})
        store = SampleStore(repository, light_dark=light.get("dark_adc", 4095), light_bright=light.get("bright_adc", 0))
        hub = BoardHub(store)
        tcp_server = SensorTCPServer((args.host, args.data_port), hub)
        web_server = WebServer((args.host, args.web_port), hub, args.developer_name, args.student_id)
        for server in (tcp_server, web_server):
            threading.Thread(target=server.serve_forever, daemon=True).start()
            running_servers.append(server)
        logging.info("三板 TCP: %s:%d；浏览器: http://127.0.0.1:%d/", args.host, args.data_port, args.web_port)
        logging.info("数据保存: %s", "内存演示，未启用 MySQL" if args.memory_only else mysql_settings["database"])
        threading.Event().wait()
    except KeyboardInterrupt:
        logging.info("正在停止服务")
    except Exception as exc:
        logging.error("启动失败: %s", exc)
        raise SystemExit(1) from exc
    finally:
        for server in running_servers:
            server.shutdown()
        if hub:
            hub.close()
        for server in (tcp_server, web_server):
            if server:
                server.server_close()
        if store:
            store.close()
        elif repository:
            repository.close()


if __name__ == "__main__":
    main()
