from __future__ import annotations

import argparse
import math
import socket
import threading
import time

class BoardSimulator:
    def __init__(self, board_id: int, host: str, port: int, duration: float,
                 sample_seconds: float, sharing_period: float) -> None:
        self.board_id = board_id
        self.host, self.port = host, port
        self.duration, self.sample_seconds = duration, sample_seconds
        self.sharing_period = sharing_period
        self.sequence = 0
        self.sharing = board_id == 1
        self.stop = threading.Event()

    def samples(self, elapsed: float) -> tuple[int, int, int, int, int]:
        board = self.board_id
        temperature = round(24.0 + board * 2.0 + 2.2 * math.sin(elapsed / 8.0 + board))
        humidity = round(52.0 + board * 4.0 + 5.0 * math.cos(elapsed / 10.0 + board * 0.5))
        light = round(2048.0 + 1500.0 * math.sin(elapsed / 7.0 + board * 1.2))
        co2 = round(760.0 + board * 120.0 + 260.0 * math.sin(elapsed / 11.0 + board))
        hcho = round(3.0 + board * 1.2 + 2.0 * abs(math.sin(elapsed / 9.0)))
        return (temperature * 100, humidity * 100, max(0, min(4095, light)),
                max(400, min(2000, co2)), max(0, min(100, hcho)))

    def run(self) -> None:
        with socket.create_connection((self.host, self.port), timeout=3) as connection:
            connection.settimeout(0.05)
            started = time.monotonic()
            connection.sendall(f"HELLO,{self.board_id},0\r\n".encode("ascii"))
            next_sample = 0.0
            while not self.stop.is_set() and time.monotonic() - started < self.duration:
                elapsed = time.monotonic() - started
                if self.board_id == 1:
                    enabled = self.sharing_period <= 0 or elapsed % self.sharing_period < self.sharing_period * 0.65
                    if enabled != self.sharing:
                        self.sharing = enabled
                        connection.sendall(f"CTRL,1,{int(enabled)}\r\n".encode("ascii"))
                if elapsed >= next_sample:
                    self.sequence += 1
                    temperature, humidity, light, co2, hcho = self.samples(elapsed)
                    frame = (f"DATA,{self.board_id},{self.sequence},{round(elapsed * 1000)},"
                             f"{temperature},{humidity},{light},{co2},{hcho},15,{int(self.sharing)}\r\n")
                    connection.sendall(frame.encode("ascii"))
                    next_sample = elapsed + self.sample_seconds
                try:
                    if not connection.recv(512):
                        break
                except socket.timeout:
                    pass


def run_board(board_id: int, host: str, port: int, duration: float,
              sample_seconds: float, sharing_period: float, errors: list[str]) -> None:
    try:
        BoardSimulator(board_id, host, port, duration, sample_seconds, sharing_period).run()
    except OSError as exc:
        errors.append(f"板 {board_id}: {exc}")


def main() -> None:
    parser = argparse.ArgumentParser(description="模拟三块 STM32 开发板通过 WiFi 上报 V2 数据")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5107)
    parser.add_argument("--duration", type=float, default=60.0)
    parser.add_argument("--interval", type=float, default=2.0, help="每块板的采样间隔（秒）")
    parser.add_argument("--sharing-period", type=float, default=20.0,
                        help="板 1 共享开关切换周期（秒），0 表示始终共享")
    args = parser.parse_args()
    if args.duration <= 0 or args.interval <= 0 or args.sharing_period < 0:
        parser.error("duration/interval 必须大于 0，sharing-period 不能小于 0")
    errors: list[str] = []
    threads = [threading.Thread(target=run_board,
                                args=(board, args.host, args.port, args.duration,
                                      args.interval, args.sharing_period, errors),
                                name=f"board-{board}")
               for board in (1, 2, 3)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()
    if errors:
        raise SystemExit("\n".join(errors))


if __name__ == "__main__":
    main()
