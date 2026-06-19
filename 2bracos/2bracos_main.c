/*
 * MAPEAMENTO DE PINOS - 8 SAÍDAS (DUAS PONTES COMPLETAS)
 *
 * PONTE COMPLETA 1 (Primeiro "Braço" Completo):
 * - EPWM1A (GPIO0) : cmpA -> Chave Superior Esquerda
 * - EPWM1B (GPIO1) : cmpA -> Chave Inferior Esquerda (Complementar com Dead-Band)
 * - EPWM2A (GPIO2) : cmpB -> Chave Superior Direita
 * - EPWM2B (GPIO3) : cmpB -> Chave Inferior Direita (Complementar com Dead-Band)
 *
 * PONTE COMPLETA 2 (Segundo "Braço" Completo - Complementar à Ponte 1):
 * - EPWM3A (GPIO4) : cmpB -> Chave Superior Esquerda
 * - EPWM3B (GPIO5) : cmpB -> Chave Inferior Esquerda (Complementar com Dead-Band)
 * - EPWM4A (GPIO6) : cmpA -> Chave Superior Direita
 * - EPWM4B (GPIO7) : cmpA -> Chave Inferior Direita (Complementar com Dead-Band)
 */

#include "driverlib.h"
#include "device.h"
#include <math.h>

#define PI       3.14159265358979f
#define DOIS_PI  6.28318530717958f
#define FASE_180 PI
#define FREQ_PWM 10000U

volatile float freq_moduladora = 60.0f;
volatile float indice_modulacao = 0.99f;
float theta = 0.0f;
uint16_t periodo_meio = 0;

// Protótipos
__interrupt void epwm1ISR(void);
void ConfiguraBaseEPWM(uint32_t base);
void ConfiguraSinaisComplementares(uint32_t base, uint16_t deadTimeTics);

void main(void)
{
    Device_init();
    Device_initGPIO();
    Interrupt_initModule();
    Interrupt_initVectorTable();

    // -------------------------------------------------------------
    // Configuração dos GPIOs para as 8 saídas
    // -------------------------------------------------------------

    // Ponte Completa 1
    GPIO_setPadConfig(0, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(GPIO_0_EPWM1A);
    GPIO_setPadConfig(1, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(GPIO_1_EPWM1B);

    GPIO_setPadConfig(2, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(GPIO_2_EPWM2A);
    GPIO_setPadConfig(3, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(GPIO_3_EPWM2B);

    // Ponte Completa 2
    GPIO_setPadConfig(4, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(GPIO_4_EPWM3A);
    GPIO_setPadConfig(5, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(GPIO_5_EPWM3B);

    GPIO_setPadConfig(6, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(GPIO_6_EPWM4A);
    GPIO_setPadConfig(7, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(GPIO_7_EPWM4B);

    Interrupt_register(INT_EPWM1, &epwm1ISR);
    SysCtl_disablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);

    // Configuração das bases de tempo
    ConfiguraBaseEPWM(EPWM1_BASE);
    ConfiguraBaseEPWM(EPWM2_BASE);
    ConfiguraBaseEPWM(EPWM3_BASE);
    ConfiguraBaseEPWM(EPWM4_BASE);

    // Configuração do Tempo Morto (Dead-Band)
    ConfiguraSinaisComplementares(EPWM1_BASE, 100);
    ConfiguraSinaisComplementares(EPWM2_BASE, 100);
    ConfiguraSinaisComplementares(EPWM3_BASE, 100);
    ConfiguraSinaisComplementares(EPWM4_BASE, 100);

    // Sincronização de Fase entre os 4 Módulos
    EPWM_disablePhaseShiftLoad(EPWM1_BASE);
    EPWM_setPhaseShift(EPWM1_BASE, 0U);
    EPWM_setSyncOutPulseMode(EPWM1_BASE, EPWM_SYNC_OUT_PULSE_ON_COUNTER_ZERO);

    EPWM_enablePhaseShiftLoad(EPWM2_BASE);
    EPWM_setPhaseShift(EPWM2_BASE, 0U);

    EPWM_enablePhaseShiftLoad(EPWM3_BASE);
    EPWM_setPhaseShift(EPWM3_BASE, 0U);

    EPWM_enablePhaseShiftLoad(EPWM4_BASE);
    EPWM_setPhaseShift(EPWM4_BASE, 0U);

    EPWM_setInterruptSource(EPWM1_BASE, EPWM_INT_TBCTR_ZERO);
    EPWM_setInterruptEventCount(EPWM1_BASE, 1U);
    EPWM_enableInterrupt(EPWM1_BASE);

    periodo_meio = EPWM_getTimeBasePeriod(EPWM1_BASE) / 2;

    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);
    Interrupt_enable(INT_EPWM1);

    EINT;
    ERTM;

    for(;;) {
        asm ("  NOP");
    }
}

void ConfiguraBaseEPWM(uint32_t base)
{
    uint32_t epwmClkFreq = 100000000U;
    uint16_t tbprd = (uint16_t)(epwmClkFreq / (2U * FREQ_PWM));

    EPWM_setTimeBasePeriod(base, tbprd);
    EPWM_setPhaseShift(base, 0U);
    EPWM_setTimeBaseCounter(base, 0U);
    EPWM_setTimeBaseCounterMode(base, EPWM_COUNTER_MODE_UP_DOWN);

    EPWM_setClockPrescaler(base, EPWM_CLOCK_DIVIDER_1, EPWM_HSCLOCK_DIVIDER_1);

    EPWM_setCounterCompareValue(base, EPWM_COUNTER_COMPARE_A, tbprd / 2U);

    EPWM_setActionQualifierAction(base, EPWM_AQ_OUTPUT_A,
                                  EPWM_AQ_OUTPUT_HIGH,
                                  EPWM_AQ_OUTPUT_ON_TIMEBASE_UP_CMPA);

    EPWM_setActionQualifierAction(base, EPWM_AQ_OUTPUT_A,
                                  EPWM_AQ_OUTPUT_LOW,
                                  EPWM_AQ_OUTPUT_ON_TIMEBASE_DOWN_CMPA);
}

void ConfiguraSinaisComplementares(uint32_t base, uint16_t deadTimeTics)
{
    EPWM_setRisingEdgeDeadBandDelayInput(base, EPWM_DB_INPUT_EPWMA);
    EPWM_setFallingEdgeDeadBandDelayInput(base, EPWM_DB_INPUT_EPWMA);

    EPWM_setDeadBandDelayPolarity(base, EPWM_DB_RED, EPWM_DB_POLARITY_ACTIVE_HIGH);
    EPWM_setDeadBandDelayPolarity(base, EPWM_DB_FED, EPWM_DB_POLARITY_ACTIVE_LOW);

    EPWM_setDeadBandDelayMode(base, EPWM_DB_RED, true);
    EPWM_setDeadBandDelayMode(base, EPWM_DB_FED, true);

    EPWM_setRisingEdgeDelayCount(base, deadTimeTics);
    EPWM_setFallingEdgeDelayCount(base, deadTimeTics);
}

__interrupt void epwm1ISR(void)
{
    // 1. Calcula o passo angular
    float delta_theta = DOIS_PI * freq_moduladora / (float)FREQ_PWM;

    // 2. Acumulador de fase
    theta += delta_theta;
    if(theta >= DOIS_PI)
    {
        theta -= DOIS_PI;
    }

    // 3. Calcula os ângulos defasados
    float theta_A = theta;
    float theta_B = theta_A - FASE_180;

    if(theta_B < 0.0f)
    {
        theta_B += DOIS_PI;
    }

    // 4. Extrai os valores senoidais (-1.0 a 1.0)
    float sinA = sinf(theta_A);
    float sinB = sinf(theta_B);

    // 5. Calcula os valores de comparação para os semiciclos
    uint16_t cmpA = (uint16_t)(periodo_meio * (1.0f + (indice_modulacao * sinA)));
    uint16_t cmpB = (uint16_t)(periodo_meio * (1.0f + (indice_modulacao * sinB)));

    // 6. Atualiza os Registradores de Comparação para as duas Pontes

    // Ponte Completa 1
    EPWM_setCounterCompareValue(EPWM1_BASE, EPWM_COUNTER_COMPARE_A, cmpA);
    EPWM_setCounterCompareValue(EPWM2_BASE, EPWM_COUNTER_COMPARE_A, cmpB);

    // Ponte Completa 2 (Invertida em relação à Ponte 1)
    EPWM_setCounterCompareValue(EPWM3_BASE, EPWM_COUNTER_COMPARE_A, cmpB);
    EPWM_setCounterCompareValue(EPWM4_BASE, EPWM_COUNTER_COMPARE_A, cmpA);

    // 7. Reconhecimento da interrupção
    EPWM_clearEventTriggerInterruptFlag(EPWM1_BASE);
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP3);
}
