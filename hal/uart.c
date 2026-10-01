/* ================================================================
 * NumWorks OS — UART Driver (debug / shell via USB-UART adapter)
 * File: hal/uart.c
 *
 * USART6 on PC6 (TX) / PC7 (RX), 115200 8N1 — the N0110's console
 * pins (PA9, which an older version used, is the USB VBUS sense)
 * Uses a 64-byte RX ring buffer (no DMA needed at 115200)
 * ================================================================ */
#include "uart.h"
#include <stdbool.h>
#include "../include/stm32f730.h"
#include "../include/config.h"
#include <string.h>

#define RX_BUF 64
static char     s_rxbuf[RX_BUF];
static volatile uint8_t s_rxhead = 0, s_rxtail = 0;   /* shared with the IRQ */
static bool     s_tx_dead = false;

void hal_uart_init(void) {
    RCC->APB2ENR |= RCC_APB2ENR_USART6EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
    (void)RCC->APB2ENR;

    gpio_af(CONSOLE_TX_PORT, CONSOLE_TX_PIN, 8U, 1U);   /* AF8 = USART6 */
    gpio_af(CONSOLE_RX_PORT, CONSOLE_RX_PIN, 8U, 1U);
    gpio_pull(CONSOLE_RX_PORT, CONSOLE_RX_PIN, GPIO_PULL_UP);  /* idle high if unconnected */

    /* BRR = fAPB2 / baud (oversampling by 16), rounded; written while UE=0 */
    USART6->CR1 = 0;
    USART6->BRR = (uint32_t)((APB2_HZ + DEBUG_BAUD / 2) / DEBUG_BAUD);
    USART6->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;
    USART6->CR1 |= USART_CR1_UE;

    nvic_enable(71);  /* USART6 global interrupt */
}

/* RX interrupt */
void USART6_IRQHandler(void) {
    uint32_t isr = USART6->ISR;
    if (isr & USART_ISR_ORE) {
        /* Overrun also raises this IRQ and stays set until cleared */
        USART6->ICR = USART_ICR_ORECF;
    }
    if (isr & USART_ISR_RXNE) {
        char c = (char)(USART6->RDR & 0xFF);
        uint8_t next = (s_rxtail + 1) % RX_BUF;
        if (next != s_rxhead) {
            s_rxbuf[s_rxtail] = c;
            s_rxtail = next;
        }
    }
}

void hal_uart_putc(char c) {
    /* Debug output must never be able to hang the system: if the
     * transmitter doesn't drain, give up on the UART for good. */
    if (s_tx_dead) return;
    if (c == '\n') hal_uart_putc('\r');
    uint32_t spins = 0;
    while (!(USART6->ISR & USART_ISR_TXE)) {
        if (++spins > 1000000U) { s_tx_dead = true; return; }
    }
    USART6->TDR = (uint8_t)c;
}

void hal_uart_puts(const char *s) {
    while (*s) hal_uart_putc(*s++);
}

/* Wait until the last character has left the shift register (before
 * the clock changes) */
void hal_uart_flush(void) {
    if (s_tx_dead) return;
    for (uint32_t spins = 0; spins < 1000000U; spins++)
        if (USART6->ISR & USART_ISR_TC) return;
}

int hal_uart_getc(void) {
    if (s_rxhead == s_rxtail) return -1;
    char c = s_rxbuf[s_rxhead];
    s_rxhead = (s_rxhead + 1) % RX_BUF;
    return (unsigned char)c;
}

int hal_uart_available(void) {
    return s_rxtail != s_rxhead;
}
