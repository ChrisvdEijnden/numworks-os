/* Hardware stand-ins for hal/display.c on the host */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
void host_cmd(uint16_t); void host_data(uint16_t); uint16_t host_read(void);
void host_pin(int port, int pin, int level);   /* an output pin changed */
void hal_delay_ms(uint32_t); void hal_uart_puts(const char *);
typedef struct { uint32_t MODER, OTYPER, OSPEEDR, PUPDR, IDR, ODR, BSRR, LCKR, AFR[2]; } GPIO_TypeDef;
extern GPIO_TypeDef fake_ports[5];
#define GPIOA (&fake_ports[0])
#define GPIOB (&fake_ports[1])
#define GPIOC (&fake_ports[2])
#define GPIOD (&fake_ports[3])
#define GPIOE (&fake_ports[4])
#define GPIO_PULL_NONE 0U
#define GPIO_PULL_UP   1U
static inline int port_no(GPIO_TypeDef *p) { return (int)(p - fake_ports); }
static inline void gpio_write(GPIO_TypeDef *p, uint32_t pin, bool high) {
    p->ODR = (p->ODR & ~(1U << pin)) | ((uint32_t)high << pin);
    if (((p->MODER >> (pin * 2)) & 3U) == 1U) host_pin(port_no(p), (int)pin, high);
}
static inline void gpio_output(GPIO_TypeDef *p, uint32_t pin, bool high) {
    p->MODER = (p->MODER & ~(3U << (pin * 2))) | (1U << (pin * 2));
    gpio_write(p, pin, high);
}
static inline void gpio_input(GPIO_TypeDef *p, uint32_t pin, uint32_t pull) { (void)pull; p->MODER &= ~(3U << (pin * 2)); }
static inline void gpio_analog(GPIO_TypeDef *p, uint32_t pin) { p->MODER |= 3U << (pin * 2); }
static inline void gpio_af(GPIO_TypeDef *p, uint32_t pin, uint32_t af, uint32_t speed) {
    (void)af; (void)speed; p->MODER = (p->MODER & ~(3U << (pin * 2))) | (2U << (pin * 2));
}
static struct { uint32_t AHB1ENR, AHB3ENR; } fake_rcc;
#define RCC (&fake_rcc)
static uint32_t fake_bcr1, fake_btr1, fake_bwtr1;
