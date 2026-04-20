# ⚡ Gerador SPWM Trifásico Dinâmico

Este repositório contém a implementação de um gerador de Modulação por Largura de Pulso Senoidal (SPWM) trifásico para a família de microcontroladores Texas Instruments C2000 (testado na série F2837xD). 

Diferente das implementações tradicionais baseadas em tabelas de busca (*Lookup Tables* - LUT), este código utiliza um **Acumulador de Fase** e a biblioteca matemática nativa para calcular os ângulos em tempo real. Isso permite o controle dinâmico da frequência e amplitude da onda.

## ✨ Funcionalidades

* **Controle Dinâmico:** Ajuste da frequência moduladora e do índice de modulação (amplitude) em tempo de execução.
* **Tempo Morto por Hardware (Dead-Band):** Injeção de tempo morto configurável nativamente pelo módulo EPWM, evitando curto-circuito na ponte inversora (*shoot-through*).
* **Sinais Complementares:** Geração automática do sinal invertido para os interruptores inferiores da ponte (Modo Active High Complementary).
* **Sincronização de Fase:** Defasagem exata de 120 graus elétricos entre as fases A, B e C.
* **Alta Performance:** Utilização da unidade TMU (*Trigonometric Math Unit*) para cálculos de seno em tempo real de altíssima velocidade.

## 📌 Mapeamento de Hardware (Pinos)

| Fase | EPWM Master | EPWM Complementar | Módulo |
| :--- | :--- | :--- | :--- |
| **Fase A** | GPIO0 (Pino 40) | GPIO1 (Pino 39) | EPWM1 |
| **Fase B** | GPIO2 (Pino 38) | GPIO3 (Pino 37) | EPWM2 |
| **Fase C** | GPIO4 (Pino 36) | GPIO5 (Pino 35) | EPWM3 |

## 🧠 Lógica do Algoritmo: Acumulador de Fase e Geração da Senoide

A geração da modulação senoidal (SPWM) neste projeto dispensa o uso de tabelas de seno pré-calculadas (*Lookup Tables*). Em vez disso, o algoritmo calcula o valor exato da onda em tempo real a cada ciclo do PWM utilizando o conceito de **Acumulador de Fase**.

O processo ocorre dentro da Rotina de Tratamento de Interrupção (ISR) do EPWM, repetindo-se na frequência da portadora (ex: 15 kHz), seguindo estes passos lógicos:

**1. Cálculo do Incremento Angular ($\Delta\theta$)**
A cada interrupção, o algoritmo determina o quanto o ângulo da senoide deve avançar. Esse "passo" é dinâmico e depende da frequência desejada para a onda ($f_{mod}$) e da frequência fixa do timer PWM ($f_{pwm}$):
> $\Delta\theta = 2\pi \cdot \left(\frac{f_{mod}}{f_{pwm}}\right)$

**2. Acumulação de Fase (Integração)**
O incremento é somado ao ângulo atual da Fase A ($\theta$). Para evitar overflow e manter a coerência matemática, se o ângulo atingir $2\pi$ (um ciclo completo), subtrai-se $2\pi$ dele (*wrap-around*), reiniciando o ciclo suavemente.

**3. Defasagem Trifásica**
Com o ângulo base da Fase A definido, os ângulos das Fases B e C são encontrados aplicando uma subtração e adição direta de 120 graus elétricos ($\frac{2\pi}{3}$ radianos):
* `Ângulo B = Ângulo A - 120°`
* `Ângulo C = Ângulo A + 120°`

**4. Extração da Senoide**
Os três ângulos são passados para a função matemática `sinf()`, gerando os valores instantâneos da senoide pura, variando de `-1.0` a `1.0`. *(Nota: O hardware FPU/TMU do C2000 resolve isso em poucos ciclos de clock).*

**5. Cálculo do Duty Cycle e Modulação**
A senoide bipolar precisa ser "esmagada" e deslocada para caber no contador unipolar do Timer do PWM. Aplicamos o **Índice de Modulação** (que controla a amplitude/tensão) e escalamos o valor para o período do registrador:
> $CMPA = \frac{PRD}{2} \cdot (1 + (Amplitude \cdot \sin(\theta)))$

O resultado ($CMPA$) é carregado no registrador de comparação do EPWM, determinando a largura exata do pulso digital (Duty Cycle) para aquele micro-instante de tempo.

## 🚀 Como Utilizar no Loop Principal

As variáveis de controle são globais e voláteis. Você pode alterá-las em qualquer parte do seu código (como no `main()` ou via comunicação serial/ADC):

```c
// Modificando a frequência da onda gerada para 50 Hz
freq_moduladora = 50.0f;

// Alterando a amplitude do sinal (tensão) para 80%
indice_modulacao = 0.8f;
