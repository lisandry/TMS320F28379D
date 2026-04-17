# Desenvolvimento de Sistemas de Controle Digital com TMS320F28379D

Este repositório centraliza os desenvolvimentos, bibliotecas e validações experimentais de uma arquitetura de controle digital em tempo real baseada no microcontrolador **Texas Instruments TMS320F28379D**. O foco principal dos projetos aqui hospedados abrange aplicações avançadas em eletrônica de potência e controle de acionamentos elétricos.

A infraestrutura de software foi projetada para garantir processamento rápido e determinístico, explorando os periféricos de hardware do dispositivo para contornar as limitações de latência encontradas em arquiteturas de uso geral.

## 🎯 Visão Geral dos Projetos e Aplicações

Os projetos desenvolvidos neste repositório visam resolver demandas críticas em ambientes industriais e sistemas de energia. As implementações estão direcionadas para:
* **Conversores Estáticos de Potência:** Implementação de malhas fechadas para controle preciso de corrente e tensão, utilizando técnicas de Modulação por Largura de Pulso (PWM) simétrica para evitar contaminação por ruído de alta frequência no chaveamento de IGBTs e MOSFETs.
* **Acionamentos Elétricos Complexos:** Controle de motores de alta precisão com suporte a geração de tempos mortos (dead-band) configurados via hardware para evitar curto-circuitos (shoot-through).
* **Processamento Digital de Sinais e Aquisição Síncrona:** Reconstrução digital fiel de sinais sem descontinuidades, isolando os domínios de comutação e analógico de forma autônoma.

## 🧠 Arquitetura do Microcontrolador (Família C2000 Delfino)

O sistema utiliza a arquitetura otimizada do TMS320F28379D, que possui as seguintes especificações exploradas nos códigos:

* **Processamento Dual-Core:** Dois núcleos independentes de 32 bits (C28x) operando a 200 MHz.
* **Aceleração Matemática:** Unidades de Ponto Flutuante (FPU) e Aceleradores Trigonométricos (TMU) integrados para alto desempenho computacional.
* **Control Law Accelerator (CLA):** Coprocessador independente a 220 MHz destinado a descarregar as CPUs da execução de tarefas matemáticas repetitivas.
* **Gestão de Clock:** Arquitetura de duplo PLL, com o System PLL fornecendo 200 MHz para a malha de controle e o Auxiliary PLL gerando um domínio de 60 MHz para periféricos de comunicação.
* **Memória:** Até 1 MB de Flash interna e 204 KB de RAM (SRAM) para alocação crítica de dados e instruções.

## ⚙️ Coordenação de Periféricos e Estratégias de Tempo Real

Este repositório afasta-se de abordagens baseadas exclusivamente em interrupções de software (como CPU Timers), pois estas estão sujeitas a variações (jitter) sob alta carga de processamento. Em vez disso, os projetos focam na **sincronização via hardware**, coordenando os seguintes periféricos:

### 1. Enhanced Pulse Width Modulator (ePWM)
O microcontrolador dispõe de um sistema ePWM com 24 canais, dividido em submódulos autônomos (Time Base, Action Qualifier, Dead-Band, etc.). Nos projetos, o ePWM atua de duas formas principais:
* **Geração de Sinal:** Modulação dinâmica das saídas físicas (EPWMxA e EPWMxB) e criação de tempos mortos via submódulo DB.
* **Sincronização Mestre:** O submódulo Event Trigger (ET) gera disparos precisos (Start-of-Conversion - SOC) para o ADC a cada ciclo do contador (ex: a 25 kHz), eliminando intervenções da CPU.

### 2. Analog-to-Digital Converter (ADC)
A aquisição de dados conta com quatro ADCs independentes baseados no algoritmo de aproximações sucessivas (SAR).
* **Arquitetura SOC Independente:** Diferente de arquiteturas tradicionais, são utilizados dezesseis unidades SOC (Start-of-Conversion) por módulo, permitindo janelas de aquisição específicas para cada leitura.
* **Amostragem no Centro do Período:** Ao receber o disparo (trigger) diretamente do ePWM, os sinais são amostrados com alinhamento de fase fixo. Isso assegura que a leitura das correntes e tensões ocorra livre dos transientes eletromagnéticos de chaveamento.

### 3. General Purpose Input/Output (GPIO)
* **Multiplexação Avançada:** Organização em seis portas principais mapeadas para até 12 funções periféricas por pino, oferecendo flexibilidade extrema para roteamento de sinais.
* **Validação Temporal:** Os pinos de saída digital são manipulados via registradores de controle e empregados para instrumentação e validação da latência das Rotinas de Serviço de Interrupção (ISRs) diretamente via osciloscópio.

## 🛠️ Ambiente de Desenvolvimento e Dependências

* **Ambiente de Desenvolvimento Integrado (IDE):** Code Composer Studio (CCS).
* **Toolchain:** TMS320C28x Optimizing C/C++ Compiler.
* **Bibliotecas (SDK):** C2000Ware (Drivers e Device Support).
* **Depuração:** Boot de emulação (EMU Boot) via interface JTAG, utilizando os pinos TRST, TMS, TDI, TDO e TCK.

## 📄 Autoria

Desenvolvimentos baseados em pesquisa para sistemas de controle digital.

* **Autora:** Azuaje F. Lisandry Paola
