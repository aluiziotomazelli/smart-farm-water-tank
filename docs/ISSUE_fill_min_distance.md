# Issue: `min_distance_cm` desalinhado com a instalação real causou leituras falso-positivas

**Status:** Causa raiz identificada — aguardando calibração final da altura de montagem
**Componente:** `ultrasonic_sensor` (config `UsConfig::min_distance_cm`) + instalação mecânica
**Contexto:** Levantada durante teste de campo (2026-07-15), leituras intermitentes de ~20cm
mesmo com a caixa bem mais cheia que isso.

---

## Causa raiz

`UsConfig::min_distance_cm` estava configurado em **15cm** (valor de teste, antes de
saber a distância mínima real da instalação). Isso fez o próprio componente
`ultrasonic_sensor` **aceitar como válida** qualquer leitura de ~20cm — que na
prática era o sensor captando um refletor rígido próximo (a caixa de madeira/nylon
onde ele estava montado), não a superfície da água.

```cpp
struct UsConfig
{
    uint16_t ping_interval_ms = 70;
    uint16_t ping_duration_us = 20;
    uint32_t timeout_us = 30000;
    Filter filter = Filter::MEDIAN;
    float min_distance_cm = 10.0f;  // <-- estava 15cm nos testes, causa do problema
    float max_distance_cm = 200.0f;
    float max_dev_cm = 15.0f;
    uint16_t warmup_time_ms = 600;
};
```

O componente já possui a lógica correta (`UsResult::OUT_OF_RANGE` quando fora de
`[min_distance_cm, max_distance_cm]`, reportado ao hub) — o problema não era falta
de tratamento, era o **parâmetro de configuração** não refletir a instalação real.

## Evidência de campo

Logs mostrando o comportamento antes da correção mecânica, com `min_distance_cm = 15cm`:

```
UsSensor: 20.2-0, 20.3-0, 20.3-0, 20.3-0, 20.3-0, 20.3-0, 20.3-0
WaterTankApp: Distance: 20.3 - UsResult 0 | Battery: 3810   <- OK, mas é obstrução
```

Ponto importante observado: quando **todos os 7 pings concordam no valor da
obstrução** (refletor fixo dominando o eco), o filtro por variância reporta
`UsResult::OK` com confiança máxima — porque variância baixa não implica leitura
correta, só implica que os pings concordam entre si. Casos de obstrução parcial
(1-2 de 7 pings em ~20cm) já eram corretamente sinalizados como `WEAK_SIGNAL` ou
`HIGH_VARIANCE`; o caso perigoso era só a obstrução total e consistente.

Com `min_distance_cm` ajustado corretamente para a instalação final, esse cenário
passa a ser pego nativamente como `OUT_OF_RANGE` pelo próprio componente, sem
precisar de lógica extra na aplicação.

## Correção mecânica já aplicada

- Sensor estava semi-embutido numa peça de nylon torneada (1cm embutido, cone de
  45°) dentro de um recorte na tampa — possível causa do refletor próximo.
- **Novo arranjo**: sensor agora fica na ponta de um tubo de alumínio de 25mm,
  preso por uma bucha de nylon na tampa — sem nada próximo ao sensor, e com
  **altura ajustável** (dá pra baixar mais sem precisar subir na caixa de novo).
- Distância mínima esperada da instalação atual: **~26-28cm** (nível mais cheio).

## Ação pendente

- [ ] Com a instalação mecânica nova, medir a distância mínima real (nível mais
      cheio) de forma confiável antes de fechar o valor final.
- [ ] Ajustar `min_distance_cm` para um pouco abaixo da distância mínima real
      esperada (margem de segurança, ex: 3-5cm abaixo do mínimo físico), garantindo
      que qualquer leitura de obstrução (~20-22cm, valor já conhecido desse setup)
      caia fora do range e vire `OUT_OF_RANGE` nativamente.
- [ ] Confirmar que o hub recebe e trata corretamente o `OUT_OF_RANGE` reportado
      (esperado, já que o componente já envia isso — só validar em campo com o
      valor de config corrigido).
- [ ] Repetir teste de campo focado, monitorando se leituras espúrias em ~20cm
      desaparecem após a correção mecânica + config.

## Relação com outras issues

Complementa `ISSUE_fill_state_detection.md` — aquela trata de robustez do delta
entre leituras válidas; esta trata de garantir que a leitura em si seja plausível
antes de chegar à lógica de `fill_state`. Com `min_distance_cm` corrigido, a
maior parte do risco identificado ali (leitura obstruída sendo tratada como
válida) já fica coberta no nível do componente, sem precisar de checagem
duplicada na aplicação.
