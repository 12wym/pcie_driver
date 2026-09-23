int ft_i2c_probe(struct udevice *bus)
{
    struct ft_i2c *priv = dev_get_priv(bus);

    // 强制打印，不受日志级别限制
    // printf("FT_I2C_DEBUG: Probing I2C bus %s\n", bus->name);
    
#if !CONFIG_IS_ENABLED(DM_RTC)
    uint comp_type;

    dev_info(bus, "ft_i2c_probe: entering\n");
    comp_type = readl(&priv->regs->comp_type);
    // if (comp_type != FT_I2C_COMP_TYPE) {
    //     // 使用 printf 确保错误信息显示
    //     printf("FT_I2C_ERROR: I2C bus %s has unknown type %#x\n", 
    //            bus->name, comp_type);
    //     return -ENXIO;
    // }
	// 临时绕过COMP_TYPE检查
    if (comp_type == 0x0) {
        printf("FT_I2C_WARN: I2C bus %s comp_type is 0, but continuing...\n", bus->name);
        // 不返回错误，继续初始化
    } else if (comp_type != FT_I2C_COMP_TYPE) {
        printf("FT_I2C_WARN: I2C bus %s has unknown type %#x (expected %#x), continuing...\n", 
               bus->name, comp_type, FT_I2C_COMP_TYPE);
        // 不返回错误，继续初始化
    } else {
        printf("FT_I2C_DEBUG: I2C bus %s version %#x\n", 
               bus->name, readl(&priv->regs->comp_version));
    }

    printf("FT_I2C_DEBUG: I2C bus %s version %#x\n", 
           bus->name, readl(&priv->regs->comp_version));
#endif
    
    printf("FT_I2C_DEBUG: init controller regs=%p\n", priv->regs);
    return __ft_i2c_init(priv->regs, 0, 0);
}

static int __ft_i2c_init(struct i2c_regs *i2c_base, int speed, int slaveaddr)
{
	int ret;
	uint32_t reg_val;

	// printf("FT_I2C_DEBUG: Starting I2C init, IC_ENABLE=%#x\n", 
    //        readl(&i2c_base->ic_enable));

	/* Disable i2c */
	ret = ft_i2c_enable(i2c_base, false);
	if (ret)
		return ret;

	writel(IC_CON_SD | IC_CON_RE | IC_CON_SPD_FS | IC_CON_MM,
	       &i2c_base->ic_con);
	writel(IC_RX_TL, &i2c_base->ic_rx_tl);
	writel(IC_TX_TL, &i2c_base->ic_tx_tl);
	writel(IC_STOP_DET, &i2c_base->ic_intr_mask);
#if !CONFIG_IS_ENABLED(DM_I2C)
	_ft_i2c_set_bus_speed(NULL, i2c_base, speed, IC_CLK);
	writel(slaveaddr, &i2c_base->ic_sar);
#endif

	/* Enable i2c */
	ret = ft_i2c_enable(i2c_base, true);
	if (ret)
		return ret;
	printf("FT_I2C_DEBUG: After enable, IC_ENABLE=%#x\n", 
           readl(&i2c_base->ic_enable));
	return 0;
}