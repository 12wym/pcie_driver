#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_mdio.h>
#include <linux/phy.h>
#include <linux/gpio.h>
#include <linux/timer.h>
#include <linux/jiffies.h>

#define GPIO_PIN 92  // GPIO2_D4 2*32 + 3*8 + 4
static struct timer_list gpio_timer;
static int gpio_state = 0;

static struct mii_bus *mdio_bus = NULL;
static int phy_addr_6 = 0x016; // port 6 -> CPU1
static int phy_addr_5 = 0x015; // port 5 -> CPU2

// 获取 GMAC 设备树中的 MDIO 总线
static struct mii_bus *get_mdio_from_gmac(void)
{
    struct device_node *mdio_node;
    struct mii_bus *bus;

    // 查找 MDIO 子节点（路径：/ethernet@fe2a0000/mdio）
    mdio_node = of_find_node_by_path("/ethernet@fe2a0000/mdio");
    if (!mdio_node) {
        pr_err("MDIO node not found under GMAC\n");
        return NULL;
    }

    // 获取 MDIO 总线
    bus = of_mdio_find_bus(mdio_node);
    of_node_put(mdio_node); // 释放 MDIO 节点

    if (!bus) {
        pr_err("Failed to get MDIO bus\n");
        return NULL;
    }

    pr_info("MDIO bus acquired successfully\n");
    return bus;
}

// 读取 PHY 寄存器
static void read_phy_register(void)
{
    int ret;
    int i;
    u16 value;

    if (!mdio_bus) {
        pr_err("MDIO bus is NULL\n");
        return;
    }
    pr_info("PORT 6");
    for(i = 0; i < 32; i++)
    {
        ret = mdiobus_read(mdio_bus, phy_addr_6, i);
        if (ret < 0) {
            pr_err("Failed to read PHY[%d] REG[0x%02x]\n", phy_addr_6, i);
        } else {
            value = ret;
            pr_info("Read PHY[%d] REG[0x%02x] = 0x%04x\n", phy_addr_6, i, value);
        }
    }
    pr_info("PORT 5");
    for(i = 0; i < 32; i++)
    {
        ret = mdiobus_read(mdio_bus, phy_addr_5, i);
        if (ret < 0) {
            pr_err("Failed to read PHY[%d] REG[0x%02x]\n", phy_addr_5, i);
        } else {
            value = ret;
            pr_info("Read PHY[%d] REG[0x%02x] = 0x%04x\n", phy_addr_5, i, value);
        }
    }
}

// 写入 PHY 寄存器
static void write_phy_register(int phy_addr, int reg, unsigned short value)
{
    int ret;

    if (!mdio_bus) {
        pr_err("MDIO bus is NULL\n");
        return;
    }

    
    ret = mdiobus_write(mdio_bus, phy_addr, reg, value);
    if (ret < 0) {
        pr_err("Failed to write PHY[%d] REG[0x%02x]\n", phy_addr, reg);
    } else {
        pr_info("Wrote 0x%0x4 to PHY[%d] REG[0x%02x]\n", value, phy_addr, reg);
    }
}

static void gpio_timer_callback(struct timer_list *t)
{
    gpio_state = !gpio_state;
    gpio_set_value(GPIO_PIN, gpio_state);
    mod_timer(&gpio_timer, jiffies + msecs_to_jiffies(500));
}

// 驱动初始化
static int __init mdio_driver_init(void)
{
    int ret;

    pr_info("MDIO Driver Loading...\n");

    mdio_bus = get_mdio_from_gmac();
    if (!mdio_bus) {
        pr_err("Failed to get MDIO bus\n");
        return -ENODEV;
    }
    // 初始化GPIO
    ret = gpio_request(GPIO_PIN, "GPIO2_D4");
    if (ret) {
        pr_err("Failed to request GPIO %d\n", GPIO_PIN);
        return ret;
    }
    else
    {
        printk("succeed to set GPIO\n");
    }

    gpio_direction_output(GPIO_PIN, 1);

    // 初始化定时器
    timer_setup(&gpio_timer, gpio_timer_callback, 0);
    mod_timer(&gpio_timer, jiffies + msecs_to_jiffies(500));//HZ -> 1s

    // 执行读写操作
    read_phy_register();
    // set port6 to 100M
    write_phy_register(phy_addr_6, 0x01, 0x0001);
    // set port5 to 100M
    write_phy_register(phy_addr_5, 0x01, 0x0001);
    read_phy_register();
    return 0;
}

// 驱动卸载
static void __exit mdio_driver_exit(void)
{
    del_timer(&gpio_timer);
    gpio_set_value(GPIO_PIN, 0);
    gpio_free(GPIO_PIN);
    pr_info("MDIO Driver Unloading...\n");
}

module_init(mdio_driver_init);
module_exit(mdio_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("guo liang");
MODULE_DESCRIPTION("marvell mdio set and GPIO timer");
