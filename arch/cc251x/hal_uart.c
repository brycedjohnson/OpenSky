/*
    Copyright 2017 fishpepper <AT> gmail.com

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http:// www.gnu.org/licenses/>.

   author: fishpepper <AT> gmail.com
*/

#include "cc2510fx.h"
#include "hal_cc25xx.h"
#include "hal_uart.h"
#include "hal_defines.h"
#include "hal_delay.h"
#include "config.h"
#include "hal_dma.h"
#include "uart.h"
#include "debug.h"
#include "wdt.h"
#include "delay.h"
#include "led.h"

#if defined(BUFFER_ENABLE_PIN) && defined(BUFFER_ENABLE_PORT)
#define BUFFER_ENABLE_DIR PORT2DIR(BUFFER_ENABLE_PORT)
#define BUFFER_ENABLE_BIT PORT2BIT(BUFFER_ENABLE_PORT, BUFFER_ENABLE_PIN)
#endif

void hal_uart_init(void) {
    EXTERNAL_MEMORY union hal_uart_config_t sbus_uart_config;

#ifdef BUFFER_ENABLE_DIR
	BUFFER_ENABLE_DIR |= (1 << BUFFER_ENABLE_PIN);
	BUFFER_ENABLE_BIT = 1;
#endif

#if SBUS_UART == USART0_P0
    // -> USART0_P0
    //    use ALT1 -> clear flag -> P0_3 = TX / P0_2 = RX
    PERCFG &= ~(PERCFG_U0CFG);

    // configure pins as peripheral:
    P0SEL |= (1<<3) | (1<<2);

    // make sure all P1 pins switch to normal GPIO
    //P1SEL &= ~(0xF0);

    // make tx pin output:
    P0DIR |= (1<<3);
#elif SBUS_UART == USART0_P1
    // -> USART0_P1
    //    use ALT2 -> Set flag -> P1_5 = TX / P1_4 = RX
    PERCFG |= (PERCFG_U0CFG);

    // configure pins as peripheral:
    P1SEL |= (1<<5) | (1<<4);

    // make sure all P0 pins switch to normal GPIO
    P0SEL &= ~(0x3C);

    // make tx pin output:
    P1DIR |= (1<<5);
#elif SBUS_UART == USART1_P0
    // USART1 use ALT1 -> Clear flag -> Port P0_4 = TX
    PERCFG &= ~(PERCFG_U1CFG);

    // USART1 has priority when USART0 is also enabled
    P2DIR = (P2DIR & 0x3F) | 0b01000000;

    // configure pin P0_4 (TX) and P0_5 (RX) as special function:
    P0SEL |= (1<<4) | (1<<5);

    // make sure all P1 pins switch to normal GPIO
//    P1SEL &= ~(0xF0);

    // make tx pin output:
    P0DIR |= (1<<4);
#elif SBUS_UART == USART1_P1
    // USART1 use ALT2 -> SET flag -> Port P1_6 = TX
    PERCFG |= (PERCFG_U1CFG);

    // USART1 has priority when USART0 is also enabled
    P2DIR = (P2DIR & 0x3F) | 0b01000000;

    // configure pin P1_6 (TX) and P1_7(RX) as special function:
    P1SEL |= (1<<6) | (1<<7);

    // make tx pin output:
    P1DIR |= (1<<6);
#else
    #error "UNSUPPORTED UART"
#endif  // SBUS_UART == ...

    // set baudrate
#if (SBUS_UART == USART0_P1) || (SBUS_UART == USART0_P0)
    U0BAUD = CC2510_BAUD_M_100000;
    U0GCR = (U0GCR & ~0x1F) | (CC2510_BAUD_E_100000);
#else
    U1BAUD = CC2510_BAUD_M_100000;
    U1GCR = (U1GCR & ~0x1F) | (CC2510_BAUD_E_100000);
#endif  // SBUS_UART == ...

    // set up config for USART -> 8E2
    #ifdef SBUS_INVERTED
        // this is a really nice feature of the cc2510:
        // we can invert the idle level of the usart
        // by setting STOP to zero. by inverting
        // the parity, the startbit, and the data
        // by using the SBUS_PREPARE_DATA() macro
        // we can effectively invert the usart in software :)
        sbus_uart_config.bit.START  = 1;  // startbit level = low
        sbus_uart_config.bit.STOP   = 0;  // stopbit level = high
        sbus_uart_config.bit.D9     = 1;  // UNEven parity
    #else
        // standard usart, non-inverted mode
        // NOTE: most sbus implementations use inverted mode
        sbus_uart_config.bit.START  = 0;  // startbit level = low
        sbus_uart_config.bit.STOP   = 1;  // stopbit level = high
        sbus_uart_config.bit.D9     = 0;  // Even parity
    #endif  // SBUS_INVERTED

    sbus_uart_config.bit.SPB    = 1;  // 1 = 2 stopbits
    sbus_uart_config.bit.PARITY = 1;  // 1 = parity enabled, D9=0 -> even parity
    sbus_uart_config.bit.BIT9   = 1;  // 8bit
    sbus_uart_config.bit.FLOW   = 0;  // no hw flow control
    sbus_uart_config.bit.ORDER  = 0;  // lsb first

    // activate uart config
    hal_uart_set_mode(&sbus_uart_config);

    // use dma channel 3 for transmission:
    hal_dma_config[3].PRIORITY       = DMA_PRI_LOW;
    hal_dma_config[3].M8             = DMA_M8_USE_7_BITS;
    hal_dma_config[3].IRQMASK        = DMA_IRQMASK_DISABLE;
#if (SBUS_UART == USART0_P1) || (SBUS_UART == USART0_P0)
    hal_dma_config[3].TRIG           = DMA_TRIG_UTX0;
#else
    hal_dma_config[3].TRIG           = DMA_TRIG_UTX1;
#endif  // SBUS_UART == ...
    hal_dma_config[3].TMODE          = DMA_TMODE_SINGLE;
    hal_dma_config[3].WORDSIZE       = DMA_WORDSIZE_BYTE;

    // source address will be set during tx start
    SET_WORD(hal_dma_config[3].SRCADDRH,  hal_dma_config[3].SRCADDRL,  0);
#if (SBUS_UART == USART0_P1) || (SBUS_UART == USART0_P0)
    SET_WORD(hal_dma_config[3].DESTADDRH, hal_dma_config[3].DESTADDRL, &X_U0DBUF);
#else
    SET_WORD(hal_dma_config[3].DESTADDRH, hal_dma_config[3].DESTADDRL, &X_U1DBUF);
#endif  // SBUS_UART == ...

    hal_dma_config[3].VLEN           = DMA_VLEN_USE_LEN;

    // len will be set during tx start
    SET_WORD(hal_dma_config[3].LENH, hal_dma_config[3].LENL, 0);

    // configure src and dest increments
    hal_dma_config[3].SRCINC         = DMA_SRCINC_1;
    hal_dma_config[3].DESTINC        = DMA_DESTINC_0;

    // set pointer to the DMA configuration struct into DMA-channel 1-4
    // configuration, should have happened in adc.c already...
    SET_WORD(DMA1CFGH, DMA1CFGL, &hal_dma_config[1]);

    // arm the relevant DMA channel for UART TX, and apply 45 NOP's
    // to allow the DMA configuration to load
    // -> do a sleep instead of those nops...
    DMAARM |= DMA_ARM_CH3;
    hal_delay_45nop();

#ifdef HUB_TELEMETRY_ON_SBUS_UART
    // activate serial rx interrupt
#if (SBUS_UART == USART0_P1) || (SBUS_UART == USART0_P0)
    URX0IF = 0;

    // enable receiption
    U0CSR |= UxCSR_RX_ENABLE;

    // enable RX interrupt
    URX0IE = 1;
#else
    URX1IF = 0;

    // enable receiption
    U1CSR |= UxCSR_RX_ENABLE;

    // enable RX interrupt
    URX1IE = 1;
#endif  // HUB_TELEMETRY_ON_SBUS_UART

    // enable global ints
    EA = 1;
#endif  // SBUS_UART == ...
}

static void hal_uart_set_mode(EXTERNAL_MEMORY union hal_uart_config_t *cfg) {
#if (SBUS_UART == USART0_P1) || (SBUS_UART == USART0_P0)
    // enable uart mode
    U0CSR |= 0x80;

    // store config to U1UCR register
    U0UCR = cfg->byte & (0x7F);

    // store config to U1GCR: (msb/lsb)
    if (cfg->bit.ORDER) {
        U0GCR |= U0GCR_ORDER;
    } else {
        U0GCR &= ~U0GCR_ORDER;
    }

    // interrupt prio to 1 (0..3=highest)
    IP0 |= (1<<2);
    IP1 &= ~(1<<2);
#else
    // enable uart mode
    U1CSR |= 0x80;

    // store config to U1UCR register
    U1UCR = cfg->byte & (0x7F);

    // store config to U1GCR: (msb/lsb)
    if (cfg->bit.ORDER) {
        U1GCR |= U1GCR_ORDER;
    } else {
        U1GCR &= ~U1GCR_ORDER;
    }

    // interrupt prio to 1 (0..3=highest)
    IP0 |= (1<<3);
    IP1 &= ~(1<<3);
#endif  // SBUS_UART
}

void hal_uart_start_transmission(uint8_t *data, uint8_t len) {
    // important: src addr start is data[1]
    SET_WORD(hal_dma_config[3].SRCADDRH,  hal_dma_config[3].SRCADDRL,  &data[1]);

    // configure length of transfer
    SET_WORD(hal_dma_config[3].LENH, hal_dma_config[3].LENL, len);

    // time to send this frame!
    // re-arm dma:
    DMAARM |= DMA_ARM_CH3;

    // 45 nops to make sure the dma config is loaded
    hal_delay_45nop();

    // send the very first UART byte to trigger a UART TX session:
#if (SBUS_UART == USART0_P1) || (SBUS_UART == USART0_P0)
    U0DBUF = data[0];
#else
    U1DBUF = data[0];
#endif  // SBUS_UART
}

#ifdef HUB_TELEMETRY_ON_SBUS_UART
void HAL_UART_RX_ISR(void) {
    uint8_t rx;

    HAL_UART_RX_ISR_CLEAR_FLAG();  // THIS SHOULD NEVER BE THE LAST LINE IN AN ISR!

#ifdef SBUS_INVERTED
    rx = 0xFF ^ HAL_UART_RX_GETCH();  // remove data inversion
#else
    rx = HAL_UART_RX_GETCH();
#endif  // SBUS_INVERTED

    if (uart_rx_callback != 0) {
        // execute callback
        uart_rx_callback(rx);
    }
}
#endif  // HUB_TELEMETRY_ON_SBUS_UART
