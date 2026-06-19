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

//=============================================================================
// VARIÁVEIS GLOBAIS DE CONTROLE
//=============================================================================

/** * @brief Frequência da onda moduladora (senoide) em Hz.
 * @note Declarada como volatile pois pode ser alterada no loop principal.
 */
volatile float freq_moduladora = 60.0f;

/** * @brief Índice de modulação de amplitude (0.0 a 1.0).
 * @note Declarada como volatile pois pode ser alterada no loop principal.
 */
volatile float indice_modulacao = 1.0f;

/** @brief Ângulo atual (acumulador de fase) da Fase A em radianos */
float theta = 0.0f;

/** @brief Metade do período do timer PWM, usado no cálculo matemático do Duty Cycle */
uint16_t periodo_meio = 0;

//=============================================================================
// PROTÓTIPOS DE FUNÇÕES
//=============================================================================

__interrupt void epwm1ISR(void);
void ConfiguraSinaisComplementares(uint32_t base, uint16_t deadTimeTics);

//=============================================================================
// ESTRUTURAS DE CONFIGURAÇÃO
//=============================================================================

/** * @brief Estrutura de configuração base dos sinais EPWM.
 * @details Define a frequência base, ciclo de trabalho inicial nulo, e configura
 * o contador para o modo Up-Down (simétrico).
 */
EPWM_SignalParams pwmSignal = {
    FREQ_PWM,
    0.5f, 0.5f,
    true,
    DEVICE_SYSCLK_FREQ,
    SYSCTL_EPWMCLK_DIV_2,
    EPWM_COUNTER_MODE_UP_DOWN,
    EPWM_CLOCK_DIVIDER_1,
    EPWM_HSCLOCK_DIVIDER_1
};

//=============================================================================
// FUNÇÃO PRINCIPAL
//=============================================================================

/**
 * @brief Função principal do sistema.
 * @details Inicializa os periféricos, configura o pinmux, estabelece os módulos EPWM,
 * o tempo morto de hardware, a sincronização entre módulos e entra no loop infinito.
 */
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
    GPIO_setPinConfig(GPIO_1_EPWM1B); // Complementar Fase A

    GPIO_setPadConfig(2, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(GPIO_2_EPWM2A);
    GPIO_setPadConfig(3, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(GPIO_3_EPWM2B); // Complementar Fase B

    GPIO_setPadConfig(4, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(GPIO_4_EPWM3A);
    GPIO_setPadConfig(5, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(GPIO_5_EPWM3B); // Complementar Fase C

    // 3. Configuração de Interrupções
    Interrupt_register(INT_EPWM1, &epwm1ISR);

    // Congela os contadores (Time-Base) do EPWM para sincronização global
    SysCtl_disablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);

    // 4. Inicialização dos Módulos EPWM
    EPWM_configureSignal(EPWM1_BASE, &pwmSignal);
    EPWM_configureSignal(EPWM2_BASE, &pwmSignal);
    EPWM_configureSignal(EPWM3_BASE, &pwmSignal);

    // 5. Configuração do Tempo Morto (Dead-Band)
    // Injeta 100 ticks de clock para evitar curto-circuito (Shoot-through)
    ConfiguraSinaisComplementares(EPWM1_BASE, 100);
    ConfiguraSinaisComplementares(EPWM2_BASE, 100);
    ConfiguraSinaisComplementares(EPWM3_BASE, 100);

    // 6. Sincronização de Fase entre os Módulos
    // EPWM1 atua como Mestre
    EPWM_disablePhaseShiftLoad(EPWM1_BASE);
    EPWM_setPhaseShift(EPWM1_BASE, 0U);
    EPWM_setSyncOutPulseMode(EPWM1_BASE, EPWM_SYNC_OUT_PULSE_ON_COUNTER_ZERO);

    // EPWM2 e EPWM3 atuam como Escravos sincronizados
    EPWM_enablePhaseShiftLoad(EPWM2_BASE);
    EPWM_setPhaseShift(EPWM2_BASE, 0U);
    EPWM_enablePhaseShiftLoad(EPWM3_BASE);
    EPWM_setPhaseShift(EPWM3_BASE, 0U);

    // 7. Configuração de Disparo de Interrupção
    EPWM_setInterruptSource(EPWM1_BASE, EPWM_INT_TBCTR_ZERO);
    EPWM_setInterruptEventCount(EPWM1_BASE, 1U);
    EPWM_enableInterrupt(EPWM1_BASE);

    // Armazena PRD/2 para uso na ISR
    periodo_meio = EPWM_getTimeBasePeriod(EPWM1_BASE) / 2;

    // 8. Inicialização Final
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);
    Interrupt_enable(INT_EPWM1);

    // Habilita interrupções globais
    EINT;
    ERTM;

    // 9. Loop Infinito
    for(;;)
    {
        // Alterações em tempo de execução podem ser feitas aqui
        // Exemplo: freq_moduladora = 50.0f;
        // Exemplo: indice_modulacao = 0.8f;
        
        asm ("  NOP");
    }
}

//=============================================================================
// FUNÇÕES AUXILIARES
//=============================================================================

/**
 * @brief Configura o módulo Dead-Band para gerar sinais complementares com tempo morto.
 *
 * @details Esta função utiliza o sinal EPWMxA como referência e aplica o modo
 * Active High Complementary (AHC). O canal B é invertido em hardware,
 * simplificando a lógica de controle via software.
 *
 * @param base Endereço base do módulo EPWM correspondente (ex: EPWM1_BASE).
 * @param deadTimeTics Quantidade de ciclos de clock para atraso nas bordas (Tempo Morto).
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
 * @brief ISR do Módulo EPWM1 - Calcula as larguras de pulso da modulação senoidal em tempo real.
 *
 * @details Executada a cada ciclo do PWM (quando o contador atinge zero). Esta rotina
 * incrementa o acumulador de fase com base na frequência desejada, calcula a
 * defasagem para as três fases, extrai o seno correspondente e aplica ao
 * registrador de comparação (CMPA).
 */
__interrupt void epwm1ISR(void)
{
    // 1. Calcula o passo do ângulo com base na frequência dinâmica atual
    float delta_theta = DOIS_PI * freq_moduladora / (float)FREQ_PWM;

    // 2. Atualiza o acumulador de fase da referência (Fase A)
    theta += delta_theta;
    if(theta >= DOIS_PI)
    {
        theta -= DOIS_PI; // Wrap-around para manter o ângulo entre 0 e 2*Pi
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

    // 4. Extrai os valores senoidais (-1.0 a 1.0) utilizando o hardware FPU/TMU
    float sinA = sinf(theta_A);
    float sinB = sinf(theta_B);
    float sinC = sinf(theta_C);

    // 5. Calcula o Duty Cycle e ajusta para o contador unipolar do PWM
    uint16_t cmpA = (uint16_t)(periodo_meio * (1.0f + (indice_modulacao * sinA)));
    uint16_t cmpB = (uint16_t)(periodo_meio * (1.0f + (indice_modulacao * sinB)));
    uint16_t cmpC = (uint16_t)(periodo_meio * (1.0f + (indice_modulacao * sinC)));

    // 6. Atualiza os Registradores de Comparação (Duty Cycle em vigor)
    EPWM_setCounterCompareValue(EPWM1_BASE, EPWM_COUNTER_COMPARE_A, cmpA);
    EPWM_setCounterCompareValue(EPWM2_BASE, EPWM_COUNTER_COMPARE_A, cmpB);
    EPWM_setCounterCompareValue(EPWM3_BASE, EPWM_COUNTER_COMPARE_A, cmpC);

    // 7. Reconhecimento da interrupção e limpeza de flags do PIE e EPWM
    EPWM_clearEventTriggerInterruptFlag(EPWM1_BASE);
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP3);
}

// End of File
