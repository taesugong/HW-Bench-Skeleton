/*
 * ===========================================================================
 * ESP32 펌웨어 스켈레톤 - HW Bench Monitor용 (Arduino 프레임워크)
 * ===========================================================================
 * Nucleo와 동일한 프로토콜을 그대로 씁니다. 즉, server.py 쪽 코드는 수정 없이
 * Nucleo 대신(또는 동시에) ESP32를 꽂아도 그대로 동작합니다.
 *
 *   보드 -> PC : "DATA:<channel>:<value>:<board_ts_ms>\n"
 *   PC -> 보드 : "CMD:<name>:<value>\n"
 *
 * 지금은 USB Serial 기준. 나중에 WiFi로 옮길 때는
 *   - Serial.print(...) 대신 client.print(...) (TCP/WebSocket)
 *   - Serial.available()/readStringUntil 대신 소켓 read
 * 로 바꾸기만 하면 프로토콜/로직은 그대로 재사용 가능합니다.
 *
 * 사용법: Arduino IDE에서 보드를 ESP32 Dev Module로 선택 후 업로드.
 * ===========================================================================
 */

const int ADC_PIN = 34;          // ESP32 ADC1 입력 가능 핀 중 하나
const int LED_PIN = 2;           // 보통 내장 LED

unsigned long lastSendMs = 0;
const unsigned long SEND_INTERVAL_MS = 50;  // 20Hz

String rxBuffer = "";

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  analogReadResolution(12); // 0-4095 범위로 Nucleo와 스케일 맞춤
}

void loop() {
  // 1) 명령 수신 처리 (한 줄씩)
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      handleCommand(rxBuffer);
      rxBuffer = "";
    } else if (c != '\r') {
      rxBuffer += c;
    }
  }

  // 2) 주기적으로 ADC 값 송신
  unsigned long now = millis();
  if (now - lastSendMs >= SEND_INTERVAL_MS) {
    lastSendMs = now;
    sendAdcData();
  }
}

void handleCommand(String line) {
  if (!line.startsWith("CMD:")) return;

  String rest = line.substring(4); // "<name>:<value>"
  int sep = rest.indexOf(':');
  if (sep < 0) return;

  String name = rest.substring(0, sep);
  String value = rest.substring(sep + 1);

  if (name == "GPIO") {
    if (value == "LED") {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }
  }
  else if (name == "FREQ") {
    int freq = value.toInt();
    // TODO: PWM(ledc) 주파수 설정 등 필요한 로직 구현
    (void)freq;
  }
  // 새 명령은 여기 else if로 계속 추가
}

void sendAdcData() {
  int value = analogRead(ADC_PIN);
  unsigned long ts = millis();
  Serial.printf("DATA:0:%d:%lu\n", value, ts);
}
