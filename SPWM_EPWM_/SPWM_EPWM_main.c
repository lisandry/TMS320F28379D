/**
 * @file    SPWM_EPWM.c
 * @brief   Geração de PWM Senoidal (SPWM) Trifásico em Tempo Real.
 *
 * @details Este módulo gera três pares de sinais PWM modulados por uma referência
 * senoidal dinâmica defasados em 120 graus. Diferente de implementações
 * baseadas em Lookup Table (LUT), este código utiliza um Acumulador de Fase
 * e a função sinf() para permitir o ajuste da frequência e amplitude em
 * tempo de execução, ideal para controle V/f em inversores trifásicos.
 * O tempo morto (Dead-Band) é gerido por hardware.
 *
 * SINAIS GERADOS:
 * - Fase A: EPWM1A (GPIO0) -> pino 40 | Complementar A: EPWM1B (GPIO1) -> pino 39
 * - Fase B: EPWM2A (GPIO2) -> pino 38 | Complementar B: EPWM2B (GPIO3) -> pino 37
 * - Fase C: EPWM3A (GPIO4) -> pino 36 | Complementar C: EPWM3B (GPIO5) -> pino 35
 */

#include "driverlib.h"
#include "device.h"
#include <math.h>

//=============================================================================
// DEFINIÇÕES MATEMÁTICAS E DE HARDWARE
//=============================================================================

/** @brief Constante Pi */
#define PI       3.14159265358979f

/** @brief Constante 2*Pi (Um ciclo completo em radianos) */
#define DOIS_PI  6.28318530717958f

/** @brief Defasagem angular de 120 graus em radianos (2*Pi/3) */
#define FASE_120 (DOIS_PI / 3.0f)

/** @brief Frequência de chaveamento (portadora) do PWM em Hz */
#define FREQ_PWM 15000U

/** * @brief Cálculo Automático do Período do Timer (Modo UP-DOWN)
 * TBPRD = Clock_do_Sistema / (2 * Frequencia_Desejada)
 */
#define VALOR_TBPRD (DEVICE_SYSCLK_FREQ / (2U * FREQ_PWM))

//=============================================================================
// VARIÁVEIS GLOBAIS DE CONTROLE
//=============================================================================

volatile float freq_moduladora = 60.0f;
volatile float indice_modulacao = 1.0f;
float theta = 0.0f;

/** @brief Metade do período do timer PWM, usado no cálculo matemático do Duty Cycle */
uint16_t periodo_meio = 0;

//=============================================================================
// PROTÓTIPOS DE FUNÇÕES
//=============================================================================

__interrupt void epwm1ISR(void);
void ConfiguraEPWM(uint32_t base);
void ConfiguraSinaisComplementares(uint32_t base, uint16_t deadTimeTics);

//=============================================================================
// FUNÇÃO PRINCIPAL
//=============================================================================

void main(void)
{
    // 1. Inicialização do Sistema e Periféricos
    Device_init();
    Device_initGPIO();
    Interrupt_initModule();
    Interrupt_initVectorTable();

    // 2. Configuração de Roteamento de Pinos (Pinmux)
    GPIO_setPadConfig(0, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(GPIO_0_EPWM1A);
    GPIO_setPadConfig(1, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(GPIO_1_EPWM1B);

    GPIO_setPadConfig(2, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(GPIO_2_EPWM2A);
    GPIO_setPadConfig(3, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(GPIO_3_EPWM2B);

    GPIO_setPadConfig(4, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(GPIO_4_EPWM3A);
    GPIO_setPadConfig(5, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(GPIO_5_EPWM3B);

    // 3. Configuração de Interrupções
    Interrupt_register(INT_EPWM1, &epwm1ISR);

    // Congela os contadores do EPWM para configuração e sincronização global
    SysCtl_disablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);

    // 4. Inicialização Manual e Segura dos Módulos EPWM (AQUI ESTÁ A CORREÇÃO PRINCIPAL)
    ConfiguraEPWM(EPWM1_BASE);
    ConfiguraEPWM(EPWM2_BASE);
    ConfiguraEPWM(EPWM3_BASE);

    // 5. Configuração do Tempo Morto (Dead-Band)
    ConfiguraSinaisComplementares(EPWM1_BASE, 100);
    ConfiguraSinaisComplementares(EPWM2_BASE, 100);
    ConfiguraSinaisComplementares(EPWM3_BASE, 100);

    // 6. Sincronização de Fase entre os Módulos
    // EPWM1 atua como Mestre (gera o pulso quando zera, mas não carrega fase de ninguém)
    EPWM_disablePhaseShiftLoad(EPWM1_BASE);
    EPWM_setPhaseShift(EPWM1_BASE, 0U);
    EPWM_setSyncOutPulseMode(EPWM1_BASE, EPWM_SYNC_OUT_PULSE_ON_COUNTER_ZERO);

    // EPWM2 e EPWM3 atuam como Escravos sincronizados
    EPWM_enablePhaseShiftLoad(EPWM2_BASE);
    EPWM_setPhaseShift(EPWM2_BASE, 0U);
    EPWM_enablePhaseShiftLoad(EPWM3_BASE);
    EPWM_setPhaseShift(EPWM3_BASE, 0U);

    // 7. Configuração de Disparo de Interrupção no Mestre
    EPWM_setInterruptSource(EPWM1_BASE, EPWM_INT_TBCTR_ZERO);
    EPWM_setInterruptEventCount(EPWM1_BASE, 1U);
    EPWM_enableInterrupt(EPWM1_BASE);

    // 8. Inicialização Final
    // Calcula o meio do período em tempo de compilação baseado no macro correto
    periodo_meio = VALOR_TBPRD / 2U;

    // Libera os clocks (os PWMs começam a contar todos juntos)
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);
    Interrupt_enable(INT_EPWM1);

    // Habilita interrupções globais e de tempo real
    EINT;
    ERTM;

    // 9. Loop Infinito
    for(;;)
    {
        // Alterações em tempo de execução
        asm ("  NOP");
    }
}

//=============================================================================
// FUNÇÕES AUXILIARES DE CONFIGURAÇÃO (SUBSTITUEM A CAIXA PRETA DA BIBLIOTECA)
//=============================================================================

/**
 * @brief Configura manualmente os parâmetros essenciais de cada módulo PWM.
 */
void ConfiguraEPWM(uint32_t base)
{
    // A) Time Base (Período exato, Modo Up-Down e Prescaler 1:1)
    EPWM_setTimeBasePeriod(base, VALOR_TBPRD);
    EPWM_setTimeBaseCounter(base, 0U);
    EPWM_setTimeBaseCounterMode(base, EPWM_COUNTER_MODE_UP_DOWN);
    EPWM_setClockPrescaler(base, EPWM_CLOCK_DIVIDER_1, EPWM_HSCLOCK_DIVIDER_1);

    // B) Action Qualifier (Sobe cruzando CMPA = ALTO | Desce cruzando CMPA = BAIXO)
    EPWM_setActionQualifierAction(base, EPWM_AQ_OUTPUT_A, EPWM_AQ_OUTPUT_HIGH, EPWM_AQ_OUTPUT_ON_TIMEBASE_UP_CMPA);
    EPWM_setActionQualifierAction(base, EPWM_AQ_OUTPUT_A, EPWM_AQ_OUTPUT_LOW, EPWM_AQ_OUTPUT_ON_TIMEBASE_DOWN_CMPA);

    // C) Shadow Mode (Garante que a atualização do Duty Cycle só ocorra no início do ciclo)
    EPWM_setCounterCompareShadowLoadMode(base, EPWM_COUNTER_COMPARE_A, EPWM_COMP_LOAD_ON_CNTR_ZERO);
}

/**
 * @brief Configura o módulo Dead-Band para gerar sinais complementares com tempo morto.
 */
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

//=============================================================================
// ROTINA DE TRATAMENTO DE INTERRUPÇÃO (ISR)
//=============================================================================

/**
 * @brief ISR do Módulo EPWM1 - Calcula as larguras de pulso da modulação senoidal.
 */
__interrupt void epwm1ISR(void)
{
    // 1. Calcula o passo do ângulo com base na frequência dinâmica atual
    float delta_theta = DOIS_PI * freq_moduladora / (float)FREQ_PWM;

    // 2. Atualiza o acumulador de fase da referência (Fase A)
    theta += delta_theta;
    if(theta >= DOIS_PI)
    {
        theta -= DOIS_PI;
    }

    // 3. Calcula os ângulos defasados para as Fases B e C
    float theta_A = theta;

    float theta_B = theta_A - FASE_120;
    if(theta_B < 0.0f)
    {
        theta_B += DOIS_PI;
    }

    float theta_C = theta_A + FASE_120;
    if(theta_C >= DOIS_PI)
    {
        theta_C -= DOIS_PI;
    }

    // 4. Extrai os valores senoidais (-1.0 a 1.0)
    float sinA = sinf(theta_A);
    float sinB = sinf(theta_B);
    float sinC = sinf(theta_C);

    // 5. Calcula o Duty Cycle adaptado aos limites do triângulo UP-DOWN
    uint16_t cmpA = (uint16_t)(periodo_meio * (1.0f + (indice_modulacao * sinA)));
    uint16_t cmpB = (uint16_t)(periodo_meio * (1.0f + (indice_modulacao * sinB)));
    uint16_t cmpC = (uint16_t)(periodo_meio * (1.0f + (indice_modulacao * sinC)));

    // 6. Atualiza os Registradores de Comparação
    EPWM_setCounterCompareValue(EPWM1_BASE, EPWM_COUNTER_COMPARE_A, cmpA);
    EPWM_setCounterCompareValue(EPWM2_BASE, EPWM_COUNTER_COMPARE_A, cmpB);
    EPWM_setCounterCompareValue(EPWM3_BASE, EPWM_COUNTER_COMPARE_A, cmpC);

    // 7. Reconhecimento da interrupção
    EPWM_clearEventTriggerInterruptFlag(EPWM1_BASE);
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP3);
}
