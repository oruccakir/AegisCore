# AegisCore Edge Communication Guide

Bu doküman, edge firmware tarafındaki haberleşme yolunu öğrenmek için yazılmıştır.
Amaç sadece "hangi dosya ne yapıyor" demek değil; UART byte'ının sisteme girip nasıl güvenlikten geçtiğini, nasıl command'a dönüştüğünü ve edge'den gateway'e nasıl telemetry olarak çıktığını anlamaktır.

Odak alanı:

```text
edge/app/comms/
edge/app/runtime/
edge/bsp/uart_driver.*
```

## Büyük Resim

Edge ile gateway klasik HTTP backend/frontend gibi konuşmaz. Edge tarafı STM32 üzerinde RTOS task'larıyla çalışan bir firmware'dir. Gateway ile haberleşme UART üzerinden, binary AC2 frame formatı ile yapılır.

Inbound, yani gateway -> edge akışı:

```text
Gateway
  -> UART bytes
  -> UartDriver DMA/IDLE IRQ
  -> UartRxTask
  -> AC2Parser
  -> OnAC2Frame security checks
  -> gRemoteCmdQueue
  -> StateMachineTask
  -> DispatchRemoteCmd
```

Outbound, yani edge -> gateway akışı:

```text
Edge event / telemetry / ACK / NACK
  -> QueueTx(...)
  -> gTxQueue
  -> TelemetryTxTask
  -> AC2Framer::EncodeTelemetry(...)
  -> UartDriver::Write(...)
  -> Gateway
```

Bu mimaride en önemli fikir şudur:

```text
UART driver byte taşır.
AC2 parser frame çözer.
OnAC2Frame güvenlik kapısıdır.
Command dispatch davranış üretir.
TelemetryTxTask UART TX sahibidir.
```

Komut geldiği anda doğrudan state değiştirilmez. Komut önce frame olarak parse edilir, güvenlik ve safety kontrollerinden geçer, sonra queue üzerinden `StateMachineTask` context'ine aktarılır.

## Dosya Haritası

### `edge/app/comms/telemetry.hpp`

Protokol sözlüğüdür.

Bu dosya data göndermez, UART bilmez, frame encode etmez. Sadece edge ve gateway'in ortak dilini tanımlar:

- Command ID'ler
- Error code'lar
- Audit event code'ları
- Payload struct'ları
- ACK/NACK formatı
- Telemetry payload formatı

Örnek:

```cpp
namespace CmdId {
    inline constexpr std::uint8_t kCreateTask = 0x50U;
    inline constexpr std::uint8_t kDeleteTask = 0x51U;
    inline constexpr std::uint8_t kAck        = 0x80U;
    inline constexpr std::uint8_t kNack       = 0x81U;
}
```

UART'tan gelen frame içinde sadece `cmd = 0x50` vardır. Bunun "create task" anlamına geldiğini `telemetry.hpp` söyler.

Payload struct'ları `packed` tanımlıdır:

```cpp
struct __attribute__((packed)) PayloadAck
{
    std::uint32_t echoed_seq;
};
static_assert(sizeof(PayloadAck) == 4U);
```

Bu çok önemlidir. Haberleşmede byte sayısı sabit olmalıdır. Compiler struct içine padding eklerse gateway ve edge farklı byte dizisi bekler. Bu yüzden:

```text
__attribute__((packed)) -> padding yok
static_assert(sizeof(...)) -> protokol boyutu garanti
```

### `edge/app/comms/ac2_framer.hpp`

AC2 frame formatının sabitlerini ve parser/encoder interface'ini tanımlar.

Frame formatı:

```text
SYNC | VER | SEQ0 SEQ1 SEQ2 SEQ3 | LEN | CMD | PAYLOAD | HMAC[8] | CRC[2]
```

Sabitler:

```cpp
kAC2Sync        = 0xAA
kAC2Version     = 0x02
kAC2MaxPayload  = 128
kAC2MaxFrame    = 146
kAC2HmacLen     = 8
kAC2HeaderLen   = 8
kAC2OverheadLen = 18
```

Parser sonunda şu struct'ı üretir:

```cpp
struct AC2Frame
{
    std::uint32_t seq;
    std::uint8_t  cmd;
    std::uint8_t  payload[kAC2MaxPayload];
    std::uint8_t  payload_len;
    std::uint8_t  hmac[kAC2HmacLen];
};
```

### `edge/app/comms/ac2_framer.cpp`

Payload'ı UART byte frame'ine çevirir veya UART byte stream'ini `AC2Frame` haline getirir.

Gönderirken:

```text
CMD + payload
  -> BuildHeader
  -> append payload
  -> append HMAC
  -> append CRC
  -> UART bytes
```

Alırken:

```text
UART byte stream
  -> AC2Parser::Feed(byte)
  -> parser state machine
  -> CRC check
  -> callback OnAC2Frame(frame)
```

Parser state'leri:

```text
WaitSync
WaitVersion
WaitSeq0
WaitSeq1
WaitSeq2
WaitSeq3
WaitLength
WaitCmd
WaitPayload
WaitHmac
WaitCrc0
WaitCrc1
```

Her `Feed(byte)` çağrısı sadece tek byte işler. Bu sayede frame iki farklı UART read'e bölünse bile parser kaldığı yerden devam eder.

Önemli ayrım:

- CRC frame bütünlüğünü kontrol eder.
- HMAC komutun güvenilir gateway'den geldiğini doğrular.

CRC güvenlik değildir. HMAC güvenliktir.

### `edge/bsp/uart_driver.hpp/.cpp`

Fiziksel UART katmanıdır.

Bu dosya AC2 frame bilmez. Payload bilmez. Command bilmez. Sadece byte taşır.

Kullanılan donanım:

```text
USART2
PA2 -> TX
PA3 -> RX
115200 baud
8N1
RX -> DMA
TX -> blocking HAL transmit
```

RX akışı:

```text
UART RX byte gelir
  -> DMA rx_dma_buf_ içine yazar
  -> UART IDLE interrupt veya DMA complete olur
  -> ISR SignalRxData()
  -> FreeRTOS semaphore verilir
  -> UartRxTask uyanır
```

TX akışı:

```text
TelemetryTxTask
  -> gUart.Write(encoded_frame, len)
  -> HAL_UART_Transmit(...)
```

`IDLE` interrupt önemlidir. Çünkü frame'ler değişken uzunluktadır. Sadece DMA buffer dolmasını beklersen küçük frame'lerde task uyanmayabilir. IDLE line sayesinde "hat kısa süre sessiz kaldı, paket bitmiş olabilir" sinyali alınır.

ISR içinde ağır iş yapılmaz. ISR sadece hazır byte sayısını hesaplar, DMA'yı durdurur ve semaphore verir.

### `edge/app/comms/uart_rx_task.cpp`

UART driver ile AC2 parser arasındaki köprüdür.

Kodun özü:

```cpp
gUart.WaitForData();
n = gUart.Read(gUartRxBuf, sizeof(gUartRxBuf));
for each byte:
    gParser.Feed(byte);
```

Bu task şunları yapmaz:

- HMAC doğrulamaz
- Command dispatch etmez
- State machine değiştirmez
- ACK/NACK kararını vermez

Sadece:

```text
UART byte stream -> AC2Parser
CRC error count -> FailSafeSupervisor
```

Parser CRC hatası sayısını artırırsa `UartRxTask` bunu safety katmanına bildirir:

```cpp
FailSafeSupervisor::Instance().OnCrcError();
```

### `edge/app/runtime/edge_runtime.cpp::OnAC2Frame`

Inbound haberleşmenin güvenlik kapısıdır.

Parser CRC doğruysa `OnAC2Frame` çağrılır. Ama CRC doğru olması komutun güvenilir olduğu anlamına gelmez.

`OnAC2Frame` sırasıyla şunları kontrol eder:

```text
1. HMAC
2. Replay sequence
3. Heartbeat special case
4. Fail-safe lock
5. Rate limit
6. Remote command queue
```

HMAC input:

```text
CMD + PAYLOAD
```

Örnek:

```text
CMD = CreateTask = 0x50
PAYLOAD = 03 1E
HMAC input = 50 03 1E
```

HMAC yanlışsa:

```text
FailSafeSupervisor::OnHmacFailure()
NACK AuthFail
return
```

Replay guard:

```text
Yeni seq > son kabul edilen seq
```

Eski veya tekrar sequence gelirse:

```text
NACK Replay
return
```

Heartbeat özel davranır:

```cpp
if (frame.cmd == CmdId::kHeartbeat) {
    FailSafeSupervisor::Instance().OnGatewayHeartbeatReceived(now_ms);
    return;
}
```

Heartbeat normal command dispatch'e gitmez. Sadece gateway'in canlı olduğunu bildirir.

Fail-safe aktifse reset haricindeki komutlar reddedilir:

```text
NACK FailSafeLock
```

Bütün kontrollerden geçen command `gRemoteCmdQueue` içine konur. Gerçek davranış değişimi `StateMachineTask` context'inde yapılır.

### `edge/app/comms/command_dispatch.cpp`

Güvenlikten geçmiş command'ı sistem davranışına çevirir.

Buraya gelen command için artık şunlar geçilmiştir:

- CRC
- HMAC
- Replay
- Fail-safe lock
- Rate limit

Örnekler:

```text
ManualLock
  -> state machine fail-safe'e zorlanır
  -> LCD/audit alert
  -> ACK
```

```text
GetVersion
  -> VersionReport payload
  -> QueueTx(kVersionReport)
```

```text
SystemReset
  -> ACK
  -> LCD/audit alert
  -> 250 ms sonra NVIC_SystemReset
```

```text
DetectionResult
  -> payload parse edilir
  -> LCD detection state güncellenir
  -> person confidence yüksekse TRACK
  -> ACK
```

```text
CreateTask
  -> payload: task_type, param
  -> CreateUserTask(...)
  -> ACK veya NACK Busy
  -> AuditEvent
  -> TaskList
```

```text
DeleteTask
  -> payload: slot_index
  -> DeleteUserTask(...)
  -> ACK veya NACK
  -> AuditEvent
  -> TaskList
```

Bu dosya UART'a doğrudan yazmaz. `SendAck`, `SendNack` ve `QueueTx` kullanır. Böylece bütün TX çıkışı `TelemetryTxTask` üzerinden tek sıraya girer.

### `edge/app/comms/telemetry_task.cpp`

Edge -> gateway yönündeki ana çıkış kapısıdır.

Üç sorumluluğu vardır:

```text
1. QueueTaskList()
2. QueueTelemetryTick()
3. TelemetryTxTask()
```

`QueueTaskList()` FreeRTOS task durumunu payload'a çevirir:

```text
task count
task name
task state
priority
stack watermark
cpu load
task id
```

`QueueTelemetryTick()` periyodik sistem sağlık raporu üretir:

```text
state
CPU load
task stack watermark
heartbeat miss count
```

Aynı zamanda LCD global state'lerini de günceller.

`TelemetryTxTask()` UART TX'in tek sahibidir:

```cpp
xQueueReceive(gTxQueue, &item, portMAX_DELAY);
AC2Framer::EncodeTelemetry(...);
gUart.Write(encoded, flen);
```

Bu tasarımda farklı task'lar aynı anda UART'a yazmaya çalışmaz. Her şey `gTxQueue` üzerinden sıraya girer.

### `edge/app/comms/heartbeat_task.cpp`

Canlılık kontrolünü yapar.

Her 1 saniyede bir:

```text
1. Gateway heartbeat timeout kontrolü
2. Edge heartbeat gönderimi
```

Kodun özü:

```cpp
vTaskDelayUntil(&last_wake, kHeartbeatPeriodTicks);
FailSafeSupervisor::Instance().CheckHeartbeatTimeout(now_ms);
QueueTx(CmdId::kHeartbeat, ...);
```

Inbound heartbeat:

```text
Gateway -> Edge heartbeat
  -> OnAC2Frame
  -> FailSafeSupervisor::OnGatewayHeartbeatReceived(...)
```

Outbound heartbeat:

```text
HeartbeatTask
  -> QueueTx(kHeartbeat)
  -> TelemetryTxTask
  -> UART
```

## ACK / NACK Mantığı

Gateway command gönderirken `seq` verir.

Örnek:

```text
SEQ = 42
CMD = CreateTask
```

Edge başarılı olursa:

```text
CMD = ACK
echoed_seq = 42
```

Başarısız olursa:

```text
CMD = NACK
echoed_seq = 42
err_code = InvalidPayload / AuthFail / Replay / Busy / ...
```

Bu sayede gateway hangi komutun başarılı veya başarısız olduğunu bilir.

## Queue'lar

Runtime içinde iki kritik comms queue vardır:

```text
gRemoteCmdQueue
gTxQueue
```

`gRemoteCmdQueue`:

```text
OnAC2Frame
  -> güvenlikten geçmiş RemoteCmd
  -> StateMachineTask
  -> DispatchRemoteCmd
```

`gTxQueue`:

```text
SendAck / SendNack / QueueTelemetryTick / QueueTaskList / HeartbeatTask
  -> TxItem
  -> TelemetryTxTask
  -> UART
```

Bu iki queue task sınırlarını netleştirir:

```text
RX/security tarafı command'ı direkt çalıştırmaz.
TX isteyen kod UART'a direkt yazmaz.
```

## Örnek Akışlar

### Create Task

```text
Gateway
  -> AC2 frame CMD=CreateTask, payload={task_type, param}
  -> UART RX
  -> UartRxTask
  -> AC2Parser
  -> OnAC2Frame
      HMAC OK
      Replay OK
      Fail-safe OK
  -> gRemoteCmdQueue
  -> StateMachineTask
  -> DispatchRemoteCmd
  -> CreateUserTask
  -> SendAck
  -> SetLcdAlert(TaskCreate)
  -> QueueTaskList
  -> gTxQueue
  -> TelemetryTxTask
  -> UART TX
  -> Gateway
```

### System Reset

```text
Gateway
  -> CMD=SystemReset
  -> OnAC2Frame security checks
  -> DispatchRemoteCmd
  -> SendAck
  -> LCD/audit alert
  -> schedule reset after 250 ms
  -> StateMachineTask later calls NVIC_SystemReset()
```

Reset hemen yapılmaz. ACK'in UART'tan çıkması için kısa gecikme bırakılır.

### Detection Result

```text
Gateway vision
  -> CMD=DetectionResult, payload={class_id, confidence_pct}
  -> security checks
  -> DispatchRemoteCmd
  -> update LCD detection fields
  -> if person confidence >= threshold:
         StateMachine -> TRACK
     else:
         StateMachine -> IDLE
  -> ACK
```

### Heartbeat

Gateway -> Edge:

```text
Gateway heartbeat frame
  -> OnAC2Frame
  -> FailSafeSupervisor::OnGatewayHeartbeatReceived
```

Edge -> Gateway:

```text
HeartbeatTask every 1 second
  -> PayloadHeartbeatTx uptime_ms
  -> QueueTx(kHeartbeat)
  -> TelemetryTxTask
  -> UART
```

## Dikkat Edilmesi Gereken Yerler

### Outbound telemetry HMAC kullanmıyor

`AC2Framer::EncodeTelemetry` telemetry yönünde HMAC alanını sıfır yazıyor.

```text
Edge -> Gateway: CRC var, HMAC yok
Gateway -> Edge: CRC + HMAC doğrulaması var
```

Bu bilinçli bir tasarım olabilir, ama güvenlik açısından önemli bir farktır.

### TX queue dolarsa mesaj düşebilir

`QueueTx` şu an:

```cpp
xQueueSend(gTxQueue, &item, 0U);
```

Return value kontrol edilmiyor. Queue dolarsa mesaj sessizce düşebilir.

Özellikle bir command aynı anda birkaç response üretebilir:

```text
ACK
AuditEvent
TaskList
```

Aynı anda heartbeat veya telemetry tick gelirse `gTxQueue` dolabilir. İleride iyileştirme fikri:

- TX dropped counter
- ACK/NACK priority
- queue length artırma
- return value kontrolü
- ayrı high-priority TX queue

### UART RX DMA buffer boyutu

`AC2MaxFrame = 146`, ama UART RX DMA buffer yorumu eski 66 byte frame hesabına dayanıyor.

Parser byte byte çalıştığı için parçalı frame teoride desteklenir. Ancak driver DMA receive'i abort/restart yaptığı için büyük inbound frame ve yoğun stream altında dikkat etmek gerekir.

Şu an inbound command payload'ları küçük olduğu için pratikte sorun yaşamayabilir.

## Öğrenme Sırası

Bu konuyu tekrar çalışırken şu sırayla ilerlemek mantıklı:

1. `telemetry.hpp`
2. `ac2_framer.hpp/.cpp`
3. `uart_driver.hpp/.cpp`
4. `uart_rx_task.cpp`
5. `edge_runtime.cpp::OnAC2Frame`
6. `command_dispatch.cpp`
7. `telemetry_task.cpp`
8. `heartbeat_task.cpp`

Bu sırayı takip edersen önce protokol dilini, sonra byte frame'i, sonra UART fiziksel katmanını, sonra RTOS queue/task sınırlarını anlarsın.

## Kısa Mental Model

```text
telemetry.hpp
  "Hangi byte ne anlama geliyor?"

ac2_framer.cpp
  "Bu anlamlı command/payload nasıl frame byte'larına dönüşüyor?"

uart_driver.cpp
  "Bu byte'lar STM32 UART/DMA/interrupt üzerinden nasıl taşınıyor?"

uart_rx_task.cpp
  "Gelen byte stream parser'a nasıl veriliyor?"

OnAC2Frame
  "Bu frame güvenilir mi?"

command_dispatch.cpp
  "Bu güvenilir command sistemde ne yapmalı?"

telemetry_task.cpp
  "Edge cevapları ve telemetry nasıl sırayla gönderiliyor?"

heartbeat_task.cpp
  "Gateway ve edge birbirinin canlı olduğunu nasıl takip ediyor?"
```

Bu sistemde haberleşmeyi anlamanın özü budur.
