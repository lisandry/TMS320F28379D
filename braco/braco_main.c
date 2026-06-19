#include "driverlib.h"
#include "device.h"
#include <math.h>

#define PI       3.14159265358979f
#define DOIS_PI  6.28318530717958f
#define FASE_180 PI
#define FREQ_PWM 10000U

volatile float freq_moduladora = 60.0f;
volatile float indice_modulacao = -1.0f;
float theta = 0.0f;
uint16_t periodo_meio = 0;

int bufferADC[3000];

// Protótipos
__interrupt void epwm1ISR(void);
void ConfiguraBaseEPWM(uint32_t base);
void ConfiguraSinaisComplementares(uint32_t base, uint16_t deadTimeTics);


//__interrupt void adcA1ISR(void);
//
//void configureGPIO(void);
//void configureADC(uint32_t adcBase);
//void setupADCSingleTarget(uint32_t adcBase, uint32_t channel, uint16_t acqps);
//void configureEPWM(uint32_t epwmBase);
//void configureDAC(uint32_t dacBase);

void main(void)
{
    Device_init();
    Device_initGPIO();
    Interrupt_initModule();
    Interrupt_initVectorTable();

    GPIO_setPadConfig(0, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(GPIO_0_EPWM1A);
    GPIO_setPadConfig(1, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(GPIO_1_EPWM1B);

   GPIO_setPadConfig(2, GPIO_PIN_TYPE_STD);
   GPIO_setPinConfig(GPIO_2_EPWM2A);
   GPIO_setPadConfig(3, GPIO_PIN_TYPE_STD);
   GPIO_setPinConfig(GPIO_3_EPWM2B);

    // Pino de debug da ISR
    GPIO_setPadConfig(7, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(GPIO_7_GPIO7);
    GPIO_setDirectionMode(7, GPIO_DIR_MODE_OUT);
    GPIO_writePin(7, 0);

    Interrupt_register(INT_EPWM1, &epwm1ISR);
    SysCtl_disablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);

    ConfiguraBaseEPWM(EPWM1_BASE);
    ConfiguraBaseEPWM(EPWM2_BASE);

    // 3. Configuração do Tempo Morto (Dead-Band)
    ConfiguraSinaisComplementares(EPWM1_BASE, 100);
    ConfiguraSinaisComplementares(EPWM2_BASE, 100);

    // 4. Sincronização de Fase entre os Módulos
    EPWM_disablePhaseShiftLoad(EPWM1_BASE);
    EPWM_setPhaseShift(EPWM1_BASE, 0U);
    EPWM_setSyncOutPulseMode(EPWM1_BASE, EPWM_SYNC_OUT_PULSE_ON_COUNTER_ZERO);

    EPWM_enablePhaseShiftLoad(EPWM2_BASE);
    EPWM_setPhaseShift(EPWM2_BASE, 0U);

    EPWM_setInterruptSource(EPWM1_BASE, EPWM_INT_TBCTR_ZERO);
    EPWM_setInterruptEventCount(EPWM1_BASE, 1U);
    EPWM_enableInterrupt(EPWM1_BASE);






    periodo_meio = EPWM_getTimeBasePeriod(EPWM1_BASE) / 2;

    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);
    Interrupt_enable(INT_EPWM1);

//
//    Interrupt_register(INT_ADCA1, &adcA1ISR);
//       Interrupt_enable(INT_ADCA1);
//
//       // ==========================================================
//       // 1. ENTRADA: O seu gerador está no PINO 30 (Canal 0 do ADCA)
//       // ==========================================================
//       configureADC(ADCA_BASE);
//       setupADCSingleTarget(ADCA_BASE, 0U, 19U); // Canal 0U = Pino 30
//
//       // ==========================================================
//       // 2. SAÍDA: Vamos cuspir a onda no PINO 70 (Módulo DACB)
//       // ==========================================================
//       configureDAC(DACB_BASE);
//
//       SysCtl_disablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);
//       configureEPWM(EPWM2_BASE);
//       SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);
//
//       HWREGH(ADCA_BASE + ADC_O_INTFLGCLR) = 0x3U;
//       EPWM_forceADCTriggerEventCountInit(EPWM2_BASE, EPWM_SOC_A);
//       EPWM_clearADCTriggerFlag(EPWM2_BASE, EPWM_SOC_A);

    EINT;
    ERTM;

//    EPWM_enableADCTrigger(EPWM2_BASE, EPWM_SOC_A);

    for(;;) {
        asm ("  NOP");
    }
}

void ConfiguraBaseEPWM(uint32_t base)
{
    // 1. Cálculo do Período (TBPRD)
    // Assumindo SYSCLK = 200 MHz e EPWMCLK = 100 MHz (divisão padrão por 2)
    // Fórmula modo Up-Down: TBPRD = EPWMCLK / (2 * FREQ_PWM)
    uint32_t epwmClkFreq = 100000000U;
    uint16_t tbprd = (uint16_t)(epwmClkFreq / (4U * FREQ_PWM));

    // 2. Configura a Base de Tempo
    EPWM_setTimeBasePeriod(base, tbprd);
    EPWM_setPhaseShift(base, 0U);
    EPWM_setTimeBaseCounter(base, 0U);
    EPWM_setTimeBaseCounterMode(base, EPWM_COUNTER_MODE_UP_DOWN);

    EPWM_setClockPrescaler(base, EPWM_CLOCK_DIVIDER_1, EPWM_HSCLOCK_DIVIDER_1);


    EPWM_setCounterCompareValue(base, EPWM_COUNTER_COMPARE_A, tbprd / 4U);

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
    GPIO_writePin(7, 1);

    // 1. Calcula o passo angular
    float delta_theta = DOIS_PI * freq_moduladora / (float)FREQ_PWM;

    // 2. Acumulador de fase
    theta += delta_theta;
    if(theta >= DOIS_PI)
    {
        theta -= DOIS_PI;
    }

    // 3. Calcula os ângulos defasados em 180º (Ponte H)
    float theta_A = theta;
    float theta_B = theta_A - FASE_180;

    if(theta_B < 0.0f)
    {
        theta_B += DOIS_PI;
    }

    // 4. Extrai os valores senoidais (-1.0 a 1.0)
    // Nota matemática: sinB será sempre o inverso de sinA (-sinA)
    float sinA = sinf(theta_A);
    float sinB = sinf(theta_B);

    // 5. Calcula o Duty Cycle e ajusta para o contador unipolar do PWM
    uint16_t cmpA = (uint16_t)(periodo_meio * (1.0f + (indice_modulacao)));// sinA)));
    uint16_t cmpB = (uint16_t)(periodo_meio * (1.0f + (indice_modulacao)));// * sinB)));

    // 6. Atualiza os Registradores de Comparação
    EPWM_setCounterCompareValue(EPWM1_BASE, EPWM_COUNTER_COMPARE_A, cmpA);
    EPWM_setCounterCompareValue(EPWM2_BASE, EPWM_COUNTER_COMPARE_A, cmpB);

    // 7. Reconhecimento da interrupção
    EPWM_clearEventTriggerInterruptFlag(EPWM1_BASE);
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP3);

    GPIO_writePin(7, 0);
}

//// ----------------------------------------------------------------------------
//// INTERRUPÇÃO: FIO DIRETO (Bypass) a 50 kHz
//// ----------------------------------------------------------------------------
//#pragma CODE_SECTION(adcA1ISR, ".TI.ramfunc");
//__interrupt void adcA1ISR(void)
//{
//    // Lê o que está a entrar no Pino 30 (ADCINA0)
//    uint16_t valor_bruto = ADC_readResult(ADCARESULT_BASE, ADC_SOC_NUMBER0);
//    int i=0;
//    for(i=0; i<=3000; i++){
//    bufferADC[i] = valor_bruto;
//    }
//
//    // Cospe imediatamente no Pino 70 (DACOUTB)
//    DAC_setShadowValue(DACB_BASE, valor_bruto);
//
//    ADC_clearInterruptStatus(ADCA_BASE, ADC_INT_NUMBER1);
//    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
//}
//
//// ----------------------------------------------------------------------------
//// CONFIGURAÇÕES DE HARDWARE
//// ----------------------------------------------------------------------------
//void configureGPIO(void)
//{
//    // Pino 38 (Diagnóstico do Clock a 50kHz)
//    GPIO_setPadConfig(2, GPIO_PIN_TYPE_STD);
//    GPIO_setPinConfig(GPIO_2_EPWM2A);
//}
//
//void configureDAC(uint32_t dacBase)
//{
//    // LIGA A ENERGIA DO DACB (Pino 70)
//    if(dacBase == DACB_BASE) {
//        SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_DACB);
//    }
//
//    DAC_setReferenceVoltage(dacBase, DAC_REF_ADC_VREFHI);
//    DAC_setLoadMode(dacBase, DAC_LOAD_SYSCLK);
//    DAC_enableOutput(dacBase); // Ativa o amplificador físico que vai para o Pino 70
//    DAC_setShadowValue(dacBase, 0U);
//    DEVICE_DELAY_US(10);
//}
//
//void configureEPWM(uint32_t epwmBase)
//{
//    HWREGH(epwmBase + EPWM_O_TBCTL) = 0x0000U;
//
//    // 50 kHz Amostragem
//    EPWM_setTimeBasePeriod(epwmBase, 1000U);
//    EPWM_setActionQualifierAction(epwmBase, EPWM_AQ_OUTPUT_A, EPWM_AQ_OUTPUT_HIGH, EPWM_AQ_OUTPUT_ON_TIMEBASE_ZERO);
//    EPWM_setActionQualifierAction(epwmBase, EPWM_AQ_OUTPUT_A, EPWM_AQ_OUTPUT_LOW, EPWM_AQ_OUTPUT_ON_TIMEBASE_UP_CMPA);
//    EPWM_setCounterCompareValue(epwmBase, EPWM_COUNTER_COMPARE_A, 500U);
//    EPWM_setADCTriggerSource(epwmBase, EPWM_SOC_A, EPWM_SOC_TBCTR_ZERO);
//    EPWM_setADCTriggerEventPrescale(epwmBase, EPWM_SOC_A, 1U);
//}
//
//void configureADC(uint32_t adcBase)
//{
//    ADC_setPrescaler(adcBase, ADC_CLK_DIV_4_0);
//    ADC_setMode(adcBase, ADC_RESOLUTION_12BIT, ADC_MODE_SINGLE_ENDED);
//    ADC_setInterruptPulseMode(adcBase, ADC_PULSE_END_OF_CONV);
//    ADC_enableConverter(adcBase);
//    DEVICE_DELAY_US(1000);
//}
//
//void setupADCSingleTarget(uint32_t adcBase, uint32_t channel, uint16_t acqps)
//{
//    ADC_setupSOC(adcBase, ADC_SOC_NUMBER0, ADC_TRIGGER_EPWM2_SOCA, (ADC_Channel)channel, acqps);
//    ADC_setInterruptSource(adcBase, ADC_INT_NUMBER1, ADC_SOC_NUMBER0);
//    ADC_enableContinuousMode(adcBase, ADC_INT_NUMBER1);
//    ADC_enableInterrupt(adcBase, ADC_INT_NUMBER1);
//    ADC_clearInterruptStatus(adcBase, ADC_INT_NUMBER1);
//}

