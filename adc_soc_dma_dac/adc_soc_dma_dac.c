//#############################################################################
//
// FILE:  adc_soc_bypass_corrigido.c
// TITLE:  Gerador no Pino 30 -> Microcontrolador -> Osciloscópio no Pino 70
//
//#############################################################################

#include "driverlib.h"
#include "device.h"

__interrupt void adcA1ISR(void);

void configureGPIO(void);
void configureADC(uint32_t adcBase);
void setupADCSingleTarget(uint32_t adcBase, uint32_t channel, uint16_t acqps);
void configureEPWM(uint32_t epwmBase);
void configureDAC(uint32_t dacBase);

void main(void)
{
    Device_init();
    Device_initGPIO();
    configureGPIO();

    Interrupt_initModule();
    Interrupt_initVectorTable();

    Interrupt_register(INT_ADCA1, &adcA1ISR);
    Interrupt_enable(INT_ADCA1);

    // ==========================================================
    // 1. ENTRADA: O seu gerador está no PINO 30 (Canal 0 do ADCA)
    // ==========================================================
    configureADC(ADCA_BASE);
    setupADCSingleTarget(ADCA_BASE, 0U, 19U); // Canal 0U = Pino 30

    // ==========================================================
    // 2. SAÍDA: Vamos cuspir a onda no PINO 70 (Módulo DACB)
    // ==========================================================
    configureDAC(DACB_BASE);

    SysCtl_disablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);
    configureEPWM(EPWM2_BASE);
    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);

    HWREGH(ADCA_BASE + ADC_O_INTFLGCLR) = 0x3U;
    EPWM_forceADCTriggerEventCountInit(EPWM2_BASE, EPWM_SOC_A);
    EPWM_clearADCTriggerFlag(EPWM2_BASE, EPWM_SOC_A);

    EINT;
    ERTM;

    EPWM_enableADCTrigger(EPWM2_BASE, EPWM_SOC_A);

    while(1) {
        // CPU livre
    }
}

// ----------------------------------------------------------------------------
// INTERRUPÇÃO: FIO DIRETO (Bypass) a 50 kHz
// ----------------------------------------------------------------------------
#pragma CODE_SECTION(adcA1ISR, ".TI.ramfunc");
__interrupt void adcA1ISR(void)
{
    // Lê o que está a entrar no Pino 30 (ADCINA0)
    uint16_t valor_bruto = ADC_readResult(ADCARESULT_BASE, ADC_SOC_NUMBER0);

    // Cospe imediatamente no Pino 70 (DACOUTB)
    DAC_setShadowValue(DACB_BASE, valor_bruto);

    ADC_clearInterruptStatus(ADCA_BASE, ADC_INT_NUMBER1);
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}

// ----------------------------------------------------------------------------
// CONFIGURAÇÕES DE HARDWARE
// ----------------------------------------------------------------------------
void configureGPIO(void)
{
    // Pino 38 (Diagnóstico do Clock a 50kHz)
    GPIO_setPadConfig(2, GPIO_PIN_TYPE_STD);
    GPIO_setPinConfig(GPIO_2_EPWM2A);
}

void configureDAC(uint32_t dacBase)
{
    // LIGA A ENERGIA DO DACB (Pino 70)
    if(dacBase == DACB_BASE) {
        SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_DACB);
    }

    DAC_setReferenceVoltage(dacBase, DAC_REF_ADC_VREFHI);
    DAC_setLoadMode(dacBase, DAC_LOAD_SYSCLK);
    DAC_enableOutput(dacBase); // Ativa o amplificador físico que vai para o Pino 70
    DAC_setShadowValue(dacBase, 0U);
    DEVICE_DELAY_US(10);
}

void configureEPWM(uint32_t epwmBase)
{
    HWREGH(epwmBase + EPWM_O_TBCTL) = 0x0000U;

    // 50 kHz Amostragem
    EPWM_setTimeBasePeriod(epwmBase, 1000U);
    EPWM_setActionQualifierAction(epwmBase, EPWM_AQ_OUTPUT_A, EPWM_AQ_OUTPUT_HIGH, EPWM_AQ_OUTPUT_ON_TIMEBASE_ZERO);
    EPWM_setActionQualifierAction(epwmBase, EPWM_AQ_OUTPUT_A, EPWM_AQ_OUTPUT_LOW, EPWM_AQ_OUTPUT_ON_TIMEBASE_UP_CMPA);
    EPWM_setCounterCompareValue(epwmBase, EPWM_COUNTER_COMPARE_A, 500U);
    EPWM_setADCTriggerSource(epwmBase, EPWM_SOC_A, EPWM_SOC_TBCTR_ZERO);
    EPWM_setADCTriggerEventPrescale(epwmBase, EPWM_SOC_A, 1U);
}

void configureADC(uint32_t adcBase)
{
    ADC_setPrescaler(adcBase, ADC_CLK_DIV_4_0);
    ADC_setMode(adcBase, ADC_RESOLUTION_12BIT, ADC_MODE_SINGLE_ENDED);
    ADC_setInterruptPulseMode(adcBase, ADC_PULSE_END_OF_CONV);
    ADC_enableConverter(adcBase);
    DEVICE_DELAY_US(1000);
}

void setupADCSingleTarget(uint32_t adcBase, uint32_t channel, uint16_t acqps)
{
    ADC_setupSOC(adcBase, ADC_SOC_NUMBER0, ADC_TRIGGER_EPWM2_SOCA, (ADC_Channel)channel, acqps);
    ADC_setInterruptSource(adcBase, ADC_INT_NUMBER1, ADC_SOC_NUMBER0);
    ADC_enableContinuousMode(adcBase, ADC_INT_NUMBER1);
    ADC_enableInterrupt(adcBase, ADC_INT_NUMBER1);
    ADC_clearInterruptStatus(adcBase, ADC_INT_NUMBER1);
}
