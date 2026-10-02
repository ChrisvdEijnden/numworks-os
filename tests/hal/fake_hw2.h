/* Fake registers for hal/backlight.c, hal/led.c, hal/battery.c */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef volatile uint32_t vu32;
typedef struct { uint32_t MODER, OTYPER, OSPEEDR, PUPDR, IDR, ODR, BSRR, LCKR, AFR[2]; } GPIO_TypeDef;
extern GPIO_TypeDef fake_ports[5];
#define GPIOA (&fake_ports[0])
#define GPIOB (&fake_ports[1])
#define GPIOC (&fake_ports[2])
#define GPIOD (&fake_ports[3])
#define GPIOE (&fake_ports[4])
#define GPIO_PULL_NONE 0U
#define GPIO_PULL_UP   1U
void fake_pin_changed(int port, int pin, int level);
static inline int port_no(GPIO_TypeDef *p) { return (int)(p - fake_ports); }
static inline void gpio_mode(GPIO_TypeDef *p, uint32_t pin, uint32_t m) { p->MODER = (p->MODER & ~(3U << (pin * 2))) | (m << (pin * 2)); }
static inline void gpio_pull(GPIO_TypeDef *p, uint32_t pin, uint32_t u) { p->PUPDR = (p->PUPDR & ~(3U << (pin * 2))) | (u << (pin * 2)); }
static inline void gpio_write(GPIO_TypeDef *p, uint32_t pin, bool high) {
    int was = (p->ODR >> pin) & 1;
    p->ODR = (p->ODR & ~(1U << pin)) | ((uint32_t)high << pin);
    if (was != (int)high && ((p->MODER >> (pin * 2)) & 3U) == 1U) fake_pin_changed(port_no(p), (int)pin, high);
}
static inline bool gpio_read(const GPIO_TypeDef *p, uint32_t pin) { return (p->IDR >> pin) & 1U; }
static inline void gpio_output(GPIO_TypeDef *p, uint32_t pin, bool high) {
    p->ODR = (p->ODR & ~(1U << pin)) | ((uint32_t)high << pin);
    gpio_mode(p, pin, 1U); fake_pin_changed(port_no(p), (int)pin, high);
}
static inline void gpio_input(GPIO_TypeDef *p, uint32_t pin, uint32_t pull) { gpio_pull(p, pin, pull); gpio_mode(p, pin, 0U); }
static inline void gpio_analog(GPIO_TypeDef *p, uint32_t pin) { gpio_pull(p, pin, 0); gpio_mode(p, pin, 3U); }
static inline void gpio_af(GPIO_TypeDef *p, uint32_t pin, uint32_t af, uint32_t speed) {
    (void)speed; p->AFR[pin >> 3] = (p->AFR[pin >> 3] & ~(0xFU << ((pin & 7) * 4))) | (af << ((pin & 7) * 4)); gpio_mode(p, pin, 2U);
}
typedef struct { uint32_t AHB1ENR, APB1ENR, APB2ENR; } fake_rcc_t;
extern fake_rcc_t fake_rcc;
#define RCC (&fake_rcc)
#define RCC_AHB1ENR_GPIOAEN 1U
#define RCC_AHB1ENR_GPIOBEN 2U
#define RCC_AHB1ENR_GPIOEEN 16U
#define RCC_APB1ENR_TIM3EN 2U
typedef struct { uint32_t CR1, CR2, SMCR, DIER, SR, EGR, CCMR1, CCMR2, CCER, CNT, PSC, ARR, RCR, CCR1, CCR2, CCR3, CCR4; } TIM_TypeDef;
extern TIM_TypeDef fake_tim3;
#define TIM3 (&fake_tim3)
#define TIM_CR1_CEN (1U << 0)
#define TIM_CR1_ARPE (1U << 7)
#define TIM_EGR_UG 1U
void hal_delay_us(uint32_t us);
#define APB2_BASE 0
