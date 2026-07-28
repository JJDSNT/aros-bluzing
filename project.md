# Projeto: stack Bluetooth nativa e portável para AROS

## Objetivo

Projetar e implementar uma nova stack Bluetooth Host para o AROS, com arquitetura modular, compatível com plataformas big-endian e little-endian, portável para ambientes bare metal e integrada de forma natural ao Exec, ao Poseidon e aos demais subsistemas do AROS.

A stack deve suportar progressivamente:

* Bluetooth Classic, BR/EDR;
* Bluetooth Low Energy;
* controladores USB por meio do Poseidon;
* futuramente controladores UART, SDIO ou específicos de SoC;
* HID Classic;
* HID over GATT;
* pairing e bonding;
* posteriormente áudio Bluetooth.

O AROS será o sistema operacional principal e o primeiro consumidor da stack. Entretanto, o núcleo dos protocolos deve permanecer independente do sistema operacional para permitir testes externos, fuzzing, replay de pacotes, validação em diferentes arquiteturas e uso em sistemas bare metal.

---

# Princípios obrigatórios

## 1. Arquitetura AROS com núcleo portável

Não desenvolver uma stack genérica completa para somente depois portá-la ao AROS.

Também não implementar os protocolos diretamente dentro de `bluetooth.library`, de uma classe Poseidon ou de código específico de plataforma.

A arquitetura deve ser:

```text
Aplicações e classes AROS
          │
          ▼
bluetooth.library
          │
          ▼
Bluetooth Manager Task
          │
          ▼
Núcleo portátil dos protocolos
          │
          ▼
Interface abstrata de transporte HCI
          │
          ├── Poseidon USB Bluetooth class
          ├── UART transport
          ├── SDIO transport
          └── controlador virtual para testes
```

O AROS deve ser tratado como o port principal, e não como uma adaptação secundária.

O núcleo portátil também deve admitir um port bare metal. Isso implica:

* não depender de POSIX, Exec ou outro sistema operacional;
* não exigir threads, processos, sockets ou sistema de arquivos;
* não chamar diretamente serviços de plataforma: temporização, agendamento, logs e transporte devem passar por interfaces explícitas;
* não exigir alocação dinâmica: o port pode fornecer um allocator, mas os componentes devem admitir armazenamento fornecido pelo chamador e limites definidos em compilação;
* limitar a dependência da biblioteca C ao subconjunto freestanding disponível no alvo ou fornecer abstrações/substituições documentadas;
* permitir execução cooperativa em um único contexto, sem impedir que ports com sistema operacional usem tarefas e eventos;
* manter todo estado da instância explícito, sem singleton global obrigatório.

---

## 2. Compatibilidade obrigatória com big-endian e little-endian

O AROS funciona em arquiteturas big-endian e little-endian. Portanto, neutralidade de endianness é um requisito estrutural e não uma correção posterior.

A mesma base de código deve funcionar, sem forks específicos, em alvos como:

* m68k;
* PowerPC;
* ARM;
* AArch64;
* x86;
* x86-64.

Regras obrigatórias:

* nunca fazer cast de buffers de protocolo para estruturas C;
* nunca transmitir estruturas C diretamente;
* nunca depender de padding, packing ou alinhamento nativo;
* nunca presumir que todos os protocolos Bluetooth usam a mesma ordem de bytes;
* manter os valores internos em ordem nativa da CPU;
* converter apenas nas fronteiras de serialização e desserialização;
* testar acessos deliberadamente desalinhados;
* tornar os formatos persistentes independentes da CPU.

Criar helpers explícitos:

```c
uint16_t bt_read_le16(const uint8_t *p);
uint32_t bt_read_le24(const uint8_t *p);
uint32_t bt_read_le32(const uint8_t *p);
uint64_t bt_read_le64(const uint8_t *p);

uint16_t bt_read_be16(const uint8_t *p);
uint32_t bt_read_be24(const uint8_t *p);
uint32_t bt_read_be32(const uint8_t *p);
uint64_t bt_read_be64(const uint8_t *p);

void bt_write_le16(uint8_t *p, uint16_t value);
void bt_write_le24(uint8_t *p, uint32_t value);
void bt_write_le32(uint8_t *p, uint32_t value);
void bt_write_le64(uint8_t *p, uint64_t value);

void bt_write_be16(uint8_t *p, uint16_t value);
void bt_write_be24(uint8_t *p, uint32_t value);
void bt_write_be32(uint8_t *p, uint32_t value);
void bt_write_be64(uint8_t *p, uint64_t value);
```

Observar que:

* HCI, ACL, L2CAP e ATT usam predominantemente little-endian;
* SDP possui codificação de Data Elements e inteiros em big-endian;
* RTP, usado em A2DP, utiliza network byte order;
* endereços Bluetooth e UUIDs exigem representação explícita;
* bytes recebidos do transporte não devem ser reinterpretados de acordo com a CPU.

---

## 3. Separação entre Poseidon e Bluetooth

Poseidon é responsável pelo USB.

A stack Bluetooth é responsável pelos protocolos Bluetooth.

Entre eles deve existir um driver de classe USB Bluetooth que implemente o transporte HCI sobre USB.

```text
Stack Bluetooth
      │
      ▼
HCI transport interface
      │
      ▼
Poseidon Bluetooth USB class
      │
      ▼
Poseidon
      │
      ▼
HCD da plataforma
      │
      ▼
Controlador USB físico
```

### Responsabilidades do Poseidon

* enumeração USB;
* leitura de descriptors;
* escolha de configuração e interfaces;
* gerenciamento de endpoints;
* transferências control, interrupt, bulk e isochronous;
* hotplug;
* cancelamento de transferências;
* interação com o HCD da plataforma.

### Responsabilidades da classe USB Bluetooth

* reconhecer dispositivos Bluetooth USB;
* associar-se à interface apropriada;
* localizar os endpoints;
* enviar comandos HCI por control transfer;
* receber eventos HCI por interrupt IN;
* enviar e receber ACL por bulk endpoints;
* suportar SCO por endpoints isócronos quando necessário;
* suportar ISO quando isso for implementado;
* manter várias transferências de recepção pendentes;
* tratar attach e detach;
* entregar os bytes HCI intactos ao núcleo;
* executar inicialização específica de fabricante quando necessária.

### Responsabilidades da stack Bluetooth

* comandos e eventos HCI;
* estado e capacidades do controlador;
* controle de buffers e créditos;
* conexões ACL, SCO e ISO;
* L2CAP;
* SDP;
* RFCOMM;
* ATT;
* GATT;
* SMP;
* pairing e bonding;
* perfis;
* API pública para aplicações.

A stack não deve conhecer endpoints, pipes, requests ou estruturas internas do Poseidon.

A classe Poseidon não deve interpretar L2CAP, ATT, SDP, HID ou outros protocolos acima de HCI.

---

## 4. Não criar um HIDD Bluetooth por arquitetura

O requisito para cada plataforma não deve ser “implementar um HIDD Bluetooth”.

Para adaptadores USB:

* a classe Bluetooth USB deve ser comum;
* Poseidon deve ser comum;
* apenas o HCD do controlador USB é específico da plataforma.

Exemplo:

```text
AROS m68k:
    bluetooth.library
        → btusb.class
            → Poseidon
                → HCD da placa USB

AROS ARM:
    bluetooth.library
        → btusb.class
            → Poseidon
                → DWC2 ou outro HCD

AROS x86:
    bluetooth.library
        → btusb.class
            → Poseidon
                → UHCI/OHCI/EHCI/XHCI
```

Para controladores integrados por UART, SDIO ou outro barramento, criar transportes HCI próprios sobre os devices, HIDDs ou subsistemas existentes.

---

# Arquitetura proposta

## Estrutura do projeto

Inicialmente, manter o núcleo em repositório independente, com integração contínua ao AROS.

```text
aros-bt/
├── include/
│   └── bluetooth/
│       ├── types.h
│       ├── endian.h
│       ├── buffer.h
│       ├── transport.h
│       ├── hci.h
│       ├── l2cap.h
│       ├── sdp.h
│       ├── rfcomm.h
│       ├── att.h
│       ├── gatt.h
│       ├── smp.h
│       ├── hid.h
│       └── status.h
│
├── core/
│   ├── buffer/
│   ├── event/
│   ├── timer/
│   ├── device/
│   ├── security/
│   └── controller/
│
├── protocols/
│   ├── hci/
│   ├── l2cap/
│   ├── sdp/
│   ├── rfcomm/
│   ├── att/
│   ├── gatt/
│   └── smp/
│
├── profiles/
│   ├── hidp/
│   ├── hogp/
│   ├── a2dp/
│   └── avrcp/
│
├── ports/
│   ├── aros/
│   │   ├── library/
│   │   ├── task/
│   │   ├── storage/
│   │   ├── poseidon/
│   │   ├── uart/
│   │   └── input/
│   │
│   └── test-host/
│       ├── virtual_transport/
│       ├── btsnoop_replay/
│       └── timer/
│
├── tools/
│   ├── bttool/
│   ├── btsnoop/
│   └── packetdump/
│
└── tests/
    ├── endian/
    ├── parser/
    ├── encoder/
    ├── state_machine/
    ├── virtual_controller/
    ├── replay/
    └── interoperability/
```

---

# Núcleo independente

O núcleo não deve depender diretamente de:

* `AllocMem`;
* `FreeMem`;
* `CreateTask`;
* `CreateMsgPort`;
* `Wait`;
* `Signal`;
* `OpenDevice`;
* `OpenLibrary`;
* Poseidon;
* pthreads;
* sockets;
* file descriptors;
* APIs POSIX.

Criar uma camada mínima de plataforma:

```c
struct bt_platform_ops
{
    void *(*alloc)(size_t size);
    void (*free)(void *ptr);

    uint64_t (*time_us)(void);

    int (*timer_start)(
        void *platform,
        struct bt_timer *timer,
        uint64_t delay_us);

    void (*timer_cancel)(
        void *platform,
        struct bt_timer *timer);

    void (*schedule)(
        void *platform);

    void (*log)(
        void *platform,
        unsigned level,
        const char *message);
};
```

Não criar uma abstração equivalente a um sistema operacional completo.

O núcleo deve funcionar preferencialmente em modelo single-owner: uma única task ou event loop é proprietária das máquinas de estado.

---

# Interface de transporte HCI

Criar uma interface independente do barramento:

```c
enum bt_hci_packet_type
{
    BT_HCI_PACKET_COMMAND,
    BT_HCI_PACKET_EVENT,
    BT_HCI_PACKET_ACL,
    BT_HCI_PACKET_SCO,
    BT_HCI_PACKET_ISO
};

struct bt_hci_transport;

struct bt_hci_transport_ops
{
    int (*open)(
        struct bt_hci_transport *transport);

    void (*close)(
        struct bt_hci_transport *transport);

    int (*send_command)(
        struct bt_hci_transport *transport,
        const uint8_t *data,
        size_t length);

    int (*send_acl)(
        struct bt_hci_transport *transport,
        const uint8_t *data,
        size_t length);

    int (*send_sco)(
        struct bt_hci_transport *transport,
        const uint8_t *data,
        size_t length);

    int (*send_iso)(
        struct bt_hci_transport *transport,
        const uint8_t *data,
        size_t length);

    int (*start_receive)(
        struct bt_hci_transport *transport);

    void (*stop_receive)(
        struct bt_hci_transport *transport);
};
```

A recepção deve entregar:

* tipo do pacote;
* ponteiro para bytes;
* comprimento;
* identificação do adaptador;
* erro ou status de transporte.

O transporte não deve converter endianness nem interpretar protocolos superiores.

---

# Modelo de execução no AROS

Criar uma Bluetooth Manager Task.

```text
Poseidon callback ou device completion
                │
                ▼
        fila de eventos HCI
                │
              Signal
                │
                ▼
      Bluetooth Manager Task
                │
       máquinas de estado
                │
        replies e notificações
```

Regras:

* callbacks e interrupções não devem executar máquinas de estado completas;
* não chamar aplicações diretamente a partir de callbacks USB;
* toda mudança importante de estado deve ocorrer na Bluetooth Manager Task;
* utilizar mensagens, ports e signals do Exec no port AROS;
* minimizar locks;
* preferir filas single-producer/single-consumer quando apropriado;
* suportar múltiplos adaptadores;
* tratar corretamente remoção física durante operações pendentes.

---

# API pública AROS

Criar uma `bluetooth.library` com API estável para:

* enumerar adaptadores;
* consultar capacidades;
* iniciar e cancelar discovery;
* listar dispositivos;
* conectar e desconectar;
* iniciar pairing;
* responder a solicitações de confirmação ou PIN;
* consultar serviços;
* registrar clientes de perfil;
* receber eventos assíncronos;
* acessar dispositivos bonded;
* remover bonding;
* consultar estado e erros.

A API pública não deve expor detalhes do Poseidon nem exigir que aplicações manipulem pacotes HCI.

A biblioteca deve ser projetada para permitir futuras classes e ferramentas:

```text
Bluetooth Preferences
BTTool
HID class
A2DP AHI driver
networking profiles
file transfer
diagnostic tools
```

---

# Persistência

Definir um formato versionado e independente de endianness.

Não gravar estruturas C nativas diretamente.

Persistir:

* identidade do adaptador;
* endereço e tipo de endereço;
* dispositivos conhecidos;
* nomes e serviços em cache;
* Classic link keys;
* LE LTK;
* IRK;
* CSRK;
* bonding flags;
* preferências;
* versão do formato;
* checksum ou validação estrutural.

Possível localização:

```text
ENVARC:Sys/bluetooth/
    adapters.db
    devices.db
    keys.db
    policy.conf
```

Evitar imprimir chaves em logs.

---

# Suporte funcional planejado

## Bluetooth Classic

Implementar progressivamente:

* HCI Classic;
* inquiry;
* ACL;
* L2CAP;
* SDP client;
* pairing legado quando necessário;
* Secure Simple Pairing;
* link keys;
* HIDP Host;
* RFCOMM;
* posteriormente AVDTP, A2DP e AVRCP.

## Bluetooth Low Energy

Implementar progressivamente:

* HCI LE;
* advertising scan;
* conexão LE;
* canais fixos L2CAP;
* ATT;
* GATT Client;
* SMP;
* Legacy Pairing;
* LE Secure Connections;
* bonding;
* HOGP Client;
* posteriormente GATT Server e outros perfis.

## HID

Criar um parser HID reutilizável.

```text
USB HID reports ──┐
                  ├── parser comum
HIDP reports ─────┤       │
HOGP reports ─────┘       ├── input.device
                          └── lowlevel.library
```

Não duplicar desnecessariamente o parser de descriptors e reports entre USB HID e Bluetooth HID.

---

# Áudio

Não implementar áudio na primeira entrega.

Quando a base estiver estável, implementar primeiro:

* AVDTP;
* A2DP Source;
* SBC encoder;
* RTP;
* integração com AHI;
* controle de buffer;
* compensação de diferença entre relógios;
* AVRCP básico.

Arquitetura:

```text
Aplicação
   │
  AHI
   │ PCM
   ▼
Bluetooth A2DP backend
   ├── resampling
   ├── SBC
   ├── RTP
   └── AVDTP
          │
          ▼
        L2CAP
```

SCO, HFP, microfone e LE Audio devem ficar fora do escopo inicial.

---

# Licenciamento e clean-room

A nova stack deve possuir procedência auditável.

## Referências permitidas

Usar como referências principais:

* especificações Bluetooth;
* documentação pública dos protocolos;
* documentação USB Bluetooth HCI;
* código do Haiku com licença confirmada arquivo por arquivo;
* lwBT com licença confirmada arquivo por arquivo;
* código original escrito para o projeto.

## BTstack

O código do BTstack não deve ser copiado, traduzido, reescrito mecanicamente ou usado como base direta da implementação permissiva.

Pode ser usado externamente para:

* observar comportamento;
* produzir traces;
* testar interoperabilidade;
* comparar sequências de pacotes;
* validar equipamentos;
* gerar resultados comportamentais.

O agente que implementa a stack não deve receber trechos do BTstack como instrução de implementação.

## NimBLE

Até que a compatibilidade jurídica entre Apache 2.0 e a licença atual do AROS esteja formalmente resolvida, tratar NimBLE como:

* referência externa;
* fonte de testes comportamentais;
* oráculo de interoperabilidade BLE;
* base para comparação de traces.

Não copiar código do NimBLE para componentes que serão licenciados exclusivamente sob a APL sem autorização explícita.

## Processo recomendado

Separar papéis:

### Agente de especificação

Recebe documentação, traces e resultados de referência.

Produz:

* requisitos;
* formatos de pacotes;
* diagramas de estados;
* invariantes;
* vetores de teste;
* comportamento esperado.

### Agente implementador

Recebe:

* especificação independente;
* código do AROS;
* código MIT/BSD previamente aprovado;
* testes.

Não recebe código-fonte incompatível.

### Agente de validação

Compara:

* a implementação;
* stacks externas;
* dispositivos reais;
* traces HCI;
* arquivos btsnoop.

Produz relatórios de divergência, não código derivado.

---

# Etapas de implementação

## Fase 0 — levantamento do AROS

Antes de escrever código:

1. localizar a implementação atual do Poseidon;
2. identificar como classes USB são registradas;
3. localizar exemplos de class drivers Poseidon;
4. identificar o modelo de attach/detach;
5. estudar como requests assíncronos são concluídos;
6. identificar os HCDs existentes;
7. localizar padrões para libraries, devices, tasks e message ports;
8. localizar a integração com `input.device`;
9. localizar parsers HID já existentes;
10. identificar o sistema de build para componentes externos.

Produzir um documento com caminhos, estruturas, APIs e exemplos reais do repositório.

Não inventar APIs do AROS.

---

## Fase 1 — fundação portátil

Implementar:

* tipos;
* endianness;
* buffer cursor;
* packet builder;
* filas;
* eventos;
* timers abstratos;
* códigos de erro;
* logging;
* controlador virtual;
* testes de LE/BE;
* testes desalinhados.

Critérios:

* nenhum cast de pacote para struct;
* nenhum warning;
* testes com sanitizers no harness;
* encode/decode round-trip;
* simulação de memória hostil;
* documentação de cada formato.

---

## Fase 2 — HCI mínimo

Implementar:

* command packet;
* event packet;
* ACL packet;
* command complete;
* command status;
* reset;
* leitura de versão;
* leitura de recursos;
* leitura de buffer sizes;
* controle de créditos;
* fila de comandos;
* timeout;
* estado do controlador.

Criar transporte virtual antes do transporte USB.

Critério de aceite:

* sequência determinística de inicialização;
* testes com eventos gravados;
* nenhuma dependência de endianness;
* tratamento correto de comandos simultâneos e timeouts.

---

## Fase 3 — integração Poseidon

Implementar a classe USB Bluetooth:

* detecção por classe, subclass e protocol;
* fallback controlado por VID/PID quando necessário;
* attach;
* detach;
* endpoint discovery;
* command transport;
* event RX;
* ACL RX/TX;
* cancelamento;
* múltiplas requisições RX;
* recuperação de erros;
* hot unplug;
* registro do adaptador.

Critério de aceite:

* adaptador aparece no AROS;
* HCI Reset funciona;
* versão e capacidades são exibidas;
* remoção não causa use-after-free;
* reconexão recria corretamente o adaptador.

---

## Fase 4 — discovery Classic e LE

Implementar:

* Classic inquiry;
* inquiry result parsing;
* remote name;
* LE scan;
* advertising report parsing;
* duplicate filtering;
* banco unificado de dispositivos;
* identificação de dispositivos dual-mode.

Critério de aceite:

* ferramenta AROS lista dispositivos Classic e LE;
* resultados são consistentes em BE e LE;
* arquivos de replay produzem os mesmos resultados.

---

## Fase 5 — L2CAP

Implementar:

* signaling;
* connection-oriented channels;
* configuration;
* MTU;
* ACL fragmentation;
* ACL reassembly;
* fixed channels LE;
* timeouts;
* channel lifecycle.

Critério de aceite:

* testes com fragmentação e remontagem;
* testes de pacotes truncados;
* testes de comprimento inválido;
* testes de remoção durante negociação.

---

## Fase 6 — serviços e segurança

Classic:

* SDP client;
* pairing;
* SSP;
* link keys.

LE:

* ATT;
* GATT Client;
* SMP;
* LE Legacy Pairing;
* LE Secure Connections;
* LTK, IRK e CSRK;
* bonding.

Critério de aceite:

* consulta de serviços Classic;
* descoberta GATT;
* persistência independente de endianness;
* reconexão bonded;
* logs sem exposição de chaves.

---

## Fase 7 — HID dual-mode

Implementar:

* HIDP Host;
* HOGP Client;
* descriptor parsing;
* report protocol;
* boot protocol quando aplicável;
* keyboard;
* mouse;
* consumer controls;
* game controllers;
* integração com `input.device` e `lowlevel.library`.

Critério de aceite:

* teclado Classic;
* mouse Classic;
* teclado BLE;
* mouse BLE;
* reconexão bonded;
* funcionamento em pelo menos um alvo BE e um alvo LE.

---

## Fase 8 — RFCOMM e demais perfis

Somente após estabilizar HID:

* RFCOMM;
* perfis seriais;
* A2DP;
* AVRCP;
* outros perfis conforme prioridade.

---

# Estratégia de testes

## Testes unitários

* parsers;
* serializers;
* endianness;
* UUIDs;
* Bluetooth addresses;
* Data Elements SDP;
* ATT PDUs;
* HCI events;
* L2CAP signaling;
* state machines.

## Testes round-trip

```text
bytes conhecidos
   → parser
   → representação nativa
   → encoder
   → mesmos bytes
```

## Testes desalinhados

Executar parsers com buffers iniciados em offsets não alinhados.

## Testes BE/LE

Os mesmos vetores devem produzir os mesmos valores sem depender da arquitetura.

Quando não houver hardware big-endian disponível, incluir build de teste que force caminhos de byte swapping ou execute em emulador adequado. Isso não substitui testes reais posteriores em m68k ou PowerPC.

## Fuzzing

Fuzzers para:

* HCI events;
* ACL;
* L2CAP;
* SDP;
* ATT;
* advertising data;
* HID descriptors;
* arquivos persistentes.

## Replay

Criar ferramenta para reproduzir:

* btsnoop;
* traces HCI;
* sequências sintéticas;
* falhas de transporte;
* desconexões;
* timeouts;
* hot unplug.

## Interoperabilidade

Testar com:

* vários dongles USB;
* teclado Classic;
* mouse Classic;
* teclado BLE;
* mouse BLE;
* dispositivos dual-mode;
* diferentes versões de controlador.

---

# Requisitos de qualidade

* C compatível com o toolchain do AROS;
* sem dependências desnecessárias de C++;
* sem POSIX no núcleo;
* núcleo compatível com ambiente C freestanding e port bare metal;
* alocação dinâmica opcional, nunca requisito obrigatório do núcleo;
* warnings tratados como erros quando viável;
* nenhuma alocação em callbacks de baixo nível quando evitável;
* limites explícitos para buffers;
* validação de todos os comprimentos;
* tratamento de overflow;
* nenhuma leitura fora do buffer;
* nenhuma confiança cega em dados do controlador;
* state machines documentadas;
* logs estruturados;
* erros propagados;
* cleanup correto em todos os caminhos;
* attach/detach idempotente;
* suporte a múltiplos adaptadores;
* nenhum singleton global obrigatório no núcleo;
* formatos persistentes versionados.

---

# Primeira tarefa do agente

Não começar implementando a stack completa.

Executar primeiro uma investigação do repositório AROS e produzir:

1. mapa da arquitetura USB/Poseidon;
2. exemplos reais de class drivers;
3. fluxo de attach/detach;
4. modelo de I/O assíncrono;
5. convenções para libraries e devices;
6. integração com tasks, signals e message ports;
7. localização e capacidade do parser HID atual;
8. integração com `input.device`;
9. convenções de build;
10. proposta de diretórios;
11. proposta de API do transporte HCI;
12. riscos de big-endian, alinhamento e concorrência;
13. plano de implementação em commits pequenos.

Cada afirmação sobre o AROS deve citar o arquivo, símbolo e trecho relevante do repositório.

Não presumir que uma API existe.

Ao encontrar divergências entre a arquitetura proposta e o código real do AROS, registrar a divergência e adaptar a proposta à arquitetura real.

---

# Primeira entrega de código

Após o levantamento, implementar apenas:

* helpers LE/BE;
* buffer reader;
* buffer writer;
* testes unitários;
* abstração de transporte HCI;
* transporte virtual;
* parser de HCI Event;
* encoder de HCI Command;
* sequência HCI Reset simulada.

Não integrar Poseidon antes que essa base esteja testada.

A primeira entrega deve demonstrar:

```text
HCI Reset Command
        ↓
transporte virtual
        ↓
Command Complete Event
        ↓
controller state = initialized
```

O mesmo teste deve passar em configuração big-endian e little-endian.

---

# Critério arquitetural final

A implementação será considerada correta quando:

* o núcleo dos protocolos não depender do AROS;
* o núcleo puder ser integrado em um alvo bare metal, sem POSIX, sistema de arquivos ou heap obrigatório;
* o port principal usar naturalmente Exec e Poseidon;
* Poseidon permanecer isolado abaixo da interface HCI;
* o mesmo driver de classe Bluetooth USB servir às diferentes arquiteturas AROS;
* o núcleo funcionar em big-endian e little-endian;
* os formatos persistentes forem independentes de arquitetura;
* Classic e LE puderem coexistir;
* novas plataformas precisarem implementar apenas transportes ou suporte de barramento, e não uma nova stack;
* a procedência de todo código for auditável;
* a implementação puder ser testada extensivamente fora do hardware real sem se transformar em uma stack POSIX.
