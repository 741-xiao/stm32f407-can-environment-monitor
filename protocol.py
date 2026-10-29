from __future__ import annotations

from dataclasses import dataclass
import re


@dataclass(frozen=True)
class WireSample:
    board_id: int
    sequence: int
    uptime_ms: int
    temperature_centi: int
    humidity_centi: int
    light_adc: int
    co2_ppm: int
    formaldehyde_mg_m3_centi: int
    valid: int
    sharing: bool


def integer(value: str, minimum: int, maximum: int) -> int:
    if not re.fullmatch(r"-?[0-9]{1,10}", value):
        raise ValueError("字段必须为十进制整数")
    result = int(value)
    if not minimum <= result <= maximum:
        raise ValueError("字段超出范围")
    return result


def parse_frame(line: str) -> WireSample:
    fields = line.strip().split(",")
    # V2 carries CO2 and HCHO. V1 remains readable so existing MySQL rows and
    # older firmware builds can be inspected without a schema rewrite.
    if fields[0] != "DATA" or len(fields) not in (9, 11):
        raise ValueError("数据帧格式错误")
    is_v2 = len(fields) == 11
    return WireSample(
        board_id=integer(fields[1], 1, 3),
        sequence=integer(fields[2], 0, 0xFFFFFFFF),
        uptime_ms=integer(fields[3], 0, 0xFFFFFFFF),
        temperature_centi=integer(fields[4], -4000, 12500),
        humidity_centi=integer(fields[5], 0, 10000),
        light_adc=integer(fields[6], 0, 4095),
        co2_ppm=integer(fields[7], 0, 5000) if is_v2 else 0,
        formaldehyde_mg_m3_centi=integer(fields[8], 0, 1000) if is_v2 else 0,
        valid=integer(fields[9] if is_v2 else fields[7], 0, 15),
        sharing=bool(integer(fields[10] if is_v2 else fields[8], 0, 1)),
    )


def light_percent(adc: int, dark: int = 4095, bright: int = 0) -> float:
    if dark == bright:
        raise ValueError("光敏标定值不能相同")
    return round(max(0.0, min(100.0, (adc - dark) * 100.0 / (bright - dark))), 2)
