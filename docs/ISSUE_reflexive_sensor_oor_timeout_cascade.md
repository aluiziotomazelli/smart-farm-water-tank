# Issue: Sensor reflexivo gera cascata `OUT_OF_RANGE → TIMEOUT` em distâncias maiores (~69 cm)

**Status:** Causa raiz identificada — aguardando correção mecânica e/ou ajuste de configuração  
**Componente:** `ultrasonic_sensor` (config `UsConfig`) + instalação mecânica do sensor reflexivo  
**Contexto:** Levantado durante validação de campo do sensor reflexivo (2026-07-20), testes
comparativos com e sem condensação em 4 momentos distintos do dia.

---

## Contexto da instalação

O sensor ultrassônico está montado **verticalmente** dentro de uma caixinha que se sobressai
da tampa da caixa d'água. Um espelho a 45° redireciona o feixe de som horizontalmente até a
superfície da água:

```
                |\
                | \
             ___|  \
                |   \
         sensor |    \
             ___|     \ espelho 45°
                |      \
                |       \
________________|        \_______________
|               |         \             |
|               |          \            |
|                                       |
|                                       | ← caixinha do sensor (sobressaída da tampa)
|                                       |
|_______________________________________|
               tampa da caixa d'água

               caixa d'água

________________________________________________
                                      água
```

A caixinha serve para afastar o sensor da dead-zone e da umidade direta.  
`SENSOR_OFFSET_CM = 29.5 cm` (offset do sensor em relação ao topo do nível máximo de água).

---

## Resultado dos testes de campo — sensor reflexivo (2026-07-20)

| Horário | Condição | Dist. Média ± StdDev | Range Total | UsResult |
|---|---|---|---|---|
| **17:30** | Quente, sem condensação | 37.33 ± 0.35 cm | 1.7 cm | **100% OK** (294/294) |
| **23:30** | Noite, início de condensação | 51.32 ± 0.35 cm | 1.6 cm | **100% OK** (206/206) |
| **06:41** | Fria (~20°C), condensação provável | 60.01 ± 0.27 cm | 1.4 cm | **100% OK** (297/297) |
| **10:00** | Quente, sem condensação | 69.3 cm (válidos) | — | **88.1% OK, 9.5% OOR, 2.4% Weak** |

**Conclusão sobre condensação:** o sensor reflexivo se mostrou **completamente imune à
condensação** — os 3 primeiros testes (incluindo manhã fria com condensação provável) tiveram
100% de leituras válidas e estabilidade excelente. O sensor antigo em condição equivalente
apresentava desvio padrão de ~4 cm e variação total de até 19.5 cm.

O problema identificado é **exclusivo do teste das 10:00**, quando a distância de medição
aumentou para ~69 cm (caixa d'água mais consumida ao longo do dia de testes).

---

## Causa raiz: cascata `OUT_OF_RANGE → TIMEOUT`

### Padrão observado nos pings crus

```
Ping N:   0.0-3  0.0-2  69.0-0  0.0-3  0.0-2  0.0-3  0.0-2   (6 de 7 ruins)
App:      Distance: 0.0 - UsResult 3 (OUT_OF_RANGE)

Ping N+1: 68.9-0  69.0-0  69.0-0  69.0-0  69.0-0  69.0-0  69.0-0  (7/7 OK)
App:      Distance: 69.0 - UsResult 0 (OK)
```

O padrão **R T R T** (OUT_OF_RANGE=3, TIMEOUT=2) é altamente sistemático:

- **111** pings com `OUT_OF_RANGE` no log das 10:00
- **96/111 (86%)** dos pings OOR são **imediatamente seguidos por TIMEOUT** no mesmo ciclo
- Padrões mais frequentes: `[R T ✓ R T R T]`, `[R T R T ✓ R T]`, `[R T R T R T R]`

### Mecânica do que ocorre

O `OUT_OF_RANGE` no driver (`us_driver.cpp:146`) ocorre quando:

```cpp
if (cm < cfg.min_distance_cm || cm > cfg.max_distance_cm) {
    return {UsResult::OUT_OF_RANGE, 0.0f};  // distância real descartada!
}
```

Com `SENSOR_MIN_DISTANCE_CM = 25.0 cm`, qualquer eco que retorne com distância
**< 25 cm é rejeitado como OOR** — mesmo que seja apenas o eco da parede interna
da caixinha do sensor.

**O que acontece passo a passo:**

1. **Ping N** — eco da **parede interna da caixinha** (< 25 cm) chega antes do eco
   legítimo da água via espelho → `OUT_OF_RANGE`
2. O driver descarta o eco e aguarda `ping_interval_ms = 70 ms` antes do próximo ping
3. **Ping N+1** — a reverberação residual da reflexão anterior ainda está presente no
   canal, **ou** o ECHO pin ficou em estado indeterminado → `TIMEOUT` (25.000 µs sem
   eco válido detectado)
4. Quando a reverberação dissipa: **Ping N+2** → OK normalmente

Isso é confirmado pelos ciclos onde apenas **1 de 7 pings** é válido e o `DOMINANT_CLUSTER`
consegue eleger esse ping como resultado — ciclos que com menos pings resultariam em
`INSUFFICIENT_SAMPLES`.

### Por que só às 10:00 e não antes?

À medida que a caixa d'água foi sendo consumida ao longo dos testes, a distância foi
crescendo progressivamente:

| Teste | Distância | Eco parasita (% dos pings individuais) |
|---|---|---|
| 17:30 | ~37 cm | 0% |
| 23:30 | ~51 cm | 0% |
| 06:41 | ~60 cm | 0% |
| **10:00** | **~69 cm** | **~40%** |

Conforme a distância aumenta, a **amplitude do eco legítimo diminui** (percurso mais longo,
atenuação por divergência do cone de som). Em distâncias maiores, o sensor começa a capturar
reflexões parasitas das paredes internas da caixinha com frequência comparável ao eco principal.

---

## Histórico: comportamento com sensor horizontal (2026-07-15)

Antes de ajustar o `min_distance_cm`, com o sensor horizontal (sem espelho), o eco
parasita da parede (~20.2–20.3 cm) era aceito como **leitura válida silenciosa**:

```
UsSensor: 20.2-0, 20.3-0, 20.3-0, 20.3-0, 20.3-0, 20.3-0, 20.3-0
App: Distance: 20.3 - UsResult 0   ← FALSO POSITIVO, sem nenhum alerta
```

O eco parasita era **extremamente estável** (stdev = 0.042 cm) — superfície sólida fixa
próxima ao sensor. O eco real (água) ficava em ~52 cm (stdev ~2 cm).

Em ciclos mistos, o `DOMINANT_CLUSTER` às vezes elegia o cluster errado:

```
UsSensor: 20.2-0, 31.1-0, 20.3-0, 51.6-0, 20.3-0, 51.6-0, 51.6-0
App: Distance: 20.3 - UsResult 1   ← Cluster dos 20cm venceu com WEAK_SIGNAL
```

Nesse log, dos 27 ciclos medidos:

| Tipo de ciclo | Ocorrências | Resultado App |
|---|---|---|
| 7C + 0L (parede total) | 6x | `Distance: 20.3 - UsResult 0` — **falso positivo limpo** |
| 4C + 3L (empate) | 3x | `Distance: 0.0 - UsResult 4` (HIGH_VARIANCE) |
| 5C + 2L / 6C + 1L | 2x | `Distance: 20.3 - UsResult 1` (WEAK, mas valor errado) |
| 0C + 7L (água total) | 9x | `Distance: ~52 - UsResult 0` — correto |
| Misto L domina | 7x | `Distance: ~52 - UsResult 1` (WEAK_SIGNAL) |

**O `WEAK_SIGNAL` nesse contexto é um indicador diagnóstico valioso:** toda leitura com
eco duplo resultou em `WEAK_SIGNAL` ou `HIGH_VARIANCE`. Alta frequência de `WEAK_SIGNAL`
= sensor com eco parasita de parede competindo com o eco legítimo.

**O ajuste de `min_distance_cm = 25.0 cm` transformou esse falso positivo silencioso
em `OUT_OF_RANGE` explícito** — avanço de segurança significativo. O problema atual
(cascata OOR→TIMEOUT) é a manifestação visível desse eco parasita em distâncias maiores.

---

## Caminhos para resolver

### Caminho A — Mecânico (recomendado, já validado empiricamente)

**Baixar o sensor dentro da caixinha**, afastando-o das paredes laterais e aproximando-o
mais do espelho.

- Já resolveu problema análogo antes (sensor estava alto na caixinha e o cone de som
  esbarrava nas laterais).
- Com o sensor mais baixo, o cone de som (~15°) tem menos parede para interceptar antes
  de atingir o espelho.
- No laboratório, com o suporte reflexivo, o sensor mediu tranquilamente distâncias de
  110 cm sem problemas — o problema atual é mecânico/posicional, não de alcance.
- Verificar após o ajuste que a distância mínima (caixa cheia) ainda fica acima de
  `min_distance_cm = 25 cm`.

> **Ação:** Baixar o sensor na caixinha e repetir teste com caixa consumida (~69 cm de
> distância). Verificar desaparecimento do padrão OOR→TIMEOUT.

---

### Caminho B — Aumentar `ping_interval_ms`

O intervalo atual entre pings é de **70 ms**. Aumentar para **120–150 ms** daria mais
tempo para a reverberação da parede dissipar antes do próximo pulso, reduzindo o
TIMEOUT no ping subsequente ao OOR.

```cpp
// main.cpp — config atual vs proposta
ultrasonic::UsConfig us_config{
    .ping_interval_ms = 70,   // ← aumentar para 120 ou 150
    .ping_duration_us = 20,   // mínimo para sensor à prova d'água, não alterar
    .timeout_us = 25000,
    ...
};
```

> **Impacto:** Cada ciclo de 7 pings demora ~840–1050 ms vs ~490 ms atuais. Com
> deep-sleep de minutos, o impacto no consumo de energia é desprezível.
>
> **Limitação:** Não elimina o eco parasita — apenas reduz a cascata. Se o eco parasita
> dominar o ciclo inteiro, o resultado final ainda será `OUT_OF_RANGE`.

---

### Caminho C — Reduzir `timeout_us` ao range real do sistema

O `timeout_us` atual é **25.000 µs**, o que equivale a alcance de 428 cm — muito além
do necessário. O range real do sistema é:

```
Distância máxima real = TANK_HEIGHT_CM + SENSOR_OFFSET_CM + margem de segurança
                      = 150 cm         + 29.5 cm           + ~10 cm
                      = ~190 cm

Timeout correspondente = (190 cm × 2) / 0.0343 cm/µs ≈ 11.080 µs
```

Reduzir `timeout_us` para **~12.000–13.000 µs** libera o ECHO pin mais rápido após
pings sem retorno, reduzindo a janela em que reverberações tardias confundem o próximo
ping.

```cpp
ultrasonic::UsConfig us_config{
    .ping_interval_ms = 70,
    .ping_duration_us = 20,   // não alterar — mínimo para sensor à prova d'água
    .timeout_us = 12000,      // ← reduzir de 25000 para ~12000
    ...
};
```

> **Nota:** O pulso de **20 µs** (`ping_duration_us`) é o mínimo recomendado para
> o sensor à prova d'água. **10 µs não é aconselhado** para esse modelo e não deve
> ser alterado.
>
> **Limitação:** Assim como o Caminho B, não elimina o eco parasita. Se `timeout_us`
> for muito baixo, pode gerar falsos TIMEOUT em condições de ar quente (velocidade
> do som menor). Uma margem de ~30% acima do mínimo calculado é recomendada.

---

### Caminho D — Aumentar `PING_COUNT`

O sistema usa **7 pings por ciclo** (`PING_COUNT = 7`). Com ~40% dos pings sendo
OOR/TIMEOUT, apenas 3–4 chegam como válidos — margem apertada para o `DOMINANT_CLUSTER`.

Aumentar para **11–13 pings** daria mais amostras válidas mesmo com interferência parcial:

```cpp
// main.cpp
static constexpr uint8_t PING_COUNT = 11;  // ← era 7
```

> **Impacto no tempo:** ~280–420 ms extras por ciclo de medição — desprezível com
> deep-sleep de minutos.
>
> **Limitação:** Não resolve ciclos em que o OOR domina totalmente (ex: 6/7 ou 7/7
> pings ruins) — nesses casos, mais pings só adicionam mais amostras ruins.

---

## Ações pendentes

- [ ] **[Mecânica — prioridade 1]** Baixar o sensor dentro da caixinha e repetir teste
      com caixa consumida (~69 cm ou mais). Verificar se o padrão OOR→TIMEOUT desaparece.
- [ ] **[Config]** Se o ajuste mecânico não for suficiente, testar `ping_interval_ms = 120`
      e `timeout_us = 12000` e comparar logs antes/depois.
- [ ] **[Config]** Avaliar aumento de `PING_COUNT` para 11 como complemento aos demais ajustes.
- [ ] **[Diagnóstico]** Considerar logar a distância real antes de descartar o OOR —
      atualmente o driver retorna `0.0f` sem expor o valor medido (`us_driver.cpp:148`).
      Conhecer a distância real do eco parasita (< 25 cm = parede da caixa; > 150 cm =
      reflexão secundária distante) confirmaria definitivamente a causa.

---

## Relação com outras issues

- **`ISSUE_fill_min_distance.md`** — Esta issue é consequência direta daquela: o ajuste
  de `min_distance_cm = 25.0 cm` foi correto e necessário, transformando o falso positivo
  silencioso em `OUT_OF_RANGE` explícito. O problema atual é o eco parasita que esse OOR
  revela — não o OOR em si.
