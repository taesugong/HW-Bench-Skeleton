"""
하드웨어 벤치 모니터 - PC 백엔드 서버
======================================
역할:
  1) Nucleo(또는 ESP32)와 USB Serial로 통신
  2) 들어오는 데이터를 파싱해서 WebSocket으로 브라우저에 실시간 전송
  3) 브라우저에서 오는 명령(REST)을 받아 Serial로 보드에 전달
  4) 원하면 CSV로 로그 저장

실행:
  pip install fastapi uvicorn pyserial
  python server.py

설계 포인트:
  - 보드는 "한 줄짜리 텍스트 프로토콜"만 주고받는다고 가정 (디버깅 쉬움).
    PC -> 보드: "CMD:<name>:<value>\n"          예) CMD:DAC:1500\n
    보드 -> PC: "DATA:<ch>:<value>:<timestamp>\n" 예) DATA:0:2345:102938\n
  - 나중에 채널이 늘어나거나 ESP32(WiFi)가 추가돼도 이 server.py 구조는 그대로 두고
    SerialBridge 옆에 비슷한 클래스 하나만 더 추가하면 됨.
"""

import asyncio
import csv
import time
import json
from contextlib import asynccontextmanager
from datetime import datetime
from pathlib import Path

import serial
import serial.tools.list_ports
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel

# ----------------------------------------------------------------------
# 설정
# ----------------------------------------------------------------------
SERIAL_PORT = None          # None이면 자동 탐색, 안되면 "COM5" 또는 "/dev/ttyACM0" 직접 지정
BAUD_RATE = 115200
LOG_DIR = Path("./logs")
LOG_DIR.mkdir(exist_ok=True)


# ----------------------------------------------------------------------
# Serial 브릿지: 별도 스레드 없이 asyncio로 논블로킹 read
# ----------------------------------------------------------------------
class SerialBridge:
    def __init__(self):
        self.ser: serial.Serial | None = None
        self.connected = False
        self.csv_writer = None
        self.csv_file = None
        self.logging_enabled = False

    def find_port(self) -> str | None:
        """연결된 포트 중 첫 번째를 자동으로 찾아준다. 여러 보드 있으면 직접 지정 권장."""
        ports = list(serial.tools.list_ports.comports())
        for p in ports:
            print(f"  발견된 포트: {p.device} ({p.description})")
        return ports[0].device if ports else None

    def connect(self, port: str | None = None):
        target = port or SERIAL_PORT or self.find_port()
        if not target:
            print("[경고] 사용 가능한 시리얼 포트를 찾지 못했습니다. 보드 연결을 확인하세요.")
            return False
        try:
            self.ser = serial.Serial(target, BAUD_RATE, timeout=0.1)
            self.connected = True
            print(f"[연결됨] {target} @ {BAUD_RATE}bps")
            return True
        except serial.SerialException as e:
            print(f"[에러] 포트 연결 실패: {e}")
            return False

    def send_command(self, name: str, value: str = ""):
        if not self.connected or not self.ser:
            print("[경고] 보드 미연결 상태에서 명령 무시됨")
            return
        line = f"CMD:{name}:{value}\n"
        self.ser.write(line.encode("utf-8"))

    def read_line(self) -> str | None:
        if not self.connected or not self.ser:
            return None
        try:
            if self.ser.in_waiting:
                raw = self.ser.readline()
                return raw.decode("utf-8", errors="ignore").strip()
        except serial.SerialException:
            self.connected = False
        return None

    def parse(self, line: str) -> dict | None:
        """'DATA:<ch>:<value>:<timestamp>' -> dict. 보드 프로토콜 바뀌면 여기만 수정."""
        if not line.startswith("DATA:"):
            return None
        parts = line.split(":")
        if len(parts) != 4:
            return None
        try:
            return {
                "channel": int(parts[1]),
                "value": float(parts[2]),
                "board_ts": int(parts[3]),
                "host_ts": time.time(),
            }
        except ValueError:
            return None

    def start_csv_log(self):
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        path = LOG_DIR / f"log_{ts}.csv"
        self.csv_file = open(path, "w", newline="")
        self.csv_writer = csv.writer(self.csv_file)
        self.csv_writer.writerow(["host_ts", "board_ts", "channel", "value"])
        self.logging_enabled = True
        print(f"[로깅 시작] {path}")
        return str(path)

    def stop_csv_log(self):
        self.logging_enabled = False
        if self.csv_file:
            self.csv_file.close()
            self.csv_file = None
            self.csv_writer = None
        print("[로깅 종료]")

    def log_row(self, data: dict):
        if self.logging_enabled and self.csv_writer:
            self.csv_writer.writerow(
                [data["host_ts"], data["board_ts"], data["channel"], data["value"]]
            )


bridge = SerialBridge()


# ----------------------------------------------------------------------
# WebSocket 연결 관리 (여러 브라우저 탭이 동시에 봐도 되게)
# ----------------------------------------------------------------------
class ConnectionManager:
    def __init__(self):
        self.active: list[WebSocket] = []

    async def connect(self, ws: WebSocket):
        await ws.accept()
        self.active.append(ws)

    def disconnect(self, ws: WebSocket):
        if ws in self.active:
            self.active.remove(ws)

    async def broadcast(self, message: dict):
        dead = []
        for ws in self.active:
            try:
                await ws.send_json(message)
            except Exception:
                dead.append(ws)
        for ws in dead:
            self.disconnect(ws)


manager = ConnectionManager()


# ----------------------------------------------------------------------
# 백그라운드 태스크: Serial -> 파싱 -> WebSocket 브로드캐스트 -> (선택) CSV
# ----------------------------------------------------------------------
async def serial_poll_loop():
    while True:
        line = bridge.read_line()
        if line:
            data = bridge.parse(line)
            if data:
                bridge.log_row(data)
                await manager.broadcast({"type": "data", "payload": data})
        await asyncio.sleep(0.005)  # 너무 빡빡하게 돌지 않도록 살짝 양보


@asynccontextmanager
async def lifespan(app: FastAPI):
    bridge.connect()
    task = asyncio.create_task(serial_poll_loop())
    yield
    task.cancel()
    if bridge.ser:
        bridge.ser.close()


app = FastAPI(lifespan=lifespan)

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],  # 로컬 도구이므로 전체 허용. 외부 노출 시 좁혀야 함.
    allow_methods=["*"],
    allow_headers=["*"],
)


# ----------------------------------------------------------------------
# REST API: 명령 전송 / 로깅 제어 / 상태 조회
# ----------------------------------------------------------------------
class CommandRequest(BaseModel):
    name: str          # 예: "DAC", "FREQ", "GPIO"
    value: str = ""


@app.post("/api/command")
async def send_command(cmd: CommandRequest):
    bridge.send_command(cmd.name, cmd.value)
    return {"ok": True, "sent": f"CMD:{cmd.name}:{cmd.value}"}


@app.post("/api/logging/start")
async def start_logging():
    path = bridge.start_csv_log()
    return {"ok": True, "file": path}


@app.post("/api/logging/stop")
async def stop_logging():
    bridge.stop_csv_log()
    return {"ok": True}


@app.get("/api/status")
async def status():
    return {
        "connected": bridge.connected,
        "logging": bridge.logging_enabled,
        "port": bridge.ser.port if bridge.ser else None,
    }


@app.post("/api/reconnect")
async def reconnect(port: str | None = None):
    ok = bridge.connect(port)
    return {"ok": ok}


# ----------------------------------------------------------------------
# WebSocket: 실시간 데이터 스트림
# ----------------------------------------------------------------------
@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await manager.connect(websocket)
    try:
        while True:
            # 브라우저 쪽에서 ping이나 명령을 보낼 수도 있게 열어둠 (지금은 안 써도 됨)
            await websocket.receive_text()
    except WebSocketDisconnect:
        manager.disconnect(websocket)


if __name__ == "__main__":
    import uvicorn

    uvicorn.run(app, host="0.0.0.0", port=8000)
