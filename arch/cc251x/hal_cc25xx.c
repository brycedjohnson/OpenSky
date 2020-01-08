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

#include "hal_cc25xx.h"
#include "cc25xx.h"
#include "hal_defines.h"
#include "hal_dma.h"
#include "delay.h"
#include "timeout.h"
#include "debug.h"
#include "led.h"
#include "frsky.h"
#include <cc2510fx.h>

EXTERNAL_MEMORY volatile uint8_t hal_cc25xx_mode;

void hal_cc25xx_init(void) {
    // set highest prio for ch0 (RF)
    IP1 |=  (1<<0);
    IP0 |=  (1<<0);


    hal_cc25xx_mode = CC25XX_MODE_RX;

    // if we support LNA/PA make sure to config the pin as output:
    #ifdef RF_LNA_PORT
      PORT2DIR(RF_LNA_PORT) |= (1 << RF_LNA_PIN);
      // set default to LNA active
      RF_LNA_ENABLE();
      #ifdef RF_PA_PORT
        PORT2DIR(RF_PA_PORT)  |= (1 << RF_PA_PIN);
        RF_PA_DISABLE();
      #endif
    #endif  // RF_LNA_PORT

    // if we support Diversity make sure to config the pin as output:
    #ifdef RF_ANTENNA_SWITCH_PORT
      PORT2DIR(RF_ANTENNA_SWITCH_PORT) |= (1 << RF_ANTENNA_SWITCH_PIN);
    #ifdef RF_ANTENNA_SWITCH_PORT2
      PORT2DIR(RF_ANTENNA_SWITCH_PORT2) |= (1 << RF_ANTENNA_SWITCH_PIN2);
    #endif
      // select first antenna
      RF_ANTENNA_SELECT_A();
    #endif  // RF_ANTENNA_SWITCH_PORT

    // if we support HIGH GAIN mode config pin as output:
    #ifdef RF_HIGH_GAIN_MODE_PORT
      PORT2DIR(RF_HIGH_GAIN_MODE_PORT) |= (1 << RF_HIGH_GAIN_MODE_PIN);
      // enable high gain mode?
      #ifdef RF_HIGH_GAIN_MODE_ENABLED
        RF_HIGH_GAIN_MODE_ENABLE();
      #else
        RF_HIGH_GAIN_MODE_DISABLE();
      #endif  // RF_HIGH_GAIN_MODE_ENABLED
    #endif  // RF_HIGH_GAIN_MODE_PORT

    // if we support Bypass mode make sure to config the pin as output:
    #ifdef RF_BYPASS_PORT
      PORT2DIR(RF_BYPASS_MODE_PORT) |= (1 << RF_BYPASS_MODE_PIN);
      // set default to Bypass off
      #ifdef RF_BYPASS_MODE_ENABLED
        RF_BYPASS_MODE_ENABLE();
      #else
        RF_BYPASS_MODE_DISABLE();
      #endif  // RF_BYPASS_MODE_ENABLED
    #endif  // RF_BYPASS_PORT
}

uint32_t hal_cc25xx_set_antenna(uint8_t id) {
    // select antenna 0 or 1:
    #ifdef RF_ANTENA_SWITCH_PORT
      if (id) {
          RF_ANTENNA_SELECT_B();
      } else {
          RF_ANTENNA_SELECT_A();
      }
    #endif  // RF_ANTENNA_SWITCH_PORT
    return id;
}

void hal_cc25xx_disable_rf_interrupt(void) {
    IEN2 &= ~(IEN2_RFIE);
    RFIM = 0;
}

void hal_cc25xx_enter_rxmode(void) {
#ifdef RF_LNA_PORT
    RF_LNA_ENABLE();
    delay_us(20);
    #ifdef RF_PA_PORT
      RF_PA_DISABLE();
      delay_us(5);
    #endif
#endif  // RF_LNA_PORT

    // set up dma for radio--->buffer
    hal_cc25xx_setup_rf_dma(CC25XX_MODE_RX);

    // configure interrupt for every received packet
    IEN2 |= (IEN2_RFIE);

    // mask done irq
    RFIM = (1<<4);
    // interrupts should be enabled globally already..
    // skip this! sei();
}

void hal_cc25xx_enter_txmode(void) {
#ifdef RF_LNA_PORT
    RF_LNA_DISABLE();
    delay_us(20);
    #ifdef RF_PA_PORT
      RF_PA_ENABLE();
      delay_us(5);
    #endif
#endif  // RF_LNA_PORT

    // abort ch0
    DMAARM = DMA_ARM_ABORT | DMA_ARM_CH0;
    hal_cc25xx_setup_rf_dma(CC25XX_MODE_TX);
}

void hal_cc25xx_setup_rf_dma(uint8_t mode) {
    // CPU has priority over DMA
    // Use 8 bits for transfer count
    // No DMA interrupt when done
    // DMA triggers on radio
    // Single transfer per trigger.
    // One byte is transferred each time.

    hal_dma_config[0].PRIORITY       = DMA_PRI_HIGH;
    hal_dma_config[0].M8             = DMA_M8_USE_8_BITS;
    hal_dma_config[0].IRQMASK        = DMA_IRQMASK_DISABLE;
    hal_dma_config[0].TRIG           = DMA_TRIG_RADIO;
    hal_dma_config[0].TMODE          = DMA_TMODE_SINGLE;
    hal_dma_config[0].WORDSIZE       = DMA_WORDSIZE_BYTE;

    // store mode
    hal_cc25xx_mode = mode;

    if (hal_cc25xx_mode == CC25XX_MODE_TX) {
        // Transmitter specific DMA settings
        // Source: radioPktBuffer
        // Destination: RFD register
        // Use the first byte read + 1
        // Sets the maximum transfer count allowed (length byte + data)
        // Data source address is incremented by 1 byte
        // Destination address is constant
        SET_WORD(hal_dma_config[0].SRCADDRH, hal_dma_config[0].SRCADDRL, packet);
        SET_WORD(hal_dma_config[0].DESTADDRH, hal_dma_config[0].DESTADDRL, &X_RFD);
        hal_dma_config[0].VLEN           = DMA_VLEN_FIRST_BYTE_P_1;
        SET_WORD(hal_dma_config[0].LENH, hal_dma_config[0].LENL, (FRSKY_PACKET_LENGTH+1));
        hal_dma_config[0].SRCINC         = DMA_SRCINC_1;
        hal_dma_config[0].DESTINC        = DMA_DESTINC_0;
    } else {
        // Receiver specific DMA settings:
        // Source: RFD register
        // Destination: radioPktBuffer
        // Use the first byte read + 3 (incl. 2 status bytes)
        // Sets maximum transfer count allowed (length byte + data + 2 status bytes)
        // Data source address is constant
        // Destination address is incremented by 1 byte for each write
        SET_WORD(hal_dma_config[0].SRCADDRH, hal_dma_config[0].SRCADDRL, &X_RFD);
        SET_WORD(hal_dma_config[0].DESTADDRH, hal_dma_config[0].DESTADDRL, packet);
        hal_dma_config[0].VLEN           = DMA_VLEN_FIRST_BYTE_P_3;
        SET_WORD(hal_dma_config[0].LENH, hal_dma_config[0].LENL, (FRSKY_PACKET_LENGTH+3));
        hal_dma_config[0].SRCINC         = DMA_SRCINC_0;
        hal_dma_config[0].DESTINC        = DMA_DESTINC_1;
    }

    // Save pointer to the DMA configuration struct into DMA-channel 0
    // configuration registers
    SET_WORD(DMA0CFGH, DMA0CFGL, &hal_dma_config[0]);

    // frsky_packet_received = 0;
}

void hal_cc25xx_enable_receive(void) {
    // start receiving on dma channel 0
    DMAARM = DMA_ARM_CH0;
}



void hal_cc25xx_rf_interrupt(void) __interrupt RF_VECTOR {
    // clear int flag
    RFIF &= ~(1<<4);

    // clear general statistics reg
    S1CON &= ~0x03;

    if (hal_cc25xx_mode == CC25XX_MODE_RX) {
        // mark as received:
        frsky_packet_received = 1;
        // re arm DMA channel 0
        hal_cc25xx_enable_receive();
    } else {
        frsky_packet_sent = 1;
    }
}

uint8_t hal_cc25xx_transmission_completed(void) {
    // this flag is set in the RF isr
    return (frsky_packet_sent);
}


void hal_cc25xx_transmit_packet(volatile uint8_t *buffer, uint8_t len) {
    UNUSED(buffer);
    UNUSED(len);

    RFST = RFST_STX;

    // start transmitting on dma channel 0
    DMAARM = DMA_ARM_CH0;

    // mark packet as not sent (will be modified in RF isr):
    frsky_packet_sent = 0;

    // tricky: this will force an int request and
    //        initiate the actual transmission
    S1CON |= 0x03;
}

