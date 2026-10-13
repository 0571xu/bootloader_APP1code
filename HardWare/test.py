//配合服务器示例代码
import socket
import time
import requests
from datetime import datetime

# ============ 配置 ============
LISTEN_IP   = "192.168.1.103"
LISTEN_PORT = 7755

AREA_MAP = {
    0x01: "hangzhou",
    0x02: "shanghai",
    0x03: "shenzhen",
}

# 天气聚合枚举
W_SUNNY, W_CLOUD, W_RAIN, W_SNOW, W_FOG, W_UNKNOWN = 1, 2, 3, 4, 5, 0

# ============ BCD ============
def to_bcd(n: int) -> int:
    return ((n // 10) << 4) | (n % 10)

def build_time_bcd(now: datetime) -> bytes:
    """7 字节 BCD：年 月 日 时 分 秒 星期"""
    weekday = now.isoweekday() % 7
    return bytes([
        to_bcd(now.year % 100),
        to_bcd(now.month),
        to_bcd(now.day),
        to_bcd(now.hour),
        to_bcd(now.minute),
        to_bcd(now.second),
        to_bcd(weekday),
    ])

# ============ 天气聚合 ============
def classify_weather(desc: str) -> int:
    s = desc.lower()
    if "snow" in s or "blizzard" in s or "sleet" in s:
        return W_SNOW
    if "rain" in s or "drizzle" in s or "shower" in s or "thunder" in s:
        return W_RAIN
    if "fog" in s or "mist" in s or "haze" in s:
        return W_FOG
    if "cloud" in s or "overcast" in s:
        return W_CLOUD
    if "sunny" in s or "clear" in s:
        return W_SUNNY
    return W_UNKNOWN

def get_weather(city: str):
    """返回 (天气码, 最高温, 最低温, 当前温)"""
    proxies = {"http": None, "https": None}
    try:
        url = f"https://wttr.in/{city}?format=j1"
        r = requests.get(url, timeout=5, proxies=proxies)
        j = r.json()

        cur = j["current_condition"][0]
        desc = cur["weatherDesc"][0]["value"]
        temp_now = int(cur["temp_C"])

        today = j["weather"][0]
        temp_max = int(today["maxtempC"])
        temp_min = int(today["mintempC"])

        code = classify_weather(desc)
        return code, temp_max, temp_min, temp_now
    except Exception as e:
        print("天气获取失败:", e)
        return W_UNKNOWN, -1, -1, -1

# ============ 帧解析 ============
def parse_frame(data: bytes):
    if len(data) != 10:
        return None
    if data[0] != 0x55 or data[1] != 0x44:
        return None
    if data[8] != 0x33 or data[9] != 0x22:
        return None
    return {
        "time_valid": data[3],
        "area_index": data[5],
    }

# ============ 构造 19 字节应答 ============
def build_response(time_valid, area_valid, area_index,
                   weather_code, tmax, tmin, tnow):
    now = datetime.now()
    t_bcd = build_time_bcd(now)

    frame = bytearray(19)
    frame[0]     = 0x55
    frame[1]     = 0x44
    frame[2]     = 0x01
    frame[3]     = time_valid
    frame[4:11]  = t_bcd
    frame[11]    = 0x02
    frame[12]    = area_valid
    frame[13]    = weather_code & 0xFF
    frame[14]    = tmax & 0xFF      # 有符号，负数直接是 0xF6 之类
    frame[15]    = tmin & 0xFF
    frame[16]    = tnow & 0xFF
    frame[17]    = 0x33
    frame[18]    = 0x22
    return bytes(frame)

# ============ 主循环 ============
def main():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((LISTEN_IP, LISTEN_PORT))
    srv.listen(5)

    print(f"服务器已启动，监听 {LISTEN_IP}:{LISTEN_PORT}")

    while True:
        conn, addr = srv.accept()
        print(f"客户端接入: {addr}")

        try:
            buf = b""
            while True:
                data = conn.recv(256)
                if not data:
                    break
                buf += data

                while len(buf) >= 10:
                    frame = buf[:10]
                    buf   = buf[10:]

                    info = parse_frame(frame)
                    if info is None:
                        print("非法帧:", frame.hex(" "))
                        continue

                    print("收到帧:", frame.hex(" "))

                    time_valid = info["time_valid"] & 0x01
                    area_index = info["area_index"]

                    if area_index in AREA_MAP:
                        area_valid = 1
                        city = AREA_MAP[area_index]
                        wcode, tmax, tmin, tnow = get_weather(city)
                        print(f"[天气] {city}: code={wcode}, "
                              f"max={tmax}, min={tmin}, now={tnow}")
                    else:
                        area_valid = 0
                        wcode, tmax, tmin, tnow = W_UNKNOWN, 0, 0, 0

                    resp = build_response(time_valid, area_valid, area_index,
                                          wcode, tmax, tmin, tnow)
                    conn.sendall(resp)
                    print("已回复:", resp.hex(" "))

        except Exception as e:
            print("连接异常:", e)
        finally:
            conn.close()

if __name__ == "__main__":
    main()