# HW Bench Monitor - 코드 스켈레톤

하드웨어 개발용 데이터 로깅 / 테스트 신호 생성 웹 콘솔의 최소 구현입니다.
"Nucleo ADC 1채널 → PC 서버 → 웹 브라우저 실시간 그래프" 한 바퀴를 끝까지 돌리는 것이 목표입니다.

## 구조

```
hw-bench-skeleton/
├── backend/
│   └── server.py              # PC에서 실행할 FastAPI 서버 (Serial ↔ WebSocket 브릿지)
├── firmware/
│   ├── nucleo_main_skeleton.c # STM32CubeIDE main.c에 붙여넣을 핵심 로직
│   └── esp32_main_skeleton.ino# Arduino IDE용 ESP32 스케치 (동일 프로토콜)
└── web/
    └── index.html              # 브라우저 UI (실시간 차트 + 명령 패널)
```

## 통신 프로토콜 (단순 텍스트 라인 기반)

```
보드 -> PC : DATA:<channel>:<value>:<board_timestamp_ms>\n
PC -> 보드 : CMD:<name>:<value>\n
```

예시: `DATA:0:2345:102938` (채널 0, ADC값 2345, 보드 기준 102938ms)
예시: `CMD:DAC:1500` (DAC 채널에 1500 출력 요청)

텍스트 기반으로 한 이유는 터미널에서 시리얼 모니터로 직접 찍어봐도 바로 읽히기 때문입니다.
나중에 처리량이 부족하면 바이너리 프로토콜로 바꾸면 되지만, 처음엔 디버깅 편의가 우선입니다.

## 실행 순서

### 1. 보드 펌웨어 굽기

**Nucleo (STM32CubeIDE)**
1. CubeMX에서 새 프로젝트: ADC1 (예: PA0, IN0), USART2 (보통 ST-LINK VCP로 기본 연결됨) 활성화.
   DAC를 쓸 거면 DAC1 OUT1 (PA4)도 활성화.
2. USART2 Global Interrupt를 NVIC에서 Enable.
3. `nucleo_main_skeleton.c`의 내용을 CubeMX가 생성한 `main.c`의 해당
   `USER CODE BEGIN/END` 구역에 맞춰 옮겨 넣기 (CubeMX 재생성 시 사라지지 않게).
4. 빌드 후 보드에 플래시.

**ESP32 (Arduino IDE)**
1. 보드 매니저에서 ESP32 추가 (이미 스마트팜 프로젝트로 해보셨으니 익숙하실 거예요).
2. `esp32_main_skeleton.ino`를 그대로 열어서 업로드.

### 2. PC 백엔드 실행

```bash
pip install -r backend/requirements.txt
python backend/server.py
```

콘솔에 `[연결됨] COM5 @ 115200bps` 같은 메시지가 뜨면 보드 인식 성공.
포트를 못 찾으면 `server.py` 상단의 `SERIAL_PORT = "COM5"` (또는 `/dev/ttyACM0`)로 직접 지정하세요.

### 3. 웹 UI 열기

`web/index.html`을 브라우저에서 더블클릭으로 그냥 열면 됩니다 (별도 웹서버 불필요,
파일을 직접 열어도 JS가 `localhost:8000`의 백엔드를 호출하는 구조라 동작함).

연결되면 차트가 실시간으로 움직이기 시작합니다. 우측 패널에서 DAC 값을 바꾸거나
GPIO를 토글하면 즉시 보드에 명령이 전달됩니다.

## 확장 방향 (이 스켈레톤이 끝난 다음)

- **다채널**: `DATA:<channel>:...`의 channel 값을 0,1,2...로 늘리고, 웹 쪽 `chart.data.datasets`를
  채널별로 추가하면 됩니다. server.py 로직은 그대로 둬도 됩니다.
- **임의 파형 생성**: DAC에 사인파/램프 등을 만들려면 보드 쪽에 타이머 인터럽트로 룩업테이블을
  순회하는 로직을 추가하고, `CMD:WAVE:sine:1000`처럼 파형 종류+주파수를 같이 보내는 식으로
  프로토콜만 확장하면 됩니다.
- **ESP32 WiFi 전환**: `esp32_main_skeleton.ino`의 Serial 통신 부분만 `WiFiClient`/`WebSocketsServer`
  로 바꾸면 되고, server.py 쪽에는 SerialBridge와 비슷한 `WiFiBridge` 클래스를 하나 더 추가해서
  같은 `manager.broadcast()`로 흘려보내면 됩니다.
- **Raspberry Pi 상시 로깅**: 지금 PC에서 돌리는 server.py를 그대로 Pi에 옮겨서 24/7 띄워두고,
  PC 브라우저는 Pi의 IP로 접속하기만 하면 됩니다 (이미 Python/Linux 코드라 포팅 거의 불필요).
