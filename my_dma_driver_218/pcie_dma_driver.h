#ifndef PCIE_DMA_DRIVER__H
#define PCIE_DMA_DRIVER__H

/* commands codes */
// #define DMA_MEMCPY_SETUP 		0x01000000
// #define DMA_MEMCPY_START 		0x02000000
// #define DMA_MEMCPY_GET_TIME 	0x03000000
// #define DMA_MEMCPY_SHUTDOWN 	0x04000000
// #define DMA_MEMCPY_STATUE		0x05000000
#define IO_RD 					0x06000000
#define IO_WR 					0x07000000
#define IO_RD64 				0x06000001
#define IO_WR64 				0x07000001

#define GET_FRAME_SIZE			0x08000000
#define GET_FRAME_NUM			0x08100000

#define DAC_IDLE_FRAME_NUM      0x08200000
#define DAC_DATA_SEND           0x08300000

#define ADC_IDLE_FRAME_NUM      0x08400000`
#define ADC_DATA_RECEIVE        0x08500000

#define DMA_PPS_SIGNAL_GET 		0x09000000
#define SELECTCLOCK             0x0a000000

#define DMA_USERMEM_TO_DEV		0x0
#define DMA_DEV_TO_USERMEM		0x1


/* dwc pcie dma reg */
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
#define DMA_READ_ENGINE_EN_OFF (0x2c)
#define DMA_READ_INT_MASK_OFF (0xa8)
#define DMA_CH_CONTROL1_OFF_RDCH_0 (0x300)
#define DMA_TRANSFER_SIZE_OFF_RDCH_0 (0x308)
#define DMA_SAR_LOW_OFF_RDCH_0 (0x30c)
#define DMA_SAR_HIGH_OFF_RDCH_0 (0x310)
#define DMA_DAR_LOW_OFF_RDCH_0 (0x314)
#define DMA_DAR_HIGH_OFF_RDCH_0 (0x318)
#define DMA_READ_DOORBELL_OFF (0x30)
#define DMA_READ_INT_STATUS_OFF (0xa0)
#define DMA_READ_INT_CLEAR_OFF (0xac)

#define DEFAULT_TIMEOUT_MS (1000)

//BUFF SIZE
#define BUFFER_SIZE 2048 // 假设缓冲区大小为1024帧
// #define PPS_SIGNAL_ENABLE 0x1  // FPGA PPS 中断信号使能寄存器地址


//DAC command codes
#define DAC_PCIE_ADDRESS_OFFSET		0
#define DAC_CONTROL_OFFSET		    4096
#define DAC_BUF_COUNT_REG_OFFSET	0x20
#define DAC_FPGA_RECEIVE_BUF_NUM	1000
#define DAC_FRAMENUM 4 * 1024		// 4k write

//ADC command codes
#define ADC_PCIE_ADDRESS_OFFSET		0
#define ADC_CONTROL_OFFSET		    4096
#define ADC_BUF_COUNT_REG_OFFSET	0x20
#define ADC_FPGA_RECEIVE_BUF_NUM	1000
#define ADC_FRAMENUM 4 * 1024		// 4k write

//PPS mode
#define TMSYNC_SRC_GPS              0x00000001  /* [00]: 输入GPS(双模/多模接口推荐使用此宏) */
#define TMSYNC_SRC_BDS              0x00000002  /* [01]: 输入BDS(北斗) */
#define TMSYNC_SRC_PPS1             0x00000004  /* [02]: 输入PPS(PPS-1) */
#define TMSYNC_SRC_B1               0x00000020  /* [05]: 输入B码(B码-1) */

//queue define
#define MAX_QUEUES 3  // 定义最大队列数
#define DAC_QUEUS  0
#define ADC_QUEUS  1
#define PPS_QUEUS  2
#define QUEUE_SIZE 100  // 队列容量，根据需求调整

/* setup ioctl pass parameter structure */
struct dma_setup_params_stu {
	unsigned int dev_phy_addr;			// dma src phy addr
	unsigned int dst_phy_addr;			// dma dst phy addr
	unsigned int offset_phy_addr;
	unsigned int transfer_direction;	// 0: write, 1: read
	unsigned int transfer_size;	// dma transfer size [byte]
};

/* dma transfer parameter structure */
struct phipheral_dma_stu {
	void __iomem    *pcie_dma_reg_base;
	dma_addr_t	 	trans_src_phy_addr;
	dma_addr_t 		trans_dst_phy_addr;
	unsigned char   *trans_dst_virt_addr;
	unsigned char   *trans_src_virt_addr;
	unsigned int 	trans_total_size;
	struct mutex mm_lock;	/* Lock for mmap */
	spinlock_t spinlock;	/* lock for dma transfer timing */
	struct work_struct work;
	wait_queue_head_t wait;
	atomic_t in_dma_transfer;
};

/* IO space manipulate structure */
struct io_wr_data{
    u32 reg;
    u32 data;
};

struct io_wr_data64{
    u64 reg;
    u64 data;
};

struct devparams {
    int         	dev_fd;
    unsigned int    address;
    unsigned short  *vir_addr;
    unsigned int    frameSize;
    unsigned int    frame_num;

};

typedef struct tmsyncIrigb {
    unsigned int year;       /* 四位年 */
    unsigned int mon;        /* 月[1-12] */
    unsigned int mday;       /* 日[1-31] */
    unsigned int hour;       /* 时[0-23] */
    unsigned int min;        /* 分[0-59] */
    unsigned int sec;        /* 秒[0-60] */
    unsigned int flag;       /* 标志 */
    int 		 iZone;      /* 时区(时间偏移)，以秒为单位 */
    unsigned int quality;    /* 时间品质 */
    unsigned int yday;       /* 年天(1表示当年的1月1日) */
} TMSYNC_IRIGB;

// 环形队列结构体
typedef struct {
    TMSYNC_IRIGB *buffer;  // 存放 TMSYNC_IRIGB 数据的数组
    int head;                         // 队列头索引
    int tail;                         // 队列尾索引
    int size;                         // 队列当前大小
    int totalNum;                     // 队列总大小
    int restIdleNum;                  // 队列剩余空闲大小
	spinlock_t lock;
    struct task_struct *thread;
    struct workqueue_struct * workQueue;
    wait_queue_head_t wait_queue;
} CircularQueue;

struct pps_signal_data {
    uint32_t pps_signal_status;  // FPGA PPS 中断状态
	uint32_t *data;				 // 数据缓冲区
	size_t size;         		 // 数据大小
    uint64_t timestamp;          // PPS 时间戳
	spinlock_t lock;     		 // 锁保护数据访问
    struct dma_setup_params_stu params;    // 其他相关数据
};

#endif