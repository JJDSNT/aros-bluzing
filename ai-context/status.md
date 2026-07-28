# Status do projeto

> Atualizado em: 2026-07-27

## Objetivos atuais

- **"Primeira entrega de código" de `project.md` está completa** (ver "Feito" abaixo). Próximo passo: o que falta de Fase 1 (filas, eventos, timers abstratos) e então Fase 2 (HCI mínimo completo: command complete/status genéricos, controle de créditos, fila de comandos, timeout, estado do controlador).
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
- [x] Fase 1, parte 1: tipos (`include/bluetooth/types.h`), códigos de erro (`status.h`), helpers de endianness (`bt_read_le*`/`bt_write_le*`/`bt_read_be*`/`bt_write_be*` em `core/buffer/endian.c`), buffer reader/writer com bounds-checking (`core/buffer/buffer.c`) servindo de packet builder.
- [x] Abstração de transporte HCI (`include/bluetooth/transport.h`, `bt_hci_transport_ops`/`bt_hci_transport`, com callback de recepção `bt_hci_transport_recv_fn`).
- [x] Transporte virtual (`ports/test-host/virtual_transport/`) — controlador falso que responde a HCI Reset com Command Complete, síncrono, só para testes host.
- [x] Encoder de HCI Command e parser de HCI Event/Command Complete (`include/bluetooth/hci.h`, `protocols/hci/hci.c`).
- [x] **Sequência HCI Reset simulada** (`tests/virtual_transport/test_virtual_transport.c`): Reset Command → transporte virtual → Command Complete Event → controller state = initialized. Isso fecha a "Primeira entrega de código" de `project.md`.
- [x] Fila intrusiva SPSC (`bluetooth/queue.h`, `core/event/queue.c`) — sem alocação, nó embutido pelo chamador.
- [x] Lista de timers ordenada por expiração (`bluetooth/timer.h`, `core/timer/timer.c`) — bookkeeping puro e testável com `now_us` explícito, sem depender de relógio real; add/cancel/pop_expired/next_expiry.
- [x] Interface `bt_platform_ops` declarada (`bluetooth/platform.h`), exatamente como em `project.md`. **Sem implementação ainda** — decisão deliberada: não há consumidor real (a Bluetooth Manager Task não existe) até a Fase 2/porta AROS, então uma implementação de `ports/test-host/platform` ficaria especulativa. Implementar quando algo de fato chamar essa interface.
- Testes: 337 checks (endian, buffer, HCI encode/parse, transporte virtual, fila, timers), `make test` limpo com `-Wall -Wextra -Werror` + ASan/UBSan. Cobertura LE/BE é estrutural (nenhum código depende de endianness do host — verificado por vetores de bytes fixos), não por build cross-compilado para BE real ainda.
- [ ] Fase 2: HCI mínimo completo (command status, controle de créditos, fila de comandos usando `bt_queue`, timeout usando `bt_timer_list`, estado do controlador como módulo de verdade em `core/controller/`).
- [ ] `ports/aros/library/` e `ports/aros/task/`: esqueletos de `bluetooth.library` e Bluetooth Manager Task.
- [ ] `ports/aros/transport-usb/`: adapter sobre `usbbluetooth.device` (só depois de Fase 1/2 testadas).
- [ ] Fase 4 em diante: seguir `project.md` sem mudança de plano.

## Divergências / decisões registradas

- **HIDD não é o mecanismo do transporte HCI Bluetooth**: investigação em 3 rodadas (ver [`fase0-correcao-hidd.md`](fase0-correcao-hidd.md), que substitui a conclusão original de §11 do levantamento). Conclusão final: HIDD/BOOPSI é o padrão real do AROS para domínios "objeto com propriedades e enumeração" (vídeo, teclado/mouse, o próprio barramento PCI/ATA) — mas domínios de **transferência de pacotes/dados em fluxo** (rede, Wi-Fi onboard, USB, e o único precedente Bluetooth real) usam consistentemente device/library/Exec Resource com fila `IORequest`/`MsgPort`, **independente do barramento** (confirmado comparando rede via PCI vs. USB, que usa device/usbclass nos dois casos, nunca HIDD). A stack Bluetooth deve seguir esse padrão: `usbclass`/device para USB (já existe), Exec Resource para chips onboard via SDIO/UART (seguindo `bwfm`+`sdio`). Injeção de eventos HID (teclado/mouse via BT) segue `input.device` + `IND_WRITEEVENT`, sem relação com essa questão.
- **`btusb.class` não precisa ser escrito do zero**: ver "Achado chave" acima e `fase0-propostas.md`.
