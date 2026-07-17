# Issue: Revisão da lógica de detecção de `FillState`

**Status:** Aberta — aguardando mais dados de campo e geometria validada
**Componente:** `WaterTankLogic::update_fill_state()`
**Contexto:** Levantada durante análise dos primeiros logs de campo (2026-07-14/15)

---

## Motivação

A lógica atual de `update_fill_state()` foi escrita quando o tanque ainda usava
geometria cilíndrica padrão (antes do `tank_geometry.hpp` com os 5 gomos reais).
Com dados de campo em mãos, apareceram dúvidas sobre robustez ao ruído e sobre
o significado físico do threshold em `permille`, que precisam ser resolvidas
antes de fechar o intervalo de wake definitivo (candidato: 60s).

## Código atual

```cpp
struct WaterTankStats
{
    uint16_t level_permille = 0;
    uint16_t last_level_permille = 0;
    FillState fill_state = FillState::UNKNOWN;
    float last_distance_cm = 0.0f;
    ultrasonic::UsResult last_result = ultrasonic::UsResult::HW_FAULT;
    // ... outras coisas (falhas, etc.)
};

void WaterTankLogic::update_fill_state(uint16_t current_permille, WaterTankStats& stats) const
{
    int32_t delta = static_cast<int32_t>(current_permille) - static_cast<int32_t>(stats.last_level_permille);
    uint32_t abs_delta = (delta < 0) ? -delta : delta;
    if (abs_delta < LEVEL_DELTA_MIN) {          // LEVEL_DELTA_MIN = 5
        stats.fill_state = FillState::STABLE;
    }
    else if (delta > 0) {
        stats.fill_state = FillState::FILLING;
    }
    else {
        stats.fill_state = FillState::DRAINING;
    }
}
```

Características da implementação atual:
- Compara **apenas a leitura atual contra a imediatamente anterior** (delta de 1 amostra).
- Threshold fixo em `permille` (`LEVEL_DELTA_MIN = 5`), sem relação explícita com cm.
- Sem janela/histerese — uma única leitura ruidosa pode trocar o estado.

## Dados de campo coletados (2026-07-14)

Dois logs via UDP (`UsSensor` = pings brutos pré-filtro, `WaterTankApp` = distância filtrada):

| Cenário | Range médio (max-min) | Range máx observado | StDev médio |
|---|---|---|---|
| **Estável** (topo, sem vazar, >5min parado) | 0.61 cm | 2.00 cm | 0.24 cm |
| **Enchendo** (primeiros 90s, pedestal balançando) | 1.09 cm | 4.20 cm | 0.37 cm |
| **Enchendo** (5-6 min depois, estabilizado) | 1.09 cm | 2.10 cm | 0.37 cm |
| **Enchendo** (perto do final) | 1.01 cm | 7.40 cm (outlier isolado) | 0.38 cm |

Taxa de enchimento observada (último gomo, bomba ~1400 L/h nominal):
- ~16.5 cm em 927s → **~1.06 cm/min** (~1 cm a cada 60s)

Nenhum erro de `UsResult` (TIMEOUT, HW_FAULT, etc.) apareceu em nenhum dos dois logs —
sensor/filtro se comportaram bem, incluindo durante turbulência.

## Achados

| # | Achado | Severidade | Status |
|---|---|---|---|
| 1 | Delta de 1 amostra é sensível a outliers isolados (ex: pico de 7.4cm observado perto do final do enchimento) — pode disparar transição de estado falsa mesmo pós-filtro | **Alta** | Aberto |
| 2 | `LEVEL_DELTA_MIN = 5` (permille) não tem correspondência documentada em cm — margem real de ruído depende da conversão, que depende da geometria | **Alta** | Aberto |
| 3 | Conversão `permille → cm` não é constante entre gomos (diâmetros diferentes → mesmo delta em permille representa cm diferentes dependendo do segmento) | **Média** | Aberto — depende de `tank_geometry.hpp` estar validado |
| 4 | Com wake de 60s, delta esperado (~1cm) e ruído por amostra (stdev ~0.37cm, outliers até ~7cm) ficam com margem apertada (~3x em condição média, mas outlier isolado pode dominar leitura única) | **Média** | Aberto — ver recomendação abaixo |
| 5 | Sem histerese/confirmação — não há distinção entre "mudou uma vez" e "mudança consistente" | **Média** | Aberto |

## Recomendações a avaliar

1. **Trocar delta de 1 amostra por janela/confirmação.** Exigir N leituras
   consecutivas na mesma direção antes de declarar `FILLING`/`DRAINING` (ex: 2-3
   leituras seguidas com delta > threshold), evitando que um outlier isolado
   dispare a transição.
2. **Documentar/validar a relação `LEVEL_DELTA_MIN` (permille) ↔ cm** por segmento,
   uma vez que `tank_geometry.hpp` esteja calibrado. Se a margem de segurança
   (delta mínimo vs. ruído esperado) variar muito entre gomos, considerar um
   threshold que leve em conta a taxa cm/permille local, não um valor fixo global.
3. **Revisar o intervalo de wake de 60s** à luz do outlier observado (7.4cm) —
   avaliar se a janela de confirmação (item 1) já resolve, ou se vale aumentar
   para 90-120s para folga extra.
4. **Considerar comparar contra média de últimas N leituras** (não só a leitura
   anterior) para suavizar ruído sem precisar esperar confirmação sequencial.
5. **Repetir a análise de ruído/outlier com mais dados de campo** — a amostra
   atual é de um único ciclo de enchimento parcial (último gomo apenas); vale
   confirmar se o padrão se mantém em ciclo completo e em drenagem.

## Próximos passos

- [ ] Validar/fechar `tank_geometry.hpp` (breakpoints e diâmetros dos 5 gomos)
- [ ] Definir método de conversão cm ↔ permille por segmento
- [ ] Prototipar lógica de janela/confirmação e testar contra os logs já coletados
- [ ] Repetir teste de campo com ciclo completo (não só último gomo) para validar em condições variadas
- [ ] Decidir intervalo de wake final para `FILLING`/`DRAINING`
