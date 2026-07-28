# Detecção de adaptador e bring-up de firmware — propostas de desenho

Este documento materializa a direção discutida para duas perguntas que surgiram tratando do
próximo passo real da Fase 3 (`project.md`) — "como sabemos que um device está disponível e
o habilitamos" e "o que fazer com upload de firmware específico de fabricante" — incluindo
duas correções de engano cometidas na própria discussão, para não serem repetidas.
Ainda **sem decisão de implementação fechada**; é o estado atual do raciocínio, para
avaliação e para dar continuidade sem perder o que já foi verificado em código real.

---

## Princípio geral: disponibilidade de adaptador não é uma pergunta única

O núcleo (`bluetooth/manager.h`, `bt_hci_transport_ops` em `bluetooth/transport.h`) é
transporte-neutro por design — não sabe nada de USB/UART/SDIO. "Saber que o device está
disponível" **não é responsabilidade do core nem de uma futura API pública única**: cada
`ports/aros/transport-*/` resolve isso de um jeito diferente, e a resposta depende
inteiramente de que tipo de port está rodando.

### Engano cometido e corrigido nesta rodada

* Cheguei a listar `transport-sdio/` como opção para o mesmo chip combo do Raspberry Pi.
  **Errado** — nenhum alvo conhecido deste projeto tem Bluetooth via SDIO; o combo Broadcom
  do Pi faz Wi-Fi por SDIO (`bwfm`) e Bluetooth por um barramento físico totalmente separado,
  o UART (PL011). Já corrigido diretamente em
  [`fase0-propostas.md`](fase0-propostas.md).
* Cheguei a explicar "hardware fixo, sem necessidade de detecção" como se fosse válido em
  geral. **Só vale para ports de placa única** (Raspberry Pi, ou qualquer port AROS
  compilado para uma placa específica conhecida). **x64 nativo não tem isso** — o mesmo
  binário roda em hardware desconhecido em tempo de build, então lá a detecção dinâmica não
  é opcional.

---

## Mecanismo por tipo de port

### x64 nativo (hardware desconhecido em build-time)

Praticamente todo chip Bluetooth em notebook/desktop x64 — mesmo os soldados na placa —
se apresenta ao sistema como um dispositivo USB interno. Não existe "fato fixo de board"
equivalente ao do Raspberry Pi.

Mecanismo confirmado em código real do Poseidon (checkout `/home/jaime/AROS`, branch
`feature/aarch64-vc4-gallium` — **checkout de outro trabalho do usuário, só consultado
para leitura, não usado para código deste projeto**):

* `psdAddEventHandler(struct MsgPort *mp, ULONG msgmask)` /
  `psdRemEventHandler(APTR peh)` — funções públicas de `poseidon.library`
  (`compiler/include/libraries/poseidon.h:88-89`, implementadas em
  `rom/usb/poseidon/poseidon.library.c:6587`), chamáveis por qualquer código que abra a
  library — **não exigem ser uma classe USB**.
* Uso real confirmado por um consumidor não-classe: a própria GUI de preferências do
  Poseidon (`rom/usb/poseidon/popo.gui.c:109`) registra
  `EHMF_ADDDEVICE|EHMF_REMDEVICE|EHMF_ADDBINDING|EHMF_CONFIGCHG|EHMF_DEVICEDEAD|EHMF_DEVICELOWPW`
  no próprio `MsgPort` e recebe `struct PsdEventNote` (`pen_Event`, `pen_Param1`,
  `pen_Param2`) via `PutMsg`/`ReplyMsg` (`poseidon.library.c:6594-6611`) — mesmo padrão de
  posse de mensagem (quem recebe deve responder para o slot ser liberado) já adotado nas
  propostas de evento da própria `bluetooth.library` em
  [`api-publica-propostas.md`](api-publica-propostas.md#3-mensagens-de-evento-versionadas).

Fluxo proposto: nosso port abre `poseidon.library`, registra
`EHMF_ADDBINDING|EHMF_REMBINDING`; ao receber `EHMB_ADDBINDING`, verifica se a classe
vinculada é `bluetooth.class` via `usbGetAttrs(UGA_CLASS, ...)`
(`rom/usb/poseidon/popo.gui.c:1130-1140` mostra o padrão real de uso); se for, abre
`usbbluetooth.device` para aquela unit através de `transport-usb`. `EHMB_REMBINDING` aciona
o teardown correspondente.

### Raspberry Pi (placa única, arch fixo)

O transporte é fixo (UART/PL011) — não precisa de detecção de anexação, mas **a revisão
exata do chip** (que determina qual arquivo de firmware carregar, ver seção seguinte) não é
fixa entre os vários modelos de Pi (3B, 3B+, Zero W, 4 etc. usam variantes diferentes do
mesmo chip Broadcom).

Novo dado verificado nesta rodada: existe de fato um parser de device tree real na árvore —
`arch/aarch64-raspi/boot/devicetree.c` + `arch/aarch64-raspi/boot/include/devicetree.h`
(`dt_parse`, `dt_find_node`, `dt_find_property`, `dt_find_node_by_phandle`). É candidato
natural para consultar o node `compatible` do chip Bluetooth (ex. `brcm,bcm43438-bt`, mesmo
identificador que o Linux usa) em vez de descobrir a revisão por uma query HCI.
**Ainda não verificado**: se essa API de device tree fica acessível para um driver rodando
depois do boot (fora do bootstrap) ou se só é usada durante o processo de boot do kernel —
checar antes de depender disso; se não estiver exposta, a alternativa é descobrir a revisão
via HCI (algumas famílias de chip respondem a uma leitura de versão antes do firmware).

---

## Bring-up de firmware vendor-specific

Achado chave: upload de firmware (Broadcom "Download Minidriver"/"Launch RAM" `0xFC2E`/
`0xFC4C`, Intel secure boot `0xFC01` + Secure Send, Realtek RTL_EPATCH `0xFC20`, Qualcomm/
Atheros) é feito inteiramente através de **comandos HCI vendor-specific normais** (OGF
0x3F) — confirmado indiretamente pelo header real do `usbbluetooth.device`
(`compiler/include/devices/bluetoothhci.h:46-55`): o único comando de escrita de comando é
`BTCMD_WRITEHCI`, não existe nem precisa existir um comando de controle USB "cru" separado.
Isso torna a etapa **transporte-neutra** — o mesmo chip Broadcom do Pi precisa do mesmo tipo
de upload de patchram, só que trafegando por UART em vez de USB.

Modelo de distribuição decidido: **tabela compilada, dispatch em runtime — não builds
separados por chipset**. Motivo: no x64 o hardware é desconhecido em build-time (não dá
para compilar "um build por notebook"); mesmo no Raspberry Pi, a revisão exata do chip varia
por modelo. O padrão real que resolve isso (mesma estrutura do `btusb`+`btintel`/`btrtl`/
`btbcm`/`btqca` do Linux): uma lista pequena e finita de fabricantes, compilada inteira no
binário, com dispatch por `idVendor`/`idProduct` (`compiler/include/devices/usb.h:127-128`)
em runtime.

Camadas propostas:

* **Portátil** (`protocols/vendor_init/`, já materializado no repositório: `bt_vendor_init_ops`
  em `include/bluetooth/vendor_init.h`, um módulo de referência totalmente implementado e
  testado em `protocols/vendor_init/dummy/` + `tests/vendor_init/`, e stubs com README
  apontando o que já se sabe para `broadcom/`, `intel/`, `realtek/` e `qualcomm/` — ver
  `protocols/vendor_init/README.md`) — conhece o protocolo de cada fabricante (formato dos
  comandos vendor-specific, como fatiar o binário de firmware em pacotes), roda sobre o
  `bt_hci_transport`/`command_queue` que já existem. Não sabe nada de Exec, filesystem ou
  Poseidon — mesma regra de portabilidade do resto do núcleo. Nenhum módulo real (Broadcom/
  Intel/Realtek/Qualcomm) tem código ainda, só o contrato e os stubs.
* **Porte** (`ports/aros/firmware/` ou dentro de cada `transport-*`) — só duas
  responsabilidades: identificar o chip (VID/PID via Poseidon no x64; device tree ou HCI no
  Pi) e localizar/ler o arquivo de firmware no filesystem AROS; depois invoca a camada
  portátil com os bytes.
* **Firmware nunca empacotado no repositório** — mesma preocupação de licenciamento que
  `project.md` já registra para BTstack/NimBLE: os blobs de Broadcom/Intel/Realtek são
  binários redistribuíveis mas não abertos. Carregar de um caminho tipo
  `SYS:Firmware/bluetooth/<vendor>/<arquivo>`, fornecido pelo usuário — mesmo modelo do
  pacote `linux-firmware` separado do kernel Linux.
* **Fallback automático, sem escolha do usuário** — `idVendor`/`idProduct` sem entrada na
  tabela (ex. CSR8510) pula a etapa inteira; `bt_controller` genérico assume que o
  controlador já fala HCI puro, sem instrução manual de qual chip é.

---

## Questões em aberto

1. Confirmar se `dt_find_node`/`dt_find_property` (device tree do Pi) ficam acessíveis a um
   driver depois do boot, ou só durante o bootstrap — determina se a revisão do chip no Pi
   vem de device tree ou de uma query HCI equivalente ao que o x64 precisaria de qualquer
   forma.
2. Em que ponto da Fase 3 do `project.md` entra a tabela de vendor init: depois do adapter
   USB básico funcionar sem firmware (CSR, primeiro alvo real já citado em
   `api-publica-propostas.md`), antes de declarar suporte a hardware popular real (Intel/
   Realtek) como concluído?
3. Onde exatamente mora o limite entre "porte identifica o chip" e "camada portátil sabe o
   protocolo" quando o próprio processo de identificação depende de uma troca HCI (Intel
   precisa de uma leitura de versão antes de saber que arquivo pedir) — a leitura de versão
   é genérica o suficiente para entrar em `protocols/vendor_init/`, ou é específica demais e
   fica no port?
