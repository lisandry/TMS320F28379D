//#############################################################################
//
// FILE:   SPWM_EPWM.c
//
// TITLE:  Geração de PWM Senoidal (SPWM) Trifásico com Tempo Morto.
//
// DESCRIPTION:
// Este módulo gera três pares de sinais PWM modulados por uma referência
// senoidal (SPWM) defasados em 120 graus. Cada fase possui um sinal
// complementar com injeção de tempo morto (Dead-Band) gerido por hardware,
// ideal para o acionamento de inversores trifásicos (pontes inversoras).
//
// SINAIS GERADOS:
// Fase A: EPWM1A (GPIO0) -> pino 40 | Complementar A: EPWM1B (GPIO1) -> pino 39
// Fase B: EPWM2A (GPIO2) -> pino 38 | Complementar B: EPWM2B (GPIO3) -> pino 37
// Fase C: EPWM3A (GPIO4) -> pino 36 | Complementar C: EPWM3B (GPIO5) -> pino 35
//
//#############################################################################

#include "driverlib.h"
#include "device.h"

//=============================================================================
// DEFINIÇÕES E PARÂMETROS GERAIS
//=============================================================================

/** @brief Frequência de chaveamento do PWM em Hz */
#define FREQ_PWM  15000U

/** @brief Índice de modulação de amplitude (0.0 a 1.0) */
float indice_modulacao = 1.0f;

/** @brief Posição atual fracionária na tabela de seno (Lookup Table) */
float indice_atual = 0.0f;

/** @brief Metade do período do timer PWM (usado para o cálculo do Duty Cycle) */
uint16_t periodo_meio = 0;

//=============================================================================
// PROTÓTIPOS DE FUNÇÕES
//=============================================================================

__interrupt void epwm1ISR(void);
void ConfiguraSinaisComplementares(uint32_t base, uint16_t deadTimeTics);

//=============================================================================
// ESTRUTURAS DE CONFIGURAÇÃO
//=============================================================================

/** @brief Configuração base dos sinais EPWM */
EPWM_SignalParams pwmSignal = {
    FREQ_PWM,
    0.5f, 0.5f,
    true,P
    DEVICE_SYSCLK_FREQ,
    SYSCTL_EPWMCLK_DIV_2,
    EPWM_COUNTER_MODE_UP_DOWN,
    EPWM_CLOCK_DIVIDER_1,
    EPWM_HSCLOCK_DIVIDER_1
};

//=============================================================================
// TABELA DE REFERÊNCIA SENOIDAL (LOOKUP TABLE)
//=============================================================================

/** @brief Número de amostras em um ciclo completo da senoide */
#define TAMANHO_LUT 256

/** * @brief Incremento do índice por interrupção.
 * * Cálculo: Passo = Tamanho_Tabela * (Freq_Moduladora / Freq_Portadora)
 * Exemplo para Moduladora de 60Hz e Portadora de 1250Hz (Up-Down mode ajusta a taxa):
 * Passo = 256 * (60.0 / 1250.0) = 12.288
 */
#define PASSO_INDICE 12.288f

/** @brief Defasagem em pontos da tabela correspondente a 120 graus eletricos (256 / 3 = 85.33) */
#define DEFASAMENTO_120 85

/** @brief Lookup table com valores pré-calculados da função seno (range: -1.0f a 1.0f) */
const float SENO_LUT[TAMANHO_LUT] = {
    0.0000f,  0.0245f,  0.0491f,  0.0736f,  0.0980f,  0.1224f,  0.1467f,  0.1710f,
    0.1951f,  0.2191f,  0.2429f,  0.2666f,  0.2902f,  0.3135f,  0.3368f,  0.3599f,
    0.3827f,  0.4052f,  0.4276f,  0.4496f,  0.4714f,  0.4929f,  0.5141f,  0.5350f,
    0.5556f,  0.5758f,  0.5957f,  0.6152f,  0.6344f,  0.6532f,  0.6716f,  0.6895f,
    0.7071f,  0.7242f,  0.7410f,  0.7573f,  0.7732f,  0.7886f,  0.8036f,  0.8181f,
    0.8322f,  0.8458f,  0.8589f,  0.8716f,  0.8838f,  0.8955f,  0.9067f,  0.9174f,
    0.9276f,  0.9373f,  0.9465f,  0.9552f,  0.9634f,  0.9711f,  0.9783f,  0.9850f,
    0.9911f,  0.9967f,  1.0000f,  1.0000f,  0.9967f,  0.9911f,  0.9850f,  0.9783f,
    0.9711f,  0.9634f,  0.9552f,  0.9465f,  0.9373f,  0.9276f,  0.9174f,  0.9067f,
    0.8955f,  0.8838f,  0.8716f,  0.8589f,  0.8458f,  0.8322f,  0.8181f,  0.8036f,
    0.7886f,  0.7732f,  0.7573f,  0.7410f,  0.7242f,  0.7071f,  0.6895f,  0.6716f,
    0.6532f,  0.6344f,  0.6152f,  0.5957f,  0.5758f,  0.5556f,  0.5350f,  0.5141f,
    0.4929f,  0.4714f,  0.4496f,  0.4276f,  0.4052f,  0.3827f,  0.3599f,  0.3368f,
    0.3135f,  0.2902f,  0.2666f,  0.2429f,  0.2191f,  0.1951f,  0.1710f,  0.1467f,
    0.1224f,  0.0980f,  0.0736f,  0.0491f,  0.0245f,  0.0000f, -0.0245f, -0.0491f,
   -0.0736f, -0.0980f, -0.1224f, -0.1467f, -0.1710f, -0.1951f, -0.2191f, -0.2429f,
   -0.2666f, -0.2902f, -0.3135f, -0.3368f, -0.3599f, -0.3827f, -0.4052f, -0.4276f,
   -0.4496f, -0.4714f, -0.4929f, -0.5141f, -0.5350f, -0.5556f, -0.5758f, -0.5957f,
   -0.6152f, -0.6344f, -0.6532f, -0.6716f, -0.6895f, -0.7071f, -0.7242f, -0.7410f,
   -0.7573f, -0.7732f, -0.7886f, -0.8036f, -0.8181f, -0.8322f, -0.8458f, -0.8589f,
   -0.8716f, -0.8838f, -0.8955f, -0.9067f, -0.9174f, -0.9276f, -0.9373f, -0.9465f,
   -0.9552f, -0.9634f, -0.9711f, -0.9783f, -0.9850f, -0.9911f, -0.9967f, -1.0000f,
   -1.0000f, -0.9967f, -0.9911f, -0.9850f, -0.9783f, -0.9711f, -0.9634f, -0.9552f,
   -0.9465f, -0.9373f, -0.9276f, -0.9174f, -0.9067f, -0.8955f, -0.8838f, -0.8716f,
   -0.8589f, -0.8458f, -0.8322f, -0.8181f, -0.8036f, -0.7886f, -0.7732f, -0.7573f,
   -0.7410f, -0.7242f, -0.7071f, -0.6895f, -0.6716f, -0.6532f, -0.6344f, -0.6152f,
   -0.5957f, -0.5758f, -0.5556f, -0.5350f, -0.5141f, -0.4929f, -0.4714f, -0.4496f,
   -0.4276f, -0.4052f, -0.3827f, -0.3599f, -0.3368f, -0.3135f, -0.2902f, -0.2666f,
   -0.2429f, -0.2191f, -0.1951f, -0.1710f, -0.1467f, -0.1224f, -0.0980f, -0.0736f,
   -0.0491f, -0.0245f
};

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
    // Fase A
    GPIO_setPadConfig(0, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(GPIO_0_EPWM1A);
    GPIO_setPadConfig(1, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(GPIO_1_EPWM1B); // Complementar

    // Fase B
    GPIO_setPadConfig(2, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(GPIO_2_EPWM2A);
    GPIO_setPadConfig(3, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(GPIO_3_EPWM2B); // Complementar

    // Fase C
    GPIO_setPadConfig(4, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(GPIO_4_EPWM3A);
    GPIO_setPadConfig(5, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(GPIO_5_EPWM3B); // Complementar

    // 3. Configuração de Interrupções
    Interrupt_register(INT_EPWM1, &epwm1ISR);

    // Congela os contadores (Time-Base) do EPWM para sincronização global
    SysCtl_disablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);

    // 4. Inicialização dos Módulos EPWM
    EPWM_configureSignal(EPWM1_BASE, &pwmSignal);
    EPWM_configureSignal(EPWM2_BASE, &pwmSignal);
    EPWM_configureSignal(EPWM3_BASE, &pwmSignal);

    // 5. Configuração do Tempo Morto (Dead-Band) nos canais complementares
    // Injeta 100 ticks de clock de hardware para evitar curto-circuito na ponte (Shoot-through)
    ConfiguraSinaisComplementares(EPWM1_BASE, 100);
    ConfiguraSinaisComplementares(EPWM2_BASE, 100);
    ConfiguraSinaisComplementares(EPWM3_BASE, 100);

    // 6. Sincronização de Fase entre os Módulos
    // EPWM1 atua como Mestre (gera pulso de sincronismo)
    EPWM_disablePhaseShiftLoad(EPWM1_BASE);
    EPWM_setPhaseShift(EPWM1_BASE, 0U);
    EPWM_setSyncOutPulseMode(EPWM1_BASE, EPWM_SYNC_OUT_PULSE_ON_COUNTER_ZERO);

    // EPWM2 e EPWM3 atuam como Escravos (sincronizados em fase 0 com o EPWM1)
    EPWM_enablePhaseShiftLoad(EPWM2_BASE);
    EPWM_setPhaseShift(EPWM2_BASE, 0U);
    EPWM_enablePhaseShiftLoad(EPWM3_BASE);
    EPWM_setPhaseShift(EPWM3_BASE, 0U);

    // 7. Configuração de Disparo de Interrupção
    // Interrompe quando o contador do EPWM1 chegar a zero
    EPWM_setInterruptSource(EPWM1_BASE, EPWM_INT_TBCTR_ZERO);
    EPWM_setInterruptEventCount(EPWM1_BASE, 1U);
    EPWM_enableInterrupt(EPWM1_BASE);

    // Armazena PRD/2 para evitar divisão em tempo de execução dentro da ISR
    periodo_meio = EPWM_getTimeBasePeriod(EPWM1_BASE) / 2;

    // 8. Inicialização Final
    // Retoma o clock dos periféricos (os timers começam a contar de forma síncrona)
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);

    Interrupt_enable(INT_EPWM1);

    // Habilita interrupções globais de CPU e tempo real
    EINT;
    ERTM;

    // 9. Loop Infinito (Background Loop)
    for(;;)
    {
        // Operações não-críticas podem ser adicionadas aqui
        asm ("  NOP");
    }
}

//=============================================================================
// FUNÇÕES AUXILIARES
//=============================================================================

/**
 * @brief Configura a sub-rotina Dead-Band para gerar sinais complementares.
 *
 * Esta função utiliza a saída A (EPWMxA) como referência para ambas as bordas,
 * aplicando modo Active High Complementary (AHC). O canal B é invertido em
 * hardware, dispensando cálculos adicionais de software para o canal complementar.
 *
 * @param base Endereço base do módulo EPWM (ex: EPWM1_BASE).
 * @param deadTimeTics Atraso do tempo morto em ciclos de clock do EPWM.
 */
void ConfiguraSinaisComplementares(uint32_t base, uint16_t deadTimeTics)
{
    // Atribui o sinal EPWMxA como fonte para bordas de subida (RED) e descida (FED)
    EPWM_setRisingEdgeDeadBandDelayInput(base, EPWM_DB_INPUT_EPWMA);
    EPWM_setFallingEdgeDeadBandDelayInput(base, EPWM_DB_INPUT_EPWMA);

    // Configura Modo Active High Complementary (Sinal A = Não invertido; Sinal B = Invertido)
    EPWM_setDeadBandDelayPolarity(base, EPWM_DB_RED, EPWM_DB_POLARITY_ACTIVE_HIGH);
    EPWM_setDeadBandDelayPolarity(base, EPWM_DB_FED, EPWM_DB_POLARITY_ACTIVE_LOW);

    // Habilita os blocos de atraso
    EPWM_setDeadBandDelayMode(base, EPWM_DB_RED, true);
    EPWM_setDeadBandDelayMode(base, EPWM_DB_FED, true);

    // Configura os contadores de hardware para o tempo morto estipulado
    EPWM_setRisingEdgeDelayCount(base, deadTimeTics);
    EPWM_setFallingEdgeDelayCount(base, deadTimeTics);
}

//=============================================================================
// ROTINA DE TRATAMENTO DE INTERRUPÇÃO (ISR)
//=============================================================================

/**
 * @brief Interrupção do Módulo EPWM1 - Calcula as larguras de pulso da modulação senoidal.
 *
 * Executada a cada ciclo do PWM (Counter = 0). Mapeia a tabela de seno
 * para os registradores de comparação (CMPA), criando a modulação SPWM.
 */
__interrupt void epwm1ISR(void)
{
    // 1. Extração dos índices defasados (120 graus elétricos)
    uint16_t idxA = (uint16_t)indice_atual;
    uint16_t idxB = (idxA + DEFASAMENTO_120) % TAMANHO_LUT;
    uint16_t idxC = (idxA + (2 * DEFASAMENTO_120)) % TAMANHO_LUT;

    // 2. Coleta dos valores da senoide na Lookup Table (-1.0 a 1.0)
    float sinA = SENO_LUT[idxA];
    float sinB = SENO_LUT[idxB];
    float sinC = SENO_LUT[idxC];

    // 3. Atualização do índice base para a próxima amostragem (avanço de fase)
    indice_atual += PASSO_INDICE;
    if(indice_atual >= (float)TAMANHO_LUT)
    {
        indice_atual -= (float)TAMANHO_LUT; // Overflow seguro (Wrap-around)
    }

    // 4. Cálculo de Modulação e Duty Cycle
    // Fórmula: CMPA = (PRD/2) * (1 + (Modulação * Sen(theta)))
    // Desloca e escala o sinal senoidal (originalmente bipolar) para a base do contador unipolar.
    uint16_t cmpA = (uint16_t)(periodo_meio * (1.0f + (indice_modulacao * sinA)));
    uint16_t cmpB = (uint16_t)(periodo_meio * (1.0f + (indice_modulacao * sinB)));
    uint16_t cmpC = (uint16_t)(periodo_meio * (1.0f + (indice_modulacao * sinC)));

    // 5. Atualização dos Registradores de Comparação (Duty Cycle da fase em vigor)
    // O canal B é gerido por hardware via bloco Dead-Band configurado previamente.
    EPWM_setCounterCompareValue(EPWM1_BASE, EPWM_COUNTER_COMPARE_A, cmpA);
    EPWM_setCounterCompareValue(EPWM2_BASE, EPWM_COUNTER_COMPARE_A, cmpB);
    EPWM_setCounterCompareValue(EPWM3_BASE, EPWM_COUNTER_COMPARE_A, cmpC);

    // 6. Reconhecimento de Interrupção (Limpeza de Flags)
    EPWM_clearEventTriggerInterruptFlag(EPWM1_BASE);
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP3); // Reconhece ao PIE
}

//
// End of File
//
