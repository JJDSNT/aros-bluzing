# Status do projeto

> Atualizado em: 2026-07-27

## Objetivos atuais

- Continuar a **Fase 1**: próximo passo é o transporte HCI virtual (`ports/test-host/virtual_transport`) + parser de HCI Event + encoder de HCI Command + sequência HCI Reset simulada (itens 4-5 do plano de commits em `fase0-propostas.md`), fechando a "Primeira entrega de código" de `project.md`.
- Checkout de trabalho do AROS para este projeto: `/home/jaime/AROS-bluetooth`, branch `feature/bluetooth-stack` (checkout próprio, separado de outros checkouts/trabalhos do usuário) — ainda não usado para código, só para a investigação da Fase 0.

## Feito

- Documento de especificação do projeto (`project.md`) redigido, cobrindo objetivo, princípios obrigatórios, arquitetura proposta, modelo de execução, API pública, persistência, licenciamento/clean-room, fases de implementação e estratégia de testes.
- Pasta `ai-context/` criada para acompanhamento contínuo do trabalho pelo assistente.
- **Fase 0 concluída**: levantamento factual do repositório AROS real, com citação de arquivo/símbolo/trecho para 11 tópicos (Poseidon, class drivers, attach/detach, I/O assíncrono, libraries/devices, tasks/signals/msgports, parser HID, input.device, build system, HCDs, sistema HIDD). Ver [`fase0-levantamento.md`](fase0-levantamento.md).
- **Síntese/propostas da Fase 0**: ver [`fase0-propostas.md`](fase0-propostas.md) — inclui a decisão estratégica mais importante até agora.

## Achado chave da Fase 0

Já existe no AROS um `bluetooth.class` real (`rom/usb/classes/bluetooth/`, Chris Hodges) que implementa quase exatamente o transporte HCI-sobre-USB que `project.md` pede como `btusb.class`, expondo um `usbbluetooth.device` com IORequests HCI. **Decisão**: adaptar esse driver existente (via um adapter fino de `bt_hci_transport_ops` sobre o device) em vez de escrever uma classe Poseidon do zero. Detalhes em `fase0-propostas.md`.

## A fazer

- [x] Fase 0 (levantamento + propostas).
- [x] Fase 1, parte 1: tipos (`include/bluetooth/types.h`), códigos de erro (`status.h`), helpers de endianness (`bt_read_le*`/`bt_write_le*`/`bt_read_be*`/`bt_write_be*` em `core/buffer/endian.c`), buffer reader/writer com bounds-checking (`core/buffer/buffer.c`) servindo de packet builder. Testes em `tests/` (round-trip, vetores conhecidos, buffers desalinhados, overflow/underflow) — 266 checks, `make test` roda limpo com `-Wall -Wextra -Werror` + ASan/UBSan.
- [ ] Fase 1, parte 2: filas, eventos, timers abstratos (ainda não implementados — avaliar se são necessários antes do transporte virtual ou junto dele).
- [ ] Transporte HCI virtual (`ports/test-host/virtual_transport`), parser de HCI Event, encoder de HCI Command, sequência HCI Reset simulada (LE e BE) — fecha a "Primeira entrega de código" de `project.md`.
- [ ] Fase 2: HCI mínimo completo sobre transporte virtual.
- [ ] `ports/aros/library/` e `ports/aros/task/`: esqueletos de `bluetooth.library` e Bluetooth Manager Task.
- [ ] `ports/aros/transport-usb/`: adapter sobre `usbbluetooth.device` (só depois de Fase 1/2 testadas).
- [ ] Fase 4 em diante: seguir `project.md` sem mudança de plano.

## Divergências / decisões registradas

- **HIDD não é o mecanismo do transporte HCI Bluetooth**: investigação em 3 rodadas (ver [`fase0-correcao-hidd.md`](fase0-correcao-hidd.md), que substitui a conclusão original de §11 do levantamento). Conclusão final: HIDD/BOOPSI é o padrão real do AROS para domínios "objeto com propriedades e enumeração" (vídeo, teclado/mouse, o próprio barramento PCI/ATA) — mas domínios de **transferência de pacotes/dados em fluxo** (rede, Wi-Fi onboard, USB, e o único precedente Bluetooth real) usam consistentemente device/library/Exec Resource com fila `IORequest`/`MsgPort`, **independente do barramento** (confirmado comparando rede via PCI vs. USB, que usa device/usbclass nos dois casos, nunca HIDD). A stack Bluetooth deve seguir esse padrão: `usbclass`/device para USB (já existe), Exec Resource para chips onboard via SDIO/UART (seguindo `bwfm`+`sdio`). Injeção de eventos HID (teclado/mouse via BT) segue `input.device` + `IND_WRITEEVENT`, sem relação com essa questão.
- **`btusb.class` não precisa ser escrito do zero**: ver "Achado chave" acima e `fase0-propostas.md`.
