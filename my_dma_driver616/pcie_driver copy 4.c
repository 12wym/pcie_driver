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

#define DRIVER_VERSION 2506050102

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
		spin_lock_irqsave(&(dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].spinlock), flags);
		// if(dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].restIdleNum <= 9)
		// {
		// 	if(inum != last_inum)
		// 	{
		// 		printk("full %d\n",inum);
		// 		last_inum = inum;
		// 	}
		// 	// 限制打印频率，避免日志洪水
        //     // if(full_print_count++ % 50 == 0) {
        //     //     printk(KERN_WARNING "Queue full, restIdleNum=%d, inum=%d\n",
        //     //           dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].restIdleNum,
        //     //           inum);
        //     // }
		// 	// for(i = 0;i < ADC_READ_SIZE;i++)
		// 	// {
		// 	// 	temp = ioread64(io_hwaddr + ADC_PCIE_ADDRESS_OFFSET + ADC_Offset * i);
		// 	// 	temp = ioread64(io_hwaddr + ADC_PCIE_ADDRESS_OFFSET + 8 + ADC_Offset * i);
		// 	// 	temp = ioread64(io_hwaddr + ADC_PCIE_ADDRESS_OFFSET + 16 + ADC_Offset * i);
		// 	// 	temp = ioread64(io_hwaddr + ADC_PCIE_ADDRESS_OFFSET + 24 + ADC_Offset * i);
		// 	// }
		// 	temp = ioread64(io_hwaddr + ADC_PCIE_ADDRESS_OFFSET + 320);//触发信号
			
		// 	spin_unlock_irqrestore(&(dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].spinlock), flags);
		// 	return IRQ_HANDLED;
		// }
		// else
		// {
			// printk("irq interrupt 0\n");
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
				// printk("data: %d",i);
				// for(index = 0; index < ADC_READ_SIZE; index++)
				// {
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
			// printk("%016lx", temp);
			// printk("\n");
				// printk("%llx ", queueBufAddr[index]);
				//printk("index:%u",index);
			// }
			
			// printk("\n");
			if((dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].tail + ADC_READ_SIZE) >= dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].totalNum)
			{
				tail_count++;
			}
			dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].tail = (dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].tail + ADC_READ_SIZE) % \
																	dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].totalNum;
			// if(full_print_count2++ % 1 == 0) {
			// 	printk("tail = %u\n",dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].tail);
			// 	printk("inum = %u\n",inum);
			// 	printk("restIdleNum = %u\n",dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].restIdleNum);
			// }
			// dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].restIdleNum-=ADC_READ_SIZE;
		// }
		spin_unlock_irqrestore(&(dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].spinlock), flags);
	}
	else if(irq == irq_msi_vec[1])
    {
		// spin_lock_irqsave(&(dmaQueueManagerHandler->queueArray[PPS_WAIT_QUEUS].spinlock),flagsto);
		// pps_flag = 1;
		// spin_unlock_irqrestore(&(dmaQueueManagerHandler->queueArray[PPS_WAIT_QUEUS].spinlock),flagsto);
			up(&pps_semaphore);//pps
		// printk("irq interrupt 1\n");
	}
	else if(irq == irq_msi_vec[2])
	{
		printk("irq interrupt 3\n");
		// printk("misc_signal_pid:%d\n", misc_signal_pid);
		if(misc_signal_pid > 0)
		{
			struct task_struct *task = pid_task(find_vpid(misc_signal_pid), PIDTYPE_PID);
			if(task)
			{
				send_sig(misc_irq_signal, task, 0);//发送信号给应用层
				// pr_info("send signal %d to pid %d\n", misc_irq_signal, misc_signal_pid);
				// warn_signal = ioread32(io_hwaddr + ADC_MANIPULATE_OFFSET + ADC_WARN_OFFSET);
				// pr_info("ADC warn signal: %x\n", warn_signal);
			}
		}
	}
	// else
	// {
	// 	printk("no irq interrupt\n");
	// }
	// pr_info("msi int: %d\n", irq);
	// set xdma
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
		// if(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].tail != frameIndex)
		// {
		// 	spin_unlock(&(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].spinlock));
		// 	return -ENOMEM;
		// }
		wake_up_process(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].thread);
		//
		dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].tail = (dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].tail + frameNum) % dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].totalNum;
		dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].restIdleNum -= frameNum; // = dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].totalNum - ((dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].tail - dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].head) % dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].totalNum)
		spin_unlock(&(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].spinlock));
	}
	return 0;
}

// DAC send thread
static int dac_thread_fn(void *data)
{
	int head;
	
	int ret;
	struct cpumask mask;
	struct sched_param params;
	params.sched_priority = 99;
	sched_setscheduler(current, SCHED_FIFO, &params);
    cpumask_clear(&mask);
    cpumask_set_cpu(3, &mask); // 绑定到 CPU 3
    sched_setaffinity(0, &mask); // 绑定当前线程
	transfer_times = 0;
	transfer_error_times = 0;
	total_transfer_times = 0;
	// struct timespec64 ts;
	pr_info("Running on CPU: %d\n", smp_processor_id());
	while(!kthread_should_stop())
	{
		spin_lock(&(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].spinlock));
		// judge empty
		if (dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].totalNum - dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].restIdleNum <= 0)
		{
			// empty -> no data
			spin_unlock(&(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].spinlock));
			total_transfer_times = 0;
			set_current_state(TASK_INTERRUPTIBLE);
        	schedule(); 
			// usleep_range(3, 7);

			// ktime_get_real_ts64(&ts);
			// printk("Seconds: %lld, Nanoseconds: %ld\n", ts.tv_sec, ts.tv_nsec);
		}
		else
		{
			head = dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].head;
			spin_unlock(&(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].spinlock));
			// send data
			// printk("index = %d\n", head);
			ret = adc_dma_trans_start(head, 1);
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
			spin_lock(&(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].spinlock));
			dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].restIdleNum++;
			dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].head = (dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].head + 1) % dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].totalNum;
			spin_unlock(&(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].spinlock));
			if(total_transfer_times >= 7000)
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
	if(dac_fpga_buf_num <= 0)
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

	writel(0x1, adc_dma_manipulate_base + DMA_WRITE_ENGINE_EN_OFF);
	/* 
	 * 2. DMA Write Interrupt unMask 
	 * 0x0 : unmask
	 * 0x10001 : mask complete and abort int
	 */
	writel(0x10001, adc_dma_manipulate_base + DMA_WRITE_INT_MASK_OFF);	
	/*
	 * 3. DMA Channel Control 1 register
	 * Local Interrupt Enable (LIE) =1
	 * Remote Interrupt Enable (RIE) =0
	 * AT, RO, NS, TC, Function Number =0
	 */
	writel(0x04000008, adc_dma_manipulate_base + DMA_CH_CONTROL1_OFF_WRCH_0);
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
		   adc_dma_manipulate_base + DMA_TRANSFER_SIZE_OFF_WRCH_0);
	writel(lower_32_bits(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].queue + sendFrameIndex * FRAMESIZE),
		   adc_dma_manipulate_base + DMA_SAR_LOW_OFF_WRCH_0);
	writel(upper_32_bits(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].queue + sendFrameIndex * FRAMESIZE),
		   adc_dma_manipulate_base + DMA_SAR_HIGH_OFF_WRCH_0);
	dest_addr = pcie_base0_address + DAC_PCIE_ADDRESS_OFFSET;
	writel(lower_32_bits(dest_addr),
		   adc_dma_manipulate_base + DMA_DAR_LOW_OFF_WRCH_0);
	writel(upper_32_bits(dest_addr),
		   adc_dma_manipulate_base + DMA_DAR_HIGH_OFF_WRCH_0);

	/* enable dma write channel 0 */
	writel(0x0, adc_dma_manipulate_base + DMA_WRITE_DOORBELL_OFF);
	// printk("dma enter\n");
	while (1)
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
	writel(BIT(16) | BIT(0), adc_dma_manipulate_base + DMA_WRITE_INT_CLEAR_OFF);

	/* DMA Write Engine Disable */
	writel(0x0, adc_dma_manipulate_base + DMA_WRITE_ENGINE_EN_OFF);

	if (test_bit(INT_STATUS_ABORT_BIT, &reg_val))
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
		// if (ret == 0) {
		// 	printk(KERN_INFO "copy_from_user success\n");
		// } else {
		// 	printk(KERN_ERR "copy_from_user failed, %lu bytes not copied\n", ret);
		// }
		spin_lock_irqsave(&(dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].spinlock), flags);
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
	// free DMA queue
	spin_lock_irqsave(&(dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].spinlock), flags);
	dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].restIdleNum = 0;
	spin_unlock_irqrestore(&(dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].spinlock), flags);
	dma_free_coherent(&g_pdev->dev, ADC_FRAMENUM * FRAMESIZE,
					  dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].virtualAddr,
					  dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].queue);
	dmaQueueManagerHandler->queueArray[ADC_READ_QUEUE].isValid = INVALID;
	
	return 0;
}

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
	if (offset < dmaQueueTotalSize)
	{
		/* alloc space for dma_memcpy module */
		len = dmaQueueTotalSize - offset;
		vma->vm_pgoff = (phys_addr + offset) >> PAGE_SHIFT;
	}
	else
	{
		// printk("e1");
		ret = -EINVAL;
		goto err_quit;
	}

	len = PAGE_ALIGN(len);
	if (vma->vm_end - vma->vm_start > len)
	{
		// printk("e2");
		ret = -EINVAL;
		goto err_quit;
	}

	/* make buffers bufferable */
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

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
	// dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].workQueue = create_workqueue("dac_workqueue");
	// if (dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].workQueue == NULL)
	// {
	// 	return -EINVAL;
	// }
	// INIT_WORK(&(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].work), dma_dac_work);
	// init_waitqueue_head(&dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].wait);
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
	// test_zero = 0;
	// cancel_work_sync(&(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].work)); // 取消任务并等待完成
	// destroy_workqueue(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].workQueue);
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

// -> ioctl DAC
// used to map dma buffer to usr area virtual address
static int pcie_dac_mmap(struct file *file, struct vm_area_struct *vma)
{
	u32 len;
	int ret = -EINVAL;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	phys_addr_t phys_addr;
	unsigned long dmaQueueTotalSize = dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].frameSize * dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].totalNum;
	phys_addr = dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].queue;
	mutex_lock(&(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].mm_lock));

	/* get page protection attribute in vma */
	vma->vm_page_prot = pgprot_decrypted(vma->vm_page_prot);

	/* check there is enough space in vma */
	if (offset < dmaQueueTotalSize)
	{
		/* alloc space for dma_memcpy module */
		len = dmaQueueTotalSize - offset;
		vma->vm_pgoff = (phys_addr + offset) >> PAGE_SHIFT;
	}
	else
	{
		ret = -EINVAL;
		goto err_quit;
	}

	len = PAGE_ALIGN(len);
	if (vma->vm_end - vma->vm_start > len)
	{
		ret = -EINVAL;
		goto err_quit;
	}

	/* make buffers bufferable */
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	/* remap the physical space to vma space */
	if (remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
						vma->vm_end - vma->vm_start, vma->vm_page_prot))
	{
		dev_err(&g_pdev->dev, "mmap remap_pfn_range failed\n");
		ret = -ENOBUFS;
		goto err_quit;
	}

	mutex_unlock(&(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].mm_lock));
	return 0;

err_quit:
	mutex_unlock(&(dmaQueueManagerHandler->queueArray[DAC_WRITE_QUEUE].mm_lock));
	return ret;
}

static int time_open(struct inode *inode, struct file *file)
{
	// struct phipheral_dma_stu *phipheral_pps = p_phipheral_dma;

	// time_manipulate_base = ioremap(0x3c0800000, 0x10000);
	// if (!time_manipulate_base) {
	// 	dev_err(&g_pdev->dev, "ioremap pcie dma reg failed\n");
	// 	return -EFAULT;
	// }
	// else{
		// sema_init(&pps_semaphore,0);
		// printk("pps_semaphore open success\n");
	// }

	return 0;
}

static int time_release(struct inode *inode, struct file *filp)
{
	// struct phipheral_dma_stu *phipheral_pps = p_phipheral_dma;

	// flush_work(&phipheral_pps->work);
	// if(time_manipulate_base != 0){
	// 	iounmap(time_manipulate_base);
	// 	time_manipulate_base = 0;
	// }

	return 0;
}

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
	if (pci_enable_device(pdev))
	{
		printk(KERN_ERR "Failed to enable PCIe device\n");
		return -ENODEV;
	}
	g_pdev = pdev;

	ret = pci_request_regions(pdev, DRIVER_NAME);
	if (ret)
	{
		dev_err(&pdev->dev, "Failed to request PCI regions\n");
		goto err1;
	}
	pci_set_master(pdev);
	pci_read_config_word(pdev, PCI_VENDOR_ID, &vendor_id);
	pci_read_config_word(pdev, PCI_DEVICE_ID, &device_id);

	// 读取 BAR0 寄存器的值
	pci_read_config_dword(pdev, PCI_BASE_ADDRESS_0, &bar0);
	io_start = pci_resource_start(pdev, 0);
	io_len = pci_resource_len(pdev, 0);
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
	else
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
	dmaQueueManagerHandler = kzalloc(sizeof(struct dmaQueueManagerSpace), GFP_KERNEL);
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
	// dmaQueueManagerHandler->currentMemMapControl = NONE;

	/* 2. register misc device */
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
	sema_init(&pps_semaphore,0);
	// request msi/msix IRQ
	num_vectors = pci_alloc_irq_vectors(pdev, 3, 8, PCI_IRQ_MSIX | PCI_IRQ_MSI);
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
	irq_msi_vec = (int *)kzalloc(sizeof(int) * num_vectors, GFP_KERNEL);
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
		ret = request_irq(irq_msi, pcie_xdma_read_req_handler, IRQF_SHARED, "my_pcie_irq", pdev);
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
