# Aquisição e Reconstrução de Sinal em Tempo Real (ADC-to-DAC Bypass)

Este módulo implementa uma arquitetura de passagem direta (*Bypass*) no microcontrolador TMS320F28379D. O sistema realiza a amostragem de um sinal analógico (proveniente de um gerador de funções), processa a interrupção com latência ultrabaixa e reconstrói o sinal instantaneamente utilizando o conversor digital-analógico (DAC) interno.

Este código é fundamental para validar a latência da malha de controle, a integridade do conversor A/D e o determinismo do disparo por hardware via ePWM.

## 📋 Visão Geral do Fluxo

O funcionamento baseia-se em uma cadeia de eventos estritamente controlada por hardware:
1. O temporizador **ePWM2** gera um pulso base na frequência exata de **50 kHz**.
2. Esse pulso dispara automaticamente o **ADCA** (via evento SOC - *Start of Conversion*), que realiza a leitura de 12 bits do sinal de entrada.
3. Ao fim da conversão (EOC), uma interrupção é gerada.
4. A rotina de serviço (ISR) copia o valor digital do registrador de resultado do ADC diretamente para o registrador do **DACB**, reconstruindo o sinal.

## 📍 Mapeamento de Hardware (Pinout)

O sistema foi configurado para utilizar pinos físicos específicos na placa de desenvolvimento, facilitando a instrumentação direta com geradores de sinais e osciloscópios.

| Periférico | Canal / Função | Pino Físico | Direção | Descrição |
| :--- | :--- | :--- | :--- | :--- |
| **ADC** | ADCA - Canal 0 | **Pino 30** | Entrada | Recebe o sinal analógico do gerador de funções. |
| **DAC** | DACB - Saída | **Pino 70** | Saída | Cospe o sinal reconstruído para o osciloscópio. |
| **ePWM** | EPWM2A | **Pino 38** | Saída | Sinal digital de diagnóstico (onda quadrada a 50 kHz) para validação da base de tempo. |

## ⚙️ Arquitetura do Firmware e Otimizações

Para garantir o funcionamento em $50 \text{ kHz}$ sem perda de pacotes ou instabilidade (*jitter*), o código adota as seguintes estratégias de otimização de tempo real:

### 1. Disparo de Conversão via Hardware (Hardware Triggering)
O ADC **não** é disparado por software no loop principal. O submódulo *Event Trigger* do ePWM2 está configurado para emitir o sinal `EPWM_SOC_A` toda vez que o contador atinge zero (`EPWM_SOC_TBCTR_ZERO`). Isso garante alinhamento temporal perfeito entre o relógio do sistema e a amostragem analógica.

### 2. Execução da ISR na Memória RAM
A função de interrupção `adcA1ISR` é a etapa mais crítica do processo. Ela foi decorada com o pragma `#pragma CODE_SECTION(adcA1ISR, ".TI.ramfunc");`.
Isso instrui o *linker* a copiar esta função da memória Flash (mais lenta) para a memória RAM (SRAM) durante a inicialização, garantindo que a execução ocorra em *zero-wait-states* (sem estados de espera), minimizando ao máximo a latência de reconstrução do sinal entre o ADC e o DAC.

### 3. Loop Infinito (Idle Loop)
Uma vez que todo o processo de aquisição e reconstrução de sinal é governado por temporizadores de hardware e interrupções, a CPU principal (no `main`) fica totalmente livre (`while(1) { // CPU livre }`), podendo ser alocada futuramente para processar compensadores PI/PID, comunicação serial ou filtros digitais complexos sem comprometer o tempo de amostragem.

## 🚀 Como Executar e Validar

1. Compile e grave o firmware na placa TMS320F28379D.
2. Conecte um **Gerador de Funções** ao **Pino 30** (Certifique-se de que a amplitude do sinal esteja dentro dos limites do ADC: 0 a 3.0V ou 3.3V, confira a referência da placa).
3. Conecte a ponta de prova do **Osciloscópio (Canal 1)** ao **Pino 70** para visualizar a onda reconstruída pelo DAC.
4. *(Opcional)* Conecte o **Osciloscópio (Canal 2)** ao **Pino 38** para verificar o *clock* de amostragem a exatos 50 kHz, comprovando o determinismo do sistema.
