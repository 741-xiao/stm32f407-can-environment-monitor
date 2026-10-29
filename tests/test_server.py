from __future__ import annotations

from datetime import datetime, timezone
import json
import os
import socket
import threading
import time
import unittest
from urllib.error import HTTPError
from urllib.parse import urlencode
from urllib.request import urlopen

from protocol import light_percent, parse_frame
from server import BoardHub, SensorTCPServer, WebServer
from storage import MySQLRepository, SampleStore


class ProtocolTests(unittest.TestCase):
    def test_v1_v2_and_sensor_limits(self) -> None:
        v1 = "DATA,1,4294967295,4294967295,-25,6000,2048,3,1"
        v2 = "DATA,3,7,1200,2650,7025,1980,812,27,15,1"
        self.assertEqual(parse_frame(v1).temperature_centi, -25)
        self.assertEqual(parse_frame(v1).co2_ppm, 0)
        sample = parse_frame(v2)
        self.assertEqual((sample.board_id, sample.co2_ppm, sample.formaldehyde_mg_m3_centi), (3, 812, 27))
        self.assertEqual((sample.valid, sample.sharing), (15, True))
        for bad in (
            v1.replace("DATA,1", "DATA,0"), v1.replace("DATA,1", "DATA,4"),
            v1.replace("-25", "NaN"), v1.replace("-25", "12501"),
            v1.replace("6000", "10001"), v1.replace("2048", "4096"),
            v1 + ",0", v1.replace(",3,1", ",16,1"),
            v2.replace(",812,", ",5001,"), v2.replace(",27,15", ",1001,15"),
            v2.replace(",15,1", ",15,2"), v2 + ",0",
        ):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                parse_frame(bad)
        self.assertEqual(light_percent(4095), 0)
        self.assertEqual(light_percent(0), 100)
        self.assertEqual(light_percent(2048, 0, 4095), 50.01)


class NetworkTests(unittest.TestCase):
    def setUp(self) -> None:
        self.store = SampleStore()
        self.hub = BoardHub(self.store)
        self.tcp = SensorTCPServer(("127.0.0.1", 0), self.hub)
        self.web = WebServer(("127.0.0.1", 0), self.hub)
        self.connections: list[socket.socket] = []
        self.files: list[object] = []
        self.threads: list[threading.Thread] = []
        for server in (self.tcp, self.web):
            thread = threading.Thread(target=server.serve_forever, kwargs={"poll_interval": 0.02}, daemon=True)
            thread.start()
            self.threads.append(thread)

    def tearDown(self) -> None:
        for file in self.files:
            file.close()
        for connection in self.connections:
            try:
                connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            connection.close()
        self.hub.close()
        for server in (self.tcp, self.web):
            server.shutdown()
            server.server_close()
        for thread in self.threads:
            thread.join(timeout=1)
        self.store.close()

    def connect(self, board_id: int, uptime: int = 1000) -> tuple[socket.socket, object]:
        connection = socket.create_connection(self.tcp.server_address, timeout=2)
        self.connections.append(connection)
        reader = connection.makefile("rb")
        self.files.append(reader)
        connection.sendall(f"HELLO,{board_id},{uptime}\r\n".encode("ascii"))
        return connection, reader

    @staticmethod
    def line(reader: object) -> str:
        return reader.readline().decode("ascii").strip()

    def api(self, path: str = "/api/data") -> dict:
        with urlopen(f"http://127.0.0.1:{self.web.server_port}{path}", timeout=2) as response:
            return json.load(response)

    def wait_count(self, received: int) -> None:
        deadline = time.monotonic() + 2
        while self.store.status()["received"] < received and time.monotonic() < deadline:
            time.sleep(0.01)
        self.assertEqual(self.store.status()["received"], received)

    def test_three_registration_and_only_board1_forwarded(self) -> None:
        board1, reader1 = self.connect(1)
        board2, reader2 = self.connect(2)
        self.assertEqual(self.line(reader2), "CTRL,1,0")
        board3, reader3 = self.connect(3)
        self.assertEqual(self.line(reader3), "CTRL,1,0")
        board1.sendall(b"CTRL,1,1\r\n")
        self.assertEqual(self.line(reader2), "CTRL,1,1")
        self.assertEqual(self.line(reader3), "CTRL,1,1")
        data1 = "DATA,1,1,1001,2500,6000,2048,812,27,15,1"
        board1.sendall(data1[:11].encode("ascii"))
        board1.sendall((data1[11:] + "\r\n").encode("ascii"))
        self.assertEqual(self.line(reader2), data1)
        self.assertEqual(self.line(reader3), data1)
        board2.sendall(b"CTRL,2,1\r\nDATA,2,1,1002,3000,7000,1024,650,18,15,1\r\n")
        board3.sendall(b"CTRL,3,1\r\nDATA,3,1,1003,3100,7100,900,720,20,15,1\r\n")
        self.wait_count(3)
        payload = self.api()
        self.assertEqual([sample["board_id"] for sample in payload["samples"]], [1, 2, 3])
        self.assertEqual(payload["samples"][0]["temperature"], 25)
        self.assertEqual(payload["samples"][1]["co2_ppm"], 650)
        self.assertEqual(payload["samples"][2]["formaldehyde_mg_m3"], 0.2)
        self.assertEqual(len(self.api("/api/data?board=3")["samples"]), 1)
        self.assertEqual([item["connected"] for item in payload["boards"]], [True, True, True])

    def test_board1_stop_and_disconnect_turn_off_remote_displays(self) -> None:
        board1, reader1 = self.connect(1)
        board2, reader2 = self.connect(2)
        board3, reader3 = self.connect(3)
        self.assertEqual(self.line(reader2), "CTRL,1,0")
        self.assertEqual(self.line(reader3), "CTRL,1,0")
        data = "DATA,1,1,1001,2500,6000,2048,812,27,15,1"
        board1.sendall(("CTRL,1,1\r\n" + data + "\r\n").encode("ascii"))
        self.assertEqual(self.line(reader2), "CTRL,1,1")
        self.assertEqual(self.line(reader2), data)
        self.assertEqual(self.line(reader3), "CTRL,1,1")
        self.assertEqual(self.line(reader3), data)
        board1.sendall(b"CTRL,1,0\r\nDATA,1,2,1003,2600,6100,2000,820,28,15,0\r\n")
        self.assertEqual(self.line(reader2), "CTRL,1,0")
        self.assertEqual(self.line(reader3), "CTRL,1,0")
        self.wait_count(2)
        self.close_socket(board1)
        self.assertEqual(self.line(reader2), "CTRL,1,0")
        self.assertEqual(self.line(reader3), "CTRL,1,0")
        deadline = time.monotonic() + 2
        while self.api()["boards"][0]["connected"] and time.monotonic() < deadline:
            time.sleep(0.02)
        self.assertFalse(self.api()["boards"][0]["connected"])

    @staticmethod
    def close_socket(connection: socket.socket) -> None:
        try:
            connection.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        connection.close()

    def test_late_receiver_replay_has_control_then_latest_sample(self) -> None:
        board1, reader1 = self.connect(1)
        data = "DATA,1,1,1001,2500,6000,2048,812,27,15,1"
        board1.sendall(("CTRL,1,1\r\n" + data + "\r\n").encode("ascii"))
        self.wait_count(1)
        _board3, reader3 = self.connect(3)
        self.assertEqual(self.line(reader3), "CTRL,1,1")
        self.assertEqual(self.line(reader3), data)

    def test_identity_duplicate_order_and_invalid_frames(self) -> None:
        board1, reader1 = self.connect(1)
        board1.sendall(b"DATA,1,1,1001,2500,6000,2048,812,27,15,0\r\n")
        self.wait_count(1)
        board1.sendall(
            b"DATA,2,2,1002,2500,6000,2048,812,27,15,0\r\n"
            b"DATA,1,1,1001,2500,6000,2048,812,27,15,0\r\n"
            b"DATA,1,0,1000,2500,6000,2048,812,27,15,0\r\n"
            b"DATA,1,2,1002,NaN,6000,2048,812,27,15,0\r\n"
            b"DATA,1,3,1003,2500,6000,2048,812,27,16,0\r\n"
            b"CTRL,2,1\r\n"
            b"DATA,1,4,1004,0,0,2048,0,0,2,0\r\n"
        )
        self.wait_count(2)
        self.assertEqual(self.store.status()["invalid_frames"], 6)
        sample = self.api()["samples"][-1]
        self.assertIsNone(sample["temperature"])
        self.assertIsNone(sample["humidity"])
        self.assertIsNotNone(sample["light_percent"])
        self.assertIsNone(sample["co2_ppm"])
        self.assertIsNone(sample["formaldehyde_mg_m3"])
        duplicate, duplicate_reader = self.connect(1)
        self.assertEqual(self.line(duplicate_reader), "")
        self.assertTrue(self.api()["boards"][0]["connected"])

    def test_clock_and_sequence_rollover(self) -> None:
        board1, reader = self.connect(1, 0xFFFFFFF0)
        board1.sendall(b"DATA,1,4294967295,16,2500,6000,2048,812,27,15,0\r\n"
                       b"DATA,1,0,32,2500,6000,2048,812,27,15,0\r\n")
        self.wait_count(2)
        samples = self.api()["samples"]
        difference = datetime.fromisoformat(samples[1]["timestamp"]) - datetime.fromisoformat(samples[0]["timestamp"])
        self.assertEqual(difference.total_seconds(), 0.016)

    def test_memory_history_pagination(self) -> None:
        board1, reader = self.connect(1)
        lines = [f"DATA,1,{index},{index * 10},2500,6000,2048,812,27,15,0" for index in range(1, 96)]
        board1.sendall(("\r\n".join(lines) + "\r\n").encode("ascii"))
        self.wait_count(95)
        first = self.api("/api/history?page=1&page_size=40")
        second = self.api("/api/history?page=2&page_size=40")
        last = self.api("/api/history?page=3&page_size=40")
        self.assertEqual((first["page"], first["page_size"], first["total"], first["page_count"]), (1, 40, 95, 3))
        self.assertEqual(len(first["samples"]), 40)
        self.assertEqual(len(second["samples"]), 40)
        self.assertEqual(len(last["samples"]), 15)
        self.assertEqual(first["samples"][0]["sequence"], 56)
        self.assertEqual(first["samples"][-1]["sequence"], 95)
        self.assertEqual(second["samples"][0]["sequence"], 16)
        self.assertEqual(second["samples"][-1]["sequence"], 55)
        self.assertEqual(last["samples"][0]["sequence"], 1)
        self.assertEqual(last["samples"][-1]["sequence"], 15)
        self.assertEqual(self.api("/api/history?board=1&page=1&page_size=200")["total"], 95)
        self.assertEqual(self.api("/api/history?board=2&page=1&page_size=40")["total"], 0)
        for path in ("/api/history?page=0", "/api/history?page_size=0", "/api/history?page_size=201",
                     "/api/history?board=4", "/api/data?board=0"):
            with self.subTest(path=path), self.assertRaises(HTTPError) as caught:
                self.api(path)
            self.assertEqual(caught.exception.code, 400)

    def test_web_assets(self) -> None:
        for path in ("/", "/dashboard.js", "/dashboard.css"):
            with urlopen(f"http://127.0.0.1:{self.web.server_port}{path}", timeout=2) as response:
                self.assertEqual(response.status, 200)
                body = response.read()
                self.assertGreater(len(body), 1000)
                self.assertNotIn(b"TFTLCD", body)


class PersistenceTests(unittest.TestCase):
    def test_transient_database_failure_retries_without_blocking_collection(self) -> None:
        class Repository:
            def __init__(self) -> None:
                self.calls = 0
                self.saved = []

            def recent(self, limit: int) -> list:
                return []

            def save(self, sample: object) -> None:
                self.calls += 1
                if self.calls == 1:
                    raise OSError("temporary database outage")
                self.saved.append(sample)

            def close(self) -> None:
                pass

        repository = Repository()
        store = SampleStore(repository)
        try:
            for sequence in (1, 2):
                wire = parse_frame(f"DATA,1,{sequence},1000,2500,6000,2048,812,27,15,0")
                store.add(wire, "a" * 32, datetime.now(timezone.utc))
            deadline = time.monotonic() + 3
            while store.status()["saved"] < 2 and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertEqual([sample.sequence for sample in repository.saved], [1, 2])
            self.assertEqual(store.status()["pending"], 0)
            self.assertFalse(store.status()["database_error"])
        finally:
            store.close()

    @unittest.skipUnless(os.environ.get("MYSQL_TEST_PORT"), "未指定隔离 MySQL 测试实例")
    def test_real_mysql_round_trip_nulls_filter_and_pagination(self) -> None:
        repository = MySQLRepository({"host": "127.0.0.1", "port": int(os.environ["MYSQL_TEST_PORT"]),
                                      "user": "root", "password": "", "database": "stm32_lab5_test"})
        store = SampleStore(repository)
        session = "test" + str(time.time_ns()).ljust(28, "0")
        try:
            for board in (1, 2, 3):
                wire = parse_frame(f"DATA,{board},1,1000,-25,6000,2048,812,27,{15 if board == 1 else 7},0")
                store.add(wire, session, datetime.now(timezone.utc))
            deadline = time.monotonic() + 4
            while store.status()["saved"] < 3 and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertEqual(store.status()["saved"], 3)
            samples, total = repository.history(None, 1, 40)
            self.assertGreaterEqual(total, 3)
            self.assertEqual(samples[-1].board_id, 3)
            self.assertIsNone(samples[-1].temperature)
            self.assertEqual(samples[-1].light_adc, 2048)
            repository.save(samples[-1])
            filtered, filtered_total = repository.history(1, 1, 40)
            self.assertGreaterEqual(filtered_total, 1)
            self.assertTrue(all(sample.board_id == 1 for sample in filtered))
        finally:
            store.close()


if __name__ == "__main__":
    unittest.main()
