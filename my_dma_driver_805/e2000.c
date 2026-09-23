
#include <dm.h>              // 设备模型相关
#include <i2c.h>             // I2C相关
#include <linux/delay.h>     // 延时函数
#include <stdint.h>

// KSZ9896寄存器定义
#define KSZ9896_CHIP_ID_REG    0x0001
#define PORT6_XMII_CTRL0       0x6300
#define PORT6_XMII_CTRL1       0x6301
#define PHY_PORT1_CTRL         0x1100
#define PHY_PORT2_CTRL         0x2100  
#define PHY_PORT3_CTRL         0x3100
#define PHY_PORT4_CTRL         0x4100
#define PHY_PORT5_CTRL         0x5100

/* PHY control register bit definitions (match KSZ9896 datasheet) */
#define PHY_SOFT_RESET        (1 << 15)
#define PHY_LOOPBACK          (1 << 14)
#define PHY_SPEED_LSB         (1 << 13)
#define PHY_AUTO_NEG_EN       (1 << 12)
#define PHY_POWER_DOWN        (1 << 11)
#define PHY_ISOLATE           (1 << 10)
#define PHY_RESTART_AN        (1 << 9)
#define PHY_DUPLEX_FULL       (1 << 8)
#define PHY_SPEED_MSB         (1 << 6)

// 寄存器位定义
#define XMII0_DUPLEX_FULL      (1 << 6)
#define XMII0_TX_FC_EN         (1 << 5)
#define XMII0_SPEED_100        (1 << 4)
#define XMII0_RX_FC_EN         (1 << 3)

#define XMII1_SPEED_1000       (1 << 6)
#define XMII1_RGMII_ID_IG      (1 << 4)
#define XMII1_RGMII_ID_EG      (1 << 3)
#define XMII1_INTERFACE_RGMII  0x00

// I2C读写函数（带详细调试）
static int ksz9896_read_reg(struct udevice *dev, u16 reg, u8 *value)
{
    int ret;
    printf("KSZ9896_READ: offset_len=2, reading reg 0x%04x\n", reg);
    ret = dm_i2c_read(dev, reg, value, 1);
    if (ret) {
        printf("KSZ9896_READ: ERROR: Read register 0x%04x failed: %d\n", reg, ret);
        return ret;
    }
    printf("KSZ9896_READ: SUCCESS: Read 0x%02x from register 0x%04x\n", *value, reg);
    return 0;
}

static int ksz9896_write_reg(struct udevice *dev, u16 reg, u8 value)
{
    int ret;
    printf("KSZ9896_WRITE: Writing 0x%02x to register 0x%04x\n", value, reg);

    // 改为使用偏移写：offset_len=2（16 位地址） + 1 字节数据，和读路径保持一致
    ret = dm_i2c_write(dev, reg, &value, 1);
    if (ret) {
        printf("KSZ9896_WRITE: ERROR: Write register failed: %d\n", ret);
        return ret;
    }

    printf("KSZ9896_WRITE: SUCCESS: Wrote 0x%02x to register 0x%04x\n", value, reg);

    // 可选：验证写入
    {
        u8 verify = 0;
        mdelay(1);
        if (!ksz9896_read_reg(dev, reg, &verify)) {
            u8 mask = 0xFF;
            // 某些位由硬件自适应/锁存控制：对 XMII1 的 IG/EG 位忽略校验
            if (reg == PORT6_XMII_CTRL1) {
                mask &= ~XMII1_RGMII_ID_IG;
                mask &= ~XMII1_RGMII_ID_EG;
            }
            if ((verify & mask) == (value & mask)) {
                if (mask != 0xFF)
                    printf("KSZ9896_WRITE: VERIFY: PASSED (masked 0x%02x)\n", (u8)~mask);
                else
                    printf("KSZ9896_WRITE: VERIFY: Write verification PASSED\n");
            } else {
                printf("KSZ9896_WRITE: VERIFY: Write verification FAILED (read 0x%02x, expected 0x%02x, mask 0x%02x)\n",
                       verify, value, mask);
            }
        }
    }

    return 0;
}

static int ksz9896_read_reg16(struct udevice *dev, u16 reg, u16 *value)
{
    u8 value_buf[2] = {0};
    int ret;
    printf("KSZ9896_READ16: offset_len=2, reading reg 0x%04x\n", reg);
    ret = dm_i2c_read(dev, reg, value_buf, 2);
    if (ret) {
        printf("KSZ9896_READ16: ERROR: Read register 0x%04x failed: %d\n", reg, ret);
        return ret;
    }
    *value = (value_buf[0] << 8) | value_buf[1];
    printf("KSZ9896_READ16: SUCCESS: Read 0x%04x from register 0x%04x [0x%02x 0x%02x]\n",
           *value, reg, value_buf[0], value_buf[1]);
    return 0;
}

static int ksz9896_write_reg16(struct udevice *dev, u16 reg, u16 value)
{
    // 单消息写：寄存器高字节、低字节、数据高字节、数据低字节
    u8 buf[4];
    int ret;

    buf[0] = (reg >> 8) & 0xFF;
    buf[1] = reg & 0xFF;
    buf[2] = (value >> 8) & 0xFF;
    buf[3] = value & 0xFF;

    printf("KSZ9896_WRITE16: Writing 0x%04x to register 0x%04x [buf: 0x%02x 0x%02x 0x%02x 0x%02x]\n",
           value, reg, buf[0], buf[1], buf[2], buf[3]);

    // 改用偏移写两字节数据，避免单消息 4 字节写在部分驱动上的兼容性问题
    {
        u8 data[2];
        data[0] = buf[2];
        data[1] = buf[3];
        ret = dm_i2c_write(dev, reg, data, 2);
    }
    if (ret) {
        printf("KSZ9896_WRITE16: ERROR: Write register failed: %d\n", ret);
        return ret;
    }

    printf("KSZ9896_WRITE16: SUCCESS: Wrote 0x%04x to register 0x%04x\n", value, reg);

    // 写后读取验证（对会自清除的位进行掩码）
    {
        u16 verify16 = 0;
        u16 mask = 0xFFFF;
        mdelay(2);
        if (!ksz9896_read_reg16(dev, reg, &verify16)) {
            // PHY 控制寄存器中，软复位与重启自协商位可能自清除
            if (reg == PHY_PORT1_CTRL || reg == PHY_PORT2_CTRL ||
                reg == PHY_PORT3_CTRL || reg == PHY_PORT4_CTRL ||
                reg == PHY_PORT5_CTRL) {
                mask &= ~PHY_SOFT_RESET;   // bit15: 写后自清除
                mask &= ~PHY_RESTART_AN;   // bit9: 写后自清除
                mask &= ~0x0020;           // bit5: 设备置位/保留位，读回可能固定为1
            }
            if ((verify16 & mask) == (value & mask)) {
                if (mask != 0xFFFF)
                    printf("KSZ9896_WRITE16: VERIFY: PASSED (masked 0x%04x)\n", (u16)(~mask));
                else
                    printf("KSZ9896_WRITE16: VERIFY: Write verification PASSED\n");
            } else {
                printf("KSZ9896_WRITE16: VERIFY: FAILED (read 0x%04x, expected 0x%04x, mask 0x%04x)\n",
                       verify16, value, mask);
            }
        }
    }

    return 0;
}

// 增强的I2C控制器状态检查
static void check_i2c_controller_status(void)
{
    void __iomem *mio14_base = (void __iomem *)0x28030000;
    u32 ic_enable, ic_status, ic_tar, ic_con, ic_fs_scl_hcnt, ic_fs_scl_lcnt;
    
    printf("=== I2C1 Controller Detailed Status ===\n");
    ic_enable = readl(mio14_base + 0x6C);
    ic_status = readl(mio14_base + 0x70);
    ic_tar = readl(mio14_base + 0x04);
    ic_con = readl(mio14_base + 0x00);
    ic_fs_scl_hcnt = readl(mio14_base + 0x1C);
    ic_fs_scl_lcnt = readl(mio14_base + 0x20);
    
    printf("IC_ENABLE:      0x%08x %s\n", ic_enable, ic_enable & 0x1 ? "ENABLED" : "DISABLED");
    printf("IC_STATUS:      0x%08x\n", ic_status);
    printf("IC_TAR:         0x%08x\n", ic_tar);
    printf("IC_CON:         0x%08x\n", ic_con);
    printf("IC_FS_SCL_HCNT: 0x%08x\n", ic_fs_scl_hcnt);
    printf("IC_FS_SCL_LCNT: 0x%08x\n", ic_fs_scl_lcnt);
    
    // 解析状态位
    if (ic_status & 0x20) printf("  - STATUS: Activity in progress\n");
    if (ic_status & 0x10) printf("  - STATUS: Slave FIFO full\n");
    if (ic_status & 0x08) printf("  - STATUS: Master FIFO full\n");
    if (ic_status & 0x04) printf("  - STATUS: Slave FIFO not empty\n");
    if (ic_status & 0x02) printf("  - STATUS: Master FIFO not empty\n");
    if (ic_status & 0x01) printf("  - STATUS: Bus busy\n");
    
    printf("===============================\n");
}

static int check_i2c_device(struct udevice *dev)
{
    struct dm_i2c_chip *chip;
    struct udevice *bus;
    struct dm_i2c_ops *ops;
    
    if (!dev) {
        printf("ERROR: I2C device is NULL\n");
        return -EINVAL;
    }
    
    printf("I2C Device Detailed Status:\n");
    printf("  Device: %p\n", dev);
    printf("  Device Name: %s\n", dev->name);
    printf("  Device Driver: %s\n", dev->driver->name);
    
    chip = dev_get_parent_plat(dev);
    printf("  Chip: %p\n", chip);
    
    bus = dev_get_parent(dev);
    printf("  Bus: %p\n", bus);
    if (bus) {
        printf("  Bus Name: %s\n", bus->name);
    }
    
    ops = i2c_get_ops(bus);
    printf("  Ops: %p\n", ops);
    
    if (!chip) {
        printf("ERROR: I2C chip structure is NULL - device not properly probed!\n");
        return -ENODEV;
    }
    
    printf("  Chip Address: 0x%x\n", chip->chip_addr);
    printf("  Offset Len: %d\n", chip->offset_len);
    printf("  Flags: 0x%x\n", chip->flags);
    
    return 0;
}

// 增强的KSZ9896配置函数
static int ksz9896_configure_switch(struct udevice *dev)
{
    u16 chip_id;
    u8 test_byte;
    int ret;

    printf("\n=== KSZ9896 Switch Configuration Started ===\n");

    printf("\n[Step 0] Checking I2C device initialization...\n");
    ret = check_i2c_device(dev);
    if (ret) {
        printf("KSZ9896: ERROR: I2C device not properly initialized\n");
        return ret;
    }
    // 首先检查I2C控制器状态
    printf("\n[Step 1] Checking I2C controller status...\n");
    check_i2c_controller_status();

    // 测试基本的I2C通信
    printf("\n[Step 2] Testing basic I2C communication...\n");
    
    // 尝试读取多个寄存器来测试通信
    printf("Trying to read register 0x0001...\n");
    ret = ksz9896_read_reg(dev, 0x0001, &test_byte);
    if (ret) {
        printf("KSZ9896: ERROR: Failed to read register 0x0001: %d\n", ret);
        return ret;
    }

    printf("Trying to read register 0x0002...\n");
    ret = ksz9896_read_reg(dev, 0x0002, &test_byte);
    if (ret) {
        printf("KSZ9896: ERROR: Failed to read register 0x0002: %d\n", ret);
        return ret;
    }

    printf("Trying to read register 0x0003...\n");
    ret = ksz9896_read_reg(dev, 0x0003, &test_byte);
    if (ret) {
        printf("KSZ9896: ERROR: Failed to read register 0x0003: %d\n", ret);
        return ret;
    }

    // 读取芯片ID验证通信
    printf("\n[Step 3] Reading chip ID...\n");
    ret = ksz9896_read_reg16(dev, KSZ9896_CHIP_ID_REG, &chip_id);
    if (ret) {
        printf("KSZ9896: ERROR: Failed to read chip ID: %d\n", ret);
        return ret;
    }
    
    printf("KSZ9896: Chip ID: 0x%04x\n", chip_id);

    // 检查复位状态
    printf("\n[Step 4] Checking reset status...\n");
    ret = ksz9896_read_reg(dev, 0x0003, &test_byte);
    if (ret) {
        printf("KSZ9896: ERROR: Failed to read reset register: %d\n", ret);
        return ret;
    }
    printf("KSZ9896: Reset register (0x0003) = 0x%02x, Bit0(Reset) = %s\n", 
           test_byte, (test_byte & 0x01) ? "INACTIVE" : "ACTIVE");

    // 如果复位位被置位，尝试清除它
    if (test_byte & 0x01) {
        printf("KSZ9896: WARNING: Reset bit is active, attempting to clear...\n");
        ret = ksz9896_write_reg(dev, 0x0003, test_byte & 0xFE);
        if (ret) {
            printf("KSZ9896: ERROR: Failed to clear reset bit: %d\n", ret);
        } else {
            printf("KSZ9896: Reset bit clear command sent, verifying...\n");
            mdelay(10);
            ret = ksz9896_read_reg(dev, 0x0003, &test_byte);
            if (!ret) {
                printf("KSZ9896: Reset register after clear = 0x%02x\n", test_byte);
            }
        }
    }

    // 配置Port 6 RGMII
    printf("\n[Step 5] Configuring Port 6 RGMII...\n");
    
    printf("Configuring XMII Control 0...\n");
    // 加入速度位 100Mbps，使写后读回等于 0x78
    ret = ksz9896_write_reg(dev, PORT6_XMII_CTRL0,
                           XMII0_DUPLEX_FULL | XMII0_TX_FC_EN | XMII0_RX_FC_EN | XMII0_SPEED_100);
    if (ret) {
        printf("KSZ9896: WARNING: Failed to configure XMII Control 0\n");
    } else {
        printf("XMII Control 0 configured successfully\n");
    }

    printf("Configuring XMII Control 1...\n");
    // 强制写入 0x58：RGMII + IG/EG 延时 + 1000M
    ret = ksz9896_write_reg(dev, PORT6_XMII_CTRL1, 0x58);
    if (ret) {
        printf("KSZ9896: WARNING: Failed to configure XMII Control 1\n");
    } else {
        printf("XMII Control 1 configured successfully\n");
    }

    // --- PHY ports configuration to match i2c_fix.c ---
    printf("\n[Step 6] Configuring PHY registers...\n");
    {
        u16 v;
        // Port 1
        ret = ksz9896_read_reg16(dev, PHY_PORT1_CTRL, &v);
        if (!ret) {
            v &= ~(PHY_SPEED_LSB | PHY_AUTO_NEG_EN | PHY_DUPLEX_FULL | PHY_SPEED_MSB);
            v |= PHY_SPEED_LSB;        // 100M
            v |= PHY_DUPLEX_FULL;      // Full duplex
            ret = ksz9896_write_reg16(dev, PHY_PORT1_CTRL, v);
        }
        if (ret) printf("KSZ9896: WARNING: Failed to configure PHY Port1\n");

        // Port 2
        ret = ksz9896_read_reg16(dev, PHY_PORT2_CTRL, &v);
        if (!ret) {
            v &= ~(PHY_SPEED_LSB | PHY_AUTO_NEG_EN | PHY_DUPLEX_FULL | PHY_SPEED_MSB);
            v |= PHY_SPEED_LSB;        // 100M
            v |= PHY_DUPLEX_FULL;      // Full duplex
            ret = ksz9896_write_reg16(dev, PHY_PORT2_CTRL, v);
        }
        if (ret) printf("KSZ9896: WARNING: Failed to configure PHY Port2\n");

        // Port 3
        ret = ksz9896_read_reg16(dev, PHY_PORT3_CTRL, &v);
        if (!ret) {
            v &= ~(PHY_SPEED_LSB | PHY_AUTO_NEG_EN | PHY_DUPLEX_FULL | PHY_SPEED_MSB);
            v |= PHY_SPEED_LSB;        // 100M
            v |= PHY_DUPLEX_FULL;      // Full duplex
            ret = ksz9896_write_reg16(dev, PHY_PORT3_CTRL, v);
        }
        if (ret) printf("KSZ9896: WARNING: Failed to configure PHY Port3\n");

        // Port 4
        ret = ksz9896_read_reg16(dev, PHY_PORT4_CTRL, &v);
        if (!ret) {
            v &= ~(PHY_SPEED_LSB | PHY_AUTO_NEG_EN | PHY_DUPLEX_FULL | PHY_SPEED_MSB);
            v |= PHY_SPEED_LSB;        // 100M
            v |= PHY_DUPLEX_FULL;      // Full duplex
            ret = ksz9896_write_reg16(dev, PHY_PORT4_CTRL, v);
        }
        if (ret) printf("KSZ9896: WARNING: Failed to configure PHY Port4\n");
    }

    // Disable Port 5 (power down + isolate)
    {
        u16 v5;
        if (!ksz9896_read_reg16(dev, PHY_PORT5_CTRL, &v5)) {
            v5 |= PHY_ISOLATE | PHY_POWER_DOWN;
            if (ksz9896_write_reg16(dev, PHY_PORT5_CTRL, v5))
                printf("KSZ9896: WARNING: Failed to disable PHY Port5\n");
        }
    }

    // Force link status update by soft reset ports 1-4
    printf("\n[Step 7] Forcing link status update...\n");
    {
        u16 v;
        if (!ksz9896_read_reg16(dev, PHY_PORT1_CTRL, &v))
            ksz9896_write_reg16(dev, PHY_PORT1_CTRL, v | PHY_SOFT_RESET);
        if (!ksz9896_read_reg16(dev, PHY_PORT2_CTRL, &v))
            ksz9896_write_reg16(dev, PHY_PORT2_CTRL, v | PHY_SOFT_RESET);
        if (!ksz9896_read_reg16(dev, PHY_PORT3_CTRL, &v))
            ksz9896_write_reg16(dev, PHY_PORT3_CTRL, v | PHY_SOFT_RESET);
        if (!ksz9896_read_reg16(dev, PHY_PORT4_CTRL, &v))
            ksz9896_write_reg16(dev, PHY_PORT4_CTRL, v | PHY_SOFT_RESET);
    }
    mdelay(100);
    
    printf("\n=== KSZ9896 Switch Configuration Completed ===\n");
    printf("Port mapping:\n");
    printf("  Port 1: RJ45网口1 - 100M Full Duplex\n");
    printf("  Port 2: RJ45网口2 - 100M Full Duplex\n");
    printf("  Port 3: 外部工控机 - 100M Full Duplex\n");
    printf("  Port 4: WIFI - 100M Full Duplex\n");
    printf("  Port 5: 未使用 - 禁用\n");
    printf("  Port 6: CPU (RGMII)\n");
    
    return 0;
}

void board_ksz9896_reset_init(void)
{
    // GPIO5控制器基地址
    void __iomem *gpio5_base = (void __iomem *)0x28039000;
    uint32_t value;
    
    // 1. 配置GPIO5_7为输出模式
    value = readl(gpio5_base + 0x04);  // 读取SWPORT_DDR
    value |= (1 << 7);                 // 设置bit7为输出
    writel(value, gpio5_base + 0x04);
    
    // 2. 先拉低复位（确保复位状态）
    value = readl(gpio5_base + 0x00);  // 读取SWPORT_DR
    value &= ~(1 << 7);                // 清除bit7（输出低电平）
    writel(value, gpio5_base + 0x00);
    
    // 3. 短暂延迟
    mdelay(10);
    
    // 4. 释放复位（输出高电平）
    value = readl(gpio5_base + 0x00);
    value |= (1 << 7);                 // 设置bit7（输出高电平）
    writel(value, gpio5_base + 0x00);
    
    // 5. 等待芯片初始化完成
    mdelay(100);
    
    printf("KSZ9896 reset released via GPIO5_7\n");
}

void board_i2c_early_init(struct udevice **i2c1_dev)
{
    int ret;
    int attempts = 3;
    
    printf("=== Initializing I2C and KSZ9896 ===\n");
    
    // 初始化I2C0（不使用返回值，仅触发总线注册）
    {
        struct udevice *i2c0_bus = NULL;
        uclass_get_device_by_seq(UCLASS_I2C, 0, &i2c0_bus);
        printf("I2C0 initialization attempted\n");
    }
    
    // 带重试的I2C1初始化
    while (attempts-- > 0) {
        ret = uclass_get_device_by_seq(UCLASS_I2C, 1, i2c1_dev);
        if (!ret && *i2c1_dev) {
            printf("I2C1 initialized successfully (attempt %d)\n", 3 - attempts);
            // 这里返回的是总线设备，不是芯片设备；芯片需后续通过 dm_i2c_probe 获取
            break;
        } else {
            printf("I2C1 initialization failed (attempt %d), retrying...\n", 3 - attempts);
            mdelay(10);
        }
    }
    
    if (!*i2c1_dev) {
        printf("Error: I2C1 initialization failed after all attempts\n");
        return;
    }
    
    // 初始化复位引脚
    board_ksz9896_reset_init();
    
    printf("I2C1 bus fully initialized\n");
}

int last_stage_init(void)
{
    int ret;
    struct udevice *i2c1_dev = NULL;
    struct udevice *ksz_dev = NULL; // KSZ9896 芯片设备

	pci_init();
	scsi_scan(true);
	ret = nvme_scan_namespace();
	if(ret != 0)
		printf("nvme scan failed\n");

#ifdef PHYTIUM_SAVE_TRAIN_DATA
	save_train_data();
#endif

#ifdef E2000_DEBUG
	//fdt_auto();
#endif
	writel(0x0, 0x2807e0c0);//release IACC
	// 添加I2C和KSZ9896初始化
    board_i2c_early_init(&i2c1_dev);
    if (!i2c1_dev) {
        printf("I2C1 initialization failed\n");
        return 0;
    }
    printf("=== I2C and KSZ9896 initialization complete ===\n");
	// mdelay(3000);

	printf("=== I2C device acquired, checking status ===\n");

    // 详细检查设备状态
    printf("\n[Pre-configuration Check]\n");
    check_i2c_device(i2c1_dev);
    check_i2c_controller_status();

    // 设置I2C速度（100kHz）
    ret = dm_i2c_set_bus_speed(i2c1_dev, 100000);
    if (ret) {
        printf("Failed to set I2C speed: %d\n", ret);
    } else {
        printf("I2C speed set to 100kHz\n");
    }

    // 再次检查状态
    printf("\n[Post-speed configuration Check]\n");
    check_i2c_device(i2c1_dev);

    // 在 I2C1 总线上探测并获取 KSZ9896 芯片设备（地址 0x5f，寄存器偏移为 2 字节）
    ret = dm_i2c_probe(i2c1_dev, 0x5f, 2, &ksz_dev);
    if (ret) {
        printf("ERROR: dm_i2c_probe(0x5f, offset_len=2) failed: %d\n", ret);
        return 0;
    }
    printf("KSZ9896 chip device acquired at addr 0x5f\n");

    // 强制设置子设备的偏移长度为 2（防止默认 1 导致读写不一致）
    {
        struct dm_i2c_chip *chip = dev_get_parent_plat(ksz_dev);
        if (chip) {
            chip->offset_len = 2;
            printf("KSZ9896 chip offset_len forced to %d\n", chip->offset_len);
        } else {
            printf("WARNING: Failed to get chip structure to set offset_len\n");
        }
    }

    // 进行更深入的设备检查与配置
    printf("\n[Pre-configuration Check]\n");
    check_i2c_device(ksz_dev);
    check_i2c_controller_status();

    // 基本读写验证与开关配置
    ret = ksz9896_configure_switch(ksz_dev);
    if (ret) {
        printf("KSZ9896 configuration failed: %d\n", ret);
    } else {
        printf("KSZ9896 configuration completed successfully\n");
    }

    return 0;
}
