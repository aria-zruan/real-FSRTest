/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include <stdint.h>
#include <stdio.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define UART_FRAME_HEADER_0 0x55U
#define UART_FRAME_HEADER_1 0xAAU
#define UART_FRAME_CMD_ADC_RAW 0x00U
#define UART_FRAME_DATA_LEN 2U
#define UART_FRAME_TIMESTAMP_LEN 4U
#define UART_FRAME_PAYLOAD_LEN (1U + UART_FRAME_DATA_LEN + UART_FRAME_TIMESTAMP_LEN)
#define UART_FRAME_TOTAL_LEN (2U + 1U + 1U + UART_FRAME_PAYLOAD_LEN + 2U)

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

static uint16_t ADC_ReadChannel(uint32_t channel);
static void UART_SendAdcReading(uint8_t address, uint16_t raw_value, uint32_t timestamp_ms);
static void UART_WriteUint16LE(uint8_t *buffer, uint16_t value);
static void UART_WriteUint32LE(uint8_t *buffer, uint32_t value);
static uint16_t UART_CalculateCustomCrc(const uint8_t *buffer, uint8_t length);
static void UART_SendFrameAsHex(const uint8_t *frame, uint8_t frame_length);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC_Init();
  MX_USART1_UART_Init();
  /* USER CODE BEGIN 2 */

  HAL_UART_Transmit(&huart1, (uint8_t *)"adc frame hex\r\n", 15U, HAL_MAX_DELAY);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    uint16_t adc_pa1 = ADC_ReadChannel(ADC_CHANNEL_1);
    uint16_t adc_pa2 = ADC_ReadChannel(ADC_CHANNEL_2);
    uint16_t adc_pa3 = ADC_ReadChannel(ADC_CHANNEL_3);
    uint16_t adc_pa4 = ADC_ReadChannel(ADC_CHANNEL_4);
    uint32_t timestamp_ms = HAL_GetTick();

    UART_SendAdcReading(0x01U, adc_pa1, timestamp_ms);
    UART_SendAdcReading(0x02U, adc_pa2, timestamp_ms);
    UART_SendAdcReading(0x03U, adc_pa3, timestamp_ms);
    UART_SendAdcReading(0x04U, adc_pa4, timestamp_ms);

    HAL_Delay(200);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_HSI14;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSI14State = RCC_HSI14_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.HSI14CalibrationValue = 16;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART1;
  PeriphClkInit.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK1;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

static uint16_t ADC_ReadChannel(uint32_t channel)
{
  ADC_ChannelConfTypeDef sConfig = {0};

  hadc.Instance->CHSELR = 0U;

  sConfig.Channel = channel;
  sConfig.Rank = ADC_RANK_CHANNEL_NUMBER;
  sConfig.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;
  if (HAL_ADC_ConfigChannel(&hadc, &sConfig) != HAL_OK)
  {
    return 0U;
  }

  if (HAL_ADC_Start(&hadc) != HAL_OK)
  {
    return 0U;
  }

  if (HAL_ADC_PollForConversion(&hadc, HAL_MAX_DELAY) != HAL_OK)
  {
    (void)HAL_ADC_Stop(&hadc);
    return 0U;
  }

  uint16_t value = (uint16_t)HAL_ADC_GetValue(&hadc);
  (void)HAL_ADC_Stop(&hadc);
  return value;
}

static void UART_SendAdcReading(uint8_t address, uint16_t raw_value, uint32_t timestamp_ms)
{
  uint8_t frame[UART_FRAME_TOTAL_LEN];
  uint16_t crc;

  frame[0] = UART_FRAME_HEADER_0;
  frame[1] = UART_FRAME_HEADER_1;
  frame[2] = address;
  frame[3] = UART_FRAME_PAYLOAD_LEN;
  frame[4] = UART_FRAME_CMD_ADC_RAW;
  UART_WriteUint16LE(&frame[5], raw_value);
  UART_WriteUint32LE(&frame[7], timestamp_ms);

  crc = UART_CalculateCustomCrc(frame, (uint8_t)(UART_FRAME_TOTAL_LEN - 2U));
  UART_WriteUint16LE(&frame[11], crc);

  UART_SendFrameAsHex(frame, UART_FRAME_TOTAL_LEN);
}

static void UART_WriteUint16LE(uint8_t *buffer, uint16_t value)
{
  buffer[0] = (uint8_t)(value & 0xFFU);
  buffer[1] = (uint8_t)((value >> 8) & 0xFFU);
}

static void UART_WriteUint32LE(uint8_t *buffer, uint32_t value)
{
  buffer[0] = (uint8_t)(value & 0xFFU);
  buffer[1] = (uint8_t)((value >> 8) & 0xFFU);
  buffer[2] = (uint8_t)((value >> 16) & 0xFFU);
  buffer[3] = (uint8_t)((value >> 24) & 0xFFU);
}

static uint16_t UART_CalculateCustomCrc(const uint8_t *buffer, uint8_t length)
{
  uint16_t crc = 0xA5A5U;
  uint8_t index;

  for (index = 0U; index < length; ++index)
  {
    crc = (uint16_t)(crc + buffer[index]);
    crc = (uint16_t)((crc << 1) | (crc >> 15));
    crc ^= (uint16_t)(0x1021U + index);
  }

  return crc;
}

static void UART_SendFrameAsHex(const uint8_t *frame, uint8_t frame_length)
{
  static const char hex_digits[] = "0123456789ABCDEF";
  char msg[(UART_FRAME_TOTAL_LEN * 3U) + 2U];
  uint8_t frame_index;
  uint8_t msg_index = 0U;

  for (frame_index = 0U; frame_index < frame_length; ++frame_index)
  {
    msg[msg_index++] = hex_digits[(frame[frame_index] >> 4) & 0x0FU];
    msg[msg_index++] = hex_digits[frame[frame_index] & 0x0FU];
    msg[msg_index++] = (frame_index == (uint8_t)(frame_length - 1U)) ? '\r' : ' ';
  }

  msg[msg_index++] = '\n';
  HAL_UART_Transmit(&huart1, (uint8_t *)msg, msg_index, HAL_MAX_DELAY);
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
