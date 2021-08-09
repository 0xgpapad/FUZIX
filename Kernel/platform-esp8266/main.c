#include <stdint.h>
#include "esp8266_peri.h"
#include "kernel.h"
#include "kdata.h"
#include "printf.h"
#include "globals.h"
#include "rom.h"
#include "kernel-esp8266.def"

uaddr_t ramtop = DATATOP;
uint8_t need_resched;
uint16_t swap_dev = 0xFFFF;

bufptr bufpool_end;

/* End of RAM we can use for buffers */
#define RAM_END		0x40000000

void map_init(void)
{
	bufptr bp;
	unsigned int nbuf;

	/* Set up buffer memory */
	nbuf = 0x40000000 - (uint32_t)bufpool;
	nbuf /= sizeof(struct blkbuf);
	bufpool_end = bufpool + nbuf;
	bp = bufpool;
	kprintf("Allocated %d disk buffers.\n", nbuf);
	while(bp < bufpool_end) {
		bp->bf_dev = NO_DEVICE;
		bp->bf_busy = BF_FREE;
		bp++;
	}
}

void platform_discard(void) {}

void platform_monitor(void)
{
	while(1)
		asm volatile ("waiti 15");
}

void platform_reboot(void)
{
	system_restart();
}

uint_fast8_t platform_param(char* p)
{
	return 0;
}

int main(void)
{
	di();
	/* Check offsets */

	if ((U_DATA__U_SP != offsetof(struct u_data, u_sp)) ||
	    (U_DATA__U_PTAB != offsetof(struct u_data, u_ptab)) ||
	    (U_DATA__U_CALLNO != offsetof(struct u_data, u_callno)) ||
	    (P_TAB__P_PID_OFFSET != offsetof(struct p_tab, p_pid)) ||
		(P_TAB__P_STATUS_OFFSET != offsetof(struct p_tab, p_status)))
	{
		kprintf("U_DATA__U_SP = %d\n", offsetof(struct u_data, u_sp));
		kprintf("U_DATA__U_PTAB = %d\n", offsetof(struct u_data, u_ptab));
		kprintf("U_DATA__U_CALLNO = %d\n", offsetof(struct u_data, u_callno));
		kprintf("P_TAB__P_PID_OFFSET = %d\n", offsetof(struct p_tab, p_pid));
		kprintf("P_TAB__P_STATUS_OFFSET = %d\n", offsetof(struct p_tab, p_status));
		panic("bad offsets");
	}

	ramsize = 80;
	procmem = 64;
	kputs("\n\n\n");
	sys_cpu_feat = AF_LX106_ESP8266;


	/* And off we go */
	fuzix_main();
}

/* vim: sw=4 ts=4 et: */
