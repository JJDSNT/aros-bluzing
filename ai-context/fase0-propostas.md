# Fase 0 — Propostas derivadas do levantamento

Este documento é a síntese exigida pela seção "Primeira tarefa do agente" de `../project.md`: proposta
de diretórios, proposta de API de transporte HCI, riscos de big-endian/alinhamento/concorrência, e plano
de implementação em commits pequenos — tudo ajustado aos fatos reais registrados em
[`fase0-levantamento.md`](fase0-levantamento.md).

---

## Decisão estratégica principal: não escrever `btusb.class` do zero

O achado mais importante da Fase 0 é que `rom/usb/classes/bluetooth/` (`bluetooth.class` +
`usbbluetooth.device`, ver levantamento §1–6) já resolve, com código testado em produção, praticamente
tudo que o `project.md` pede da "classe USB Bluetooth": detecção de interface, binding/unbinding,
descoberta de endpoints, subtask dedicada, envio de comandos HCI por control transfer, recepção de
eventos por interrupt IN, ACL por bulk, cleanup seguro em hot-unplug.

Recomendação: **adaptar, não reescrever**. Dois caminhos possíveis:

* **(a) Adapter fino (recomendado para começar)** — manter `usbbluetooth.device` intocado e escrever, do
  lado da nova stack, uma implementação de `bt_hci_transport_ops` que abre o device
  (`OpenDevice("usbbluetooth.device", ...)`) e traduz `send_command`/`send_acl`/`start_receive` para os
  comandos já existentes (`BTCMD_WRITEHCI`, `BTCMD_WRITEACL`, `BTCMD_READEVENT`, `BTCMD_READACL`,
  `compiler/include/devices/bluetoothhci.h`). Caminho mais rápido para uma Fase 3 funcional, porque reusa
  código já validado e mantém exatamente a fronteira que o `project.md` exige ("a stack não deve conhecer
  endpoints/pipes/requests do Poseidon" — aqui nem chega a conhecer Poseidon, só o device).
* **(b) Fork direto de `bluetooth.class`** — remover a camada de device (`dev.c`) e plugar a subtask
  (`nBTTask`) diretamente nas filas da Bluetooth Manager Task, eliminando uma camada de IORequest. Mais
  eficiente, mais invasivo, maior risco de regressão. Avaliar só depois que (a) estiver funcionando e se
  a indireção do device se mostrar um problema real (latência, throughput, complexidade de cancelamento).

Isso desloca a Fase 3 do `project.md` de "escrever uma classe Poseidon nova" para "escrever um adaptador
de transporte sobre uma classe já existente" — reduz risco e tempo de forma significativa.

---

## Proposta de diretórios (ajuste sobre a árvore de `project.md`)

A árvore proposta em `project.md` segue válida; o ajuste é só dentro de `ports/aros/`, refletindo o que
existe de fato:

```text
ports/aros/
├── library/            # bluetooth.library (genmodule .conf + mmakefile.src,
│                        #   seguindo o padrão de rom/usb/classes/bluetooth/bluetooth.conf)
├── task/                # Bluetooth Manager Task: CreateNewProcTags via padrão
│                        #   psdSpawnSubTask (poseidon.library.c:1434) como referência,
│                        #   sem depender de Poseidon diretamente
├── transport-usb/        # implementação de bt_hci_transport_ops sobre usbbluetooth.device
│                        #   (opção "a" acima) — abre o device existente, sem reescrever bluetooth.class
├── transport-sdio/       # futuro — chip combo onboard (ex. mesmo chip do bwfm no Raspberry Pi):
│                        #   Exec Resource sobre o Resource sdio existente, ver fase0-correcao-hidd.md
├── transport-uart/       # futuro — sem precedente de driver UART geral no AROS ainda,
│                        #   a construir seguindo o mesmo padrão de Resource
├── storage/              # persistência ENVARC:Sys/bluetooth/*.db
└── input/                 # integração com input.device via IND_WRITEEVENT
                          #   (mesmo padrão de bootkeyboard.class/bootmouse.class)
```

Cada `transport-*/` implementa `bt_hci_transport_ops` para um tipo de anexação física — ver
[`fase0-correcao-hidd.md`](fase0-correcao-hidd.md) para por que o mecanismo AROS por trás de cada um é
diferente (library/Poseidon para USB, Exec Resource para chips onboard via SDIO, etc.) e não uma
hierarquia HIDD/BOOPSI única.

`ports/test-host/` de `project.md` permanece igual — é onde o transporte virtual roda independente do
AROS.

---

## Proposta de API de transporte HCI (mapeamento sobre primitivas reais)

A interface `bt_hci_transport_ops` já desenhada em `project.md` se sustenta sem alteração. O que a Fase
0 acrescenta é o mapeamento concreto de cada operação para o que existe de fato:

| `bt_hci_transport_ops` | Implementação AROS (opção a — sobre `usbbluetooth.device`) |
|---|---|
| `open` | `OpenDevice("usbbluetooth.device", unit, ioreq, 0)` |
| `close` | `CloseDevice(ioreq)` |
| `send_command` | `IOBTHCIReq` com `iobt_Command = BTCMD_WRITEHCI`, `SendIO()` (não `DoIO` — ver risco de concorrência abaixo) |
| `send_acl` | `BTCMD_WRITEACL`, `SendIO()` |
| `send_sco` | `BTCMD_SETUPSCO` + escrita subsequente (API isochronous não foi totalmente explorada — ver limitação no levantamento §"não encontrado") |
| `start_receive` | `SendIO()` assíncrono com `BTCMD_READEVENT`/`BTCMD_READACL` reenfileirado a cada conclusão (padrão idêntico ao loop `GetMsg`/`PutMsg` de `bluetooth.class.c:668-756`) |
| `stop_receive` | `AbortIO()` nos IORequests pendentes de leitura |

Ponto de atenção: `usbbluetooth.device` já entrega bytes HCI crus (`iobt_Data`/`iobt_Length`), sem
interpretar protocolo — exatamente o contrato que `project.md` exige do transporte. Não é necessário
nenhum parsing na camada de transporte.

---

## Riscos de big-endian, alinhamento e concorrência (revisados com dados reais)

**Big-endian**: a Fase 0 confirmou um alvo m68k real e já suportado em árvore
(`arch/m68k-amiga/usb/denebusb/`, HCD para a placa Deneb). Isso resolve a lacuna que eu tinha apontado
antes (falta de alvo BE concreto para CI) — `denebusb` é candidato natural para testes reais em
big-endian, além de emulação. Risco residual: não foi auditado se `usbbluetooth.device`/`bluetooth.class`
já tratam endianness corretamente nos poucos campos que interpretam (ex. campos multi-byte do
`IOBTHCIReq` ou de eventos HCI que a classe eventualmente inspeciona) — isso deve ser auditado antes de
reusar qualquer trecho que toque nesses campos, não apenas assumido como seguro por já rodar em m68k.

**Alinhamento**: baixo risco na fronteira de transporte, porque `usbbluetooth.device` só move buffers de
bytes (`iobt_Data`/`iobt_Length`) sem casts para struct — condizente com a regra do `project.md`. O risco
real de alinhamento está inteiramente dentro do núcleo portátil (parsers HCI/L2CAP/ATT), não na
integração AROS — nenhum achado da Fase 0 muda essa avaliação.

**Concorrência**: o levantamento confirma que o modelo real do AROS é 100% `PutMsg`/`ReplyMsg` +
`Signal`/`Wait`, nunca callback direto (levantamento §4) — isso bate com o design da Bluetooth Manager
Task em `project.md`. Risco concreto a evitar: usar `psdDoPipe`-equivalente (chamada bloqueante, ex.
`DoIO()` em vez de `SendIO()`) dentro da Bluetooth Manager Task bloquearia a única task que processa todas
as máquinas de estado — violaria a regra "minimizar locks" / "não chamar aplicações diretamente a partir
de callbacks" do `project.md`. Regra a fixar desde já: a Manager Task **nunca** usa `DoIO()`/chamadas
bloqueantes de I/O — sempre `SendIO()` + fila de conclusão via `MsgPort` próprio.

**Divergência adicional a registrar**: a stack **não deve** tentar se integrar via a infraestrutura HIDD
de input (`rom/hidds/input`, `kbd`, `mouse`) para HID Classic/HOGP, apesar de o `project.md` mencionar
HIDDs como possível base para transportes não-USB. A Fase 0 encontrou essa infraestrutura sem nenhum
consumidor real no repositório atual (levantamento §8 e §11) — usar `input.device` +
`IND_WRITEEVENT` diretamente, como todo driver HID real hoje faz, é o caminho comprovado.

---

## Plano de implementação em commits pequenos (revisado)

1. `ai-context/`: levantamento e propostas da Fase 0 (este commit).
2. Núcleo portátil — tipos, `bt_read_le*`/`bt_write_le*`/`bt_read_be*`/`bt_write_be*`, testes round-trip
   e desalinhados (Fase 1 de `project.md`, sem tocar no AROS).
3. Buffer cursor + packet builder + testes.
4. Transporte HCI virtual (`ports/test-host/virtual_transport`) + testes com sequência HCI Reset simulada
   em host normal (não-AROS), LE e BE forçado.
5. Parser de HCI Event + encoder de HCI Command sobre o transporte virtual (entrega descrita na seção
   "Primeira entrega de código" de `project.md`).
6. `ports/aros/library/`: esqueleto de `bluetooth.library` (genmodule `.conf` + boilerplate,
   seguindo `bluetooth.conf`/`bluetooth.class.c:28-124` como referência) — ainda sem lógica.
7. `ports/aros/task/`: Bluetooth Manager Task mínima (cria task, cria `MsgPort`, loop
   `Wait`/`GetMsg` vazio) — valida o modelo de execução antes de plugar transporte real.
8. `ports/aros/transport-usb/`: adapter `bt_hci_transport_ops` sobre `usbbluetooth.device` (opção "a"
   acima). Critério de aceite: `HCI Reset` real chega a um adaptador Bluetooth USB físico e retorna
   `Command Complete`.
9. A partir daqui, seguir as Fases 4–8 de `project.md` sem mudança de plano.

Cada item acima deve ser commit(s) pequenos e testáveis isoladamente, sem pular etapas — em particular,
não iniciar o item 8 antes dos itens 2–5 estarem testados (regra explícita de `project.md`: "não integrar
Poseidon antes que essa base esteja testada").
