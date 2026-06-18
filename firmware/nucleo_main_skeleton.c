/*
 * ===========================================================================
 * Nucleo 펌웨어 스켈레톤 - HW Bench Monitor용
 * ===========================================================================
 * 대상: STM32G4 / STM32F4 계열 Nucleo (HAL 라이브러리 기준)
 * 통신: USB Serial (Virtual COM Port, ST-LINK 경유) 또는 USART2 직결
 *
 * 프로토콜 (PC server.py와 짝):
 *   보드 -> PC : "DATA:<channel>:<value>:<board_ts_ms>\n"
 *   PC -> 보드 : "CMD:<name>:<value>\n"
 *
 * 사용법:
 *   1) STM32CubeIDE에서 새 프로젝트 생성, ADC1(channel 0번 핀, 예: PA0)과
 *      USART2(ST-LINK VCP로 보통 자동 연결됨)를 CubeMX에서 활성화.
 *   2) 아래 코드를 main.c의 해당 섹션에 붙여넣기 (USER CODE BEGIN/END 사이).
 *   3) HAL_UART_Receive_IT로 수신 인터럽트 등록, main loop에서 ADC 주기 송신.
 *
 * 이 파일은 CubeMX가 자동 생성하는 보일러플레이트(클럭 설정 등)는 생략하고
 * "USER CODE" 영역에 들어갈 핵심 로직만 담았습니다.
 * ===========================================================================
 */

/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
/* USER CODE END Includes */

/* USER CODE BEGIN PV (Private variables) */
#define RX_BUF_SIZE 64
uint8_t rxByte;                 // 인터럽트로 한 바이트씩 받음
char rxLine[RX_BUF_SIZE];       // 줄 단위로 모으는 버퍼
uint8_t rxIndex = 0;
volatile uint8_t lineReady = 0; // '\n' 받으면 1로 세팅

uint32_t lastSendTick = 0;
#define SEND_INTERVAL_MS 50     // 20Hz로 ADC 값 전송 (필요시 조정)

uint16_t dacTargetValue = 2048; // CMD:DAC:<value> 로 갱신됨
/* USER CODE END PV */


/* USER CODE BEGIN 0 (함수 정의 영역) */

/**
 * UART 수신 인터럽트 콜백.
 * CubeMX에서 USART2 Global Interrupt 활성화 + 아래처럼 콜백 등록 필요.
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART2)
    {
        if (rxByte == '\n' || rxIndex >= RX_BUF_SIZE - 1)
        {
            rxLine[rxIndex] = '\0';
            lineReady = 1;
            rxIndex = 0;
        }
        else if (rxByte != '\r')
        {
            rxLine[rxIndex++] = rxByte;
        }
        HAL_UART_Receive_IT(&huart2, &rxByte, 1); // 다음 바이트 다시 대기
    }
}

/**
 * "CMD:<name>:<value>" 형태의 줄을 파싱해서 분기 처리.
 * 새 명령 추가하려면 여기에 else-if만 추가하면 됨.
 */
void handleCommand(char *line)
{
    // 형식 체크
    if (strncmp(line, "CMD:", 4) != 0) return;

    char *name = line + 4;
    char *colon = strchr(name, ':');
    if (!colon) return;
    *colon = '\0';
    char *value = colon + 1;

    if (strcmp(name, "DAC") == 0)
    {
        int v = atoi(value);
        if (v < 0) v = 0;
        if (v > 4095) v = 4095;
        dacTargetValue = (uint16_t)v;
        HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, dacTargetValue);
        // DAC 미사용 보드라면 위 두 줄은 지우고 dacTargetValue만 저장해도 됨
    }
    else if (strcmp(name, "GPIO") == 0)
    {
        // 예시: 핀 이름 매칭은 보드에 맞게 직접 구현
        // 여기서는 PA5(보통 사용자 LED)만 토글하는 예시
        if (strcmp(value, "PA5") == 0)
        {
            HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
        }
    }
    else if (strcmp(name, "FREQ") == 0)
    {
        // 예: PWM 주파수 변경 등 - 필요한 로직으로 채우면 됨
        int freq = atoi(value);
        (void)freq; // TODO: 타이머 ARR 재계산해서 적용
    }
    // 새로운 명령은 여기에 else if로 계속 추가
}

/**
 * ADC 채널 0 값을 읽어서 "DATA:0:<value>:<timestamp>\n" 형태로 전송.
 */
void sendAdcData(void)
{
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    uint32_t adcValue = HAL_ADC_GetValue(&hadc1);
    HAL_ADC_Stop(&hadc1);

    char buf[64];
    uint32_t now = HAL_GetTick(); // 부팅 후 ms 단위 타임스탬프
    int len = snprintf(buf, sizeof(buf), "DATA:0:%lu:%lu\n",
                        (unsigned long)adcValue, (unsigned long)now);
    HAL_UART_Transmit(&huart2, (uint8_t *)buf, len, 10);
}

/* USER CODE END 0 */


/* ===========================================================================
 * main() 함수 내부에서 추가해야 할 부분
 * ===========================================================================
 */

/* USER CODE BEGIN 2 (초기화 직후, while 루프 진입 직전) */
HAL_UART_Receive_IT(&huart2, &rxByte, 1); // 수신 인터럽트 최초 1회 등록
/* USER CODE END 2 */


/* USER CODE BEGIN WHILE (메인 루프 안) */
while (1)
{
    // 1) 명령이 도착했으면 처리
    if (lineReady)
    {
        handleCommand(rxLine);
        lineReady = 0;
    }

    // 2) 주기적으로 ADC 값 송신 (busy-wait 대신 타이머 인터럽트로 바꾸면 더 정확함)
    uint32_t now = HAL_GetTick();
    if (now - lastSendTick >= SEND_INTERVAL_MS)
    {
        lastSendTick = now;
        sendAdcData();
    }

    /* USER CODE END WHILE */
    /* USER CODE BEGIN 3 */
}
/* USER CODE END 3 */
