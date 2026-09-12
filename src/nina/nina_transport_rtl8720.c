/* All Realtek/FreeRTOS dependencies of the SPI proof live here. */
#include "ameba_soc.h"
#include "rtl8721d_usi_ssi.h"
#include "FreeRTOS.h"
#include "task.h"
#include "nina_transport.h"

#ifdef NINA_TRANSPORT_DEBUG
#define NINA_TRACE(...) DiagPrintf(__VA_ARGS__)
#else
#define NINA_TRACE(...) ((void)0)
#endif

/* Wio Terminal schematic v1.2, sheet 6; see PHASE2.md. */
#define NINA_CLK _PA_30
#define NINA_MOSI _PA_25
#define NINA_MISO _PA_26
#define NINA_CS _PA_28
#define NINA_READY _PA_12
#define NINA_SYNC _PA_13
#define NINA_IRQ_PRIORITY 5

static GDMA_InitTypeDef rx_dma, tx_dma;
static uint8_t *receive_buffer;
static size_t buffer_capacity;
static bool armed, initialized;

static uint32_t dma_interrupt(void *data)
{
    GDMA_InitTypeDef *dma = (GDMA_InitTypeDef *)data;
    GDMA_ClearINT(0, dma->GDMA_ChNum);
    return 0;
}

bool nina_hw_selected(void) { return GPIO_ReadDataBit(NINA_CS) == 0; }
void nina_hw_ready(bool high) { GPIO_WriteBit(NINA_READY, high ? 1 : 0); }

static uint32_t milliseconds(bool isr)
{
    TickType_t ticks = isr ? xTaskGetTickCountFromISR() : xTaskGetTickCount();
    return ticks * portTICK_PERIOD_MS;
}

static void cs_interrupt(void *data, uint32_t event)
{
    (void)data;
    (void)event;
    bool selected = nina_hw_selected();
    nina_transport_cs_edge(selected, milliseconds(true));
}

/* These channels deliberately use one-byte transfers and bursts. The stock
 * stream helper packs aligned buffers into words, hiding short tails from a
 * CS-delimited receiver. DAR + residual USI FIFO gives actual received bytes.
 * RX also measures response clocks: DMA TX completion alone only means FIFO
 * queued, and cannot prove the host has consumed the reply. */
static void configure_dma(GDMA_InitTypeDef *dma, bool receive,
                          const uint8_t *buffer, size_t length)
{
    uint8_t channel = dma->GDMA_ChNum;
    GDMA_StructInit(dma);
    dma->GDMA_Index = 0;
    dma->GDMA_ChNum = channel;
    dma->GDMA_DIR = receive ? TTFCPeriToMem : TTFCMemToPeri;
    dma->GDMA_SrcDataWidth = TrWidthOneByte;
    dma->GDMA_DstDataWidth = TrWidthOneByte;
    dma->GDMA_SrcMsize = MsizeOne;
    dma->GDMA_DstMsize = MsizeOne;
    dma->GDMA_SrcInc = receive ? NoChange : IncType;
    dma->GDMA_DstInc = receive ? IncType : NoChange;
    dma->GDMA_SrcAddr = receive ? (uint32_t)&USI0_DEV->RX_FIFO_READ : (uint32_t)buffer;
    dma->GDMA_DstAddr = receive ? (uint32_t)buffer : (uint32_t)&USI0_DEV->TX_FIFO_WRITE;
    dma->GDMA_SrcHandshakeInterface = GDMA_HANDSHAKE_INTERFACE_USI0_RX;
    dma->GDMA_DstHandshakeInterface = GDMA_HANDSHAKE_INTERFACE_USI0_TX;
    dma->GDMA_BlockSize = length;
    dma->GDMA_IsrType = TransferType | ErrType;
    GDMA_ClearINT(0, channel);
    GDMA_Init(0, channel, dma);
    /* A valid mask is required by the HAL. Disable delivery after init:
     * CS is our completion event, and raw DMA errors are checked at stop. */
    GDMA_INTConfig(0, channel, TransferType | ErrType, DISABLE);
    GDMA_BASE->CH[channel].CFG_LOW &= ~(1u << 8);
    GDMA_Cmd(0, channel, ENABLE);
}

size_t nina_hw_stop(unsigned *faults)
{
    if (!armed) return 0;
    NINA_TRACE("NINA before stop dst=%x rx=%x\n", GDMA_GetDstAddr(0, rx_dma.GDMA_ChNum), USI_SSI_GetRxCount(USI0_DEV));
    USI_SSI_SetDmaEnable(USI0_DEV, DISABLE, (USI_TX_DMA_ENABLE | USI_RX_DMA_ENABLE));
    /* DW AHB DMA CFG_LOW: suspend new source reads, then drain its FIFO.
     * The ROM disable helper clears DAR, so snapshot only after quiescence
     * and before calling it. Byte-wide transfers leave no packed-word tail. */
    GDMA_BASE->CH[rx_dma.GDMA_ChNum].CFG_LOW |= (1u << 8);
    __DSB();
    unsigned spins = 1000;
    while (!(GDMA_BASE->CH[rx_dma.GDMA_ChNum].CFG_LOW & (1u << 9)) && --spins) {}
    if (!spins) *faults |= NINA_HW_ERROR;
    __DSB();
    size_t count = GDMA_GetDstAddr(0, rx_dma.GDMA_ChNum) - (uint32_t)receive_buffer;
    uint32_t dma_errors = GDMA_BASE->RAW_ERR;
    GDMA_Cmd(0, rx_dma.GDMA_ChNum, DISABLE);
    GDMA_Cmd(0, tx_dma.GDMA_ChNum, DISABLE);
    __DSB();

    uint32_t raw = USI_SSI_GetRawIsr(USI0_DEV);
    if (dma_errors & ((1u << rx_dma.GDMA_ChNum) | (1u << tx_dma.GDMA_ChNum)))
        *faults |= NINA_HW_ERROR;
    if (raw & (USI_RXFIFO_OVERFLOW_INTS | USI_TXFIFO_OVERFLOW_INTS)) *faults |= NINA_HW_OVERRUN;
    if (raw & (USI_RXFIFO_UNDERFLOW_INTS | USI_TXFIFO_UNDERFLOW_INTS)) *faults |= NINA_HW_UNDERRUN;
    if (raw & USI_SPI_RX_DATA_FRM_ERR_INTS) *faults |= NINA_HW_ERROR;
    DCache_Invalidate((uint32_t)receive_buffer, (buffer_capacity + 31u) & ~31u);
    NINA_TRACE("NINA received %x bytes: %x %x %x %x raw=%x\n", count, receive_buffer[0], receive_buffer[1], receive_buffer[2], receive_buffer[3], raw);
    /* Bound the tail even if a faulty host continues clocking after CS. */
    size_t tail = USI_SSI_GetRxCount(USI0_DEV);
    if (tail > USI_SPI_RX_FIFO_DEPTH) {
        tail = USI_SPI_RX_FIFO_DEPTH;
        *faults |= NINA_HW_ERROR;
    }
    for (size_t i = 0; i < tail; ++i) {
        uint8_t byte = USI_SSI_ReadData(USI0_DEV);
        if (count < buffer_capacity) receive_buffer[count] = byte;
        ++count;
    }
    USI_SSI_Cmd(USI0_DEV, DISABLE);
    armed = false;
    return count;
}

void nina_hw_reset(void)
{
    unsigned ignored = 0;
    nina_hw_stop(&ignored);
    USI_SSI_Cmd(USI0_DEV, DISABLE);
    RCC_PeriphClockCmd(APBPeriph_USI_REG, APBPeriph_USI_CLOCK, DISABLE);
    RCC_PeriphClockCmd(APBPeriph_USI_REG, APBPeriph_USI_CLOCK, ENABLE);
    USI_SSI_InitTypeDef config;
    USI_SSI_StructInit(&config);
    config.USI_SPI_Role = USI_SPI_SLAVE;
    config.USI_SPI_DataFrameSize = 7; /* register encoding: eight bits minus one */
    config.USI_SPI_SclkPhase = 0;
    config.USI_SPI_SclkPolarity = 0;
    config.USI_SPI_InterruptMask = 0;
    config.USI_SPI_DmaRxDataLevel = 0;
    config.USI_SPI_DmaTxDataLevel = 31;
    USI_SSI_Init(USI0_DEV, &config);
    USI_SSI_SetIsrClean(USI0_DEV, USI_SPI_INTERRUPT_CLEAR_MASK);
}

bool nina_hw_arm(uint8_t *rx, const uint8_t *tx, size_t capacity)
{
    if (capacity != NINA_SPI_BUFFER_SIZE) return false;
    USI_SSI_Cmd(USI0_DEV, DISABLE); /* flush previous FIFO and shifter */
    USI_SSI_SetIsrClean(USI0_DEV, USI_SPI_INTERRUPT_CLEAR_MASK);
    receive_buffer = rx;
    buffer_capacity = capacity;
    DCache_CleanInvalidate((uint32_t)rx, (capacity + 31u) & ~31u);
    DCache_Clean((uint32_t)tx, (capacity + 31u) & ~31u);
    configure_dma(&rx_dma, true, rx, capacity);
    USI_SSI_Cmd(USI0_DEV, ENABLE);
    /* Preload before READY goes low, including the first mode-0 MISO bit.
     * Remaining zero-filled bytes make command RX full-duplex without TX
     * starvation; later response bytes also stream through the same channel. */
    for (size_t i = 0; i < USI_SPI_TX_FIFO_DEPTH; ++i) USI_SSI_WriteData(USI0_DEV, tx[i]);
    configure_dma(&tx_dma, false, tx + USI_SPI_TX_FIFO_DEPTH, capacity - USI_SPI_TX_FIFO_DEPTH);
    USI_SSI_SetDmaEnable(USI0_DEV, ENABLE, (USI_TX_DMA_ENABLE | USI_RX_DMA_ENABLE));
    NINA_TRACE("NINA armed dst=%x want=%x\n", GDMA_GetDstAddr(0, rx_dma.GDMA_ChNum), (uint32_t)rx);
    armed = true;
    __DSB();
    return true;
}

static void transport_task(void *unused)
{
    (void)unused;
    NINA_TRACE("NINA task started\n");
#ifdef NINA_TRANSPORT_DEBUG
    bool first = true;
#endif
    for (;;) {
        /* CS IRQ is priority 5, masked by this short critical section. Phase 2
         * responder just copies literals; no blocking backend work is allowed.
         * DMA continues independently while the task sleeps. */
        taskENTER_CRITICAL();
        nina_transport_poll(milliseconds(false));
        taskEXIT_CRITICAL();
#ifdef NINA_TRANSPORT_DEBUG
        if (first) { NINA_TRACE("NINA polled CS=%x READY=%x\n", GPIO_ReadDataBit(NINA_CS), GPIO_ReadDataBit(NINA_READY)); first = false; }
#endif
        vTaskDelay(1);
    }
}

void nina_transport_get_counters(nina_transport_counters *out)
{
    taskENTER_CRITICAL();
    nina_transport_snapshot(out);
    taskEXIT_CRITICAL();
}

bool nina_transport_start(nina_response_fn respond)
{
    NINA_TRACE("NINA startup\n");
    if (initialized || !respond) return false;
    GPIO_InitTypeDef pin = {0};
    pin.GPIO_Pin = NINA_READY;
    pin.GPIO_Mode = GPIO_Mode_OUT;
    pin.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_WriteBit(NINA_READY, 1); /* output latch high before enabling output */
    GPIO_Init(&pin);
    pin.GPIO_Pin = NINA_SYNC;
    pin.GPIO_Mode = GPIO_Mode_IN;
    GPIO_Init(&pin);
    pin.GPIO_Pin = NINA_CS;
    pin.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(&pin);
    Pinmux_Swdoff();
    RCC_PeriphClockCmd(APBPeriph_GDMA0, APBPeriph_GDMA0_CLOCK, ENABLE);
    Pinmux_Config(NINA_CLK, PINMUX_FUNCTION_SPIS);
    Pinmux_Config(NINA_MOSI, PINMUX_FUNCTION_SPIS);
    Pinmux_Config(NINA_MISO, PINMUX_FUNCTION_SPIS);
    PAD_PullCtrl(NINA_CLK, GPIO_PuPd_DOWN);
    PAD_PullCtrl(NINA_MOSI, GPIO_PuPd_NOPULL);
    PAD_PullCtrl(NINA_MISO, GPIO_PuPd_NOPULL);
    /* USI receives clocks with CS in GPIO mode. Keep GPIO ownership for
     * direct both-edge transaction boundaries; reset USI between frames. */
    rx_dma.GDMA_ChNum = GDMA_ChnlAlloc(0, dma_interrupt, (uint32_t)&rx_dma, NINA_IRQ_PRIORITY);
    NINA_TRACE("NINA RX DMA %x\n", rx_dma.GDMA_ChNum);
    if (rx_dma.GDMA_ChNum == 0xff) return false;
    tx_dma.GDMA_ChNum = GDMA_ChnlAlloc(0, dma_interrupt, (uint32_t)&tx_dma, NINA_IRQ_PRIORITY);
    NINA_TRACE("NINA TX DMA %x\n", tx_dma.GDMA_ChNum);
    if (tx_dma.GDMA_ChNum == 0xff) {
        GDMA_ChnlFree(0, rx_dma.GDMA_ChNum);
        return false;
    }
    nina_transport_init(respond);
    nina_hw_reset();
    NINA_TRACE("NINA USI initialized, CS=%x\n", GPIO_ReadDataBit(NINA_CS));
    GPIO_UserRegIrq(NINA_CS, (void *)cs_interrupt, NULL);
    GPIO_INTMode(NINA_CS, ENABLE, GPIO_INT_Trigger_BOTHEDGE,
                 GPIO_INT_POLARITY_ACTIVE_LOW, GPIO_INT_DEBOUNCE_DISABLE);
    InterruptRegister((IRQ_FUN)GPIO_INTHandler, GPIOA_IRQ, (uint32_t)GPIOA_BASE,
                      NINA_IRQ_PRIORITY);
    InterruptEn(GPIOA_IRQ, NINA_IRQ_PRIORITY);
    GPIO_INTConfig(NINA_CS, ENABLE);
    if (xTaskCreate(transport_task, "nina_spi", 1024, NULL,
                    tskIDLE_PRIORITY + 3, NULL) != pdPASS) {
        GPIO_INTConfig(NINA_CS, DISABLE);
        GDMA_ChnlFree(0, rx_dma.GDMA_ChNum);
        GDMA_ChnlFree(0, tx_dma.GDMA_ChNum);
        return false;
    }
    initialized = true;
    return true;
}
