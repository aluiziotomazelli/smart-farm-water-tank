# Issue: Sensor reflexivo gera cascata `OUT_OF_RANGE → TIMEOUT` em distâncias maiores (~69 cm)

**Status:** Causa raiz física e bug de software identificados — aguardando correção no driver e ajuste mecânico  
**Componente:** `ultrasonic_sensor` (`us_driver.cpp` + config `UsConfig`) + instalação mecânica do sensor reflexivo  
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

## Causa raiz: Bug no Software (`us_driver.cpp`) + Eco Parasita Físico

### 1. Evidência Estatística Definitiva por Posição no Burst (Log 10:00)

Analisando a posição exata de cada ping dentro da rajada de 7 pings no log das 10:00,
revela-se um deslocamento 1-para-1 perfeito entre `OUT_OF_RANGE (3)` e `TIMEOUT (2)`:

| Posição no Burst | OK (0) | OUT_OF_RANGE (3) | TIMEOUT (2) | WEAK (1) |
|---|---|---|---|---|
| **Ping 1** | 266 | **28** | **0** | 0 |
| **Ping 2** | 262 | 4 | **28** (exatamente os 28 de Ping 1!) | 0 |
| **Ping 3** | 274 | **16** | 4 | 0 |
| **Ping 4** | 263 | 15 | **16** (exatamente os 16 de Ping 3!) | 0 |
| **Ping 5** | 270 | **9** | **15** (exatamente os 15 de Ping 4!) | 0 |
| **Ping 6** | 261 | **24** | **9** (exatamente os 9 de Ping 5!) | 0 |
| **Ping 7** | 255 | 15 | **24** (exatamente os 24 de Ping 6!) | 0 |

**Todo ping $N$ que dá `OUT_OF_RANGE` força o ping $N+1$ a dar `TIMEOUT` 100% das vezes.**

---

### 2. A Descoberta do Bug no Driver (`us_driver.cpp`)

Inspecionando a implementação do driver em `components/ultrasonic_sensor/src/us_driver.cpp`:

```cpp
Reading UsDriver::ping_once(const UsConfig &cfg)
{
    ...
    // 7. Convert to distance
    float cm = (duration_us * SOUND_SPEED_CM_PER_US) / 2.0f;

    if (cm < cfg.min_distance_cm || cm > cfg.max_distance_cm) {
        ESP_LOGD(TAG, "Out of range: %.1f cm (limits: %.1f-%.1f)", cm, cfg.min_distance_cm, cfg.max_distance_cm);
        return {UsResult::OUT_OF_RANGE, 0.0f}; // ← BUG: Retorna imediatamente!
    }

    // Inter-ping delay: applied after every ping so the sensor can reset.
    if (cfg.ping_interval_ms > 0) {
        timer_hal_->delay_ms(cfg.ping_interval_ms); // ← É PULADO quando dá OOR ou TIMEOUT!
    }

    return {UsResult::OK, cm};
}
```

**Mecânica da Falha:**

1. **Ping N** mede ~20.2 cm (eco da parede da caixinha).
2. Como `20.2 cm < 25.0 cm` (`min_distance_cm`), o driver executa a linha 148: `return {UsResult::OUT_OF_RANGE, 0.0f};`.
3. **O `timer_hal_->delay_ms(70)` da linha 154 É COMPLETAMENTE PULADO!**
4. O loop de rajada (`UsSensor::read_distance`) chama `ping_once()` para o **Ping N+1 IMEDIATAMENTE (delay 0 ms)**.
5. Com delay 0 ms, o eco acústico anterior ainda está viajando no ar / pino ECHO ainda ativo / transdutor ressoando.
6. O **Ping N+1** tenta medir e dá `TIMEOUT` (25.000 µs sem borda limpa).
7. Como o `return {UsResult::TIMEOUT, 0.0f};` (linhas 131 e 138) **também pula o `delay_ms(70)`**, a cascata destrói o restante do ciclo!

---

### 3. Comprovação Definitiva com o Log de 15/07 (`2026-07-15-out-of-range-error.txt`)

No log de 15/07 (sensor horizontal), a configuração era `min_distance_cm = 15.0 cm`:

- **O Ping 1 foi exatamente 20.2 cm em 59.3% de todos os 27 ciclos!**
- Como `20.2 cm > 15.0 cm`, o driver considerava o Ping 1 como `UsResult::OK`.
- Como era `OK`, o driver **executava o `delay_ms(70)`** normalmente.
- Como aguardava 70 ms de descanso, o **Ping 2 em diante conseguia medir a água limpadamente em ~53 cm** (ex: Ciclos 22, 23 e 26 do log, onde Ping 1 era 20.2cm e Pings 2–7 eram 52.9–53.4cm `OK`).

Quando alteramos `min_distance_cm` para 25.0 cm, o Ping 1 de 20.2 cm passou a ser classificado como `OUT_OF_RANGE`, disparando o bug de pular o delay de 70 ms e colapsando os pings subsequentes.

---

## Histórico: Comportamento com Sensor Horizontal (2026-07-15)

Antes do ajuste de `min_distance_cm`, o eco parasita da parede (~20.2–20.3 cm) era aceito como **leitura válida silenciosa**:

```
UsSensor: 20.2-0, 20.3-0, 20.3-0, 20.3-0, 20.3-0, 20.3-0, 20.3-0
App: Distance: 20.3 - UsResult 0   ← FALSO POSITIVO SILENCIOSO
```

O eco parasita da parede é **extremamente estável** (stdev = 0.042 cm) — uma superfície rígida fixa. O eco real da água fica em ~52 cm (stdev ~2 cm).

Em ciclos mistos, o `DOMINANT_CLUSTER` nem sempre vencia a favor da água:

| Tipo de ciclo no log 15/07 | Ocorrências | Resultado App |
|---|---|---|
| **7C + 0L** (parede total) | 6x | `Distance: 20.3 - UsResult 0` — **falso positivo aceito** |
| **4C + 3L** (empate) | 3x | `Distance: 0.0 - UsResult 4` (HIGH_VARIANCE) |
| **5C + 2L / 6C + 1L** | 2x | `Distance: 20.3 - UsResult 1` (WEAK, mas valor errado!) |
| **0C + 7L** (água total) | 9x | `Distance: ~52 - UsResult 0` — correto |
| **Misto L domina** | 7x | `Distance: ~52 - UsResult 1` (WEAK_SIGNAL) |

**Utilidade Diagnóstica do `WEAK_SIGNAL`:** Toda leitura com competição entre parede e água resultou em `WEAK_SIGNAL` ou `HIGH_VARIANCE`. Alta taxa de `WEAK_SIGNAL` = indício de eco parasita competindo com o eco principal.

**Conclusão:** O `min_distance_cm = 25.0 cm` foi fundamental para eliminar falsos positivos silenciosos. A correção necessária agora é garantir que o driver não destrua o ciclo ao detectar um OOR.

---

## Caminhos para Resolver

### 🛠️ Correção 1 — Software Driver (`us_driver.cpp`) — **PRIORIDADE ZERO**

Corrigir `UsDriver::ping_once()` para **SEMPRE executar `timer_hal_->delay_ms(cfg.ping_interval_ms)`**, mesmo em retornos de `OUT_OF_RANGE` ou `TIMEOUT`.

```cpp
// us_driver.cpp — Refatoração com RAII ou delay garantido
Reading UsDriver::ping_once(const UsConfig &cfg)
{
    // Usar ScopeGuard ou garantir delay antes de retornar
    struct ScopeGuard {
        std::shared_ptr<ITimerHAL> timer;
        uint16_t delay_ms;
        ~ScopeGuard() { if (delay_ms > 0 && timer) timer->delay_ms(delay_ms); }
    } guard{timer_hal_, cfg.ping_interval_ms};

    ...
}
```

> **Resultado Esperado:** Se o Ping 1 bater na parede (20.2 cm → `OOR`), o driver aguardará os 70 ms normais. O Ping 2 será disparado em canal limpo e medirá a água em ~69 cm (`OK`), permitindo que o `DOMINANT_CLUSTER` recupere a leitura com sucesso sem gerar falha no App!

---

### 🔩 Correção 2 — Mecânica (Baixar o Sensor na Caixinha)

**Baixar o sensor dentro da caixinha**, afastando-o das paredes laterais e aproximando-o mais do espelho reflexivo.

- Como o cone de som é de ~15°, afastar o sensor do topo da caixa evita que as bordas do pulso atinjam a estrutura de nylon/madeira antes de alcançar o espelho.
- No laboratório, o suporte reflexivo mediu 110 cm sem interferência de parede.

> **Ação:** Baixar o sensor na caixinha e realizar novo teste de campo com a caixa d'água consumida (~69 cm ou mais).

---

### ⚙️ Correção 3 — Reduzir `timeout_us` ao Range Real do Sistema

O `timeout_us = 25000 µs` equivale a um alcance de 428 cm. O range máximo real da caixa d'água é:

$$\text{Distância Máxima Real} = \text{TANK\_HEIGHT} (150) + \text{SENSOR\_OFFSET} (29.5) + \text{Margem} (10.5) = 190\text{ cm}$$

$$\text{Timeout Necessário} = \frac{190 \times 2}{0.0343} \approx 11.080\text{ µs}$$

Reduzir `timeout_us` para **~12.000–13.000 µs** em `us_config` reduz o tempo gasto aguardando pings perdidos em mais de 50%.

> **Nota:** O pulso `ping_duration_us = 20 µs` é o mínimo recomendado para sensores ultrassônicos blindados (à prova d'água) e **NÃO deve ser alterado para 10 µs**.

---

### ⚙️ Correção 4 — Ajuste de `ping_interval_ms` e `PING_COUNT`

- `ping_interval_ms`: Aumentar de 70 ms para **100–120 ms** oferece margem extra de atenuação acústica em caixas profundas e secas.
- `PING_COUNT`: Aumentar de 7 para **9–11 pings** garante que mesmo com 1 ou 2 pings de parede (`OOR`), haverá amostras suficientes de água para o `DOMINANT_CLUSTER` aprovar o ciclo como `OK` com confiança total.

---

## Plano de Ação

- [ ] **[Software]** Corrigir `us_driver.cpp` para aplicar `delay_ms(ping_interval_ms)` em todas as ramificações de saída de `ping_once()`.
- [ ] **[Mecânica]** Baixar o sensor dentro da caixinha para reduzir reflexões no cone de 15°.
- [ ] **[Config]** Atualizar `timeout_us = 13000` em `main.cpp`.
- [ ] **[Validação]** Rodar teste de campo com caixa d'água baixa (>70 cm) e validar ausência de cascatas de TIMEOUT e ausência de falsos `OUT_OF_RANGE`.
