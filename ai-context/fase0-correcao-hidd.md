# Correção — o que "HIDD" realmente significa para este projeto

Este documento substitui as tentativas anteriores (preservadas, riscadas, em §11 de
[`fase0-levantamento.md`](fase0-levantamento.md)) de entender o papel do sistema HIDD do AROS e sua
relação com a stack Bluetooth. As duas primeiras tentativas erraram o alvo: a primeira procurou pelo
nome de função errado dentro do subsistema de *input*; a segunda ficou presa investigando código dentro
de `rom/`, quando o precedente relevante estava em `arch/`. Este documento é a versão corrigida.

## O conceito

HIDD = **Hardware Independent Device Driver**. Não é uma tecnologia específica (BOOPSI ou não) — é uma
**fronteira arquitetural**: de um lado, uma interface genérica e independente de hardware que o resto do
sistema (ou da stack) enxerga; do outro, uma implementação específica de cada chip/placa real. O padrão
se repete em vários subsistemas do AROS, cada um materializando essa fronteira de um jeito diferente:

| Domínio | Interface genérica | Implementação específica de hardware |
|---|---|---|
| Vídeo | `rom/hidds/gfx/` (classe BOOPSI genérica) | `vc4gfx`, `p96gfx`, `sagagfx`, `amigavideo`, `vesagfx`, `vgagfx`, `x11gfx`... — uma classe BOOPSI por placa |
| Teclado/mouse PS/2 | `rom/hidds/kbd/`, `rom/hidds/mouse/` (classe BOOPSI genérica) | `i8042` (`arch/all-pc/hidds/i8042/`) — classe BOOPSI específica do controlador PS/2 de PC |
| Armazenamento IDE/ATA | camada genérica de `ata.device` | `gayle_ata` (Amiga clássico), `ata_pci` (PCI) — classes BOOPSI por controlador |
| Chip Wi-Fi onboard (SoC) | `sdio` (Resource genérico do barramento SDIO do BCM2708) | `bwfm` (Resource específico do chip Broadcom FullMAC) |
| USB (qualquer classe, inclusive `bluetooth.class`) | Poseidon (`psdAddClass`, `usbDoMethodA`) | cada `.class` (library clássica) — o hardware já foi abstraído pelo HCD antes de chegar aqui |

O que varia é **o mecanismo AROS usado para implementar a fronteira** (classe BOOPSI/OOP, Exec Resource,
ou library clássica via Poseidon) — não o conceito, que é sempre o mesmo: hardware-independente de um
lado, hardware-específico do outro.

## A fronteira já existe no `project.md` — é o `bt_hci_transport_ops`

O projeto já desenhou essa fronteira, só não a tinha nomeado como "o HIDD da stack Bluetooth":
`bt_hci_transport_ops` (`project.md`, seção "Interface de transporte HCI") é exatamente isso — um
contrato hardware-independente (`open`, `close`, `send_command`, `send_acl`, `send_sco`, `send_iso`,
`start_receive`, `stop_receive`) que o núcleo portátil e a Bluetooth Manager Task enxergam, atrás do qual
cada chipset Bluetooth real tem sua própria implementação.

A pergunta de arquitetura, então, não é "a stack Bluetooth deveria usar HIDD?" — é **"para cada tipo de
anexação física de um chip Bluetooth, qual mecanismo AROS real deve implementar o lado específico de
`bt_hci_transport_ops`?"** A resposta varia por precedente real, não por preferência:

* **Dongle Bluetooth USB** → o precedente real e testado é o Poseidon `usbclass`
  (`rom/usb/classes/bluetooth/bluetooth.class`, já existe e faz quase todo o trabalho — ver
  `fase0-propostas.md`). Implementação de `bt_hci_transport_ops` = adapter fino sobre
  `usbbluetooth.device`.
* **Chip combo onboard via SDIO num SoC** (ex. o mesmo chip do `bwfm` no Raspberry Pi, que tem Wi-Fi e
  Bluetooth no mesmo silício) → o precedente real mais próximo, na mesma placa e no mesmo barramento, é
  `bwfm` sobre `sdio`: um **Exec Resource** específico do chip (`modtype=resource`,
  `arch/arm-native/soc/broadcom/2708/bwfm/bwfm.conf`), construído sobre o Resource genérico do
  barramento (`arch/arm-native/soc/broadcom/2708/sdio/sdio.conf`). Implementação de
  `bt_hci_transport_ops` para esse chip = um Resource novo (ex. `btbcm.resource`) espelhando a estrutura
  de `bwfm`, usando as mesmas primitivas `SDIOReadByte`/`SDIOWriteExt`/`SDIOEnableFunction`/
  `SDIOSetInterrupt` do `sdio` já existente.
* **Chip Bluetooth via UART** (comum em outros SoCs/placas) → não há hoje nenhum driver UART de uso
  geral no AROS para isso; o que existe é só acesso a registrador do PL011 para console de debug do boot
  (`arch/*/boot/serialdebug.c`, `platform_bcm2708.c`). Precisaria ser criado do zero, mas seguindo o
  mesmo padrão de Resource visto em `sdio`/`bwfm`, não uma classe BOOPSI.
* **Chip Bluetooth atrás de PCI puro** (se algum dia relevante) → aí sim o precedente correto seria uma
  classe BOOPSI/HIDD, seguindo `ata_pci`/`gayle_ata` como modelo, porque é exatamente esse o padrão que
  o AROS usa para dispositivos PCI enumerados.

## O que determina o mecanismo: domínio, não barramento

Verificação adicional (pedida explicitamente, porque a hipótese anterior — "USB usa library porque USB já
abstrai hardware, então HIDD sobra pra quem não tem outro barramento" — não tinha sido checada contra
mais de um domínio): comparando **rede**, o domínio mais parecido com Bluetooth (também é
comando/evento/pacote entrando e saindo, também existe em PCI e em USB no AROS), nos dois barramentos:

| Domínio | Via PCI | Via USB (Poseidon) |
|---|---|---|
| Rede | `e1000`, `rtl8139` → `modtype=device` (SANA-II clássico) | `asixeth`, `cdceth`, `lan78xx`, `rndis` → `usbclass` |
| Wi-Fi (SoC/SDIO) | — | `bwfm` → Exec Resource |
| Bluetooth | — | `bluetooth.class`/`usbbluetooth.device` → `usbclass`/device |

**Nenhum desses usa HIDD, em nenhum barramento.** A variável que decide não é "que barramento carrega os
bytes" — é o **domínio**: subsistemas de **transferência de pacotes/dados em fluxo** (rede, áudio,
endpoints USB, e — pelo precedente real já existente — Bluetooth/HCI) usam consistentemente o mecanismo
clássico de device/library/resource com fila de `IORequest`/`MsgPort`, **independente do barramento**
embaixo. HIDD/BOOPSI aparece nos domínios que são conceitualmente "objeto com propriedades e enumeração
genérica" — vídeo, teclado/mouse (estado de tecla, não fluxo de dados), e o próprio barramento PCI/ATA
sendo enumerado como uma coleção de objetos.

Isso é reforçado pelo único precedente Bluetooth que já existe: foi escrito pelo mesmo autor
(Chris Hodges) que também escreveu as classes HIDD internas do Poseidon (`USBController`/`USBDevice`,
ver §11 do levantamento) — ou seja, alguém com os dois mecanismos disponíveis e familiaridade com ambos
escolheu device/library para Bluetooth especificamente, não HIDD.

**Conclusão revisada**: `rom/hidds/bluetooth` (uma classe HIDD genérica) não é o mecanismo certo para o
domínio de transporte HCI em si — isso é device/library/resource, seguindo `usbbluetooth.device` (USB) e
o padrão `bwfm`/`sdio` (SDIO onboard). O que continua válido, e que não depende dessa escolha, é a
observação anterior sobre Poseidon expor `USBController`/`USBDevice` como classes HIDD **auxiliares**,
só para enumeração genérica no sistema — se um dia fizer sentido o AROS enumerar controladores Bluetooth
de forma genérica (ex. para uma tela de Preferences), isso poderia ser uma classe HIDD auxiliar por cima
do mecanismo real, sem mudar como o transporte HCI em si funciona.

## Conclusão prática

Não existe *um* "HIDD Bluetooth" a implementar. Existe uma fronteira já definida
(`bt_hci_transport_ops`) e, atrás dela, uma implementação por tipo de anexação física — mas o mecanismo
AROS por trás segue o **domínio** (transferência de comando/evento/pacote), não o barramento:

* **USB** → Poseidon/`usbclass` + device, seguindo `usbbluetooth.device` (precedente real e direto).
* **SDIO onboard** (chip combo, ex. Raspberry Pi) → Exec Resource, seguindo `bwfm`+`sdio` (precedente
  real e direto, mesmo domínio de dado-em-fluxo que rede/Bluetooth).
* **UART onboard** → sem precedente ainda; a construir como Resource/device, pelo mesmo motivo de
  domínio, não como classe HIDD.
* **PCI puro** (hipotético) → também device clássico, não BOOPSI — confirmado pelo precedente de rede
  em PCI (`e1000`, `rtl8139`), que é o domínio mais próximo de Bluetooth e não usa HIDD apesar de estar
  em PCI, o mesmo barramento de `ata_pci` (que usa HIDD porque armazenamento é outro domínio).

Isso substitui a proposta anterior de uma hierarquia BOOPSI genérica (`CLID_Hidd_BluetoothHCI`) como
mecanismo do transporte em si — não há precedente real disso em nenhum domínio parecido (rede, áudio,
USB, Wi-Fi onboard, Bluetooth USB). A única sobrevivência do papel do HIDD aqui é auxiliar/opcional: uma
classe HIDD só para enumeração genérica no sistema (espelhando `USBController`/`USBDevice` do Poseidon),
sem participar do caminho de dados do HCI.

Isso não muda a proposta de diretórios de `fase0-propostas.md` (`ports/aros/transport-usb/`,
`transport-uart/`, etc. sob `bt_hci_transport_ops`) — só corrige a suposição sobre o que vai *dentro* de
cada implementação específica de chip quando esse trabalho chegar (Fase 3 em diante para USB; fases
futuras para SDIO/UART onboard).
