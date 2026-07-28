# API pública `bluetooth.library` — propostas de desenho (mensageria com a Manager Task)

Este documento registra, para avaliação posterior (ainda **sem decisão fechada**), as
possibilidades de desenho para a próxima peça em aberto do `status.md`: "Expandir
`bluetooth.library` além de `BT_GetAPIVersion`: controle do serviço, enumeração,
discovery, conexão e pairing por mensagens à Manager Task." Isso corresponde à seção
"API pública AROS" de `../project.md` (enumerar adaptadores, consultar capacidades,
discovery, listar dispositivos, conectar/desconectar, pairing, confirmação/PIN, consultar
serviços, registrar clientes de perfil, receber eventos assíncronos, dispositivos bonded,
remover bonding, consultar estado/erros — sem expor Poseidon nem pacotes HCI).

Ponto de partida factual: hoje `ports/aros/library/` só exporta `BT_GetAPIVersion`
(`bluetooth_intern.h`/`bt_getapiversion.c`), e o loop da Manager Task
(`ports/aros/task/manager_task.c:manager_process`) só espera `SIGBREAKF_CTRL_C`, o sinal
do `timer.device` e a signal mask do transporte — **não existe hoje nenhum `MsgPort` de
comando** nessa task. Qualquer um dos desenhos abaixo exige acrescentar esse `MsgPort` à
máscara do `Wait()` e um `GetMsg` de drenagem por iteração, no mesmo padrão já usado ali
para o timer.

---

## Confirmação do desenho geral proposto

A proposta (chamadas síncronas para consulta de estado/adaptadores/dispositivos/
capabilities; comandos assíncronos para discovery/conexão/pairing/remoção; cliente
registra um `MsgPort`; eventos versionados vindos da Manager Task; structs públicas com
`size`+`version`; handles opacos; zero exposição de HCI/USB/Poseidon) é compatível com
`project.md` e é a extensão natural do que já existe. Concordo com a direção. Os pontos
abaixo detalham cada peça com as opções concretas de implementação e um risco/nuance que
vale confirmar antes de codificar.

---

## 1. Split síncrono vs. assíncrono

Consultas (estado, adaptadores, dispositivos, capabilities) síncronas; discovery/conexão/
pairing/remoção assíncronas — faz sentido.

**Nuance a confirmar**: "síncrono" aqui não pode significar "sem IPC". Só a Manager Task
tem o estado real (`bt_controller`, `device_registry`); a chamada síncrona da library
ainda precisa mandar uma mensagem para o `MsgPort` de comando da Manager Task e bloquear
esperando a resposta. Quem bloqueia é a *task chamadora*, nunca a Manager Task — isso está
de acordo com a regra de `project.md` (Manager Task nunca faz I/O bloqueante), só não é
"síncrono" no sentido de "computação local sem espera".

Isso implica que toda chamada síncrona precisa de um `MsgPort` de resposta. Duas opções:

* **(a)** um `MsgPort` de resposta reaproveitado por chamada, criado/destruído a cada
  `BT_*` síncrono — simples, mas custo de `CreateMsgPort`/`DeleteMsgPort` a cada consulta.
* **(b)** um `MsgPort` de resposta persistente por sessão de cliente (ver seção 2),
  reaproveitado entre chamadas síncronas sequenciais — exige serializar chamadas
  concorrentes do mesmo cliente (mutex/semáforo local), mas evita alocar recursos Exec a
  cada consulta. Recomendo (b) por ser o padrão já usado em `manager_task.c` para o
  `timerequest` (um recurso, reaproveitado, não recriado a cada tick).

---

## 2. "Cada cliente registra um `MsgPort`" — sessão vs. `OpenLibrary`

Ponto que vale confirmar explicitamente: `bluetooth.library`, do jeito que está definida
em `bluetooth.conf`/`bluetooth_intern.h`, é uma library AROS clássica de base
compartilhada — `OpenLibrary()` incrementa um contador de referência sobre **um único**
`struct BluetoothBase`, não cria estado privado por chamador. Ou seja, não dá para pendurar
"o `MsgPort` deste cliente" diretamente na base da library sem um passo explícito, porque
todo `OpenLibrary("bluetooth.library", 0)` enxerga a mesma base.

Proposta: introduzir uma sessão explícita, independente do `OpenLibrary`/`CloseLibrary`:

```c
bt_client_handle_t BT_ClientOpen(struct MsgPort *event_port);
void BT_ClientClose(bt_client_handle_t client);
```

`BT_ClientOpen` registra o `MsgPort` do chamador junto à Manager Task (mensagem síncrona,
seção 1) e devolve um handle opaco (seção 5) que passa a identificar esse cliente em toda
chamada seguinte — inclusive para a Manager Task rotear eventos e detectar cliente morto
(fechar sem `BT_ClientClose` deve ser tratado como "porta inválida" na próxima tentativa de
`PutMsg`, com limpeza automática do registro). Isso evita conflar o ciclo de vida da
library compartilhada com o ciclo de vida de cada aplicação cliente.

---

## 3. Mensagens de evento versionadas

Envelope proposto:

```c
struct bt_event_msg
{
    struct Message header;   /* mn_Node/mn_ReplyPort de Exec */
    uint32_t size;            /* tamanho total realmente preenchido, com payload */
    uint32_t version;         /* versão do formato deste tipo de evento */
    uint32_t type;             /* bt_event_type */
    bt_client_handle_t client; /* a quem este evento se destina/refere */
    uint8_t payload[];         /* layout depende de type+version */
};
```

**Questão em aberto — posse de memória**, já que evento é fluxo unidirecional (Manager →
cliente), diferente de um comando (que tem resposta natural via `ReplyMsg`):

* **(a) Pool fixo reciclado**: Manager Task pré-aloca N `bt_event_msg` e só entrega um novo
  evento quando o cliente devolve um slot livre via `ReplyMsg` — mesma filosofia de todo o
  núcleo (`BT_CMDQ_MAX_PENDING`, `BT_DEVICE_REGISTRY_MAX`, sem heap dinâmico) e do próprio
  adapter USB (`ports/aros/transport-usb/`, que recicla `BTHCIEventMsg`). Dá back-pressure
  natural: cliente lento para de receber eventos novos em vez de a Manager Task esgotar
  memória.
* **(b) Alocação dinâmica por evento**: `AllocMem` na Manager Task, cliente libera com
  `FreeMem` após consumir. Mais simples de implementar e não viola nenhum princípio aqui
  (a camada de porte AROS já usa `AllocMem` em `manager_task.c`/`input_device.c`; a regra
  de "sem heap obrigatório" é do núcleo portátil, não desta camada), mas perde o
  back-pressure de (a) e cada tipo de evento de tamanho variável precisa de sizing manual.

Recomendo (a) por simetria com o resto do porte e por dar sinalização de sobrecarga de
graça; ponto a confirmar é o tamanho do pool e o que fazer quando esgota (descartar evento
mais antigo? bloquear a Manager Task — não, nunca — ou marcar "eventos perdidos" com um
tipo de evento sentinela quando o cliente reabrir capacidade).

---

## 4. Structs públicas com `size` + `version`

Aplicar uniformemente a `bt_adapter_info`, `bt_device_info`, `bt_capabilities` e aos
envelopes de comando/evento. Padrão: o chamador preenche `size` antes da chamada (até onde
a versão dele entende o struct); a library/Manager Task preenche só até
`min(caller_size, actual_size)` e devolve o `size`/`version` realmente escritos — o mesmo
padrão de `cbSize`/`OSVERSIONINFO` do Win32, adequado para uma ABI binária que precisa
evoluir sem quebrar clientes antigos.

Isso já tem precedente direto no projeto: o formato TLV `BTKD` v1 do
`bond_store` (`bluetooth/bond_store.h`) usa comprimentos explícitos, CRC-32 e
"suporte a ignorar record types futuros" — mesma filosofia, outro contexto (arquivo em vez
de IPC).

---

## 5. Handles opacos

Confirmo a ideia. Proposta concreta: inteiro de 32 bits (não ponteiro — cliente e Manager
Task podem estar em contextos de proteção diferentes, e mesmo sem isso, ponteiro cru para
struct interna da Manager Task nunca deveria vazar para fora), composto de índice (para um
pool fixo, mesma filosofia de todo o núcleo) + geração/epoch para invalidar handles velhos
depois que o adapter/device/operação correspondente é removido (problema ABA clássico de
todo pool reciclado).

Quatro espaços de handle independentes: `bt_client_handle_t` (seção 2),
`bt_adapter_handle_t`, `bt_device_handle_t`, `bt_operation_handle_t` (para acompanhar uma
operação assíncrona em andamento — discovery, connect, pair, remove — e casar sua
conclusão com o evento certo).

---

## 6. Zero exposição de HCI/USB/Poseidon

Já garantido pela arquitetura em camadas existente (transporte isolado do núcleo, núcleo
isolado do porte AROS); a API pública fica ainda mais alto nível. O único cuidado prático:
os novos headers públicos (`bluetooth/client_api.h` ou nome equivalente, ainda a definir)
não devem incluir `bluetooth/hci.h` nem `bluetooth/transport.h` — só tipos novos definidos
para essa camada (handles, structs versionados, envelopes).

---

## Dispositivo dummy para testes

Concordo que vale a pena, e há uma base melhor do que "criar do zero": já existe um
transporte HCI virtual (`ports/test-host/virtual_transport`, exercitado em
`tests/virtual_transport`) que simula um controlador local respondendo a Reset e, desde a
Fase 4, também simula um device Classic (via Inquiry) e um device LE (via advertising) —
ver `status.md`, entradas de Fase 2 e Fase 4. Hoje esse transporte só é usado em testes de
host; nunca passou pelo caminho real de IPC AROS, porque esse caminho (Manager Task com
`MsgPort` de comando, `bluetooth.library` além de `BT_GetAPIVersion`) ainda não existe.

Proposta: quando essa API pública nascer, o primeiro consumidor de teste dela deve ser
exatamente `bt_aros_manager_task_start()` (`ports/aros/task/manager_task.c`) apontando para
o transporte virtual em vez de `ports/aros/transport-usb/`. Isso testa a pilha inteira —
mensageria de cliente incluída — sem depender de hardware Bluetooth real nem do
`usbbluetooth.device`, no mesmo espírito do que já foi validado no QEMU/Raspberry Pi 3
(`status.md`, "5/5 testes"). Não é necessário inventar um "dispositivo dummy" de baixo
nível novo — falta só expor esse transporte virtual como uma opção de adaptador
selecionável do lado AROS (ex.: uma variante de build/selftest com uma flag), o que também
permite demonstrar Bluetooth Preferences/BTTool e testar a API pública inteira antes do
primeiro dongle real funcionar de ponta a ponta.

Sugestão adicional: dar ao "device simulado" do transporte virtual uma identidade estável
e documentada (endereço fixo, nome ex. "AROS Bluzing Virtual Device", um serviço SDP e um
perfil GATT fixos) para servir de fixture determinístico em demonstrações e regressão
manual — hoje ele é só um detalhe interno dos testes de discovery, não algo pensado para
ser mostrado a um usuário.

---

## Questões em aberto para decidirmos antes de codificar

1. Sessão de cliente: `BT_ClientOpen`/`BT_ClientClose` explícito (seção 2, recomendado) ou
   alguma alternativa mais simples que você tenha em mente?
2. Posse dos `bt_event_msg`: pool fixo reciclado via `ReplyMsg` (recomendado, seção 3) ou
   alocação dinâmica?
3. Uma única fila/`MsgPort` de comando na Manager Task para tudo (síncrono + assíncrono),
   ou duas filas separadas — para que uma consulta síncrona lenta não atrase a fila de
   comandos de discovery/connect que já estão em andamento?
4. Nome e local do novo header público (ex. `bluetooth/client_api.h`) — hoje o único
   artefato exposto é o gerado pelo `genmodule` a partir de `bluetooth.conf`; os tipos
   novos (handles, structs versionados, envelopes) precisam de um header compartilhado
   entre `core/manager` e a library, mas que não vaze para o núcleo portátil (que hoje não
   conhece Exec/`MsgPort`).
5. Vale já reservar essa flag de build "adapter = transporte virtual" na Fase 3
   (integração real), ou só depois que `usbbluetooth.device` estiver validado com hardware
   real?
