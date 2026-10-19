MEMORY
{
  FLASH (rx) : ORIGIN = 0, LENGTH = 1M
  RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 448K
}
/* The last 64 KiB (SPU regions 56..63) are reserved for the IPC mailbox. */
INCLUDE "nrf_common.ld"
