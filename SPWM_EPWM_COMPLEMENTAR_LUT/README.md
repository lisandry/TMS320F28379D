# Geração de SPWM Trifásico com Tempo Morto (Dead-Band)

Este submódulo contém a implementação em linguagem C para a geração de Modulação por Largura de Pulso Senoidal (SPWM) trifásica utilizando o microcontrolador TMS320F28379D. O código foi desenvolvido para acionar inversores trifásicos (pontes inversoras completas), garantindo segurança via hardware contra curtos-circuitos (shoot-through).

## 📋 Visão Geral do Módulo

O sistema gera três pares de sinais PWM defasados em 120 graus elétricos. Cada fase (A, B e C) possui um sinal principal e um sinal complementar. A principal vantagem desta implementação é a geração do sinal complementar e a inserção do tempo morto realizadas **exclusivamente via hardware** pelo submódulo Dead-Band (DB) do ePWM, poupando processamento da CPU.

### Principais Características
* **Frequência de Chaveamento:** 15 kHz (configurável via `FREQ_PWM`).
* **Modo de Contagem:** *Up-Down* (Geração de PWM simétrico).
* **Tabela de Referência (LUT):** 256 pontos pré-calculados contendo um ciclo completo de uma onda senoidal normalizada (-1.0 a 1.0).
* **Tempo Morto (Dead-Band):** Injeção de 100 *ticks* de clock configurados nos blocos RED (Rising Edge Delay) e FED (Falling Edge Delay).
* **Sincronização:** O módulo EPWM1 atua como "Mestre", gerando o pulso de sincronismo que mantém os módulos EPWM2 e EPWM3 perfeitamente alinhados (Phase Shift = 0).

## 📍 Mapeamento de Hardware (Pinout)

O roteamento dos sinais foi configurado via *Pinmux* para as seguintes portas GPIO:

| Fase | Módulo ePWM | Sinal | GPIO | Direção |
| :--- | :--- | :--- | :--- | :--- |
| **A** | EPWM1A | Principal | GPIO0 (Pino 40) | Saída |
| **A** | EPWM1B | Complementar | GPIO1 (Pino 39) | Saída |
| **B** | EPWM2A | Principal | GPIO2 (Pino 38) | Saída |
| **B** | EPWM2B | Complementar | GPIO3 (Pino 37) | Saída |
| **C** | EPWM3A | Principal | GPIO4 (Pino 36) | Saída |
| **C** | EPWM3B | Complementar | GPIO5 (Pino 35) | Saída |

## ⚙️ Arquitetura do Firmware

### 1. Configuração de Sinais Complementares (Modo AHC)
O código utiliza a função `ConfiguraSinaisComplementares()` para configurar o submódulo Dead-Band no modo *Active High Complementary* (AHC). Neste modo:
* O sinal de entrada (`EPWMxA`) é utilizado como referência.
* O canal A reproduz o sinal com atraso na borda de subida.
* O canal B é invertido automaticamente pelo hardware e recebe atraso na borda de descida.

### 2. Rotina de Serviço de Interrupção (ISR)
A malha de execução crítica está contida na função `epwm1ISR()`. Esta rotina é disparada pelo hardware a cada ciclo de PWM, exatamente no instante em que o contador atinge zero (`EPWM_INT_TBCTR_ZERO`), garantindo atualização determinística do *Duty Cycle*.

O processamento segue três etapas lógicas:
1. **Avanço de Fase:** O índice base percorre a *Lookup Table* baseado no `PASSO_INDICE` calculado pela razão entre a frequência moduladora desejada e a frequência portadora.
2. **Coleta de Defasagem:** Os valores para as fases B e C são obtidos somando uma constante de defasagem (85 pontos na LUT) equivalente a $120^\circ$ elétricos.
3. **Cálculo da Modulação:** O sinal senoidal normalizado é mapeado para a base do contador unipolar de 16 bits do ePWM. A equação matemática processada para atualização do registrador de comparação (CMPA) é:

$$CMPA = \frac{PRD}{2} \cdot [1 + (m \cdot \sin(\theta))]$$

Onde:
* $PRD$ é o período total do temporizador (`EPWM_getTimeBasePeriod`).
* $m$ é o índice de modulação de amplitude (`indice_modulacao` ajustável de 0.0 a 1.0).
* $\sin(\theta)$ é o valor extraído da *Lookup Table*.

## 🚀 Como Integrar e Executar

1. Adicione o arquivo `SPWM_EPWM.c` ao diretório `src/` do seu projeto no Code Composer Studio.
2. Certifique-se de que os arquivos de cabeçalho `driverlib.h` e `device.h` estão devidamente incluídos nos *include paths* do compilador.
3. A função `main()` contida neste arquivo inicializa o sistema, os registradores periféricos e entra em *loop* infinito. Caso este módulo faça parte de um projeto maior, extraia a lógica de inicialização para sua rotina de configuração principal e mantenha a ISR para ser chamada pelo Peripheral Interrupt Expansion (PIE).
