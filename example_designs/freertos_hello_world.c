/*
    Copyright (C) 2017 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
    Copyright (c) 2012 - 2022 Xilinx, Inc. All Rights Reserved.
	SPDX-License-Identifier: MIT


    http://www.FreeRTOS.org
    http://aws.amazon.com/freertos


    1 tab == 4 spaces!
*/

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"
#include "semphr.h"
/* Xilinx includes. */
#include "xil_printf.h"
#include "xparameters.h"
#include "xscugic.h"
#include "xintc.h"

#include <e_uart.h>
#include <e_uart_obj.h>


void EUartRxIsr(void *ptr, u32 bytes_avail);
static void eUartTask(void *pParams);

extern XScuGic xInterruptController;

/*-----------------------------------------------------------*/

/* The Tx and Rx tasks as described at the top of this file. */
static void prvTxTask( void *pvParameters );
/*-----------------------------------------------------------*/

/* The queue used by the Tx and Rx tasks, as described at the top of this
file. */
static TaskHandle_t xTxTask;
static TaskHandle_t xRxTask;
static SemaphoreHandle_t rxSem;
EUart EUartObj;
PEUart pUart;

char rxBuffer[1024] = "";
int  rxBuffer_wridx = 0;
int  rxBuffer_rdidx = 0;

EUart_Config EUart_Config_Table[] =
{
		{
				XPAR_EUARTS_E_UART_0_DEVICE_ID,
				XPAR_EUARTS_E_UART_0_S00_AXI_BASEADDR
		},
		{
				XPAR_EUARTS_E_UART_1_DEVICE_ID,
				XPAR_EUARTS_E_UART_1_S00_AXI_BASEADDR
		}
};

PEUart_Config EUart_LookupConfig(u16 DeviceId)
{
	PEUart_Config CfgPtr = NULL;
	u32 Index;

	for (Index=0; Index < XPAR_E_UART_NUM_INSTANCES; Index++) {
		if (EUart_Config_Table[Index].DeviceId == DeviceId) {
			CfgPtr = &EUart_Config_Table[Index];
			break;
		}
	}

	return CfgPtr;
}

void print_status() {
	u32 rx_avail;
	u32 tx_avail;
	u32 intStatus;
	u32 dbgStatus;

	//pUart = &EUartObj;
	rx_avail = EUart_RecvFifoOccupancy(pUart);
	tx_avail = EUart_SendFifoOccupancy(pUart);
	intStatus = E_UART_mReadReg(pUart->BaseAddress, EA_INT_STAT_OFFSET);
	dbgStatus = EUart_RdDebugStat(pUart);

	xil_printf("Rx Occup  = %d\r\n", rx_avail);
	xil_printf("Tx Occup  = %d\r\n", tx_avail);
	xil_printf("IntStatus = %x\r\n", intStatus);
	xil_printf("dbgStatus = %08x\r\n", dbgStatus);

}

int main( void )
{
	xil_printf( "Hello from Freertos example main\r\n" );

	rxSem = xSemaphoreCreateBinary();
	if(rxSem == NULL)
		xil_printf("Error creating semaphore");

	/* Create the two tasks.  The Tx task is given a lower priority than the
	Rx task, so the Rx task will leave the Blocked state and pre-empt the Tx
	task as soon as the Tx task places an item in the queue. */
	xTaskCreate( 	prvTxTask, 					/* The function that implements the task. */
					( const char * ) "Tx", 		/* Text name for the task, provided to assist debugging only. */
					configMINIMAL_STACK_SIZE, 	/* The stack allocated to the task. */
					NULL, 						/* The task parameter is not used, so set to NULL. */
					tskIDLE_PRIORITY,			/* The task runs at the idle priority. */
					&xTxTask );

	xTaskCreate( eUartTask,
				 ( const char * ) "GB",
				 configMINIMAL_STACK_SIZE,
				 NULL,
				 tskIDLE_PRIORITY + 1,
				 &xRxTask );

	/* Start the tasks and timer running. */
	vTaskStartScheduler();

	/* If all is well, the scheduler will now be running, and the following line
	will never be reached.  If the following line does execute, then there was
	insufficient FreeRTOS heap memory available for the idle and/or timer tasks
	to be created.  See the memory management section on the FreeRTOS web site
	for more details. */
	for( ;; );
}


/*-----------------------------------------------------------*/
static void prvTxTask( void *pvParameters )
{
char ch;
	for( ;; )
	{
		xSemaphoreTake(rxSem, portMAX_DELAY);
		while(rxBuffer_wridx != rxBuffer_rdidx) {
			ch = rxBuffer[rxBuffer_rdidx];
			EUart_SendByte(pUart, ch);
			rxBuffer_rdidx++;
			rxBuffer_rdidx &= 0x3ff;
		}
	}
}


XIntc dXIntc;
XIntc *pXIntc;

//(Xil_ExceptionHandler) FreeRTOS_Tick_Handler
void XIntc_IrqHandler(void) {
	XIntc_InterruptHandler(pXIntc);
}

static void eUartTask(void *pParams) {
	PEUart_Config cPtr;
	const uint8_t triggerType = 1;
	u32 idx;

	pXIntc = &dXIntc;
	XIntc_Initialize(pXIntc, XPAR_AXI_INTC_0_DEVICE_ID);
	XIntc_Start(pXIntc, XIN_REAL_MODE);
	// Disable all the interrupts on the xintc controller
	for(idx=0;idx<XPAR_AXI_INTC_0_NUM_INTR_INPUTS;idx++) {
		XIntc_Disable(pXIntc, idx);
	}

	// secondary irq controller hanging off here
	XScuGic_SetPriorityTriggerType( &xInterruptController, XPS_IRQ_INT_ID, portLOWEST_USABLE_INTERRUPT_PRIORITY << portPRIORITY_SHIFT, triggerType );
	// Connect xintc top level handler
	XScuGic_Connect( &xInterruptController, XPS_IRQ_INT_ID, (Xil_ExceptionHandler) XIntc_VoidInterruptHandler, ( void * ) NULL );
	/* Enable the interrupt for from the GIC*/
	XScuGic_Enable( &xInterruptController, XPS_IRQ_INT_ID );

	pUart = &EUartObj;
	cPtr = EUart_LookupConfig(0);
	//EUart_CfgInitialize(pUart, cPtr->RegBaseAddr, 50000000, 115200);
	EUart_CfgInitialize(pUart, cPtr->RegBaseAddr, 50000000, 921600);
	EUart_SetRxHandler(pUart, (EUart_Handler) EUartRxIsr, pUart);

	XIntc_Connect(pXIntc, XPAR_AXI_INTC_0_EUARTS_E_UART_0_INTR_INTR, (XInterruptHandler) EUart_Isr, pUart);
	XIntc_Acknowledge(pXIntc, XPAR_AXI_INTC_0_EUARTS_E_UART_0_INTR_INTR);
	XIntc_Enable(pXIntc, XPAR_AXI_INTC_0_EUARTS_E_UART_0_INTR_INTR);
	EUart_Enable(pUart);
	EUart_EnableRxInterrupt(pUart);

}

void EUartRxIsr(void *ptr, u32 bytes_avail) {
	PEUart pUart;
	u32 rxWord;
	u32 idx;

	pUart = ptr;
	do{
	while(bytes_avail) {
		if(bytes_avail>=4) {
			rxWord = EUart_RecvWord(pUart);
			for(idx=0;idx<4;idx++) {
				rxBuffer[rxBuffer_wridx] = (rxWord & 0xff);
				rxBuffer_wridx++;
				rxBuffer_wridx &= 0x3ff;
				rxWord >>= 8;
			}
			bytes_avail -= 4;
		} else {
		rxBuffer[rxBuffer_wridx] = EUart_RecvByte(pUart);
		bytes_avail--;
		rxBuffer_wridx++;
		rxBuffer_wridx &= 0x3ff;
		}
	}
		bytes_avail = EUart_RecvFifoOccupancy(pUart);
	} while(bytes_avail);
	XIntc_Acknowledge(pXIntc, XPAR_AXI_INTC_0_EUARTS_E_UART_0_INTR_INTR);
	xSemaphoreGiveFromISR(rxSem, NULL);
}


