# Status do projeto

> Atualizado em: 2026-07-27

## Objetivos atuais

- **Fase 4 (discovery Classic e LE) está completa** no escopo portátil/testável em host: Inquiry, Inquiry Result/Complete, LE Set Scan Parameters/Enable, LE Advertising Report, banco unificado de dispositivos com dedup e detecção de dual-mode por endereço. Próximo passo natural: Fase 5 (L2CAP) ou finalmente montar a Bluetooth Manager Task/`bt_platform_ops` para começar a tocar AROS de verdade — a decidir.
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
- [x] **Fase 2 (HCI mínimo, escopo de bring-up)**:
  - `bluetooth/hci.h`: Command Status, cabeçalho ACL, parsers de Read Local Version/Features/Buffer Size.
  - `bluetooth/command_queue.h` + `core/controller/command_queue.c`: fila de comandos com pool fixo (`BT_CMDQ_MAX_PENDING`), controle de créditos (`Num_HCI_Command_Packets`), timeout por comando via `bt_timer_list`, um comando em voo por vez, múltiplos comandos simultâneos enfileirados corretamente.
  - `bluetooth/controller.h` + `core/controller/controller.c`: máquina de estados `bt_controller` — Reset → Read Local Version → Read Local Supported Features → Read Buffer Size → READY (ou ERROR em falha/timeout).
  - Transporte virtual estendido para responder às 3 novas leituras com dados plausíveis.
  - **Bug real #1 (reentrância)**: como o transporte virtual responde de forma síncrona (dentro da própria chamada `send_command`), a fila de comandos precisava marcar o comando como outstanding *antes* de chamar o transporte, não depois — senão o processamento recursivo da resposta era sobrescrito pelo código que rodava após a chamada retornar. Corrigido em `command_queue.c`; documentado no código porque qualquer transporte síncrono real teria o mesmo risco.
  - **Bug real #2 (deadlock)**: Command Status com sucesso (status=0x00) estava deixando o slot "outstanding" indefinidamente, assumindo que um Command Complete futuro o liberaria. Mas comandos como HCI Inquiry (e Create Connection) só respondem via Command Status — nunca via Command Complete para esse opcode — e completam de verdade por um evento totalmente diferente (Inquiry Complete). Isso travaria a fila permanentemente na primeira vez que um comando desse tipo fosse usado. Corrigido: Command Status sempre libera o slot, sucesso ou falha — é apenas o "ack" de crédito do controlador, não a conclusão semântica da operação.
- [x] **Fase 4 (discovery)**:
  - `bluetooth/addr.h` + `core/addr/addr.c`: `struct bt_addr` explícito (nunca cast para inteiro nativo), `bt_addr_equal`.
  - `bluetooth/device_registry.h` + `core/device/device_registry.c`: banco unificado de dispositivos, tamanho fixo (`BT_DEVICE_REGISTRY_MAX`), dedup por endereço, detecção de dual-mode (flags Classic/LE no mesmo registro). Limitação conhecida documentada: não resolve identidade de endereços LE privados/rotativos (precisa de IRK/bonding via SMP, fora de escopo aqui).
  - `bluetooth/hci.h`: Inquiry (encode + iterator de Inquiry Result), LE Set Scan Parameters/Enable (encode), LE Advertising Report (iterator sobre LE Meta Event).
  - `bt_controller` estendido: `bt_controller_start_classic_inquiry`/`start_le_scan`, roteamento de Inquiry Result/LE Meta para o `device_registry`.
  - Transporte virtual estendido para simular um dispositivo Classic (via Command Status + Inquiry Result + Inquiry Complete) e um dispositivo LE (via Command Complete + LE Advertising Report).
  - **Bug real #3 (deadlock, achado antes mesmo do código de discovery)**: Command Status com sucesso estava deixando o slot da fila de comandos preso para sempre — exatamente o caso de Inquiry, que só responde via Command Status. Corrigido em commit separado antes de escrever qualquer código de discovery, evitando que o bug se manifestasse silenciosamente.
- Testes: 575 checks. `make test` limpo com `-Wall -Wextra -Werror` + ASan/UBSan. Cobertura LE/BE é estrutural (nenhum código depende de endianness do host — verificado por vetores de bytes fixos), não por build cross-compilado para BE real ainda.
- [ ] Fase 5 (L2CAP) — próximo passo possível, ainda não iniciado.
- [ ] `bt_platform_ops` continua só declarada — ainda sem consumidor real (precisa de um event loop de verdade, AROS ou test-host).
- [ ] `ports/aros/library/` e `ports/aros/task/`: esqueletos de `bluetooth.library` e Bluetooth Manager Task.
- [ ] `ports/aros/transport-usb/`: adapter sobre `usbbluetooth.device` (só depois de Fase 1/2 testadas).
- [ ] Fase 4 em diante: seguir `project.md` sem mudança de plano.

## Divergências / decisões registradas

- **HIDD não é o mecanismo do transporte HCI Bluetooth**: investigação em 3 rodadas (ver [`fase0-correcao-hidd.md`](fase0-correcao-hidd.md), que substitui a conclusão original de §11 do levantamento). Conclusão final: HIDD/BOOPSI é o padrão real do AROS para domínios "objeto com propriedades e enumeração" (vídeo, teclado/mouse, o próprio barramento PCI/ATA) — mas domínios de **transferência de pacotes/dados em fluxo** (rede, Wi-Fi onboard, USB, e o único precedente Bluetooth real) usam consistentemente device/library/Exec Resource com fila `IORequest`/`MsgPort`, **independente do barramento** (confirmado comparando rede via PCI vs. USB, que usa device/usbclass nos dois casos, nunca HIDD). A stack Bluetooth deve seguir esse padrão: `usbclass`/device para USB (já existe), Exec Resource para chips onboard via SDIO/UART (seguindo `bwfm`+`sdio`). Injeção de eventos HID (teclado/mouse via BT) segue `input.device` + `IND_WRITEEVENT`, sem relação com essa questão.
- **`btusb.class` não precisa ser escrito do zero**: ver "Achado chave" acima e `fase0-propostas.md`.
