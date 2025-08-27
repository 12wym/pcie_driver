#ifndef PCIE_FPGA_DRIVER__H
#define PCIE_FPGA_DRIVER__H


/* dwc pcie dma write reg */
#define DMA_WRITE_ENGINE_EN_OFF (0xc)
#define DMA_WRITE_INT_MASK_OFF (0x54)
#define DMA_CH_CONTROL1_OFF_WRCH_0 (0x200)
#define DMA_TRANSFER_SIZE_OFF_WRCH_0 (0x208)
#define DMA_SAR_LOW_OFF_WRCH_0 (0x20c)
#define DMA_SAR_HIGH_OFF_WRCH_0 (0x210)
#define DMA_DAR_LOW_OFF_WRCH_0 (0x214)
#define DMA_DAR_HIGH_OFF_WRCH_0 (0x218)
#define DMA_WRITE_DOORBELL_OFF (0x10)
#define DMA_WRITE_INT_STATUS_OFF (0x4c)
#define INT_STATUS_ABORT_BIT (16)
#define INT_STATUS_DONE_BIT (0)
#define DMA_WRITE_INT_CLEAR_OFF (0x58)


/* common commands codes */
#define FRAME_NUM_GET					0x01000000
#define FRAME_SIZ_GET					0x02000000
#define DEVICE_START					0x03000000
#define DEVICE_STOP						0x04000000
#define IO_RD							0x05000000
#define IO_WR							0x06000000
#define IO_RD_64						0x07000000
#define IO_WR_64						0x08000000

/* DAC commands codes */
#define DATA_SEND						0x0a000000
#define SEND_STATUS						0x0b000000
#define DAC_IDLE_FRAME_NUM_GET			0x0c000000

#define DEFAULT_TIMEOUT_MS 				100
#define DAC_PCIE_ADDRESS_OFFSET			0
#define DAC_MANIPULATE_OFFSET			4096

#define DAC_BUF_COUNT_REG_OFFSET		0x20
#define DAC_TOTAL_REC_FRAME_NUM_OFFSET	0x24
#define DAC_EXPIRED_FRAME_NUM_OFFSET 	0x28
#define FPGA_VERSION_OFFSET				0x2c
#define DAC_FPGA_RECEIVE_BUF_NUM		1000

/* ADC commands codes */
#define DATA_NUM_READ					0x0a000000
#define DATA_READ_SUBMIT				0x0b000000
#define TAIL_RECEIVE					0x0e000000

#define DAC_DMA_SEND_DONE_FREE 			0
#define DAC_DMA_SEND_WORKING			1

#define ADC_PCIE_ADDRESS_OFFSET			2048
#define ADC_MANIPULATE_OFFSET			4096
#define ADC_Offset						32

#define ADC_EXPIRED_FRAME_NUM_OFFSET 	0x60 //4096+96	4192
#define ADC_SEND_SUCCESS_NUM_OFFSET		0x64 //4096+100 4196
#define ADC_TOTAL_SEND_FRAME_NUM_OFFSET 0x68 //4096+104	4200
#define ADC_SAMPLING_NUM_OFFSET			0x6c //4096+108 4204
#define ADC_WARN_OFFSET					0xa0 //4096+160 4256

/* PPS commands codes */
#define WAIT_PPS_INTERRUPT				0x0a000000

/* INFO commands codes */
#define VERSION_GET						0x0a000000

/* WARN commands codes */
#define SET_MISC_SIGNAL_PID				0x0a000000
#define SET_MISC_SIGNAL_NUM				0x0b000000

/* Queue control */
#define FRAMESIZE 						2048
#define ADC_FRAMENUM 					4 * 1024		// 40k read
#define DAC_FRAMENUM 					4 * 1024		// 40k write

#define ADC_READ_QUEUE 					0
#define DAC_WRITE_QUEUE 				1
#define PPS_WAIT_QUEUS 					2

#define INVALID 						0
#define VALID 							1

// #define TTY_MAJOR_AUTO 0
// #define VIRTUAL_TTY_MINORS 2

struct queueNode
{
	int isValid;
    dma_addr_t queue;
	void __iomem * virtualAddr;
	resource_size_t pcieAddress;
    int head;
    int tail;
    int totalNum;
	int frameSize;
    int restIdleNum;
	struct workqueue_struct * workQueue;
	struct work_struct work;
	struct mutex mm_lock;	/* Lock for mmap */
	spinlock_t spinlock;	/* lock for dma transfer timing */
	atomic_t transferStatus;
	wait_queue_head_t wait;
	struct task_struct *thread;
};
//atomic_t in_dma_transfer;

struct dmaQueueManagerSpace
{
	struct queueNode queueArray[2];
	unsigned int currentMemMapControl;
};

/* setup ioctl pass parameter structure */
struct frameControlVar
{
	int index;
	int frameNum;
};

struct dmaTransferSetParam {
	unsigned int currentMemMapControl;
};

/* IO space manipulate structure */
struct ioDataStruct{
    u32 reg;
    u32 data;
};

struct ioDataStruct64{
    u64 reg;
    u64 data;
};

struct dacFrame{
	int frameIndex;
	int frameNum;
};

// struct adcFrame{
// 	int k2utail;
// 	int k2ucount;
// };

struct Version{
	int fpga_version;
	int driver_version;
};

#endif