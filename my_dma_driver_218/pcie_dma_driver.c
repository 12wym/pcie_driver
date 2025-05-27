 #include <linux/module.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
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
#include <linux/wait.h>
#include <linux/kthread.h>


#include "pcie_dma_driver.h"

#define DRIVER_NAME "pci_dma_driver"

// 保存映射后的地址
static void __iomem *bar0_virt_addr = NULL;

// static struct phipheral_dma_stu *g_phipheral_dma = NULL;
static struct phipheral_dma_stu *p_phipheral_dma = NULL;
static struct phipheral_dma_stu *d_phipheral_dma = NULL;
static struct phipheral_dma_stu *a_phipheral_dma = NULL;
// static struct dma_setup_params_stu g_dma_setup_params;
static struct pci_dev *g_pdev = NULL;
// static struct timespec64 t_start, t_end;
// static unsigned int t_cost_us;
static unsigned char *dac_memory_alloc_vir_addr;
static unsigned char *adc_memory_alloc_vir_addr;
static resource_size_t pcie_base_addr;
int dac_fpga_buf_num = 0;
int adc_fpga_buf_num = 0;

static struct pci_device_id pci_ids[] = {
    { PCI_DEVICE(0x0755, 0x0755) }, //  Vendor ID and Device ID
    { 0, }
};
MODULE_DEVICE_TABLE(pci, pci_ids);

// 中断等待队列和事件标志
static CircularQueue queues[MAX_QUEUES];

// 初始化环形队列
static void initQueue(CircularQueue *q,int capacity) {
    q->head = 0;
    q->tail = 0;
    q->size = 0;
	q->totalNum = capacity;
    q->restIdleNum = capacity;
	// 动态分配内存给 buffer
    q->buffer = kmalloc(sizeof(TMSYNC_IRIGB) * capacity, GFP_KERNEL);
    if (!q->buffer) {
        pr_err("Failed to allocate memory for queue buffer\n");
        return;
    }
    spin_lock_init(&q->lock);
    init_waitqueue_head(&q->wait_queue);
}

// 入队操作
static int enqueue(CircularQueue *q, TMSYNC_IRIGB data) {
    spin_lock(&q->lock);

    if (q->restIdleNum <= 0) {
        // 队列满，等待消费者处理
        spin_unlock(&q->lock);
        return -ENOMEM;  // 返回队列已满的错误码
    }
    q->buffer[q->tail] = data;
    q->tail = (q->tail + 1) % q->totalNum;
    q->size++;
	q->restIdleNum--;
    // 唤醒等待的消费者线程（如果有）
    wake_up(&q->wait_queue);

    spin_unlock(&q->lock);
    return 0;  // 入队成功
}

// 出队操作
static int dequeue(CircularQueue *q, TMSYNC_IRIGB *data) {
	int ret;
    spin_lock(&q->lock);

    while (q->size == 0) {
        // 如果队列为空，等待生产者线程唤醒
        spin_unlock(&q->lock);
        ret = wait_event_interruptible(q->wait_queue, q->size > 0);
        if (ret) {
            return ret;  // 返回错误码（如中断信号）
        }
        spin_lock(&q->lock);
    }
    *data = q->buffer[q->head];
    q->head = (q->head + 1) % q->totalNum;
    q->size--;
	q->restIdleNum++;
    spin_unlock(&q->lock);
    return 0;  // 出队成功
}

// 初始化多个队列
static void init_multiple_queues(void) {
	int i;
    for (i = 0; i < MAX_QUEUES; i++) {
        initQueue(&queues[i], QUEUE_SIZE);  // 初始化每个队列，假设容量为 QUEUE_SIZE
    }
}

// 模拟 MISC 中断触发函数
static void simulate_misc_interrupt(bool is_real_interrupt) {
    TMSYNC_IRIGB data;

	if(is_real_interrupt){
		printk(KERN_INFO "Handling real MISC interrupt\n");
		data.year    = ioread32(bar0_virt_addr);
		data.mon     = ioread32(bar0_virt_addr + 4);
		data.mday    = ioread32(bar0_virt_addr + 8);
		data.hour    = ioread32(bar0_virt_addr + 12);
		data.min     = ioread32(bar0_virt_addr + 16);
		data.sec     = ioread32(bar0_virt_addr + 20);
		data.flag    = ioread32(bar0_virt_addr + 24);
		data.iZone   = ioread32(bar0_virt_addr + 28);
		data.quality = ioread32(bar0_virt_addr + 32);
		data.yday    = ioread32(bar0_virt_addr + 36);
	}
	else{
		printk(KERN_INFO "Simulating MISC interrupt\n");
		// 模拟读取 FPGA 数据
		// data.year = 2024;
		// data.mon = 12;
		// data.mday = 5;
		// data.hour = 14;
		// data.min = 30;
		// data.sec = 15;
		// data.flag = 0x01;
		// data.iZone = 28800;
		// data.quality = 0x10;
		// data.yday = 340;
		data.year    = ioread32(bar0_virt_addr);
		data.mon     = ioread32(bar0_virt_addr + 4);
		data.mday    = ioread32(bar0_virt_addr + 8);
		data.hour    = ioread32(bar0_virt_addr + 12);
		data.min     = ioread32(bar0_virt_addr + 16);
		data.sec     = ioread32(bar0_virt_addr + 20);
		data.flag    = ioread32(bar0_virt_addr + 24);
		data.iZone   = ioread32(bar0_virt_addr + 28);
		data.quality = ioread32(bar0_virt_addr + 32);
		data.yday    = ioread32(bar0_virt_addr + 36);
	}
    if (enqueue(&queues[PPS_QUEUS], data) < 0) {
        printk(KERN_WARNING "Queue is full, dropping simulated FPGA data\n");
    }
}

//MISC 中断处理函数
static irqreturn_t misc_interrupt_handler(int irq, void *dev_id) {
    simulate_misc_interrupt(true);
    return IRQ_HANDLED;
}

// 函数实现：获取指定外部时钟的 PPS 时间
int GetSelectedWildClockTime(
    unsigned int theType,
    unsigned int *theSec,
    unsigned int *theNs
) {
	ktime_t now;
	struct timespec ts;
    // 检查传入参数的有效性
    if (!theSec || !theNs) {
        return -EINVAL; // 参数无效
    }

    // 根据外部时钟类型获取对应的时间
    switch (theType) {
        case TMSYNC_SRC_GPS:
            // 获取 GPS PPS 的时间
            printk(KERN_INFO "Getting GPS PPS time...\n");
            break;
        case TMSYNC_SRC_BDS:
            // 获取 BDS PPS 的时间
            printk(KERN_INFO "Getting BDS PPS time...\n");
            break;
        case TMSYNC_SRC_PPS1:
            // 获取 PPS1 的时间
            printk(KERN_INFO "Getting PPS1 time...\n");
            break;
        case TMSYNC_SRC_B1:
            // 获取 B1 的时间
            printk(KERN_INFO "Getting B1 time...\n");
            break;
        default:
            return -EINVAL; // 无效的外部时钟类型
    }

    // 假设我们通过某种方式获取到的时间，这里用当前系统时间代替
    now = ktime_get_real(); // 获取真实的系统时间
    ts = ktime_to_timespec(now); // 转换为 timespec 结构

    *theSec = ts.tv_sec; // 秒
    *theNs = ts.tv_nsec; // 纳秒

    return 0; // 成功
}

static int pps_open(struct inode *inode, struct file *file)
{
	struct phipheral_dma_stu *phipheral_pps = p_phipheral_dma;

	phipheral_pps->pcie_dma_reg_base = ioremap(0x3c0800000, 0x10000);
	if (!phipheral_pps->pcie_dma_reg_base) {
		dev_err(&g_pdev->dev, "ioremap pcie dma reg failed\n");
		return -EFAULT;
	}
	else{
		printk("pps open success\n");
	}

	return 0;
}

static int pps_release(struct inode *inode, struct file *filp)
{
	struct phipheral_dma_stu *phipheral_pps = p_phipheral_dma;

	flush_work(&phipheral_pps->work);
	if(phipheral_pps->pcie_dma_reg_base){
		iounmap(phipheral_pps->pcie_dma_reg_base);
	}

	return 0;
}

static long pps_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct io_wr_data io_data;
	long ret = -1;
	unsigned int theType;
    unsigned int theSec, theNs;
	TMSYNC_IRIGB data;

	switch(cmd) {
		case IO_RD:
			ret = copy_from_user(&io_data, (struct io_wr_data *)arg, sizeof(struct io_wr_data));
            io_data.data = ioread32(bar0_virt_addr + io_data.reg);
            ret = copy_to_user((struct io_wr_data *)arg, &io_data, sizeof(struct io_wr_data));
            return ret; // success
		case IO_WR:
			ret = copy_from_user(&io_data, (struct io_wr_data *)arg, sizeof(struct io_wr_data));
            iowrite32(io_data.data, bar0_virt_addr + io_data.reg);
            return ret; // success
            break;
		case DMA_PPS_SIGNAL_GET:
		 	// 阻塞等待数据
			// data.year    = ioread32(bar0_virt_addr);
			// data.mon     = ioread32(bar0_virt_addr + 4);
			// data.mday    = ioread32(bar0_virt_addr + 8);
			// data.hour    = ioread32(bar0_virt_addr + 12);
			// data.min     = ioread32(bar0_virt_addr + 16);
			// data.sec     = ioread32(bar0_virt_addr + 20);
			// data.flag    = ioread32(bar0_virt_addr + 24);
			// data.iZone   = ioread32(bar0_virt_addr + 28);
			// data.quality = ioread32(bar0_virt_addr + 32);
			// data.yday    = ioread32(bar0_virt_addr + 36);
			// if (!enqueue(&queues[PPS_QUEUS], data)) {
			// 	printk(KERN_WARNING "Queue is full, dropping simulated FPGA data\n");
			// }
        	// wait_event_interruptible(queues[PPS_QUEUS].wait_queue, queues[PPS_QUEUS].size > 0);
			if (dequeue(&queues[PPS_QUEUS], &data)) {
            	return -EAGAIN;
        	}

			if (copy_to_user((TMSYNC_IRIGB __user *)arg, &data, sizeof(TMSYNC_IRIGB))) {
				return -EFAULT;
			}

			// printk(KERN_INFO "Data dequeued: year=%d, mon=%d, day=%d\n", data.year, data.mon, data.mday);
			ret = 0;
			break;
		case SELECTCLOCK:
			// 从用户空间拷贝参数
            if (copy_from_user(&theType, (unsigned int *)arg, sizeof(unsigned int))) {
                return -EFAULT;
            }

            ret = GetSelectedWildClockTime(theType, &theSec, &theNs);
            if (ret == 0) {
                // 将结果返回给用户空间
                if (copy_to_user((unsigned int *)arg + 1, &theSec, sizeof(unsigned int)) ||
                    copy_to_user((unsigned int *)arg + 2, &theNs, sizeof(unsigned int))) {
                    return -EFAULT;
                }
                return ret; // 返回成功
            } else {
                return ret; // 返回错误
            }
			break;
		default:
			break;
	}
	return ret;
}

// write 实现
static ssize_t pcie_dma_write(struct file *file, const char __user *buf, size_t count, loff_t *ppos) {
    char command[16];

    if (count > sizeof(command) - 1)
        return -EINVAL;

    if (copy_from_user(command, buf, count))
        return -EFAULT;

    command[count] = '\0';

    // 检查是否是模拟中断命令
    if (strcmp(command, "trigger") == 0) {
        simulate_misc_interrupt(false); // 触发模拟中断
    } else {
        printk(KERN_WARNING "Unknown command: %s\n", command);
    }

    return count;
}

static int dac_dma_trans_start(int sendframeindex,int sendframenum)
{
	struct phipheral_dma_stu *phipheral_dac = d_phipheral_dma;
	unsigned long reg_val;
	dma_addr_t dst_addr;
	unsigned int cnt;
	int try_times = 0;

	if(dac_fpga_buf_num <= 0){
		do{
			if(try_times > 1000){
				pr_info("FPGA BUFF FULL\n");
				return -EAGAIN;
			}
			try_times++;
			cnt = ioread32(bar0_virt_addr + DAC_BUF_COUNT_REG_OFFSET + DAC_CONTROL_OFFSET);
			dac_fpga_buf_num = DAC_FPGA_RECEIVE_BUF_NUM - cnt;
		}while(dac_fpga_buf_num <= 0);
	}
	dac_fpga_buf_num--;

	/* init dma */
	/* 1. DMA Write Engine Enable */
	writel(0x1, phipheral_dac->pcie_dma_reg_base + DMA_WRITE_ENGINE_EN_OFF);
	/* 
		* 2. DMA Write Interrupt unMask 
		* 0x0 : unmask
		* 0x10001 : mask complete and abort int
	*/
	writel(0x10001, phipheral_dac->pcie_dma_reg_base + DMA_WRITE_INT_MASK_OFF);
	/*
		* 3. DMA Channel Control 1 register
		* Local Interrupt Enable (LIE) =1
		* Remote Interrupt Enable (RIE) =0
		* AT, RO, NS, TC, Function Number =0
		*/
	writel(0x04000008, phipheral_dac->pcie_dma_reg_base + DMA_CH_CONTROL1_OFF_WRCH_0);
	/*
		* 4.
		* DMA Transfer Size
		* DMA SAR Low
		* DMA SAR High
		* DMA DAR Low
		* DMA DAR High
		*/
	writel(BUFFER_SIZE * sendframenum,
		phipheral_dac->pcie_dma_reg_base + DMA_TRANSFER_SIZE_OFF_WRCH_0);
	writel(lower_32_bits(phipheral_dac->trans_src_phy_addr + sendframeindex * BUFFER_SIZE),
		phipheral_dac->pcie_dma_reg_base + DMA_SAR_LOW_OFF_WRCH_0);
	writel(upper_32_bits(phipheral_dac->trans_src_phy_addr + sendframeindex * BUFFER_SIZE),
		phipheral_dac->pcie_dma_reg_base + DMA_SAR_HIGH_OFF_WRCH_0);
	dst_addr = pcie_base_addr + DAC_PCIE_ADDRESS_OFFSET;
	writel(lower_32_bits(dst_addr),
		phipheral_dac->pcie_dma_reg_base + DMA_DAR_LOW_OFF_WRCH_0);
	writel(upper_32_bits(dst_addr),
		phipheral_dac->pcie_dma_reg_base + DMA_DAR_HIGH_OFF_WRCH_0);

	writel(0x0, phipheral_dac->pcie_dma_reg_base + DMA_WRITE_DOORBELL_OFF);
	while(1){
		reg_val = readl(phipheral_dac->pcie_dma_reg_base + DMA_WRITE_INT_STATUS_OFF);
		if(test_bit(INT_STATUS_ABORT_BIT, &reg_val) || test_bit(INT_STATUS_DONE_BIT, &reg_val)){
			break;
		}
	}
	/* clear int status  */
	writel(BIT(16) | BIT(0), phipheral_dac->pcie_dma_reg_base + DMA_WRITE_INT_CLEAR_OFF);

	/* DMA Write Engine Disable */
	writel(0x0, phipheral_dac->pcie_dma_reg_base + DMA_WRITE_ENGINE_EN_OFF);

	// printk("[%s %d] reg_val: 0x%lx\n", __func__, __LINE__, reg_val);
	if (test_bit(INT_STATUS_ABORT_BIT, &reg_val)) {
		dev_err(&g_pdev->dev, "dma transfer from mem to pcie err\n");
		return -EAGAIN;
	}

	return 0;
}

static int dac_thread(void *data)
{
	int head;
	int transfer_time = 0;
	while(!kthread_should_stop()){
		spin_lock(&queues[DAC_QUEUS].lock);
		if(queues[DAC_QUEUS].totalNum - queues[DAC_QUEUS].restIdleNum <= 0){
			spin_unlock(&queues[DAC_QUEUS].lock);
			usleep(10);
		}
		else{
			head = queues[DAC_QUEUS].head;
			spin_unlock(&queues[DAC_QUEUS].lock);
			dac_dma_trans_start(head,1);
			transfer_time++;
			spin_lock(&queues[DAC_QUEUS].lock);
			queues[DAC_QUEUS].restIdleNum++;
			queues[DAC_QUEUS].head = (queues[DAC_QUEUS].head + 1) % queues[DAC_QUEUS].totalNum;
			spin_unlock(&queues[DAC_QUEUS].lock);
		}
	}
	printk("dma transfer time: %d\n", transfer_time);
	return 0;
}

static int dac_data_send(int frameindex){
	spin_lock(&queues[DAC_QUEUS].lock);
	if(queues[DAC_QUEUS].restIdleNum <= 0){
		spin_unlock(&queues[DAC_QUEUS].lock);
		return -EAGAIN;
	}
	else{
		wake_up_process(queues[DAC_QUEUS].thread);
		queues[DAC_QUEUS].tail = (queues[DAC_QUEUS].tail + 1) % queues[DAC_QUEUS].totalNum;
		queues[DAC_QUEUS].restIdleNum--;
		spin_unlock(&queues[DAC_QUEUS].lock);
	}
	return 0;
}

static void dma_dac_work(struct work_struct *work)
{
	struct phipheral_dma_stu *phipheral_dac = d_phipheral_dma;
	atomic_set(&phipheral_dac->in_dma_transfer, 0);
	pr_info("DMA transfer completed!\n");
}

static int dac_open(struct inode *inode, struct file *file)
{
	struct phipheral_dma_stu *phipheral_dac = d_phipheral_dma;
	dac_fpga_buf_num = 0;
	
	dac_memory_alloc_vir_addr = (unsigned char *)dma_alloc_coherent(&g_pdev->dev, DAC_FRAMENUM*BUFFER_SIZE, \
											&phipheral_dac->trans_src_phy_addr, GFP_KERNEL);
	if(dac_memory_alloc_vir_addr == NULL) {
		return -ENOMEM;
	}
	queues[DAC_QUEUS].size = BUFFER_SIZE;
	queues[DAC_QUEUS].head = 0;
	queues[DAC_QUEUS].tail = 0;
	queues[DAC_QUEUS].totalNum = DAC_FRAMENUM;
	queues[DAC_QUEUS].restIdleNum = DAC_FRAMENUM;
	queues[DAC_QUEUS].workQueue = create_workqueue("dac_workqueue");
	if(!queues[DAC_QUEUS].workQueue){
		printk(KERN_ERR "create dac workqueue failed\n");
		return -ENOMEM;
	}
	// mutex_init(&phipheral_dma->mm_lock);
	// spin_lock_init(&phipheral_dma->spinlock);
	INIT_WORK(&phipheral_dac->work, dma_dac_work);
	init_waitqueue_head(&phipheral_dac->wait);
	queues[DAC_QUEUS].thread = kthread_run(dac_thread, "dac_thread_data", "dac_thread");
	if(IS_ERR(queues[DAC_QUEUS].thread)){
		printk(KERN_ERR "create dac thread failed\n");
		return PTR_ERR(queues[DAC_QUEUS].thread);
	}
	phipheral_dac->pcie_dma_reg_base = ioremap(0x3c0800000 + 0x380000, 0x324);
	if (!phipheral_dac->pcie_dma_reg_base) {
		dev_err(&g_pdev->dev, "ioremap pcie dma reg failed\n");
		return -EFAULT;
	}else{
		printk("dac ioremap success\n");
	}

	return 0;
}

static int dac_release(struct inode *inode, struct file *filp)
{	
	struct phipheral_dma_stu *phipheral_dac = d_phipheral_dma;
	dac_fpga_buf_num = 0;
	cancel_work_sync(&phipheral_dac->work);
	destroy_workqueue(queues[DAC_QUEUS].workQueue);
	if(queues[DAC_QUEUS].thread){
		kthread_stop(queues[DAC_QUEUS].thread);
		pr_info("dac thread stoped\n");
	}
	dma_free_coherent(&g_pdev->dev, DAC_FRAMENUM*BUFFER_SIZE, 
						dac_memory_alloc_vir_addr,
						phipheral_dac->trans_src_phy_addr);
	if(phipheral_dac->pcie_dma_reg_base){
		iounmap(phipheral_dac->pcie_dma_reg_base);
	}

	return 0;
}

static int dac_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct phipheral_dma_stu *phipheral_dac = d_phipheral_dma;
	u32 len;
	int ret;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	phys_addr_t phys_addr;
	unsigned long dactotalsize = queues[DAC_QUEUS].totalNum*queues[DAC_QUEUS].size;
	phys_addr = phipheral_dac->trans_src_phy_addr;

	mutex_lock(&phipheral_dac->mm_lock);

	/* get page protection attribute in vma */
	vma->vm_page_prot = pgprot_decrypted(vma->vm_page_prot);

	/* check there is enough space in vma */
	if (offset < dactotalsize) {
		/* alloc space for dma_memcpy module */
		len = dactotalsize - offset;
		vma->vm_pgoff = (phys_addr + offset) >> PAGE_SHIFT;
	} else {
		ret = -EINVAL;
		goto err_quit;
	}

	len = PAGE_ALIGN(len);
	if (vma->vm_end - vma->vm_start > len) {
		ret = -EINVAL;
		goto err_quit;
	}

	/* make buffers bufferable */
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	/* remap the physical space to vma space */
	if (remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			    vma->vm_end - vma->vm_start, vma->vm_page_prot)) {
		dev_err(&g_pdev->dev, "mmap remap_pfn_range failed\n");
		ret = -ENOBUFS;
		goto err_quit;
	}

	mutex_unlock(&phipheral_dac->mm_lock);
	return 0;

err_quit:
	mutex_unlock(&phipheral_dac->mm_lock);
	return ret;
}

static long dac_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	long ret = -1;
	int frameindex;
	int frameidlenum;
	unsigned int framesize;
	unsigned int framenum;
	struct io_wr_data io_data;
	struct io_wr_data64 io_data64;
	switch (cmd)	
	{
		case GET_FRAME_NUM:
			framenum = queues[DAC_QUEUS].totalNum;
			ret = copy_to_user((unsigned int *)arg, &framenum, sizeof(unsigned int));
			break;
		case GET_FRAME_SIZE:
			framesize = queues[DAC_QUEUS].size;
			ret = copy_to_user((unsigned int *)arg, &framesize, sizeof(unsigned int));
			break;
		case IO_RD:
			ret = copy_from_user(&io_data, (struct io_wr_data *)arg, sizeof(struct io_wr_data));
			io_data.data = ioread32(bar0_virt_addr + io_data.reg);
			ret = copy_to_user((struct io_wr_data *)arg, &io_data, sizeof(struct io_wr_data));
			break;
		case IO_WR:
			ret = copy_from_user(&io_data, (struct io_wr_data *)arg, sizeof(struct io_wr_data));
			iowrite32(io_data.data, bar0_virt_addr + io_data.reg);
			break;
		case IO_RD64:
			ret = copy_from_user(&io_data64, (struct io_wr_data64 *)arg, sizeof(struct io_wr_data64));
			io_data64.data = ioread64(bar0_virt_addr + io_data64.reg);
			ret = copy_to_user((struct io_wr_data64 *)arg, &io_data64, sizeof(struct io_wr_data64));
			break;
		case IO_WR64:
			ret = copy_from_user(&io_data64, (struct io_wr_data64 *)arg, sizeof(struct io_wr_data64));
			iowrite64(io_data64.data, bar0_virt_addr + io_data64.reg);
			break;
		case DAC_IDLE_FRAME_NUM:
			spin_lock(&queues[DAC_QUEUS].lock);
			frameidlenum = queues[DAC_QUEUS].restIdleNum;
			spin_unlock(&queues[DAC_QUEUS].lock);
			ret = copy_to_user((unsigned int *)arg, &frameidlenum, sizeof(unsigned int));
			break;
		case DAC_DATA_SEND:
			ret = copy_from_user(&frameindex, (int *)arg, sizeof(int));
			ret = dac_data_send(frameindex);
			break;
		default:
			break;
	}
	return ret;
}

static int adc_rec(int recframeindex, int recframenum)
{
    struct phipheral_dma_stu *phipheral_adc = a_phipheral_dma;
    unsigned long reg_val;
    unsigned int cnt;
	unsigned char *data_buf;
    int try_times = 0;

    // 等待缓冲区有空间可以接收数据
    if (adc_fpga_buf_num <= 0) {
        do {
            if (try_times > 1000) {
                pr_info("FPGA ADC BUF FULL\n");
                return -EAGAIN;
            }
            try_times++;
            cnt = ioread32(bar0_virt_addr + ADC_BUF_COUNT_REG_OFFSET + ADC_CONTROL_OFFSET);
            adc_fpga_buf_num = ADC_FPGA_RECEIVE_BUF_NUM - cnt;
        } while (adc_fpga_buf_num <= 0);
    }
    adc_fpga_buf_num--;

    // 直接将数据写入 ADC
    // 假设 data_buf 是你存储 ADC 数据的缓冲区
    *data_buf = (unsigned char *)(phipheral_adc->trans_src_vir_addr + recframeindex * BUFFER_SIZE);

    // 写入数据
    // 这里我们假设数据是通过某种方式（例如内存映射）写入到 ADC 的数据寄存器或缓冲区
    // 这个过程通常会直接通过 I/O 寄存器控制外设，但具体实现要根据硬件的具体细节来决定
    writel(data_buf, phipheral_adc->pcie_adc_reg_base + ADC_PCIE_ADDRESS_OFFSET);

    // 检查写入状态，模拟检查数据传输完成
    reg_val = readl(phipheral_adc->pcie_adc_reg_base + ADC_STATUS_REG_OFFSET);
    while (!(reg_val & ADC_STATUS_DONE)) {
        reg_val = readl(phipheral_adc->pcie_adc_reg_base + ADC_STATUS_REG_OFFSET);
        if (test_bit(ADC_STATUS_ABORT_BIT, &reg_val)) {
            dev_err(&g_pdev->dev, "adc transfer failed\n");
            return -EAGAIN;
        }
    }

    // 清除状态寄存器中的中断标志
    // writel(ADC_STATUS_DONE | ADC_STATUS_ABORT, phipheral_adc->pcie_adc_reg_base + ADC_STATUS_REG_CLEAR_OFFSET);

    // 更新完传输后清理工作
    return 0;
}

static int adc_thread(void *data)
{
    int head;
    int transfer_time = 0;
    while (!kthread_should_stop()) {
        spin_lock(&queues[ADC_QUEUS].lock);
        if (queues[ADC_QUEUS].totalNum - queues[ADC_QUEUS].restIdleNum <= 0) {
            spin_unlock(&queues[ADC_QUEUS].lock);
            usleep(10);  // 如果没有数据，挂起10ms
        } else {
            head = queues[ADC_QUEUS].head;
            spin_unlock(&queues[ADC_QUEUS].lock);
            adc_rec(head, 1);  // 写入1帧数据
            transfer_time++;
            
            // 更新队列状态
            spin_lock(&queues[ADC_QUEUS].lock);
            queues[ADC_QUEUS].restIdleNum++;
            queues[ADC_QUEUS].head = (queues[ADC_QUEUS].head + 1) % queues[ADC_QUEUS].totalNum;
            spin_unlock(&queues[ADC_QUEUS].lock);
        }
    }
    printk("adc write time: %d\n", transfer_time);
    return 0;
}

static int adc_data_receive(int frameindex)
{
    spin_lock(&queues[ADC_QUEUS].lock);
    
    // 如果队列中没有待接收的数据
    if (queues[ADC_QUEUS].restIdleNum <= 0) {
        spin_unlock(&queues[ADC_QUEUS].lock);
        return -EAGAIN;  // 返回资源不足的错误代码
    } else {
        // 如果队列中有待接收的数据
        wake_up_process(queues[ADC_QUEUS].thread);  // 唤醒 ADC 线程，开始数据接收

        // 更新队列尾部指针，表示一个新的数据位置
        queues[ADC_QUEUS].tail = (queues[ADC_QUEUS].tail + 1) % queues[ADC_QUEUS].totalNum;

        // 减少空闲数据数量
        queues[ADC_QUEUS].restIdleNum--;

        spin_unlock(&queues[ADC_QUEUS].lock);
    }

    return 0;
}

static void adc_work(struct work_struct *work)
{
	struct phipheral_dma_stu *phipheral_adc = a_phipheral_dma;
	atomic_set(&phipheral_adc->in_dma_transfer, 0);
	pr_info("ADC transfer completed!\n");
}

static int adc_open(struct inode *inode, struct file *file)
{
	struct phipheral_dma_stu *phipheral_adc = a_phipheral_dma;
	adc_fpga_buf_num = 0;

	// adc_memory_alloc_vir_addr = (unsigned char *)dma_alloc_coherent(&g_pdev->dev, ADC_FRAMENUM*BUFFER_SIZE, \
	// 										&phipheral_adc->trans_src_phy_addr, GFP_KERNEL);
	// if(adc_memory_alloc_vir_addr == NULL) {
	// 	return -ENOMEM;
	// }
	adc_memory_alloc_vir_addr = kmalloc(ADC_FRAMENUM*BUFFER_SIZE, GFP_KERNEL);
	if(adc_memory_alloc_vir_addr == NULL) {
		return -ENOMEM;
	}

	queues[ADC_QUEUS].size = BUFFER_SIZE;
	queues[ADC_QUEUS].head = 0;
	queues[ADC_QUEUS].tail = 0;
	queues[ADC_QUEUS].totalNum = ADC_FRAMENUM;
	queues[ADC_QUEUS].restIdleNum = ADC_FRAMENUM;
	queues[ADC_QUEUS].workQueue = create_workqueue("adc_workqueue");
	if(!queues[ADC_QUEUS].workQueue){
		printk(KERN_ERR "create adc workqueue failed\n");
		return -ENOMEM;
	}

	INIT_WORK(&phipheral_adc->work, adc_work);
	init_waitqueue_head(&phipheral_adc->wait);
	queues[ADC_QUEUS].thread = kthread_run(dac_thread, "adc_thread_data", "adc_thread");
	if(IS_ERR(queues[ADC_QUEUS].thread)){
		printk(KERN_ERR "create adc thread failed\n");
		return PTR_ERR(queues[ADC_QUEUS].thread);
	}

	phipheral_adc->pcie_dma_reg_base = ioremap(0x3c0800000 + 0x10000, 0x10000);
	if (!phipheral_adc->pcie_dma_reg_base) {
		dev_err(&g_pdev->dev, "ioremap pcie adc reg failed\n");
		return -EFAULT;
	}
	else{
		printk("adc open success\n");
	}

	return 0;
}

static int adc_release(struct inode *inode, struct file *filp)
{	
	struct phipheral_dma_stu *phipheral_adc = a_phipheral_dma;
	adc_fpga_buf_num = 0;
	cancel_work_sync(&phipheral_adc->work);
	destroy_workqueue(queues[ADC_QUEUS].workQueue);
	if(queues[ADC_QUEUS].thread){
		kthread_stop(queues[ADC_QUEUS].thread);
		pr_info("adc thread stoped\n");
	}
	// dma_free_coherent(&g_pdev->dev, ADC_FRAMENUM*BUFFER_SIZE, 
	// 					adc_memory_alloc_vir_addr,
	// 					phipheral_adc->trans_src_phy_addr);
	kfree(adc_memory_alloc_vir_addr);
	if(phipheral_adc->pcie_dma_reg_base){
		iounmap(phipheral_adc->pcie_dma_reg_base);
	}

	return 0;
}

static int adc_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct phipheral_dma_stu *phipheral_adc = a_phipheral_dma;
	u32 len;
	int ret;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	phys_addr_t phys_addr;
	unsigned long totalsize = queues[ADC_QUEUS].totalNum*queues[ADC_QUEUS].size;
	phys_addr = phipheral_adc->trans_src_phy_addr;

	mutex_lock(&phipheral_adc->mm_lock);

	/* get page protection attribute in vma */
	vma->vm_page_prot = pgprot_decrypted(vma->vm_page_prot);

	/* check there is enough space in vma */
	if (offset < totalsize) {
		/* alloc space for dma_memcpy module */
		len = totalsize - offset;
		vma->vm_pgoff = (phys_addr + offset) >> PAGE_SHIFT;
	} else {
		ret = -EINVAL;
		goto err_quit;
	}

	len = PAGE_ALIGN(len);
	if (vma->vm_end - vma->vm_start > len) {
		ret = -EINVAL;
		goto err_quit;
	}

	/* make buffers bufferable */
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	/* remap the physical space to vma space */
	if (remap_pfn_range(vma, vma->vm_start, vma->vm_pgoff,
			    vma->vm_end - vma->vm_start, vma->vm_page_prot)) {
		dev_err(&g_pdev->dev, "mmap remap_pfn_range failed\n");
		ret = -ENOBUFS;
		goto err_quit;
	}

	mutex_unlock(&phipheral_adc->mm_lock);
	return 0;

err_quit:
	mutex_unlock(&phipheral_adc->mm_lock);
	return ret;
}

static long adc_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	long ret = -1;
	int frameindex;
	int frameidlenum;
	unsigned int framesize;
	unsigned int framenum;
	struct io_wr_data io_data;
	struct io_wr_data64 io_data64;
	switch (cmd)	
	{
		case GET_FRAME_NUM:
			framenum = queues[ADC_QUEUS].totalNum;
			ret = copy_to_user((unsigned int *)arg, &framenum, sizeof(unsigned int));
			break;
		case GET_FRAME_SIZE:
			framesize = queues[ADC_QUEUS].size;
			ret = copy_to_user((unsigned int *)arg, &framesize, sizeof(unsigned int));
			break;
		case IO_RD:
			ret = copy_from_user(&io_data, (struct io_wr_data *)arg, sizeof(struct io_wr_data));
			io_data.data = ioread32(bar0_virt_addr + io_data.reg);
			ret = copy_to_user((struct io_wr_data *)arg, &io_data, sizeof(struct io_wr_data));
			break;
		case IO_WR:
			ret = copy_from_user(&io_data, (struct io_wr_data *)arg, sizeof(struct io_wr_data));
			iowrite32(io_data.data, bar0_virt_addr + io_data.reg);
			break;
		case IO_RD64:
			ret = copy_from_user(&io_data64, (struct io_wr_data64 *)arg, sizeof(struct io_wr_data64));
			io_data64.data = ioread64(bar0_virt_addr + io_data64.reg);
			ret = copy_to_user((struct io_wr_data64 *)arg, &io_data64, sizeof(struct io_wr_data64));
			break;
		case IO_WR64:
			ret = copy_from_user(&io_data64, (struct io_wr_data64 *)arg, sizeof(struct io_wr_data64));
			iowrite64(io_data64.data, bar0_virt_addr + io_data64.reg);
			break;
		case ADC_IDLE_FRAME_NUM:
			spin_lock(&queues[ADC_QUEUS].lock);
			frameidlenum = queues[ADC_QUEUS].restIdleNum;
			spin_unlock(&queues[ADC_QUEUS].lock);
			ret = copy_to_user((unsigned int *)arg, &frameidlenum, sizeof(unsigned int));
			break;
		case ADC_DATA_RECEIVE:
			ret = copy_from_user(&frameindex, (int *)arg, sizeof(int));
			ret = adc_data_receive(frameindex);
			break;
		default:
			break;
	}
	return ret;
}

// static const struct file_operations pcie_dma_fops = {
// 	.open = dma_memcpy_open,
// 	.release = dma_memcpy_release,
// 	.unlocked_ioctl	= dma_memcpy_ioctl,
// 	.mmap 		= dma_memcpy_mmap,
// };

// static struct miscdevice pcie_dma_dev = {
// 	.minor		= MISC_DYNAMIC_MINOR,
// 	.name		= "pcie_dma",
// 	.fops		= &pcie_dma_fops,
// };

static const struct file_operations pcie_pps_fops = {
	.open = pps_open,
	.release = pps_release,
	.unlocked_ioctl	= pps_ioctl,
	.write      = pcie_dma_write,
};

static struct miscdevice pcie_pps_dev = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "pcie_pps",
	.fops		= &pcie_pps_fops,
};

static const struct file_operations pcie_dac_fops = {
	.open = dac_open,
	.release = dac_release,
	.unlocked_ioctl	= dac_ioctl,
	.mmap 		= dac_mmap,
};

static struct miscdevice pcie_dac_dev = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "pcie_dac",
	.fops		= &pcie_dac_fops,
};

static const struct file_operations pcie_adc_fops = {
	.open = adc_open,
	.release = adc_release,
	.unlocked_ioctl	= adc_ioctl,
	.mmap 		= adc_mmap,
};

static struct miscdevice pcie_dac_dev = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= "pcie_adc",
	.fops		= &pcie_adc_fops,
};

static int pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
    int ret;
    unsigned long bar0_start;
    unsigned long bar0_len;
	// int irq;
	init_multiple_queues();  // 初始化多个队列

    printk(KERN_INFO "Probing PCI device: vendor=0x%x, device=0x%x\n",
           pdev->vendor, pdev->device);

    // 启用 PCI 设备
    ret = pci_enable_device(pdev);
    if (ret) {
        printk(KERN_ERR "Failed to enable PCI device\n");
        return ret;
    }

    // 设置设备为主设备
    pci_set_master(pdev);
    g_pdev = pdev;

    // 读取 BAR0 的起始地址和长度
    bar0_start = pci_resource_start(pdev, 0);
    bar0_len = pci_resource_len(pdev, 0);

    printk(KERN_INFO "BAR0 start address: 0x%lx, length: 0x%lx\n", bar0_start, bar0_len);

    if (!bar0_start || !bar0_len) {
        printk(KERN_ERR "Invalid BAR0 address or length\n");
        ret = -ENODEV;
        goto disable_device;
    }

    // 请求 BAR0 资源
    ret = pci_request_region(pdev, 0, DRIVER_NAME);
    if (ret) {
        printk(KERN_ERR "Failed to request BAR0 region\n");
        goto disable_device;
    }

    // 将 BAR0 映射到内核虚拟地址空间
    bar0_virt_addr = ioremap(bar0_start, bar0_len);
    if (!bar0_virt_addr) {
        printk(KERN_ERR "Failed to map BAR0 to virtual address\n");
        ret = -ENOMEM;
        goto release_region;
    }
	pcie_base_addr = bar0_start;
    printk(KERN_INFO "BAR0 mapped to virtual address: %p\n", bar0_virt_addr);

    /* 1. alloc g_phipheral_dma memory, store dma transport configuration properties */
	// kzalloc是Linux内核中的一个内存分配函数，用于分配零初始化的内存空间
	// g_phipheral_dma = kzalloc(sizeof(struct phipheral_dma_stu), GFP_KERNEL);
	// if(g_phipheral_dma < 0) {
	// 	printk("Can not allocate g_phipheral_dma\n");
	// 	return -ENOMEM;
	// }

	p_phipheral_dma = kzalloc(sizeof(struct phipheral_dma_stu), GFP_KERNEL);
	if(p_phipheral_dma < 0) {
		printk("Can not allocate p_phipheral_dma\n");
		return -ENOMEM;
	}

	d_phipheral_dma = kzalloc(sizeof(struct phipheral_dma_stu), GFP_KERNEL);
	if(d_phipheral_dma < 0) {
		printk("Can not allocate d_phipheral_dma\n");
		return -ENOMEM;
	}

	/* 2. register misc device */
	// ret = misc_register(&pcie_dma_dev);
	// if(ret < 0) {
	// 	printk("Can not register dma device, error code: %d\n", ret);
	// 	return ret;
    // }

	ret = misc_register(&pcie_pps_dev);
	if(ret < 0) {
		printk("Can not register pps device, error code: %d\n", ret);
		return ret;
    }

	ret = misc_register(&pcie_dac_dev);
	if(ret < 0) {
		printk("Can not register dac device, error code: %d\n", ret);
		return ret;
    }

	// 注册中断
    ret = request_irq(53, misc_interrupt_handler, IRQF_SHARED, DRIVER_NAME, &pcie_pps_dev); // 假设中断号为 42
    if (ret) {
        printk(KERN_ERR "Failed to request IRQ\n");
        misc_deregister(&pcie_pps_dev);
        return ret;
    }
	else{
		printk(KERN_INFO "IRQ %d registered successfully\n",110);
	}

    return 0;

release_region:
    pci_release_region(pdev, 0);
disable_device:
    pci_disable_device(pdev);
    return ret;
}

// 3. 设备移除函数
static void pci_remove(struct pci_dev *pdev)
{
	int i;
    printk(KERN_INFO "PCIe Device Removed\n");
	free_irq(53, &pcie_pps_dev);
    /* unregister misc device */
	// misc_deregister(&pcie_dma_dev);
	misc_deregister(&pcie_pps_dev);
	misc_deregister(&pcie_dac_dev);
    /* free dma struct */
	// if(g_phipheral_dma)
	// 	kfree(g_phipheral_dma);

	if(p_phipheral_dma)
		kfree(p_phipheral_dma);
	
	if(d_phipheral_dma)
		kfree(d_phipheral_dma);
	
	for (i = 0; i < MAX_QUEUES; i++) {
        kfree(queues[i].buffer);  // 释放队列的内存
    }

    if(bar0_virt_addr != 0)
    {
        ioport_unmap(bar0_virt_addr);
    }
    pci_release_regions(pdev);
    pci_disable_device(pdev);
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
MODULE_AUTHOR("wym");
MODULE_DESCRIPTION("PCIe DMA Driver");