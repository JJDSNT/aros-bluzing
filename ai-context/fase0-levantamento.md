# Fase 0 — Levantamento factual do AROS para a stack Bluetooth

Checkout investigado: `/home/jaime/AROS-bluetooth` (branch `feature/bluetooth-stack`), somente leitura,
nenhum build/configure executado. Todos os trechos abaixo são cópias literais do repositório, com
caminho de arquivo e símbolo indicados. Quando algo não foi encontrado, isso é dito explicitamente.

**Achado crítico prévio aos 10 tópicos**: já existe no AROS um driver de classe Poseidon chamado
`bluetooth.class`, escrito por Chris Hodges (autor original do Poseidon), em
`rom/usb/classes/bluetooth/`. Ele implementa exatamente a camada de "transporte HCI sobre USB" que o
`project.md` pede (um `btusb.class`), porém como um **device AmigaOS clássico** (`usbbluetooth.device`)
com IORequests customizados (`BTCMD_WRITEHCI`, `BTCMD_READACL`, `BTCMD_WRITEACL`, etc.), sem qualquer
stack de protocolos acima do HCI. Isso é usado extensivamente como referência abaixo e deve ser tratado
como ponto de partida real (ou pelo menos como precedente arquitetural direto) em vez de inventar uma
classe do zero.

---

## 1. Poseidon (USB stack do AROS)

Implementado em `rom/usb/poseidon/`. É uma AROS library convencional (`poseidon.library`), gerada pelo
sistema de "genmodule" do AROS (arquivo `.conf` + `mmakefile.src`).

Arquivo de configuração da library: `rom/usb/poseidon/poseidon.conf`
```
##begin config
version 5.3
libbase ps
libbasetype struct PsdBase
libbasetypeextern struct Library
residentpri 48
basename psd
copyright Copyright 2002-2009 Chris Hodges, 2009-2026 The AROS Dev Team
##end config
```
O mesmo arquivo `.conf` também declara duas classes HIDD/BOOPSI internas do próprio Poseidon
(`USBController` e `USBDevice`, ver tópico 11).

Arquitetura geral (a partir de `poseidon.library.h`, `poseidon_intern.h` e `poseidon.library.c`, ~9500
linhas):
* `struct PsdBase` — base da library, contém `ps_Classes` (lista de classes USB carregadas),
  hardware/devices/config em listas encadeadas (`struct PsdDevice`, `struct PsdConfig`,
  `struct PsdInterface`, `struct PsdEndpoint`, `struct PsdPipe`).
* Cada HCD (host controller driver) é registrado como "hardware" via `psdAddHardware()` e roda sua
  própria task/processo (`pDeviceTask`, ver tópico 4).
* Uma **classe** USB (device driver de alto nível, ex. HID, mass storage, bluetooth) é uma **AROS
  library separada** (`xxx.class`), carregada dinamicamente via `OpenLibrary()` dentro de
  `psdAddClass()`:

`rom/usb/poseidon/poseidon.library.c:5683` (`psdAddClass`):
```c
AROS_LH2(struct PsdUsbClass *, psdAddClass,
         AROS_LHA(STRPTR, name, A1),
         AROS_LHA(ULONG, vers, D0),
         LIBBASETYPEPTR, ps, 35, psd)
{
    ...
    while(*name) {
        if((cls = OpenLibrary(name, vers))) {
            break;
        }
        ...
    }
    if(cls) {
        ...
        puc->puc_Node.ln_Name = puc->puc_ClassName = psdCopyStr(cls->lib_Node.ln_Name);
        usbGetAttrs(UGA_CLASS, NULL,
                    UCCA_Priority, &pri,
                    UCCA_Description, &desc,
                    TAG_END);
        puc->puc_Node.ln_Pri = pri;
        psdLockWritePBase();
        Enqueue(&ps->ps_Classes, &puc->puc_Node);
        psdUnlockPBase();
        ...
        psdSendEvent(EHMB_ADDCLASS, puc, NULL);
        return(puc);
    }
```
Classes padrão são registradas na inicialização em `rom/usb/poseidon/usbromearlystartup.c` e
`usbromlatestartup.c`:
```c
psdAddClass("hub.class", 0);
msdclass = psdAddClass("massstorage.class", 0);
psdAddClass("hubss.class", 0);
...
psdAddClass("hid.class", 0);
psdAddClass("bootmouse.class", 0);
psdAddClass("bootkeyboard.class", 0);
```
Cada classe expõe uma interface AmigaOS-style com 3 funções de biblioteca definidas em
`compiler/include/libraries/usbclass.h`: `usbGetAttrsA`, `usbSetAttrsA`, `usbDoMethodA` — não é BOOPSI,
é uma API de library "clássica" com métodos despachados por `usbDoMethodA(methodid, ...)`.

---

## 2. Exemplos reais de class drivers Poseidon

Diretório com todos os class drivers existentes: `rom/usb/classes/` (mais de 25 classes) e
`workbench/devs/USB/classes/` (uma árvore alternativa/mais antiga com HID e MassStorage).

Três exemplos estudados em detalhe:

### a) `bluetooth.class` — `rom/usb/classes/bluetooth/`
Arquivos: `bluetooth.class.c`, `bluetooth.class.h`, `bluetooth.h`, `dev.c`, `dev.h`, `bluetooth.conf`.
Já é, na prática, um transporte HCI-sobre-USB (ver seção introdutória e tópicos 3/4/6 abaixo).

### b) `hid.class` — `rom/usb/classes/hid/hid.class.c` (7464 linhas)
Classe HID USB genérica completa: parser de report descriptor, roteamento de reports, suporte a
tablets Wacom (`nParseWacom`), GUI de configuração MUI. Ver tópico 7 para o parser.

### c) `bootkeyboard.class` / `bootmouse.class` — `rom/usb/classes/bootkeyboard/`,
`rom/usb/classes/bootmouse/`
Classes especializadas para teclado/mouse em modo "boot protocol" (HID_BOOT_SUBCLASS). Estrutura
idêntica ao padrão de binding + subtask do bluetooth.class, mas alimentam eventos diretamente para
`input.device` (ver tópico 8).

### d) `hub.class` — `rom/usb/classes/hub/hub.class.c` (1534 linhas)
Implementa hubs USB — é a classe que efetivamente decide bindings de **device** (não apenas de
interface), e reencaminha eventos hotplug para as demais classes via `UCM_Hub*` (ver tópico 3).

Estrutura comum a todos (visível em `bluetooth.class.c`, `hid.class.c`, `hub.class.c`):
```c
static const APTR DevFuncTable[] = { ... };   /* só quando a classe expõe um device */

static int libInit(LIBBASETYPEPTR nh) { ... }
static int libOpen(LIBBASETYPEPTR nh) { ... }
static int libExpunge(LIBBASETYPEPTR nh) { ... }

ADD2INITLIB(libInit, 0)
ADD2OPENLIB(libOpen, 0)
ADD2EXPUNGELIB(libExpunge, 0)

/* métodos de binding chamados pelo Poseidon via usbDoMethodA */
struct NepClassXXX * usbAttemptInterfaceBinding(...);
struct NepClassXXX * usbForceInterfaceBinding(...);
void usbReleaseInterfaceBinding(...);
```
(`bluetooth.class.h:26-28`, `bluetooth.class.c:134-358`)

Cada `.conf` do class driver (ex. `bluetooth.conf`, `rom/usb/classes/bluetooth/bluetooth.conf`) declara
o `libbasetype` e a `functionlist` com as 3 funções de despacho `usbGetAttrsA/usbSetAttrsA/usbDoMethodA`,
exatamente como qualquer outra AROS library.

---

## 3. Fluxo de attach/detach

O algoritmo central é `psdHubClassScan()`, em `rom/usb/poseidon/poseidon.library.c:6187`. Ele percorre
`ps->ps_Classes` (lista de classes registradas) e, para cada interface de um `PsdDevice`, chama o
método `UCM_AttemptInterfaceBinding` (ou `UCM_ForceInterfaceBinding` se há um "forced binding"
configurado) via `usbDoMethod()`:

```c
} else {
    binding = (APTR) usbDoMethod(UCM_AttemptInterfaceBinding, pif);
}
Forbid();
if(binding) {
    ...
    pif->pif_IfBinding = binding;
    pif->pif_ClsBinding = puc;
    hasifbinding = pc->pc_CfgNum;
    puc->puc_UseCnt++;
    psdSendEvent(EHMB_ADDBINDING, pd, NULL);
    Permit();
    break;
}
```
(`poseidon.library.c:6304-6330`)

No lado da classe, `usbAttemptInterfaceBinding()` decide (por classe/subclasse/protocolo USB, ou por
endpoints presentes) se aceita o dispositivo, e delega para `usbForceInterfaceBinding()`, que:
1. procura uma unit já existente (reconexão do mesmo dispositivo/interface), ou aloca uma nova
   `struct NepClassBT` (`AllocVec` + `AddTail` na lista de units da classe);
2. dispara uma subtask dedicada (`psdSpawnSubTask(buf, nBTTask, ncp)`), aguardando via
   `psdBorrowLocksWait()` que a subtask sinalize que terminou de alocar pipes/endpoints
   (`bluetooth.class.c:294-313`).

Detach/unbind acontece por `psdReleaseIfBinding()` / `psdReleaseDevBinding()`
(`poseidon.library.c:6100`, `:6075`), que chamam `UCM_HubReleaseIfBinding` /
`UCM_HubReleaseDevBinding` na classe hub, que por sua vez aciona `UCM_ReleaseInterfaceBinding` na
classe alvo. Na bluetooth.class isso vira `usbReleaseInterfaceBinding()`
(`bluetooth.class.c:329-358`), que sinaliza a subtask com `SIGBREAKF_CTRL_C` e espera ela terminar:
```c
void usbReleaseInterfaceBinding(struct NepBTBase *nh, struct NepClassBT *ncp)
{
    ...
    Forbid();
    ncp->ncp_ReadySignal = SIGB_SINGLE;
    ncp->ncp_ReadySigTask = FindTask(NULL);
    if(ncp->ncp_Task)
    {
        Signal(ncp->ncp_Task, SIGBREAKF_CTRL_C);
    }
    Permit();
    while(ncp->ncp_Task)
    {
        Wait(1UL<<ncp->ncp_ReadySignal);
    }
    ...
}
```
A subtask (`nBTTask`, `bluetooth.class.c:619-913`) recebe `SIGBREAKF_CTRL_C` no seu `Wait(sigmask)`
principal e sai do loop, então aborta todos os pipes pendentes (`psdAbortPipe`/`psdWaitPipe`), drena
filas de leitura/escrita e responde a todos os `IOBTHCIReq` pendentes com `IOERR_ABORTED` antes de
liberar a estrutura (`nFreeBT`) — é o padrão de cleanup idempotente/seguro contra use-after-free que o
`project.md` exige para hot-unplug.

Hub e hotplug físico: eventos de conexão/desconexão física do dispositivo chegam via
`psdSendEvent(EHMB_ADDDEVICE/EHMB_REMDEVICE/...)` (definidos em `poseidon_intern.h`), processados por
uma task de broadcast de eventos própria do Poseidon (`pEventHandlerTask`,
`poseidon.library.c:8990`).

---

## 4. Modelo de I/O assíncrono

Poseidon usa o modelo de IORequest/Message do Exec, com dois modos por "pipe" USB (`struct PsdPipe`):

**Bloqueante** — `psdDoPipe()` (`poseidon.library.c:4706`):
```c
AROS_LH3(LONG, psdDoPipe, ...)
{
    ...
    pp->pp_IOReq.iouh_Data = data;
    pp->pp_IOReq.iouh_Length = len;
    ...
    PutMsg(&pd->pd_Hardware->phw_TaskMsgPort, &pp->pp_Msg);
    ++pd->pd_IOBusyCount;
    GetSysTime((APTR) &pd->pd_LastActivity);
    return(psdWaitPipe(pp));
}
```

**Assíncrono** — `psdSendPipe()` (`poseidon.library.c:4743`), idêntico mas sem esperar:
```c
PutMsg(&pd->pd_Hardware->phw_TaskMsgPort, &pp->pp_Msg);
GetSysTime((APTR) &pd->pd_LastActivity);
++pd->pd_IOBusyCount;
```
A conclusão chega de volta como mensagem no `MsgPort` que a classe passou em `psdAllocPipe(device, mp,
endpoint)`. Do lado do HCD (`rom/usb/pciusb/uhwcmd.c`), a conclusão da transferência (via IRQ) resulta
em `Remove(&cmpioreq->iouh_Req.io_Message.mn_Node); ReplyMsg(&cmpioreq->iouh_Req.io_Message);`
(`uhwcmd.c:1277-1302`) — ou seja, o driver de hardware devolve a mensagem para o MsgPort de origem, que
sinaliza a task da classe via `mp_SigBit`.

Exemplo real de consumo assíncrono, no loop principal da subtask `nBTTask`
(`bluetooth.class.c:668-756`):
```c
sigmask = (1UL<<ncp->ncp_Unit.unit_MsgPort.mp_SigBit)
        | (1UL<<ncp->ncp_TaskMsgPort->mp_SigBit) | SIGBREAKF_CTRL_C;
...
while((pp = (struct PsdPipe *) GetMsg(ncp->ncp_TaskMsgPort)))
{
    if(pp == ncp->ncp_EPEventIntPipe) { /* evento HCI via interrupt IN concluído */ ... }
    else if(pp == ncp->ncp_EPACLOutPipe) { /* bulk OUT concluído */ ... ReplyMsg((struct Message *) ioreq); }
    else if(pp == ncp->ncp_EPACLInPipe)  { /* bulk IN concluído */ ... ReplyMsg((struct Message *) ioreq); }
}
...
sigs = Wait(sigmask);
```
Ou seja: não há callback de função direta — a notificação é **sempre** `PutMsg`/`ReplyMsg` em um
`MsgPort` mais `Signal` do bit associado, consumido com `GetMsg`/`Wait` pela task dona do binding. Isso
casa exatamente com o modelo pretendido no `project.md` ("Poseidon callback ou device completion → fila
de eventos HCI → Signal → Bluetooth Manager Task").

Transferências de controle usam `psdPipeSetup()` (monta o Setup Packet) + `psdDoPipe()`/`psdSendPipe()`
sobre o pipe do endpoint 0 (`ncp_EPCmdPipe` na bluetooth.class); transferências interrupt/bulk usam o
pipe do endpoint correspondente da mesma forma. Não foi necessário examinar isochronous em detalhe para
Bluetooth Classic (SCO), mas a API expõe `UHCMD_STARTRTISO`/`STOPRTISO`
(`poseidon.library.c:5670`, `psdAllocRTIsoHandler`) para isso — real-time isochronous com handler
próprio, distinto do mecanismo de pipe normal.

---

## 5. Convenções para libraries e devices do AROS

O padrão AROS usa um "genmodule" (arquivo `.conf` + `mmakefile.src`) que gera boilerplate (LIBBASETYPE,
tabela de vetores, `AROS_LH*`/`AROS_LD*` macros de chamada) a partir de uma `functionlist`.

**Library** — exemplo real mínimo, `rom/usb/classes/bluetooth/bluetooth.conf`:
```
##begin config
version 4.3
libbase nh
libbasetype struct NepBTBase
libbasetypeextern struct Library
residentpri 48
basename nep
##end config
##begin functionlist
LONG usbGetAttrsA(ULONG type, APTR usbstruct, struct TagItem *taglist) (D0,A0,A1)
LONG usbSetAttrsA(ULONG type, APTR usbstruct, struct TagItem *taglist) (D0,A0,A1)
IPTR usbDoMethodA(ULONG methodid, IPTR *methoddata) (D0,A1)
##end functionlist
```
Boilerplate correspondente em `bluetooth.class.c:28-124`: `libInit`/`libOpen`/`libExpunge` +
`ADD2INITLIB`/`ADD2OPENLIB`/`ADD2EXPUNGELIB`. `poseidon.conf` (`rom/usb/poseidon/poseidon.conf`) é um
segundo exemplo real, maior, de library top-level (não uma usbclass).

**Device** — exemplo real completo em `rom/usb/classes/bluetooth/dev.c` (`usbbluetooth.device`):
funções clássicas `devInit`, `devOpen`, `devClose`, `devExpunge`, `devReserved`, `devBeginIO`,
`devAbortIO`, com a tabela de vetores construída manualmente (porque este device é criado via
`MakeLibrary()` dentro do `libInit` da classe, não via genmodule):
```c
static const APTR DevFuncTable[] =
{
    &AROS_SLIB_ENTRY(devOpen, dev, 1),
    &AROS_SLIB_ENTRY(devClose, dev, 2),
    &AROS_SLIB_ENTRY(devExpunge, dev, 3),
    &AROS_SLIB_ENTRY(devReserved, dev, 4),
    &AROS_SLIB_ENTRY(devBeginIO, dev, 5),
    &AROS_SLIB_ENTRY(devAbortIO, dev, 6),
    (APTR) -1,
};
```
(`bluetooth.class.c:16-26`), criado em `libInit`:
```c
if((nh->nh_DevBase = (struct NepBTDevBase *) MakeLibrary((APTR) DevFuncTable, NULL, (APTR) devInit,
   sizeof(struct NepBTDevBase), NULL)))
{
    nh->nh_DevBase->np_ClsBase = nh;
    Forbid();
    AddDevice((struct Device *) nh->nh_DevBase);
    ...
}
```
(`bluetooth.class.c:45-59`). `devBeginIO` (`dev.c:216-310`) mostra o padrão canônico AROS: para comandos
que exigem I/O real, marca `ret = RC_DONTREPLY` e faz `PutMsg(&ncp->ncp_Unit.unit_MsgPort, ...)` para a
subtask processar; para os demais, chama `TermIO()` imediatamente (resposta síncrona/quick I/O).

Cabeçalho custom de device (`compiler/include/devices/bluetoothhci.h`) já define `struct IOBTHCIReq`
com `iobt_Actual`, `iobt_Length`, `iobt_Data`, e comandos não-padrão `BTCMD_WRITEHCI`, `BTCMD_READACL`,
`BTCMD_WRITEACL`, `BTCMD_READEVENT`, `BTCMD_SETUPSCO`, etc. — ou seja, já existe um contrato de
IORequest para "device HCI Bluetooth" no AROS, definido por Chris Hodges/AROS Dev Team em 2005/2011.
Isso é uma referência direta (embora datada e sem qualquer stack de protocolos acima) para desenhar a
interface entre `Poseidon Bluetooth USB class` e a camada de transporte HCI da nova stack.

---

## 6. Integração com tasks, signals e message ports do Exec

Criação de task: Poseidon não usa `CreateTask()` diretamente; usa `psdSpawnSubTask()`
(`rom/usb/poseidon/poseidon.library.c:1434`), que, se `dos.library` está disponível, cria um
**processo** com `CreateNewProcTags()`:
```c
if(pOpenDOS(ps)) {
    subtask = CreateNewProcTags(NP_Entry, initpc,
                                NP_StackSize, SUBTASKSTACKSIZE,
                                NP_Priority, ps->ps_GlobalCfg->pgc_SubTaskPri,
                                NP_Name, name,
                                NP_CopyVars, FALSE,
                                NP_UserData, userdata,
                                TAG_END);
    return((struct Task *) subtask);
}
```
e, caso contrário (early boot, antes de dos.library), cai para o caminho Exec puro
(`AllocEntry`/`NewAllocEntry` de memória de task + `AddTask(nt, initpc, NULL)`,
`poseidon.library.c:1466-1505`) — um exemplo real de criação de task sem `dos.library`, útil se a
Bluetooth Manager Task precisar rodar cedo no boot.

Cada classe (ex. `bluetooth.class.c:958-996`, função `nAllocBT()`) monta seu próprio conjunto de
sincronização dentro da subtask:
```c
ncp->ncp_Unit.unit_MsgPort.mp_SigBit = AllocSignal(-1);
ncp->ncp_Unit.unit_MsgPort.mp_SigTask = thistask;
ncp->ncp_Unit.unit_MsgPort.mp_Node.ln_Type = NT_MSGPORT;
ncp->ncp_Unit.unit_MsgPort.mp_Flags = PA_SIGNAL;

if((ncp->ncp_TaskMsgPort = CreateMsgPort())) {
    if((ncp->ncp_EventReplyPort = CreateMsgPort())) {
        if((ncp->ncp_EPCmdPipe = psdAllocPipe(ncp->ncp_Device, ncp->ncp_TaskMsgPort, NULL))) {
            ...
```
Sincronização "task pronta"/"task saiu" entre a task que fez o binding e a subtask usa
`SIGB_SINGLE`/`SetSignal(0, SIGF_SINGLE)` + `psdBorrowLocksWait()` (bloqueio da task chamadora até a
subtask sinalizar), e `Signal(ncp->ncp_ReadySigTask, 1UL<<ncp->ncp_ReadySignal)` no fim de
`nAllocBT()`/`nFreeBT()` (`bluetooth.class.c:633-640`, `:1013-1018`, `:1057-1062`).

Terminação controlada de task usa `SIGBREAKF_CTRL_C` (equivalente ao "Ctrl-C" do Exec) — ver tópico 3.
Fila de comandos própria da unit é apenas o `unit_MsgPort` embutido em `struct Unit`
(`ncp->ncp_Unit.unit_MsgPort`), reaproveitado como fila FIFO de `IOBTHCIReq` pendentes via
`PutMsg`/`GetMsg` — outro exemplo real e direto do padrão "message port como fila single-consumer" que
o `project.md` pede para a Bluetooth Manager Task.

---

## 7. Parser HID existente no AROS

Sim, existe um parser completo de HID report descriptor em `rom/usb/classes/hid/hid.class.c`
(7464 linhas). Função central: `nParseReport()` (`hid.class.c:2594`), que é uma máquina de estados que
percorre os bytes do report descriptor byte a byte, decodificando item tag/size/type conforme a
especificação USB HID 1.11:
```c
BOOL nParseReport(struct NepClassHid *nch, struct NepHidReport *nhr)
{
    UBYTE *rptr = nhr->nhr_ReportBuf;
    ...
    while(rptr && (rptr < rptrend))
    {
        itag = *rptr & REPORT_ITAG_MASK;
        isize = *rptr & REPORT_ISIZE_MASK;
        itype = *rptr & REPORT_ITYPE_MASK;
        if(*rptr++ == REPORT_LONGITEM) { ... }
        else { switch(isize) { case REPORT_ISIZE_0: ... case REPORT_ISIZE_4: ... } }
        switch(itype)
        {
            case REPORT_ITYPE_MAIN:   /* Input/Output/Feature/Collection/End Collection */ ...
            case REPORT_ITYPE_GLOBAL: /* Usage Page, Logical/Physical Min/Max, Report Size/Count/ID */ ...
            ...
        }
    }
```
Trata Main/Global/Local items, collections (`RP_COLL_APP`, `RP_COLL_LOGICAL`, etc.), push/pop de
estado global, múltiplos Report IDs por interface (`nch_ReportMap` indexado por `reportid`), usage
pages/usages/usage ranges. As constantes de item (`REPORT_MAIN_INPUT`, `REPORT_GLOB_USAGE`,
`REPORT_LOCL_USAGE`, `RP_PAGE_*`, etc.) estão em `compiler/include/devices/usb_hid.h`. A leitura do HID
Descriptor via control transfer (`UDT_HID`, `USR_GET_DESCRIPTOR`) e do Report Descriptor
(`UDT_REPORT`) ocorre em `nReadReports()` (`hid.class.c:2055`).

**Reaproveitável para HID over Bluetooth?** Em grande parte sim, no nível conceitual: os bytes de um
Report Descriptor HID são idênticos entre USB HID e Bluetooth HIDP/HOGP (é o mesmo formato de
especificação HID, apenas o transporte muda). Porém o parser atual está fortemente acoplado à classe
Poseidon (`struct NepClassHid`, alocação via `psdAllocVec`, chamadas diretas a `psdPipeSetup`/
`psdDoPipe` para buscar o descriptor por controle USB, e roteamento final para `input.device` embutido
no mesmo arquivo). Para reutilização real conforme pede o `project.md` ("parser comum" entre USB HID e
Bluetooth HID), seria necessário extrair a máquina de estados de `nParseReport`/`nReadReport` para uma
unidade independente de Poseidon — hoje ela **não** existe como biblioteca separada; é código interno
de uma única classe USB.

---

## 8. Integração com `input.device`

Caminho real usado por drivers HID USB existentes (`bootkeyboard.class`, `bootmouse.class`, e a
`hid.class` genérica): abrir `input.device` como um device Exec clássico e escrever eventos com
`IND_WRITEEVENT`. Abertura, em `rom/usb/classes/bootkeyboard/bootkeyboard.class.c:855-861`:
```c
if((nch->nch_InpMsgPort = CreateMsgPort()))
{
    if((nch->nch_InpIOReq = (struct IOStdReq *) CreateIORequest(nch->nch_InpMsgPort, sizeof(struct IOStdReq))))
    {
        if(!OpenDevice("input.device", 0, (struct IORequest *) nch->nch_InpIOReq, 0))
        {
            nch->nch_InputBase = (struct Library *) nch->nch_InpIOReq->io_Device;
```
Envio de evento (`bootkeyboard.class.c:679-687`):
```c
nch->nch_FakeEvent.ie_Class = IECLASS_RAWKEY;
nch->nch_FakeEvent.ie_SubClass = 0;
nch->nch_FakeEvent.ie_Code = iecode|IECODE_UP_PREFIX;
nch->nch_FakeEvent.ie_NextEvent = NULL;
nch->nch_FakeEvent.ie_Qualifier = qualifier;
nch->nch_InpIOReq->io_Data = &nch->nch_FakeEvent;
nch->nch_InpIOReq->io_Length = sizeof(struct InputEvent);
nch->nch_InpIOReq->io_Command = IND_WRITEEVENT;
DoIO((struct IORequest *) nch->nch_InpIOReq);
```
`IND_WRITEEVENT` (`compiler/include/devices/input.h:17`, `#define IND_WRITEEVENT (CMD_NONSTD + 2)`) é
o comando padrão do `input.device` para injetar um `struct InputEvent` sintético na fila global de
eventos do sistema (a mesma usada pelo teclado/mouse físico), de onde `intuition.library` e aplicações
que assinam `IND_ADDHANDLER` o recebem.

**Divergência importante encontrada**: existe *também* uma infraestrutura HIDD de input mais nova em
`rom/hidds/input/`, `rom/hidds/kbd/`, `rom/hidds/mouse/` (classes BOOPSI `CLID_Hidd_Input`,
`CLID_Hidd_Kbd`, `CLID_Hidd_Mouse` — ver tópico 11), com um mecanismo de callback
(`InputIrqCallBack_t`, `HIDD_Input_AddHardwareDriver()`). Porém, buscando em todo o repositório, **nenhum
driver real** (incluindo os drivers USB HID do próprio Poseidon) usa esse caminho —
`HIDD_Input_AddHardwareDriver` só aparece definido em `rom/hidds/input/inputclass.c` /
`input_init.c`, sem nenhum chamador externo encontrado. Ou seja: o caminho **efetivamente usado hoje**
por qualquer driver de entrada real no AROS é o clássico `OpenDevice("input.device")` +
`IND_WRITEEVENT`, e é esse o padrão que a stack Bluetooth (HIDP/HOGP) deve seguir — não a HIDD input
subsystem, que parece não estar conectada a nenhum backend concreto neste checkout.

---

## 9. Sistema de build

AROS usa `mmake` (metamake), com `mmakefile.src` por diretório e o `.conf` do genmodule para
libraries/devices/classes. Exemplo real de registro de uma usbclass,
`rom/usb/classes/bluetooth/mmakefile.src`:
```
include $(SRCDIR)/config/aros.cfg

USER_CPPFLAGS := -DMUIMASTER_YES_INLINE_STDARG
USER_LDFLAGS := -static

FILES :=    bluetooth.class dev debug

#MM- kernel-usb-classes-bluetooth : kernel-usb-usbclass kernel-usb-poseidon-includes

%build_module_library mmake=kernel-usb-classes-bluetooth \
    modname=bluetooth modtype=usbclass modsuffix="class" \
    files="$(FILES)"

%common
```
O diretório pai `rom/usb/classes/mmakefile.src` agrega todas as classes em metas `#MM`
(`kernel-usb-classes-common`, `kernel-usb-classes`, `kernel-usb-classes-kobj`, etc.) e a própria
`usbclass.library` abstrata é construída com `%build_module_abi mmake=kernel-usb-usbclass
modname=usbclass modtype=library`.

O diretório `rom/usb/poseidon/mmakefile.src` mostra a construção da library principal:
```
%build_module mmake=kernel-usb-poseidon \
    modname=poseidon modtype=library \
    files="$(FUNCS) $(FILES) $(HIDDFILES)" \
    uselibs="debug"
```

**Integração de dependência externa/terceiros**: exemplo real completo em
`workbench/libs/expat/mmakefile.src` — baixa o tarball fonte upstream (`%fetch`), aplica patch se
necessário, e compila como library AROS:
```
EXPATVERSION=2.7.3
EXPATREPOSITORIES = cache:// https://github.com/libexpat/libexpat/releases/download/R_$(EXPATVRS)
...
%fetch mmake=workbench-libs-expat-fetch archive=$(EXPATARCHBASE) destination=$(PORTSDIR)/expat \
    location=$(PORTSSOURCEDIR) archive_origins=$(EXPATREPOSITORIES) suffixes=$(EXPATARCHSUFFIX) \
    patches_specs=$(EXPATATCHSPEC)
...
%build_module mmake=workbench-libs-expat-lib \
    modname=expat modtype=library \
    files="$(EXPATFILES)"  linklibname=expat
```
Isso mostra o mecanismo real (`%fetch` + `%build_module`) que a stack Bluetooth poderia usar caso o
núcleo portátil (`aros-bt/`) seja vendorizado/baixado como fonte externa dentro da árvore AROS, em vez
de compilado localmente a partir de um submódulo git.

---

## 10. HCDs (Host Controller Drivers) USB existentes

Encontrados em `rom/usb/` e em árvores `arch/*`:

* **`pciusb.device`** — `rom/usb/pciusb/` — implementa **UHCI, OHCI e EHCI em um único driver**,
  selecionando o tipo por PCI class code. Confirmado em `rom/usb/pciusb/pciusb.h:164-166`:
  ```c
  #define HCITYPE_UHCI                    0x00
  #define HCITYPE_OHCI                    0x10
  #define HCITYPE_EHCI                    0x20
  ```
* **`pcixhci.device`** — `rom/usb/pcixhci/` — driver **xHCI** (USB 3.x) separado, com arquivos
  dedicados `xhci_hcd.c`, `xhci_controllerclass.c`, `xhci_schedule.c`, `xhci_isoch.c`, etc.
* **`denebusb`** — `arch/m68k-amiga/usb/denebusb/` — HCD para a placa Deneb (Amiga clássico, chip
  ISP1760), específico de m68k-amiga.
* **`usb2otg`** — `arch/arm-native/soc/broadcom/2708/usb/usb2otg/` — HCD para o controlador USB
  on-the-go do SoC Broadcom 2708 (Raspberry Pi original), específico de ARM.
* **`vusbhc`** — `rom/usb/vusbhc/` — um **host controller virtual** (`vusbhci_bridge.c`,
  `vusbhci_device.c`, `vusbhci_commands.c`) — potencialmente reaproveitável como inspiração/base para o
  "controlador virtual para testes" que o `project.md` pede no núcleo portátil, embora este seja para
  testar Poseidon/USB, não HCI Bluetooth.

Todos os HCDs, como `poseidon.library`/classes, são AROS libraries/devices geradas por genmodule (cada
um com seu `.conf`, ex. `rom/usb/pciusb/pciusb.conf`, `basename pciusb`, `residentpri 48`).

---

## 11. Sistema HIDD (Hardware Independent Device Driver) do AROS

A infraestrutura HIDD "genérica" vive em `rom/hidds/` (mais `arch/all-pc/hidds/` e
`workbench/hidds/` para HIDDs específicos de plataforma). HIDDs são classes **BOOPSI/OOP** (o sistema
de objetos do AROS, `oop/oop.h`, distinto tanto do usbclass "clássico" do Poseidon quanto do modelo de
device AmigaOS). São declaradas também via genmodule, mas com `type hidd` e uma seção `##begin class`
adicional que define `classid`, `classdatatype`, `superclass`.

Exemplo real, `rom/usb/poseidon/poseidon.conf:146-181` (as duas classes HIDD que o próprio Poseidon
expõe internamente):
```
##begin class
##begin config
basename USBController
type hidd
classid CLID_Hidd_USBController
classdatatype struct USBController
superclass CLID_Hidd
classptr_field ps_ContrClass
##end config
##begin methodlist
.interface Root
New
Dispose
Get
##end methodlist
##end class
```
com a implementação dos métodos BOOPSI em `rom/usb/poseidon/usb_controllerclass.c`:
```c
OOP_Object *USBController__Root__New(OOP_Class *cl, OOP_Object *o, struct pRoot_New *msg)
{
    OOP_Object *usbController = (OOP_Object *)OOP_DoSuperMethod(cl, o, (OOP_Msg)msg);
    ...
}
```
Ou seja: **dentro do próprio Poseidon** já convivem dois modelos — os "class drivers" USB
(`usbclass`, ex. `bluetooth.class`, `hid.class`) que são libraries AmigaOS-style com
`usbGetAttrsA/usbSetAttrsA/usbDoMethodA`, **e** duas classes HIDD/BOOPSI internas (`USBController`,
`USBDevice`) usadas para expor os controladores/dispositivos USB no barramento HIDD genérico do AROS
(`CLID_Hidd`).

HIDDs de input relacionados encontrados em `rom/hidds/`:
* `rom/hidds/input/` — subsistema HIDD de input genérico (mescla eventos de múltiplos "produtores" de
  hardware). Classe `CLID_Hidd_Input`. Ver `rom/hidds/input/inputclass.c`, `input.h`
  (`struct InputClassStaticData`, `icsd_producers`, `HIDD_Input_AddHardwareDriver()`,
  `HIDD_Input_RemHardwareDriver()`).
* `rom/hidds/kbd/` — HIDD de teclado, `keyboard.conf`:
  ```
  basename KBD
  classid CLID_Hidd_Kbd
  superclass CLID_Hidd_Input
  ...
  ##begin class
  basename KBDHW
  type hidd
  classid CLID_HW_Kbd
  superclass CLID_HW_Input
  ##end class
  ```
  Mostra o padrão de duas camadas: uma classe HIDD "genérica de teclado" (`CLID_Hidd_Kbd`) sobre uma
  classe HIDD "de hardware" (`CLID_HW_Kbd`), ambas subclasses de contrapartes `_Input`.
* `rom/hidds/mouse/` — equivalente para mouse (não lido em detalhe, mas presente com a mesma estrutura
  de diretório).

**CORREÇÃO (pós-revisão, ver `fase0-correcao-hidd.md`)**: a conclusão original abaixo, riscada pelos
motivos explicados no arquivo de correção, estava baseada numa busca pelo nome de função errado
(`HIDD_Input_AddHardwareDriver`) e não considerava a camada intermediária `keyboard.device`/
`gameport.device`. O registro real de handler é feito por **atributo** (`aHidd_Input_IrqHandler`), não
por chamada direta de função, e há sim consumidores reais. Ver `fase0-correcao-hidd.md` para a cadeia
completa e a conclusão revisada. O texto abaixo é mantido por rastreabilidade histórica, não como
conclusão válida.

~~**Relação com input.device e com os HCDs USB (divergência relevante para o projeto)**: apesar dessa
infraestrutura HIDD de input existir e estar completa no código-fonte, uma busca por
`HIDD_Input_AddHardwareDriver` em todo o repositório não encontrou **nenhum chamador real** fora dos
próprios arquivos que a definem (`rom/hidds/input/inputclass.c`, `input_init.c`). Em particular:
* Os drivers USB HID reais (`bootkeyboard.class`, `bootmouse.class`, `hid.class`) **não** usam HIDD —
  eles abrem `input.device` diretamente e escrevem eventos com `IND_WRITEEVENT` (tópico 8).
* `rom/devs/input/` (a implementação de `input.device` propriamente dito — `input.c`,
  `processevents.c`, `support.c`) **não referencia `hidd`/`OOP_` em nenhum lugar** (busca `grep`
  confirmada vazia).
* Os HCDs USB (`pciusb`, `pcixhci`) não são HIDDs — são devices/libraries clássicos com um
  `usb_controllerclass`/`usb_deviceclass` HIDD auxiliar apenas dentro do próprio Poseidon (visto acima),
  não uma cadeia HIDD que chegue até `input.device`.

Conclusão para o projeto Bluetooth: não há hoje, no AROS real, um caminho funcional
"HIDD de input → input.device" que a stack Bluetooth possa simplesmente "plugar-se" para keyboard/mouse
BLE. O caminho comprovadamente funcional e usado por todo driver real é
`OpenDevice("input.device")` + `IND_WRITEEVENT` (clássico Exec/AmigaOS), exatamente o mesmo caminho que
os drivers HID USB já usam. Modelar um transporte HCI (UART/SDIO) como uma classe HIDD é
tecnicamente possível em teoria (a infraestrutura BOOPSI existe e é usada pelo próprio Poseidon para
`USBController`/`USBDevice`), mas seria um caminho **não comprovado por nenhum precedente real** no
código atual — o precedente real e testado para "novo barramento de hardware integrando-se ao sistema"
é o modelo `psdAddHardware()` + task própria do Poseidon (tópico 1/3/4), não o modelo HIDD genérico de
`rom/hidds/`.~~

---

## Tópicos não encontrados / parcialmente encontrados

* **Isochronous (SCO) em detalhe**: confirmei apenas a existência da API
  (`psdAllocRTIsoHandler`/`psdStartRTIso`/`psdStopRTIso`, `UHCMD_STARTRTISO`/`STOPRTISO`), não explorei
  a implementação completa do lado do HCD para isochronous — não foi objeto explícito de nenhum dos 11
  tópicos pedidos, mas registro a limitação.
* **Parser HID como componente standalone**: não existe; está embutido em `hid.class.c` acoplado a
  Poseidon (ver tópico 7) — não é um "não encontrado" no sentido de a funcionalidade não existir, mas
  não existe como biblioteca reutilizável isolada, o que é relevante para a reciclagem pretendida.
* **HIDD input em uso real**: a infraestrutura existe (tópico 11) mas não encontrei nenhum consumidor
  real no código — registrado como divergência, não como funcionalidade ausente do repositório.

Todos os demais 10 (+1) tópicos pedidos foram encontrados com citação de arquivo e trecho real.
