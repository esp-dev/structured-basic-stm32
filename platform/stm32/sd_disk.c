#include "board.h"
#include "stm32f4xx_hal.h"
#include "diskio.h"
#include <string.h>

static SD_HandleTypeDef sd;
static DSTATUS status = STA_NOINIT;
static unsigned alternate;
static uint32_t sector_buffer[512 / sizeof(uint32_t)];

int sd_alternate(void) { return alternate != 0; }
void sd_reset(void) {
    if (sd.Instance) {
        __HAL_RCC_SDIO_FORCE_RESET(); __HAL_RCC_SDIO_RELEASE_RESET();
    }
    status = STA_NOINIT;
}
void sd_select_alternate(int use_pb15) {
    if ((use_pb15 != 0) == sd_alternate()) return;
    __HAL_RCC_GPIOB_CLK_ENABLE(); __HAL_RCC_GPIOC_CLK_ENABLE();
    if (alternate) HAL_GPIO_DeInit(GPIOB, GPIO_PIN_15);
    else HAL_GPIO_DeInit(GPIOC, GPIO_PIN_12);
    alternate = (unsigned)(use_pb15 != 0); sd_reset();
}
void HAL_SD_MspInit(SD_HandleTypeDef *handle) {
    GPIO_InitTypeDef pins = {0};
    if (handle->Instance != SDIO) return;
    __HAL_RCC_GPIOB_CLK_ENABLE(); __HAL_RCC_GPIOC_CLK_ENABLE(); __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_SDIO_CLK_ENABLE();
    pins.Mode = GPIO_MODE_AF_PP;
    pins.Pull = GPIO_PULLUP;
    pins.Speed = GPIO_SPEED_FREQ_HIGH;
    pins.Alternate = GPIO_AF12_SDIO;
    pins.Pin = GPIO_PIN_8; HAL_GPIO_Init(GPIOC, &pins);
    pins.Pin = GPIO_PIN_2; HAL_GPIO_Init(GPIOD, &pins);
    pins.Pull = GPIO_NOPULL;
    pins.Pin = alternate ? GPIO_PIN_15 : GPIO_PIN_12;
    HAL_GPIO_Init(alternate ? GPIOB : GPIOC, &pins);
}
DSTATUS disk_initialize(BYTE drive) {
    if (drive) return STA_NOINIT;
    if (!status) return status;
    sd_reset();
    memset(&sd, 0, sizeof(sd));
    sd.Instance = SDIO;
    sd.Init.ClockEdge = SDIO_CLOCK_EDGE_RISING;
    sd.Init.ClockBypass = SDIO_CLOCK_BYPASS_DISABLE;
    sd.Init.ClockPowerSave = SDIO_CLOCK_POWER_SAVE_DISABLE;
    sd.Init.BusWide = SDIO_BUS_WIDE_1B;
    sd.Init.HardwareFlowControl = SDIO_HARDWARE_FLOW_CONTROL_DISABLE;
    sd.Init.ClockDiv = 20;
    if (HAL_SD_Init(&sd) == HAL_OK) status = 0;
    return status;
}
DSTATUS disk_status(BYTE drive) { return drive ? STA_NOINIT : status; }
static int ready(void) {
    uint32_t start = HAL_GetTick();
    do {
        HAL_SD_CardStateTypeDef state = HAL_SD_GetCardState(&sd);
        if (state == HAL_SD_CARD_TRANSFER) return 1;
        if (state == HAL_SD_CARD_ERROR) break;
    } while (HAL_GetTick() - start < 600u);
    status = STA_NOINIT; return 0;
}
static int range_ok(DWORD lba, UINT count) {
    return count && lba < sd.SdCard.LogBlockNbr && count <= sd.SdCard.LogBlockNbr - lba;
}
DRESULT disk_read(BYTE drive, BYTE *data, DWORD sector, UINT count) {
    if (drive) return RES_PARERR;
    if (status) return RES_NOTRDY;
    if (!range_ok(sector, count)) return RES_PARERR;
    for (UINT i = 0; i < count; ++i) {
        if (HAL_SD_ReadBlocks(&sd, (uint8_t *)sector_buffer, sector + i, 1, 600) != HAL_OK) {
            status = STA_NOINIT; return RES_ERROR;
        }
        memcpy(data, sector_buffer, sizeof(sector_buffer)); data += sizeof(sector_buffer);
    }
    return RES_OK;
}
DRESULT disk_write(BYTE drive, const BYTE *data, DWORD sector, UINT count) {
    if (drive) return RES_PARERR;
    if (status) return RES_NOTRDY;
    if (!range_ok(sector, count)) return RES_PARERR;
    for (UINT i = 0; i < count; ++i) {
        memcpy(sector_buffer, data, sizeof(sector_buffer)); data += sizeof(sector_buffer);
        if (HAL_SD_WriteBlocks(&sd, (uint8_t *)sector_buffer, sector + i, 1, 600) != HAL_OK || !ready()) {
            status = STA_NOINIT; return RES_ERROR;
        }
    }
    return RES_OK;
}
DRESULT disk_ioctl(BYTE drive, BYTE command, void *result) {
    if (drive) return RES_PARERR;
    if (status) return RES_NOTRDY;
    switch (command) {
    case CTRL_SYNC: return ready() ? RES_OK : RES_ERROR;
    case GET_SECTOR_COUNT: *(DWORD *)result = sd.SdCard.LogBlockNbr; return RES_OK;
    case GET_SECTOR_SIZE: *(WORD *)result = 512; return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD *)result = 1; return RES_OK;
    default: return RES_PARERR;
    }
}
