#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OTA 固件发送工具（配合 STM32F103 Bootloader / APP 接收端使用）

帧格式（固定 268 字节）：
    0x55 0x44 | 总包数(1B) | 当前包号(1B) | 实际长度(2B,小端) | CRC32(4B,小端)
              | 数据区(256B,不足补 0x00) | 0x33 0x22

CRC32 采用 STM32 硬件 CRC 等价算法（CRC-32/MPEG-2，字序模式）：
    多项式 0x04C11DB7，初值 0xFFFFFFFF，不反射，无最终异或。
    关键：STM32 硬件 CRC 是「按 32 位字、字内 MSB 先出」，并按小端从内存读字，
    因此每个 4 字节组的字节处理顺序与标准逐字节 CRC 正好相反。
    本脚本按「字」计算，保证与片上 calculate_crc32_ex() / calculate_crc32() 结果一致。

结束帧：current_packet = 0xFF
    data[0:4] = 固件总长度（小端）
    data[4:8] = 整包 CRC32（小端）
    data_len  = 8

依赖：pyserial          pip install pyserial
用法：
    python ota_send.py firmware.bin
    python ota_send.py firmware.hex -p COM4 -b 115200 -d 0.05
    python ota_send.py firmware.bin --dry-run
"""

import argparse
import struct
import sys
import time

# ------------------------- 帧常量（与 ota_receive.h / uart.h 保持一致） -------------------------
FRAME_HEADER_1 = 0x55
FRAME_HEADER_2 = 0x44
FRAME_TAIL_1   = 0x33
FRAME_TAIL_2   = 0x22
DATA_FRAME_LEN = 256
PACKET_END     = 0xFF
FRAME_TOTAL_LEN = 2 + 1 + 1 + 2 + 4 + DATA_FRAME_LEN + 2   # 268

MAX_PACKETS = 255          # 总包数 1 字节


# ------------------------- CRC32（与 STM32 硬件字节序一致） -------------------------
def crc32_mpeg2(data: bytes, crc: int = 0xFFFFFFFF) -> int:
    """标准 CRC-32/MPEG-2（逐字节 MSB-first）。仅作自检基准，不用于与硬件对齐。"""
    for b in data:
        crc ^= (b << 24) & 0xFFFFFFFF
        for _ in range(8):
            if crc & 0x80000000:
                crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF
            else:
                crc = (crc << 1) & 0xFFFFFFFF
    return crc


def crc32_stm32_words(words, crc: int = 0xFFFFFFFF) -> int:
    """STM32 硬件 CRC：逐 32 位字送入，字内 MSB 先出。

    硬件按小端从内存读出字节拼成 32 位字，所以等价于
    「先把每 4 字节倒序、再做逐字节 MSB-first 的 CRC-32/MPEG-2」。
    这与片上 calculate_crc32_ex() / calculate_crc32() 的 CRC->DR = word 写法一致。
    """
    for w in words:
        crc ^= (w & 0xFFFFFFFF)
        for _ in range(32):
            if crc & 0x80000000:
                crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF
            else:
                crc = (crc << 1) & 0xFFFFFFFF
    return crc


def bytes_to_words(data: bytes, pad_byte: int = 0x00):
    """把字节流按小端拼成 32 位字列表；不足 4 字节的尾巴用 pad_byte 补齐。"""
    words = []
    for i in range(0, len(data), 4):
        chunk = data[i:i + 4]
        if len(chunk) < 4:
            chunk = chunk + bytes([pad_byte]) * (4 - len(chunk))
        words.append(int.from_bytes(chunk, "little"))
    return words


def frame_crc(total: int, current: int, length: int, payload: bytes) -> int:
    """
    帧内 CRC32，严格复刻接收端 calculate_crc32_ex()：
      1) 4 字节头组成 1 个 32 位字：total | current<<8 | len<<16
      2) 实际数据按 32 位字送入（长度 len）
      3) 若 len 不是 4 的倍数，尾部用 0xFF 补齐
    """
    words = [(total & 0xFF) | ((current & 0xFF) << 8) | ((length & 0xFFFF) << 16)]
    words += bytes_to_words(payload[:length], pad_byte=0xFF)
    return crc32_stm32_words(words)


def firmware_crc(fw: bytes) -> int:
    """整包 CRC32：复刻 Bootloader 的 calculate_crc32(ota_data, size)（要求 fw 已 4 字节对齐）。"""
    return crc32_stm32_words(bytes_to_words(fw))


def selftest_crc() -> None:
    """自检：1) 标准 CRC-32/MPEG-2 测试向量；2) 字序实现与参考实现一致。"""
    expect = 0x0376E6E7           # CRC-32/MPEG-2 of b"123456789"
    if crc32_mpeg2(b"123456789") != expect:
        raise SystemExit("CRC-32/MPEG-2 自检失败")

    data = bytes(range(24))       # 每 4 字节反转后做逐字节 CRC，作为参考值
    ref = crc32_mpeg2(b"".join(data[i:i + 4][::-1] for i in range(0, len(data), 4)))
    got = crc32_stm32_words(bytes_to_words(data))
    if got != ref:
        raise SystemExit(f"STM32 字序 CRC 自检失败：期望 0x{ref:08X}，实际 0x{got:08X}")


# ------------------------- 固件加载 -------------------------
def load_hex(path: str) -> bytes:
    """解析 Intel HEX，返回从最低地址开始的连续镜像（空洞补 0xFF）。"""
    mem = {}
    base = 0
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        for lineno, raw in enumerate(f, 1):
            line = raw.strip()
            if not line or not line.startswith(":"):
                continue
            try:
                rec = bytes.fromhex(line[1:])
            except ValueError:
                raise SystemExit(f"HEX 第 {lineno} 行格式错误：{line}")

            count = rec[0]
            addr  = (rec[1] << 8) | rec[2]
            rtype = rec[3]
            data  = rec[4:4 + count]

            if rtype == 0x00:                      # 数据
                for i, b in enumerate(data):
                    mem[base + addr + i] = b
            elif rtype == 0x01:                    # 结束
                break
            elif rtype == 0x02:                    # 扩展段地址
                base = int.from_bytes(data, "big") << 4
            elif rtype == 0x04:                    # 扩展线性地址
                base = int.from_bytes(data, "big") << 16
            # 其它类型（如起始地址 0x03/0x05）忽略

    if not mem:
        raise SystemExit("HEX 文件中没有数据记录")

    lo, hi = min(mem), max(mem)
    buf = bytearray(b"\xFF" * (hi - lo + 1))
    for a, b in mem.items():
        buf[a - lo] = b
    print(f"[HEX] 镜像地址范围 0x{lo:08X} ~ 0x{hi:08X}，长度 {hi - lo + 1} 字节")
    return bytes(buf)


def load_firmware(path: str) -> bytes:
    if path.lower().endswith(".hex"):
        return load_hex(path)
    with open(path, "rb") as f:
        data = f.read()
    if not data:
        raise SystemExit("固件文件为空")
    return data


# ------------------------- 打包 -------------------------
def build_frame(total: int, current: int, payload: bytes, real_len: int) -> bytes:
    """按帧格式组一帧（数据区固定 256 字节，不足补 0x00）。实际长度字段 2 字节小端。"""
    assert len(payload) <= DATA_FRAME_LEN
    assert 0 < real_len <= DATA_FRAME_LEN
    area = payload + b"\x00" * (DATA_FRAME_LEN - len(payload))
    crc = frame_crc(total, current, real_len, payload)
    return bytes([FRAME_HEADER_1, FRAME_HEADER_2, total, current]) \
        + struct.pack("<H", real_len) \
        + struct.pack("<I", crc) \
        + area \
        + bytes([FRAME_TAIL_1, FRAME_TAIL_2])


def build_all_frames(fw: bytes):
    """返回 (frames, 信息字典)。"""
    # 固件总长补齐到 4 字节倍数（Bootloader 按 size/4 个字算整包 CRC）
    pad = (-len(fw)) % 4
    if pad:
        print(f"[补齐] 固件长度 {len(fw)} 不是 4 的倍数，补 {pad} 个 0xFF")
        fw = fw + b"\xFF" * pad

    size = len(fw)
    total = (size + DATA_FRAME_LEN - 1) // DATA_FRAME_LEN
    if total == 0:
        raise SystemExit("固件长度为 0")
    if total > MAX_PACKETS:
        raise SystemExit(f"包数 {total} 超过上限 {MAX_PACKETS}（固件最大 {MAX_PACKETS * DATA_FRAME_LEN} 字节）")

    whole_crc = firmware_crc(fw)

    frames = []
    for i in range(total):
        chunk = fw[i * DATA_FRAME_LEN:(i + 1) * DATA_FRAME_LEN]
        # 除最后一帧外必须写满 256，否则 Flash 中间会出现空洞
        assert len(chunk) == DATA_FRAME_LEN or i == total - 1
        frames.append(build_frame(total, i, chunk, len(chunk)))

    # 结束帧
    end_payload = struct.pack("<I", size) + struct.pack("<I", whole_crc)
    frames.append(build_frame(total, PACKET_END, end_payload, len(end_payload)))

    info = {
        "size": size,
        "packets": total,
        "whole_crc": whole_crc,
        "frame_len": FRAME_TOTAL_LEN,
    }
    return frames, info


# ------------------------- 发送 -------------------------
def send(port: str, baud: int, frames, info, delay: float, listen: float):
    try:
        import serial
    except ImportError:
        raise SystemExit("未安装 pyserial，请先执行：pip install pyserial")

    print(f"固件总长 : {info['size']} 字节")
    print(f"数据帧数 : {info['packets']}（不含结束帧）")
    print(f"整包 CRC : 0x{info['whole_crc']:08X}")
    print(f"单帧长度 : {info['frame_len']} 字节，共发送 {len(frames)} 帧（含结束帧）")
    print(f"串口     : {port} @ {baud}，帧间隔 {delay:.3f}s\n")

    with serial.Serial(port, baud, timeout=0.1) as ser:
        ser.reset_input_buffer()
        t0 = time.time()
        for idx, fr in enumerate(frames):
            ser.write(fr)
            ser.flush()
            cur = "END" if fr[3] == PACKET_END else str(fr[3])
            print(f"\r发送帧 {idx + 1}/{len(frames)}  (current_packet={cur})", end="", flush=True)
            if idx != len(frames) - 1:
                time.sleep(delay)
        print("\n全部发送完成，用时 %.1fs" % (time.time() - t0))

        if listen > 0:
            print(f"\n监听板子回传 {listen:.0f} 秒（Ctrl+C 可提前结束）...")
            end = time.time() + listen
            while time.time() < end:
                data = ser.read(4096)
                if data:
                    sys.stdout.write(data.decode("utf-8", errors="replace"))
                    sys.stdout.flush()
    print()


def main():
    p = argparse.ArgumentParser(description="STM32 OTA 固件串口发送工具")
    p.add_argument("firmware", help="待升级固件：.bin 或 Keil 生成的 .hex")
    p.add_argument("-p", "--port", default="COM4", help="串口号（默认 COM4）")
    p.add_argument("-b", "--baud", type=int, default=115200, help="波特率（默认 115200）")
    p.add_argument("-d", "--delay", type=float, default=0.05,
                   help="帧间延时秒（默认 0.05；接收端 StreamBuffer 只有 512B，别设太小）")
    p.add_argument("-l", "--listen", type=float, default=8.0,
                   help="发完后监听板子回传的秒数（默认 8）")
    p.add_argument("--max-size", type=int, default=14336,
                   help="OTA 缓冲区可用大小（默认 14336 = 0x0800C000~0x0800F800）")
    p.add_argument("--dry-run", action="store_true", help="只打包不发送，打印信息")
    args = p.parse_args()

    selftest_crc()

    fw = load_firmware(args.firmware)
    frames, info = build_all_frames(fw)

    if info["size"] > args.max_size:
        print(f"\n[警告] 固件 {info['size']} 字节 > OTA 缓冲区 {args.max_size} 字节，"
              f"继续发送会越界覆盖 config 区 / 冲出 Flash，请先调整 Flash 分区！\n")

    if args.dry_run:
        print(f"固件总长 : {info['size']} 字节")
        print(f"数据帧数 : {info['packets']}")
        print(f"整包 CRC : 0x{info['whole_crc']:08X}")
        print(f"帧总数   : {len(frames)}（含结束帧）")
        print(f"首帧前 16 字节: {frames[0][:16].hex(' ')}")
        print(f"尾帧前 16 字节: {frames[-1][:16].hex(' ')}")
        return

    send(args.port, args.baud, frames, info, args.delay, args.listen)


if __name__ == "__main__":
    main()
