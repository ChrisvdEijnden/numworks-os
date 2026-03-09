/* ================================================================
 * NumWorks OS — UART Driver (debug / shell via USB-UART adapter)
 * File: hal/uart.c
 *
 * USART1 on PA9 (TX) / PA10 (RX), 115200 8N1
 * Uses a 64-byte RX ring buffer (no DMA needed at 115200)
 * ================================================================ */
#include "uart.h"
#include "../include/stm32f730.h"
#include "../include/config.h"
#include <string.h>

#define RX_BUF 64
static char     s_rxbuf[RX_BUF];
static uint8_t  s_rxhead = 0, s_rxtail = 0;

void hal_uart_init(void) {
    /* Clock USART1 + GPIOA */
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

    /* PA9 = TX (AF7), PA10 = RX (AF7) */
    /* MODER: AF mode = 10b */
    GPIOA->MODER &= ~((3U<<18) | (3U<<20));
    GPIOA->MODER |=   (2U<<18) | (2U<<20);
    /* AFR[1] bits 4-7 = PA9, bits 8-11 = PA10, AF7 = 0x7 */
    GPIOA->AFR[1] &= ~(0xFFU << 4);
    GPIOA->AFR[1] |=  (0x77U << 4);

    /* BRR = fAPB2 / baud */
    USART1->BRR = (uint32_t)(APB2_HZ / DEBUG_BAUD);
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE;

    /* Enable USART1 IRQ */
    nvic_enable(37);  /* USART1 IRQ = 37 on STM32F7 */
}

/* RX interrupt */
void USART1_IRQHandler(void) {
    if (USART1->SR & USART_SR_RXNE) {
        char c = (char)(USART1->DR & 0xFF);
        uint8_t next = (s_rxtail + 1) % RX_BUF;
        if (next != s_rxhead) {
            s_rxbuf[s_rxtail] = c;
            s_rxtail = next;
        }
    }
}

void hal_uart_putc(char c) {
    if (c == '\n') hal_uart_putc('\r');
    while (!(USART1->SR & USART_SR_TXE)) {}
    USART1->DR = (uint8_t)c;
}

void hal_uart_puts(const char *s) {
    while (*s) hal_uart_putc(*s++);
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
