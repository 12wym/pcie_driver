#include <linux/module.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/sched/types.h>
#include <linux/semaphore.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/delay.h>
#include <linux/timekeeping.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/kthread.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/timekeeping.h>
#include <linux/sched/signal.h>

#include "./pcie_driver.h"

#define DRIVER_NAME "pci_fpga_driver"

#define DRIVER_VERSION 2508080101

static struct semaphore pps_semaphore;
static void __iomem *io_hwaddr;

static struct dmaQueueManagerSpace *dmaQueueManagerHandler;
static void __iomem * adc_dma_manipulate_base;

static struct pci_dev *g_pdev = NULL;
static resource_size_t pcie_base0_address;
static int* irq_msi_vec;
static int msi_irq_num = 0;
int dac_fpga_buf_num = 0;
static u64 transfer_times = 0;
static u64 transfer_error_times = 0;
static u64 total_transfer_times = 0;

static pid_t misc_signal_pid = -1;
static int misc_irq_signal = SIGIO;

#define BUFFER_SIZE 2048
static char kernel_buffer[BUFFER_SIZE];
// unsigned int warn_signal = 0;

#define ADC_READ_SIZE 10

// pcie device driver match table
static struct pci_device_id pci_ids[] = {
	{PCI_DEVICE(0x0755, 0x0755)}, //  Vendor ID and Device ID
	{ PCI_DEVICE(0x8086, 0x2725) }, //  Vendor ID and Device ID
    { PCI_DEVICE(0x1c00, 0x5834) }, //  Vendor ID and Device ID
	{
		0,
	}};
MODULE_DEVICE_TABLE(pci, pci_ids);

// 将工作项提交到工作队列
// queue_work(my_workqueue, &my_work);

unsigned int inum = 0;
int tail_count = 0;
static irqreturn_t pcie_xdma_read_req_handler(int irq, void *dev_id)
{
	// dma_addr_t queueBufAddr;
	u64 *queueBufAddr;
	unsigned long flags;
	unsigned long temp;
	// int index = 0;
	int next_tail;
	int i = 0;
	if(irq == irq_msi_vec[0])
	{
		// misc_flag = 1;
		spin_lock_irqsave(&(dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].spinlock), flags);//关闭本地CPU中断并加自旋锁，防止中断嵌套导致的竞态
		// 检查关键指针是否有效
		if (!io_hwaddr || !dmaQueueManagerHandler) {
			pr_err("Invalid io_hwaddr or dmaQueueManagerHandler!\n");
			return IRQ_NONE;
		}
		inum++;
		for(i = 0; i < ADC_READ_SIZE ;i++){
			next_tail = (dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].tail + i) % (dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].totalNum);
			// printk("next_tail:%d",next_tail);
			queueBufAddr = next_tail * (dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].frameSize) + \
						dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].virtualAddr;//queue;
			// 检查 queueBufAddr 是否有效
			if (!queueBufAddr) {
				pr_err("Invalid queueBufAddr at next_tail=%d\n", next_tail);
				continue;
			}
			//frame1
			temp = ioread64(io_hwaddr + ADC_PCIE_ADDRESS_OFFSET + ADC_Offset * i);
			// printk("%016lx", temp);
			*((unsigned int *)queueBufAddr + 1) = (unsigned int)(temp & 0xFFFFFFFF);//s
			*((unsigned int *)queueBufAddr + 2) = (unsigned int)(temp >> 32 & 0xFFFFFFFF);//ns
			temp = ioread64(io_hwaddr + ADC_PCIE_ADDRESS_OFFSET + 8 + ADC_Offset * i);
			// printk("%016lx", temp);
			*((unsigned int *)queueBufAddr + 5) = (unsigned int)(temp & 0xFFFFFFFF);//chanel 1 0
			*((unsigned int *)queueBufAddr + 6) = (unsigned int)(temp >> 32 & 0xFFFFFFFF);//chanel 3 2
			temp = ioread64(io_hwaddr + ADC_PCIE_ADDRESS_OFFSET + 16 + ADC_Offset * i);
			// printk("%016lx", temp);
			*((unsigned int *)queueBufAddr + 7) = (unsigned int)(temp & 0xFFFFFFFF);//chanel 5 4
			*((unsigned int *)queueBufAddr + 8) = (unsigned int)(temp >> 32 & 0xFFFFFFFF);//chanel 7 6
			temp = ioread64(io_hwaddr + ADC_PCIE_ADDRESS_OFFSET + 24 + ADC_Offset * i);
			// printk("%016lx", temp);
			*((unsigned int *)queueBufAddr + 3) = (unsigned int)(temp & 0xFFFFFFFF);//seq
			*((unsigned int *)queueBufAddr + 4) = (unsigned int)(temp >> 32 & 0xFFFFFFFF);//intv
			// *((unsigned int *)queueBufAddr + 4) = inum; //for test
			// printk("\n");

			*((unsigned int *)queueBufAddr) = 0x20;//length 0x20-8 0x18
		}
		
		temp = ioread64(io_hwaddr + ADC_PCIE_ADDRESS_OFFSET + 320);//触发信号
		
		// printk("\n");
		if((dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].tail + ADC_READ_SIZE) >= dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].totalNum)
		{
			tail_count++;
		}
		dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].tail = (dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].tail + ADC_READ_SIZE) % \
																dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].totalNum;
		dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].restIdleNum-=ADC_READ_SIZE;
		spin_unlock_irqrestore(&(dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].spinlock), flags);
	}
	else if(irq == irq_msi_vec[1])
    {
		up(&pps_semaphore);//pps
	}
	else if(irq == irq_msi_vec[2])
	{
		printk("irq interrupt 3\n");
		// printk("misc_signal_pid:%d\n", misc_signal_pid);
		if(misc_signal_pid > 0)//目标用户态进程PID
		{
			struct task_struct *task = pid_task(find_vpid(misc_signal_pid), PIDTYPE_PID);//通过PID查找虚拟进程ID，将虚拟PID转为内核task_struct(进程描述符)
			if(task)
			{
				send_sig(misc_irq_signal, task, 0);//发送信号给应用层
			}
		}
	}
	/*
	 * set source addr
	 * set destiantion addr
	 * set transfer size
	 * begin xdma
	 */
	return IRQ_HANDLED;
}

static int adc_dma_trans_start(int sendFrameIndex, int sendFrameNum);
// DAC send 
static int dac_data_send(int frameIndex, int frameNum)
{
	spin_lock(&(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].spinlock));
	if(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].restIdleNum <= 0)
	{
		spin_unlock(&(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].spinlock));
		return -ENOMEM;
	}
	else
	{
		wake_up_process(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].thread);//唤醒线程
		//
		dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].tail = (dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].tail + frameNum) % dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].totalNum;
		dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].restIdleNum -= frameNum; // = dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].totalNum - ((dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].tail - dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].head) % dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].totalNum)
		spin_unlock(&(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].spinlock));
	}
	return 0;
}

// DAC send thread
static int dac_thread_fn(void *data)//负责DAC 写队列的 DMA 数据传输调度
{
	int head;
	
	int ret;
	struct cpumask mask;
	struct sched_param params;
	params.sched_priority = 99;
	sched_setscheduler(current, SCHED_FIFO, &params);//实时调度配置
    cpumask_clear(&mask);
    cpumask_set_cpu(3, &mask); // 绑定到 CPU 3
    sched_setaffinity(0, &mask); // 绑定当前线程 CPU 亲和性配置
	transfer_times = 0;
	transfer_error_times = 0;
	total_transfer_times = 0;
	// struct timespec64 ts;
	pr_info("Running on CPU: %d\n", smp_processor_id());
	while(!kthread_should_stop())
	{
		spin_lock(&(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].spinlock));
		// judge empty
		if (dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].totalNum - dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].restIdleNum <= 0)//加自旋锁 + 判断队列是否为空
		{
			// empty -> no data
			spin_unlock(&(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].spinlock));
			total_transfer_times = 0;
			set_current_state(TASK_INTERRUPTIBLE);//将线程状态设为 “可中断休眠”，表示线程可被信号唤醒
        	schedule();//主动放弃 CPU，让内核调度其他线程运行
			// usleep_range(3, 7);
		}
		else
		{
			head = dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].head;//读取队列head（待传输帧的索引），然后立即解锁（自旋锁仅保护临界区，避免长时间持有）
			spin_unlock(&(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].spinlock));
			// send data
			ret = adc_dma_trans_start(head, 1);//解锁后执行耗时的 DMA 传输操作
			// printk("done\n");
			total_transfer_times++;
			if(ret >= 0)
			{
				transfer_times++;
			}
			else
			{
				transfer_error_times++;
				// printk("%s : FPGA BUF FULL! success times : %llu, error times : %llu\n", DRIVER_NAME, transfer_times, transfer_error_times);
			}
			spin_lock(&(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].spinlock));//更新队列状态，需重新加锁
			dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].restIdleNum++;
			dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].head = (dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].head + 1) % dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].totalNum;
			spin_unlock(&(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].spinlock));
			if(total_transfer_times >= 7000)//高频传输后的调度优化：当总尝试次数≥7000 次时，清零计数，并调用schedule()主动调度
			{
				total_transfer_times = 0;
				// printk("kt_sleep\n");
				// msleep(1);
				// usleep_range(3, 7);
				set_current_state(TASK_RUNNING);
        		schedule();
			}
		}
	}
	// printk("%s : dma transfer success times = %llu, error times : %llu\n", DRIVER_NAME, transfer_times, transfer_error_times);
	transfer_times = 0;
	transfer_error_times = 0;
	total_transfer_times = 0;
	return 0;
}

//负责启动一次DAC写队列的DMA传输，从内核 DMA 队列的指定帧位置读取数据，通过 XDMA 引擎将数据从内存传输到 PCIe 设备（FPGA/DAC）的硬件缓冲区；
//包含 FPGA 缓冲区满的重试机制、DMA 寄存器配置、传输状态等待与错误处理，确保数据可靠传输。
//要传输的dac队列帧起始索引；要传输的帧数
static int adc_dma_trans_start(int sendFrameIndex, int sendFrameNum)
{
	unsigned long reg_val;
	dma_addr_t dest_addr;
	unsigned int pcie_dac_buf_counts;
	int try_times = 0;
	// if(ioread32(io_hwaddr + DAC_BUF_COUNT_REG_OFFSET + DAC_MANIPULATE_OFFSET) == 0)
	// {
	// 	++test_zero;
	// }
	if(dac_fpga_buf_num <= 0)//fpga缓冲区大小
	{
		do
		{
			// break;
			if(try_times > 5000)
			{
				// pr_info("FPGA BUF FULL\n");
				return -EAGAIN;
			}
			try_times++;
			pcie_dac_buf_counts = ioread32(io_hwaddr + DAC_BUF_COUNT_REG_OFFSET + DAC_MANIPULATE_OFFSET);
			dac_fpga_buf_num = DAC_FPGA_RECEIVE_BUF_NUM - pcie_dac_buf_counts;
		} while (dac_fpga_buf_num <= 0);
	}
	dac_fpga_buf_num--;

	writel(0x1, adc_dma_manipulate_base + DMA_WRITE_ENGINE_EN_OFF);//启用 DMA 写引擎
	/* 
	 * 2. DMA Write Interrupt unMask 
	 * 0x0 : unmask
	 * 0x10001 : mask complete and abort int
	 */
	writel(0x10001, adc_dma_manipulate_base + DMA_WRITE_INT_MASK_OFF);//屏蔽 DMA 写中断
	/*
	 * 3. DMA Channel Control 1 register
	 * Local Interrupt Enable (LIE) =1
	 * Remote Interrupt Enable (RIE) =0
	 * AT, RO, NS, TC, Function Number =0
	 */
	writel(0x04000008, adc_dma_manipulate_base + DMA_CH_CONTROL1_OFF_WRCH_0);//配置 DMA 通道控制寄存器
	/*
	 * 4.
	 * DMA Transfer Size
	 * DMA SAR Low
	 * DMA SAR High
	 * DMA DAR Low
	 * DMA DAR High
	 */
	// writel(FRAMESIZE * sendFrameNum,
	// 	   adc_dma_manipulate_base + DMA_TRANSFER_SIZE_OFF_WRCH_0);
	writel(64 * sendFrameNum,
		   adc_dma_manipulate_base + DMA_TRANSFER_SIZE_OFF_WRCH_0);//配置 DMA 传输大小
	writel(lower_32_bits(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].queue + sendFrameIndex * FRAMESIZE),
		   adc_dma_manipulate_base + DMA_SAR_LOW_OFF_WRCH_0);//配置 DMA 源地址
	writel(upper_32_bits(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].queue + sendFrameIndex * FRAMESIZE),
		   adc_dma_manipulate_base + DMA_SAR_HIGH_OFF_WRCH_0);
	dest_addr = pcie_base0_address + DAC_PCIE_ADDRESS_OFFSET;
	writel(lower_32_bits(dest_addr),
		   adc_dma_manipulate_base + DMA_DAR_LOW_OFF_WRCH_0);////配置 DMA 目的地址
	writel(upper_32_bits(dest_addr),
		   adc_dma_manipulate_base + DMA_DAR_HIGH_OFF_WRCH_0);

	/* enable dma write channel 0 */
	writel(0x0, adc_dma_manipulate_base + DMA_WRITE_DOORBELL_OFF);//触发 DMA 传输
	// printk("dma enter\n");
	while (1)//轮询 DMA 传输状态
	{
		reg_val = readl(adc_dma_manipulate_base + DMA_WRITE_INT_STATUS_OFF);
		// check DMA int status
		/*
		 * may change to
		 */
		if (test_bit(INT_STATUS_ABORT_BIT, &reg_val) || test_bit(INT_STATUS_DONE_BIT, &reg_val))
		{
			// printk("write done\n");
			break;
		}
	}

	/* clear int status  */
	writel(BIT(16) | BIT(0), adc_dma_manipulate_base + DMA_WRITE_INT_CLEAR_OFF);//清除 DMA 中断状态

	/* DMA Write Engine Disable */
	writel(0x0, adc_dma_manipulate_base + DMA_WRITE_ENGINE_EN_OFF);//禁用 DMA 写引擎

	if (test_bit(INT_STATUS_ABORT_BIT, &reg_val))//错误判断与返回
	{
		dev_err(&g_pdev->dev, "dma transfer from mem to pcie err\n");
		return -EAGAIN;
	}

	return 0;
}


// ADC
// -> ioctl
static long pcie_adc_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct ioDataStruct ioData;
	int frameNum;
	int frameSize;
	int frameIndex;
	int popNum;
	long ret = -ENOMEM;
	unsigned long flags;
	int k2utail;
	// struct adcFrame adctail;

	switch (cmd)
	{
	case FRAME_NUM_GET:
		/* get frame num */
		frameNum = dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].totalNum;
		ret = copy_to_user((int *)arg, &frameNum, sizeof(int));
		break;

	case FRAME_SIZ_GET:
		frameSize = dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].frameSize;
		ret = copy_to_user((int *)arg, &frameSize, sizeof(int));
		break;

	case IO_WR:
		ret = copy_from_user(&ioData, (struct ioDataStruct *)arg, sizeof(struct ioDataStruct));
		iowrite32(ioData.data, io_hwaddr + ioData.reg);
		return ret; // success
		// iowrite32()
		break;
	case IO_RD:
		ret = copy_from_user(&ioData, (struct ioDataStruct *)arg, sizeof(struct ioDataStruct));
		ioData.data = ioread32(io_hwaddr + ioData.reg);
		ret = copy_to_user((struct ioDataStruct *)arg, &ioData, sizeof(struct ioDataStruct));
		return ret; // success
	case DATA_NUM_READ:
		spin_lock_irqsave(&(dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].spinlock), flags);
		frameIndex = dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].totalNum - dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].restIdleNum;
		spin_unlock_irqrestore(&(dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].spinlock), flags);
		// printk("frameIndex = %d", frameIndex);
		ret = copy_to_user((int *)arg, &frameIndex, sizeof(frameIndex));
		break;

	case DATA_READ_SUBMIT:
		ret = copy_from_user(&popNum, (int *)arg, sizeof(popNum));
		spin_lock_irqsave(&(dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].spinlock), flags);//防止死锁
		dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].head = (dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].head + popNum) % dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].totalNum;
		dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].restIdleNum += popNum;
		spin_unlock_irqrestore(&(dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].spinlock), flags);
		break;

	case TAIL_RECEIVE:
		spin_lock_irqsave(&(dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].spinlock), flags);
		// adctail.k2ucount = tail_count;
		k2utail = dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].tail;
		ret = copy_to_user((int *)arg, &k2utail, sizeof(k2utail));
		spin_unlock_irqrestore(&(dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].spinlock), flags);
		break;
		
	default:
		break;
	}
	return ret;
}

// ADC
// -> open
static int pcie_adc_open(struct inode *inode, struct file *file)
{
	// ADC DMA Queue Buf alloc
	unsigned long flags;
	dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].virtualAddr = (unsigned char *)dma_alloc_coherent(&g_pdev->dev, ADC_FRAMENUM * FRAMESIZE, &(dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].queue), GFP_KERNEL);
	if (dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].virtualAddr == NULL)
	{
		return -EINVAL;
	}
	spin_lock_irqsave(&(dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].spinlock), flags);
	dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].isValid = VALID;
	dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].frameSize = FRAMESIZE;
	dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].head = 0;
	dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].tail = 0;
	dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].restIdleNum = ADC_FRAMENUM;
	dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].totalNum = ADC_FRAMENUM;
	spin_unlock_irqrestore(&(dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].spinlock), flags);
	tail_count = 0;
	return 0;
}

// ADC
// -> close
static int pcie_adc_release(struct inode *inode, struct file *filp)
{
	unsigned long flags;
	// 1. 禁止中断
    free_irq(irq_msi_vec[0], g_pdev);
	// 2. 确保所有mmap映射都被释放
    mutex_lock(&(dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].mm_lock));
	// free DMA queue
	spin_lock_irqsave(&(dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].spinlock), flags);
	dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].restIdleNum = 0;
	spin_unlock_irqrestore(&(dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].spinlock), flags);
	if (dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].virtualAddr) {
        dma_free_coherent(&g_pdev->dev, 
                         dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].totalNum * 
                         dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].frameSize,
                         dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].virtualAddr,
                         dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].queue);
        dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].virtualAddr = NULL;
    }
	dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].isValid = INVALID;
	mutex_unlock(&(dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].mm_lock));
	
	return 0;
}

// 可以添加一个vm_operations_struct来处理更精细的mmap管理
static const struct vm_operations_struct pcie_adc_vm_ops = {
    .open = NULL,
    .close = NULL,
    // 可以添加更多的VM操作回调
};

// ADC
// -> ioctl
// used to map dma buffer to usr area virtual address, which is distincted by currentMemMapControl
static int pcie_adc_mmap(struct file *file, struct vm_area_struct *vma)
{
	u32 len;
	int ret = -EINVAL;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	phys_addr_t phys_addr;
	unsigned long dmaQueueTotalSize = dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].frameSize * dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].totalNum;
	phys_addr = dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].queue;
	mutex_lock(&(dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].mm_lock));

	/* get page protection attribute in vma */
	vma->vm_page_prot = pgprot_decrypted(vma->vm_page_prot);

	/* check there is enough space in vma */
	if (offset >= dmaQueueTotalSize)
	{
		ret = -EINVAL;
		goto err_quit;
	}
	/* alloc space for dma_memcpy module */
	len = dmaQueueTotalSize - offset;
	vma->vm_pgoff = (phys_addr + offset) >> PAGE_SHIFT;

	len = PAGE_ALIGN(len);
	if (vma->vm_end - vma->vm_start > len)
	{
		// printk("e2");
		ret = -EINVAL;
		goto err_quit;
	}

	/* make buffers bufferable */
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	/* set vm ops */
    vma->vm_ops = &pcie_adc_vm_ops;

	/* remap the physical space to vma space */
	if (remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
						vma->vm_end - vma->vm_start, vma->vm_page_prot))
	{
		dev_err(&g_pdev->dev, "mmap remap_pfn_range failed\n");
		ret = -ENOBUFS;
		goto err_quit;
	}
	inum = 0;
	mutex_unlock(&(dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].mm_lock));
	return 0;

err_quit:
	mutex_unlock(&(dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].mm_lock));
	return ret;
}

// DAC
// -> ioctl
static long pcie_dac_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct ioDataStruct ioData;
	struct ioDataStruct64 ioData64;
	struct dacFrame dacFrameData;
	unsigned int frameNum;
	unsigned int frameSize;
	// int frameIndex;
	int frameIdleNum;
	long ret = -ENOMEM;

	switch (cmd)
	{
	case FRAME_NUM_GET:
		/* get frame num */
		frameNum = dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].totalNum;
		ret = copy_to_user((int *)arg, &frameNum, sizeof(int));
		break;
	case FRAME_SIZ_GET:
		frameSize = dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].frameSize;
		ret = copy_to_user((int *)arg, &frameSize, sizeof(int));
		break;
	case DAC_IDLE_FRAME_NUM_GET:
		spin_lock(&(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].spinlock));
		frameIdleNum = dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].restIdleNum;
		spin_unlock(&(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].spinlock));
		ret = copy_to_user((int *)arg, &frameIdleNum, sizeof(int));
		break;
	case IO_WR:
		ret = copy_from_user(&ioData, (struct ioDataStruct *)arg, sizeof(struct ioDataStruct));
		iowrite32(ioData.data, io_hwaddr + ioData.reg);
		break;

	case IO_RD:
		ret = copy_from_user(&ioData, (struct ioDataStruct *)arg, sizeof(struct ioDataStruct));
		ioData.data = ioread32(io_hwaddr + ioData.reg);
		ret = copy_to_user((struct ioDataStruct *)arg, &ioData, sizeof(struct ioDataStruct));
		break;

	case IO_WR_64:
		ret = copy_from_user(&ioData64, (struct ioDataStruct64 *)arg, sizeof(struct ioDataStruct64));
		iowrite64(ioData64.data, io_hwaddr + ioData64.reg);
		break;

	case IO_RD_64:
		ret = copy_from_user(&ioData64, (struct ioDataStruct64 *)arg, sizeof(struct ioDataStruct64));
		ioData64.data = ioread64(io_hwaddr + ioData64.reg);
		ret = copy_to_user((struct ioDataStruct64 *)arg, &ioData64, sizeof(struct ioDataStruct64));
		break;

	case DATA_SEND:
		ret = copy_from_user(&dacFrameData, (int *)arg, sizeof(dacFrameData));
		ret = dac_data_send(dacFrameData.frameIndex, dacFrameData.frameNum);
		break;

	case SEND_STATUS:
		return atomic_read(&(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].transferStatus));
		break;

	case DEVICE_START:
		break;

	case DEVICE_STOP:
		break;

	default:
		break;
	}
	return ret;
}

//DAC
// -> open
static int pcie_dac_open(struct inode *inode, struct file *file)
{
	// DAC DMA Queue Buf alloc
	dac_fpga_buf_num = 0;
	dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].virtualAddr = (unsigned char *)dma_alloc_coherent(&g_pdev->dev, DAC_FRAMENUM * FRAMESIZE, &(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].queue), GFP_KERNEL);
	if (dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].virtualAddr == NULL)
	{
		return -EINVAL;
	}
	dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].isValid = VALID;
	dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].frameSize = FRAMESIZE;
	dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].head = 0;
	dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].tail = 0;
	dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].restIdleNum = DAC_FRAMENUM;
	dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].totalNum = DAC_FRAMENUM;
	dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].thread = kthread_run(dac_thread_fn, "Thread Data", "dac_kthread");
	if (IS_ERR(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].thread)) {
        pr_err("Failed to create dac kernel thread\n");
        return PTR_ERR(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].thread);
    }
	// DAC write DMA address map
	adc_dma_manipulate_base = ioremap(0x3c0800000 + 0x380000, 0x324);
	if (!adc_dma_manipulate_base) {
		dev_err(&g_pdev->dev, "ioremap pcie dma reg failed\n");
		return -EFAULT;
	}
	return 0;
}

//DAC
// -> close
static int pcie_dac_release(struct inode *inode, struct file *filp)
{
	dac_fpga_buf_num = 0;
	if(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].thread)
	{
		kthread_stop(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].thread);
		printk("%s : Kernel DAC thread stopped\n", DRIVER_NAME);
	}
	// free DMA queue
	dma_free_coherent(&g_pdev->dev, DAC_FRAMENUM * FRAMESIZE,
					  dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].virtualAddr,
					  dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].queue);
	dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].isValid = INVALID;

	if(adc_dma_manipulate_base != 0)
    {
	    iounmap(adc_dma_manipulate_base);
        adc_dma_manipulate_base = 0;
    }
	return 0;
}

static const struct vm_operations_struct pcie_dac_vm_ops = {
    .open = NULL,
    .close = NULL,
    // 可以添加更多的VM操作回调
};

// -> ioctl DAC
// used to map dma buffer to usr area virtual address
static int pcie_dac_mmap(struct file *file, struct vm_area_struct *vma)
{
	u32 len;
	int ret = -EINVAL;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;//用户mmap时指定的偏移量(vm_pgoff是按页计数的偏移，左移PAGE_SHIFT(通常 12)转换为字节偏移)
	phys_addr_t phys_addr;
	unsigned long dmaQueueTotalSize = dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].frameSize * dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].totalNum;
	phys_addr = dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].queue;//要映射的物理内存起始地址
	mutex_lock(&(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].mm_lock));//保护该队列mmap操作的互斥锁，防止多进程同时映射导致竞争

	/* get page protection attribute in vma */
	vma->vm_page_prot = pgprot_decrypted(vma->vm_page_prot);//将内存页的保护属性设置为解密，用户态可访问

	/* check there is enough space in vma */
	if (offset < dmaQueueTotalSize)
	{
		/* alloc space for dma_memcpy module */
		len = dmaQueueTotalSize - offset;
		vma->vm_pgoff = (phys_addr + offset) >> PAGE_SHIFT;//重新设置vm_pgoff(将物理起始地址+偏移转换为按页技术的偏移，供后续映射使用)
	}
	else
	{
		ret = -EINVAL;
		goto err_quit;
	}

	len = PAGE_ALIGN(len);//内存长度按页对齐，将len向上对齐到最近的页面大小
	if (vma->vm_end - vma->vm_start > len)//如果请求长度大于实际可映射的len，请求超出物理内存
	{
		ret = -EINVAL;
		goto err_quit;
	}

	/* make buffers bufferable */
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);//设置内存写合并属性，适用于dma/外设的内存访问，提升批量写的性能
	/* set vm ops */
    vma->vm_ops = &pcie_dac_vm_ops;//关联虚拟内存操作集，将自定义的vm操作集指向vm_operations_struct结构体的指针

	/* remap the physical space to vma space */
	if (remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
						vma->vm_end - vma->vm_start, vma->vm_page_prot))//将物理内存映射到用户态虚拟地址空间，#要映射的虚拟内存区域，#用户态虚拟地址起始位置，#物理内存的页帧号，#映射长度，#页保护属性
	{
		dev_err(&g_pdev->dev, "mmap remap_pfn_range failed\n");
		ret = -ENOBUFS;
		goto err_quit;
	}

	mutex_unlock(&(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].mm_lock));//映射成功：解锁互斥锁
	return 0;

err_quit:
	mutex_unlock(&(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].mm_lock));
	return ret;
}
//time
// -> open
static int time_open(struct inode *inode, struct file *file)
{
	sema_init(&pps_semaphore,0);
	return 0;
}
//time
// -> close
static int time_release(struct inode *inode, struct file *filp)
{
	sema_init(&pps_semaphore,0);
	return 0;
}
//DAC
// -> ioctl
static long time_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct ioDataStruct ioData;
	struct ioDataStruct64 ioData64;
	long ret = -ENOMEM;

	switch (cmd)
	{
		case IO_WR:
			ret = copy_from_user(&ioData, (struct ioDataStruct *)arg, sizeof(struct ioDataStruct));
			iowrite32(ioData.data, io_hwaddr + ioData.reg);
			break;

		case IO_RD:
			ret = copy_from_user(&ioData, (struct ioDataStruct *)arg, sizeof(struct ioDataStruct));
			ioData.data = ioread32(io_hwaddr + ioData.reg);
			ret = copy_to_user((struct ioDataStruct *)arg, &ioData, sizeof(struct ioDataStruct));
			break;

		case IO_WR_64:
			ret = copy_from_user(&ioData64, (struct ioDataStruct64 *)arg, sizeof(struct ioDataStruct64));
			//pr_info("ioData64,data:%lu",ioData64.data);
			iowrite64(ioData64.data, io_hwaddr + ioData64.reg);
			break;

		case IO_RD_64:
			ret = copy_from_user(&ioData64, (struct ioDataStruct64 *)arg, sizeof(struct ioDataStruct64));
			ioData64.data = ioread64(io_hwaddr + ioData64.reg);
			ret = copy_to_user((struct ioDataStruct64 *)arg, &ioData64, sizeof(struct ioDataStruct64));
			break;

		case WAIT_PPS_INTERRUPT:
			down(&pps_semaphore);
			break;
		default:
			break;
	}
	return ret;
}
//warn
// -> open
static int warn_open(struct inode *inode, struct file *file)
{
	return 0;
}
//warn
// -> close
static int warn_release(struct inode *inode, struct file *filp)
{
	return 0;
}
//warn
// -> ioctl
static long warn_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct ioDataStruct ioData;
	struct ioDataStruct64 ioData64;
	long ret = -ENOMEM;

	switch (cmd)
	{
		case IO_WR:
			ret = copy_from_user(&ioData, (struct ioDataStruct *)arg, sizeof(struct ioDataStruct));
			iowrite32(ioData.data, io_hwaddr + ioData.reg);
			break;

		case IO_RD:
			ret = copy_from_user(&ioData, (struct ioDataStruct *)arg, sizeof(struct ioDataStruct));
			ioData.data = ioread32(io_hwaddr + ioData.reg);
			ret = copy_to_user((struct ioDataStruct *)arg, &ioData, sizeof(struct ioDataStruct));
			break;

		case IO_WR_64:
			ret = copy_from_user(&ioData64, (struct ioDataStruct64 *)arg, sizeof(struct ioDataStruct64));
			iowrite64(ioData64.data, io_hwaddr + ioData64.reg);
			break;

		case IO_RD_64:
			ret = copy_from_user(&ioData64, (struct ioDataStruct64 *)arg, sizeof(struct ioDataStruct64));
			ioData64.data = ioread64(io_hwaddr + ioData64.reg);
			ret = copy_to_user((struct ioDataStruct64 *)arg, &ioData64, sizeof(struct ioDataStruct64));
			break;

		case SET_MISC_SIGNAL_PID:
			misc_signal_pid = (pid_t)arg;
			printk("misc_signal_pid:%d\n", misc_signal_pid);
			break;

		case SET_MISC_SIGNAL_NUM:
			misc_irq_signal = (int)arg;
			printk("misc_irq_signal:%d\n", misc_irq_signal);
			break;

		default:
			break;
	}
	return ret;
}
//led
// -> open
static int led_open(struct inode *inode, struct file *file)
{
	return 0;
}
//led
// -> close
static int led_release(struct inode *inode, struct file *filp)
{
	return 0;
}
//led
// -> ioctl
static long led_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct ioDataStruct ioData;
	struct ioDataStruct64 ioData64;
	long ret = -ENOMEM;

	switch (cmd)
	{
		case IO_WR:
			ret = copy_from_user(&ioData, (struct ioDataStruct *)arg, sizeof(struct ioDataStruct));
			iowrite32(ioData.data, io_hwaddr + ioData.reg);
			break;

		case IO_RD:
			ret = copy_from_user(&ioData, (struct ioDataStruct *)arg, sizeof(struct ioDataStruct));
			ioData.data = ioread32(io_hwaddr + ioData.reg);
			ret = copy_to_user((struct ioDataStruct *)arg, &ioData, sizeof(struct ioDataStruct));
			break;

		case IO_WR_64:
			ret = copy_from_user(&ioData64, (struct ioDataStruct64 *)arg, sizeof(struct ioDataStruct64));
			iowrite64(ioData64.data, io_hwaddr + ioData64.reg);
			break;

		case IO_RD_64:
			ret = copy_from_user(&ioData64, (struct ioDataStruct64 *)arg, sizeof(struct ioDataStruct64));
			ioData64.data = ioread64(io_hwaddr + ioData64.reg);
			ret = copy_to_user((struct ioDataStruct64 *)arg, &ioData64, sizeof(struct ioDataStruct64));
			break;

		default:
			break;
	}
	return ret;
}

static const struct file_operations pcie_adc_fops = {
	.open = pcie_adc_open,
	.release = pcie_adc_release,
	.unlocked_ioctl = pcie_adc_ioctl,
	.mmap = pcie_adc_mmap,
};

static struct miscdevice pcie_adc_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "PCIE_ADC",
	.fops = &pcie_adc_fops,
};

static const struct file_operations pcie_dac_fops = {
	.open = pcie_dac_open,
	.release = pcie_dac_release,
	.unlocked_ioctl = pcie_dac_ioctl,
	.mmap = pcie_dac_mmap,
};

static struct miscdevice pcie_dac_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "PCIE_DAC",
	.fops = &pcie_dac_fops,
};

static const struct file_operations pcie_time_fops = {
	.open = time_open,
	.release = time_release,
	.unlocked_ioctl = time_ioctl,
};

static struct miscdevice pcie_time_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "PCIE_TIME",
	.fops = &pcie_time_fops,
};

static const struct file_operations pcie_warn_fops = {
	.open = warn_open,
	.release = warn_release,
	.unlocked_ioctl = warn_ioctl,
};

static struct miscdevice pcie_warn_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "PCIE_WARN",
	.fops = &pcie_warn_fops,
};

static const struct file_operations pcie_led_fops = {
	.open = led_open,
	.release = led_release,
	.unlocked_ioctl = led_ioctl,
};

static struct miscdevice pcie_led_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "PCIE_LED",
	.fops = &pcie_led_fops,
};

// info dev
static ssize_t info_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
	size_t data_len = 0; //strlen(kernel_buffer);
	u32 fpga_dac_expired_frame_num = 0;
	u32 fpga_dac_total_rec_frame_num = 0;
	u32 fpga_adc_expired_frame_num = 0;
	u32 adc_send_success_num = 0;
	u32 adc_fpga_total_send_frame_num = 0;
	u32 adc_sampling_num = 0;
	u32 fpga_version = 0;
	if(*ppos == 0)
	{
		fpga_dac_expired_frame_num = ioread32(io_hwaddr + DAC_EXPIRED_FRAME_NUM_OFFSET + DAC_MANIPULATE_OFFSET);
		fpga_dac_total_rec_frame_num = ioread32(io_hwaddr + DAC_TOTAL_REC_FRAME_NUM_OFFSET + DAC_MANIPULATE_OFFSET);
		fpga_adc_expired_frame_num = ioread32(io_hwaddr + ADC_EXPIRED_FRAME_NUM_OFFSET + ADC_MANIPULATE_OFFSET);
		adc_send_success_num = ioread32(io_hwaddr + ADC_SEND_SUCCESS_NUM_OFFSET + ADC_MANIPULATE_OFFSET);
		adc_fpga_total_send_frame_num = ioread32(io_hwaddr + ADC_TOTAL_SEND_FRAME_NUM_OFFSET + ADC_MANIPULATE_OFFSET);
		adc_sampling_num = ioread32(io_hwaddr + ADC_SAMPLING_NUM_OFFSET + ADC_MANIPULATE_OFFSET);
		fpga_version = ioread32(io_hwaddr + FPGA_VERSION_OFFSET + DAC_MANIPULATE_OFFSET);
		sprintf(kernel_buffer, \
"\
|----------------------BEGIN----------------------|\n\
|*********************VERSION*********************|\n\
|* DRIVER_VER: %lu | FPGA_VER: %02u%02u%02u%02u%02u *|\n\
|***********************DAC***********************|\n\
|* LINUX SUCCESS: %llu    | FAIL: %llu *|\n\
|* FPGA EXPIRE_NUM: %u    | TOTAL_NUM: %u *|\n\
|***********************ADC***********************|\n\
|* UPLOAD_EXPIRE_NUM: %u  | UPLOAD_SUCCESS_NUM: %u  *|\n\
|* UPLOAD_TOTAL_NUM: %u | SAMPLING_TOTAL_NUM: %u *|\n\
|-----------------------END-----------------------|\n", \
DRIVER_VERSION, ((fpga_version >> 9) & 0x7F), ((fpga_version >> 5) & 0xF), ((fpga_version >> 0) & 0x1F), ((fpga_version >> 24) & 0xFF), ((fpga_version >> 16) & 0xFF), transfer_times, transfer_error_times, fpga_dac_expired_frame_num, fpga_dac_total_rec_frame_num, fpga_adc_expired_frame_num, adc_send_success_num, adc_fpga_total_send_frame_num, adc_sampling_num); //|* FPGA BUF EMPTY TIMES: %u *|\n 
		data_len = strlen(kernel_buffer);
		kernel_buffer[data_len] = 0;
	}
	data_len = strlen(kernel_buffer);
    // Check if offset exceeds data length
    if (*ppos >= data_len)
        return 0; // EOF

    // Limit the number of bytes to read
    if (count > data_len - *ppos)
        count = data_len - *ppos;

    // Copy data from kernel buffer to user space
    if (copy_to_user(buf, kernel_buffer + *ppos, count))
        return -EFAULT;

    // Update the file offset
    *ppos += count;

    return count; // Return the number of bytes read
}

static long pcie_info_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct Version version;
	long ret = -ENOMEM;

	switch (cmd)
	{
		case VERSION_GET:
			version.fpga_version = ioread32(io_hwaddr + FPGA_VERSION_OFFSET + DAC_MANIPULATE_OFFSET);
			version.driver_version = DRIVER_VERSION;
			ret = copy_to_user((struct Version *)arg, &version, sizeof(struct Version));
			break;

		default:
			break;
	}
	return ret;
}

static const struct file_operations pcie_info_fops = {
	.read = info_read,
	.unlocked_ioctl = pcie_info_ioctl,
};

static struct miscdevice pcie_info = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "PCIE_INFO",
	.fops = &pcie_info_fops,
};

// 2. PCI 设备探测函数
static int pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	// int bar;
	unsigned int bar0;
	unsigned int bar1;
	unsigned short vendor_id, device_id;
	unsigned long io_start, io_len, mem_start, mem_len;
	int ret = -ENOMEM;
	int num_vectors = -ENOMEM;
	int i;

	printk(KERN_INFO "PCIe Device Detected: Vendor ID: %x, Device ID: %x\n",
		   pdev->vendor, pdev->device);

	// enable PCIE device
	if (pci_enable_device(pdev))//使能 PCIe 设备
	{
		printk(KERN_ERR "Failed to enable PCIe device\n");
		return -ENODEV;
	}
	g_pdev = pdev;//保存当前设备句柄供其他函数（如中断处理、mmap）使用

	ret = pci_request_regions(pdev, DRIVER_NAME);//申请 PCI 地址区域
	if (ret)
	{
		dev_err(&pdev->dev, "Failed to request PCI regions\n");
		goto err1;
	}
	pci_set_master(pdev);//设置设备为主控设备：pci_set_master启用 PCIe 设备的总线主控能力，允许设备发起 DMA 传输（必须调用，否则 DMA 无法工作）
	pci_read_config_word(pdev, PCI_VENDOR_ID, &vendor_id);//读取配置空间：从 PCI 配置空间读取厂商 ID 和设备 ID
	pci_read_config_word(pdev, PCI_DEVICE_ID, &device_id);

	// 读取 BAR0 寄存器的值
	pci_read_config_dword(pdev, PCI_BASE_ADDRESS_0, &bar0);//直接读取 BAR0/BAR1 寄存器值，通常bar0是IO空间，bar1是内存空间
	io_start = pci_resource_start(pdev, 0);//获取 BAR0 对应的物理地址起始值
	io_len = pci_resource_len(pdev, 0);//获取 BAR0 地址区域的长度
	pci_read_config_dword(pdev, PCI_BASE_ADDRESS_1, &bar1);
	mem_start = pci_resource_start(pdev, 1);
	mem_len = pci_resource_len(pdev, 1);

	if (pdev->vendor == 0x1c00)
	{
		pr_info("PCI io_start: 0x%lx\n", io_start);
		pr_info("PCI io_len: 0x%lx\n", io_len);
		io_hwaddr = ioport_map(io_start, io_len); // IO space map
		if (!io_hwaddr)
		{
			dev_err(&g_pdev->dev, "ioremap pcie dma reg failed\n");
			goto err2;
		}
		pr_info("io vir addr : %p\n", io_hwaddr);
		pcie_base0_address = mem_start;
	}
	else //当前是0x755
	{
		io_hwaddr = ioremap(io_start, io_len); // MEM space map, used to signl address manipulate
		if (!io_hwaddr)
		{
			dev_err(&g_pdev->dev, "ioremap pcie dma reg failed\n");
			goto err2;
		}
		pcie_base0_address = io_start; // dma pcie addr
	}

	/* 1. alloc dmaQueueManagerHandler memory, store dma queue manager properties */
	dmaQueueManagerHandler = kzalloc(sizeof(struct dmaQueueManagerSpace), GFP_KERNEL);//分配 DMA 队列管理器内存
	if (dmaQueueManagerHandler < 0)
	{
		printk("%s : Can not allocate dmaQueueManagerHandler\n", DRIVER_NAME);
		goto err3;
	}
	dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].isValid = INVALID;
	dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].isValid = INVALID;
	mutex_init(&(dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].mm_lock));
	spin_lock_init(&(dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].spinlock));
	mutex_init(&(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].mm_lock));
	spin_lock_init(&(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].spinlock));

	/* 2. register misc device 注册杂项设备主设备号为10（核心用户态接口）*/
	ret = misc_register(&pcie_adc_dev);
	if (ret < 0)
	{
		printk("%s : Can not register misc device, error code: %d\n", DRIVER_NAME, ret);
		goto err4;
	}
	ret = misc_register(&pcie_dac_dev);
	if (ret < 0)
	{
		printk("%s : Can not register misc device, error code: %d\n",DRIVER_NAME, ret);
		// misc_deregister(&pcie_adc_dev);
		goto err5;
	}
	ret = misc_register(&pcie_info);
	if (ret < 0)
	{
		printk("%s : Can not register misc device, error code: %d\n", "PCIE_INFO", ret);
		goto err6;
	}
	ret = misc_register(&pcie_time_dev);
	if (ret < 0)
	{
		printk("%s : Can not register misc device, error code: %d\n", DRIVER_NAME, ret);
		goto err7;
	}
	ret = misc_register(&pcie_warn_dev);
	if (ret < 0)
	{
		printk("%s : Can not register misc device, error code: %d\n",DRIVER_NAME, ret);
		goto err8;
	}
	ret = misc_register(&pcie_led_dev);
	if(ret < 0)
	{
		printk("%s : Can not register misc device, error code: %d\n",DRIVER_NAME, ret);
		goto err9;
	}
	sema_init(&pps_semaphore,0);//初始化 PPS 信号量
	// request msi/msix IRQ
	num_vectors = pci_alloc_irq_vectors(pdev, 3, 8, PCI_IRQ_MSIX | PCI_IRQ_MSI);//申请 MSI/MSI-X 中断向量，最少3个，最多8个
	if (num_vectors < 3)
	{
		pr_err("Failed to allocate IRQ vectors\n");
		goto err1;
	}
	else
	{
		printk("Allocated %d IRQ vectors\n", num_vectors);
	}
	msi_irq_num = num_vectors;
	irq_msi_vec = (int *)kzalloc(sizeof(int) * num_vectors, GFP_KERNEL);//全局数组，存储每个中断向量的中断号
	if(!irq_msi_vec)
	{
		goto err4;
	}
	for (i = 0; i < num_vectors; i++) {
		int irq_msi = pci_irq_vector(pdev, i);  // 获取第 i 个中断向量的中断号
		if (irq_msi < 0)
		{
			pr_err("Failed to get IRQ vector 0\n");
			goto err4;
		}
		irq_msi_vec[i] = irq_msi;
		ret = request_irq(irq_msi, pcie_xdma_read_req_handler, IRQF_SHARED, "my_pcie_irq", pdev);//注册中断处理函数pcie_xdma_read_req_handler，IRQF_SHARED表示中断可共享，pdev作为中断标识（卸载时释放）
		if (ret)
		{
			pci_disable_msi(pdev);
			pci_disable_msix(pdev);
			printk("Can not register int, error code: %d\n", ret);
			goto err4;
		}
	}

	printk("%s : PCIE_FPGA_DRIVER version : %lu, install success!\n", DRIVER_NAME, DRIVER_VERSION);
	// warn_signal = ioread32(io_hwaddr + ADC_MANIPULATE_OFFSET + ADC_WARN_OFFSET);
	// pr_info("ADC warn signal: %x\n", warn_signal);
	// success
	return 0;

	// error handle
err1:
	pci_disable_device(pdev);
	if(irq_msi_vec)
	{
		kfree(irq_msi_vec);
		irq_msi_vec = NULL;
	}
	return -EFAULT;
err2:
	pci_release_regions(pdev);
err3:
	if (io_hwaddr != 0)
	{
		ioport_unmap(io_hwaddr);
	}
err4:
	kfree(dmaQueueManagerHandler);
err5:
	misc_deregister(&pcie_adc_dev);
err6:
	misc_deregister(&pcie_adc_dev);
	misc_deregister(&pcie_dac_dev);
err7:
	misc_deregister(&pcie_adc_dev);
	misc_deregister(&pcie_dac_dev);
	misc_deregister(&pcie_info);
err8:
	misc_deregister(&pcie_warn_dev);
err9:
	misc_deregister(&pcie_led_dev);
	return -EFAULT;
}

// 3. 设备移除函数
static void pci_remove(struct pci_dev *pdev)
{
	int i = 0;
	/* unregister misc device */
	misc_deregister(&pcie_adc_dev);
	misc_deregister(&pcie_dac_dev);
	misc_deregister(&pcie_info);
	misc_deregister(&pcie_time_dev);
	// tty_unregister_driver(gps_pcie_tty_driver);
	/* free dma struct */
	while(i < msi_irq_num)
	{
		synchronize_irq(irq_msi_vec[i]);
		free_irq(irq_msi_vec[i], pdev);
		i++;
	}
	msi_irq_num = 0;
	if(irq_msi_vec)
	{
		kfree(irq_msi_vec);
		irq_msi_vec = NULL;
	}
	// free_irq(51, pdev);
	// disable device requested msi/msix interupt
	pci_disable_msi(pdev);
	pci_disable_msix(pdev);
	pci_free_irq_vectors(pdev);

	/* free dma struct */
	if (dmaQueueManagerHandler)
		kfree(dmaQueueManagerHandler);
	// pci_release_region(pdev, pci_select_bars(pdev, IORESOURCE_MEM));
	if (io_hwaddr != 0)
	{
		ioport_unmap(io_hwaddr);
	}
	
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	printk("%s : pcie driver removed\n", DRIVER_NAME);
}

// 4. 定义 PCI 驱动
static struct pci_driver pci_driver = {
	.name = DRIVER_NAME,
	.id_table = pci_ids,
	.probe = pci_probe,
	.remove = pci_remove,
};

// 5. 初始化和清理函数
static int __init pci_driver_init(void)
{
	return pci_register_driver(&pci_driver);
}

static void __exit pci_driver_exit(void)
{
	pci_unregister_driver(&pci_driver);
}

module_init(pci_driver_init);
module_exit(pci_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("guo liang & wym");
MODULE_DESCRIPTION("PCIe FPGA Driver");
