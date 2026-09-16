//
// low-level driver for 16550a UART.
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "defs.h"

#define Reg(reg) ((volatile unsigned char *)(UART0 + (reg)))
#define ReadReg(reg) (*(Reg(reg)))
#define WriteReg(reg, v) (*(Reg(reg)) = (v))

#define RHR             0
#define THR             0
#define IER             1
#define IER_RX_ENABLE   (1 << 0)
#define IER_TX_ENABLE   (1 << 1)
#define FCR             2
#define FCR_FIFO_ENABLE (1 << 0)
#define FCR_FIFO_CLEAR  (3 << 1)
#define ISR             2
#define LCR             3
#define LCR_EIGHT_BITS  (3 << 0)
#define LCR_BAUD_LATCH  (1 << 7)
#define LSR             5
#define LSR_RX_READY    (1 << 0)
#define LSR_TX_IDLE     (1 << 5)

#define UART_TX_BUF_SIZE 32

static struct spinlock tx_lock;
static char tx_buf[UART_TX_BUF_SIZE];
static uint tx_r;
static uint tx_w;

static void
uartstart(void)
{
  while (tx_w != tx_r && (ReadReg(LSR) & LSR_TX_IDLE)) {
    WriteReg(THR, tx_buf[tx_r % UART_TX_BUF_SIZE]);
    tx_r++;
  }
}

void
uartinit(void)
{
  WriteReg(IER, 0x00);
  WriteReg(LCR, LCR_BAUD_LATCH);
  WriteReg(0, 0x03);
  WriteReg(1, 0x00);
  WriteReg(LCR, LCR_EIGHT_BITS);
  WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);
  initlock(&tx_lock, "uart");
  WriteReg(IER, IER_RX_ENABLE | IER_TX_ENABLE);
}

// Queue bytes for interrupt-driven output. It may sleep only in user write().
void
uartwrite(char *buf, int n)
{
  for (int i = 0; i < n; i++) {
    acquire(&tx_lock);
    while (tx_w == tx_r + UART_TX_BUF_SIZE)
      sleep(&tx_r, &tx_lock);
    tx_buf[tx_w % UART_TX_BUF_SIZE] = buf[i];
    tx_w++;
    uartstart();
    release(&tx_lock);
  }
}

void
uartputc_sync(int c)
{
  acquire(&tx_lock);
  while ((ReadReg(LSR) & LSR_TX_IDLE) == 0)
    ;
  WriteReg(THR, c);
  release(&tx_lock);
}

static int
uartgetc(void)
{
  if (ReadReg(LSR) & LSR_RX_READY)
    return ReadReg(RHR);
  return -1;
}

void
uartintr(void)
{
  ReadReg(ISR);

  acquire(&tx_lock);
  uartstart();
  release(&tx_lock);
  wakeup(&tx_r);

  while (1) {
    int c = uartgetc();
    if (c == -1)
      break;
    uartputc_sync(c);
  }
}
