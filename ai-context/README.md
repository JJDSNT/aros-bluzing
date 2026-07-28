# AI Context

Esta pasta existe para que o assistente de IA mantenha seu próprio acompanhamento do projeto entre sessões: o que já foi entendido, o que já foi feito e o que falta fazer. Não é documentação voltada ao usuário final — é memória de trabalho persistida em arquivo.

## Sobre o projeto

O `aros-bluzing` é o projeto de uma stack Bluetooth Host nativa e portável para o AROS, descrita em detalhe em [`../project.md`](../project.md). Resumo:

- **Objetivo**: implementar Bluetooth (Classic + LE) para o AROS com um núcleo de protocolos portável (independente de SO), integrado via `bluetooth.library`, uma Bluetooth Manager Task, e transportes HCI plugáveis (Poseidon USB, UART, SDIO, virtual).
- **Requisito estrutural**: compatibilidade big-endian/little-endian desde o início (m68k, PowerPC, ARM, AArch64, x86, x86-64) — sem casts de buffer para struct, com helpers explícitos de leitura/escrita LE/BE.
- **Separação de responsabilidades**: Poseidon cuida só de USB; uma classe `btusb` implementa o transporte HCI sobre USB; o núcleo Bluetooth não conhece detalhes de Poseidon.
- **Licenciamento**: procedência auditável — BTstack não pode ser copiado/traduzido; NimBLE só como oráculo de teste/comportamento até resolução jurídica Apache 2.0 vs. licença do AROS.
- **Ordem de implementação**: Fase 0 (levantamento do AROS, sem inventar APIs) → Fase 1 (fundação portátil: tipos, endian, buffers) → Fase 2 (HCI mínimo + transporte virtual) → Fase 3 (integração Poseidon) → Fase 4 (discovery) → Fase 5 (L2CAP) → Fase 6 (SDP/GATT/segurança) → Fase 7 (HID dual-mode) → Fase 8 (RFCOMM e demais perfis).

Ver `project.md` para o texto completo dos princípios obrigatórios, arquitetura proposta, critérios de aceite por fase e estratégia de testes.

## Conteúdo desta pasta

- [`status.md`](status.md) — objetivos correntes, o que já foi feito e o que falta fazer. Deve ser atualizado a cada sessão de trabalho relevante.
- [`fase0-levantamento.md`](fase0-levantamento.md) — levantamento factual do repositório AROS real (Poseidon, class drivers, attach/detach, I/O assíncrono, libraries/devices, tasks/signals, parser HID, input.device, build system, HCDs, HIDD), com citação de arquivo/símbolo/trecho para cada achado.
- [`fase0-propostas.md`](fase0-propostas.md) — síntese sobre o levantamento: proposta de diretórios, mapeamento da API de transporte HCI sobre primitivas reais do AROS, riscos revisados e plano de implementação em commits pequenos.
- [`fase0-correcao-hidd.md`](fase0-correcao-hidd.md) — documento à parte sobre o papel do sistema HIDD do AROS e por que o transporte HCI Bluetooth não deve ser modelado como uma classe HIDD/BOOPSI (substitui a conclusão original de §11 do levantamento).

Checkout de trabalho do AROS usado nessa investigação: `/home/jaime/AROS-bluetooth` (branch `feature/bluetooth-stack`, checkout próprio e separado de outros checkouts do usuário).

## Como manter

- Atualizar `status.md` sempre que uma fase avançar, uma decisão de arquitetura for tomada, ou uma divergência entre a proposta e o código real do AROS for encontrada (conforme exigido na seção "Primeira tarefa do agente" de `project.md`).
- Manter o histórico enxuto: preferir resumir decisões e apontar para commits/arquivos reais em vez de duplicar conteúdo que já vive no código ou no `project.md`.
