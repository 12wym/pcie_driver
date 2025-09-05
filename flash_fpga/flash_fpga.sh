#!/bin/bash
#
# Flash FPGA program to spi flash
#
# Copyright 2020 Tronlong Elec. Tech. Co. Ltd. All Rights Reserved.

# --- 这里新增 msleep 定义 ---
function msleep() {
    local ms=$1
    sleep $(awk "BEGIN {print $ms/1000}")
}
# --------------------------

#################################################
## GPIO1_B3_d          --> FPGA_DONE         43       ##
## GPIO1_A5_d          --> OE pin            37       ##
## GPIO1_A6_d          --> FPGA_PROG         38       ##
#################################################

################ GPIO index #####################
## gpio index = bank * 32 + gpio bit     ##
## example: GPIO1_B3 index = 1 * 32 + (8 + 3)  ##
fpga_done_pin=43
spi_oe_pin=37
fpga_prog=38
#################################################

# Help message.
function usage() {
    echo "
    Usage: $0 <binfile>

    options:
        binfile:    bin file to write
    e.g.
        $0 /root/led_flash_pgl25g.sfc
    "
}

# Set program pin level state
function set_fpga_program_pin(){

    if [ ! -d "/sys/class/gpio/gpio$fpga_prog" ];then
        echo $fpga_prog > /sys/class/gpio/export
    fi    
    # programm pin output low
    if [ $1 -eq 0 ]; then
        echo out > /sys/class/gpio/gpio$fpga_prog/direction
        echo 0 > /sys/class/gpio/gpio$fpga_prog/value
        msleep 10
    # programm pin output high
    elif [ $1 -eq 1 ];then
        echo out > /sys/class/gpio/gpio$fpga_prog/direction
        echo 1 > /sys/class/gpio/gpio$fpga_prog/value
        msleep 10
    fi
}

# Set OE pin level state
function set_spi_oe_pin() {
    if [ ! -d "/sys/class/gpio/gpio$spi_oe_pin" ];then
        echo $spi_oe_pin > /sys/class/gpio/export
    fi

    echo out > /sys/class/gpio/gpio$spi_oe_pin/direction
    echo $1 > /sys/class/gpio/gpio$spi_oe_pin/value
}

# Mount FPGA spi flash
function mount_spi_fpga_flash() {
    lsmod | grep m25p80 >> /dev/null
    if [ $? -eq 0 ]; then
        rmmod m25p80
    fi

    # Connect arm spi interface to spi fpga flash
    set_spi_oe_pin 1

    # Register the spi flash
    insmod ./m25p80.ko -f

    ls /dev
}

# Umount FPGA spi flash
function umount_spi_fpga_flash() {
    rmmod m25p80
    ls /dev

    # Disconnect arm spi interface to spi fpga flash
    set_spi_oe_pin 0
    msleep 10
}

# Flash FPGA program to spi flash
function flash_spiflash() {
   flashcp -v $1 /dev/mtd0
}

# Get the done pin status
function get_done_pin_state() {
    local state

    if [ ! -d "/sys/class/gpio/gpio$fpga_done_pin" ];then
        echo $fpga_done_pin > /sys/class/gpio/export
    fi

    echo in > /sys/class/gpio/gpio$fpga_done_pin/direction

    state=$(cat /sys/class/gpio/gpio$fpga_done_pin/value)

    echo $state
}

######### MAIN ENTRANCE #########
function main() {
    local state

    case $1 in
        -h)
            usage
            exit 0
            ;;
        -v)
            echo "Version: flash_fpga_${MAJOR_VERSION/\./}_${MINOR_VERSION/\./}"
            exit 0
            ;;
    esac

    if [ $# != 1 ]; then
        usage
        exit 1
    fi

    # 1. Reset to configuration logic
    echo -e "\n1/6:"
    echo "Reset to configuration logic"
    set_fpga_program_pin 0

    # 2. Mount FPGA spiflash
    echo -e "\n2/6:"
    echo "Mount FPGA spi flash"
    mount_spi_fpga_flash

    # 3. Flash the FPGA app to spi flash
    echo -e "\n3/6:"
    echo "Flash FPGA program to spi flash"
    flash_spiflash $1
    if [[ $? -eq 1 ]]; then
        echo "Flash FPGA program to spi flash failed."
        umount_spi_fpga_flash
        exit 1
    fi

    # 4. UMount FPGA spiflash
    echo -e "\n4/6:"
    echo "UMount FPGA spi flash"
    umount_spi_fpga_flash

    # 5. Reload FPGA program
    echo -e "\n5/6:"
    echo "FPGA reload app"
    set_fpga_program_pin 0
    msleep 100
    set_fpga_program_pin 1

    # 6. Wait fpga config done
    echo -e "\n6/6:"
    echo "Wait fpga config done"
    loop_count=0
    while [ $loop_count -le 32 ];
    do
        # get the fpga done pin state
        state=$(get_done_pin_state)
        if [ $state -eq 1 ]; then
            echo "FPGA reload app succeeded."
            exit 0
        fi

        msleep 500
        loop_count=$((loop_count + 1))
    done

    echo "FPGA reload app failed."
}

main $@
