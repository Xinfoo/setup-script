#!/usr/bin/env bash

# 基本设置器
basic_setter() {
    local region
    local hostname

    # 设置时区
    echo "Setting timezone..." >&2
    while true; do
        read -p '(Enter region/city e.g. "Asia/Shanghai"): ' region

        if ln -sf "/usr/share/zoneinfo/$region" "/etc/localtime" &> /dev/null; then
            echo "Configuring hardware clock..." >&2
            break
        else
            echo "Timezone error: Specified timezone not found.Please re-enter." >&2
        fi
    done

    # 写入时间
    hwclock --systohc

    # 生成locale
    echo "Generating locale..." >&2
    sed -i 's/#en_US.UTF-8 UTF-8/en_US.UTF-8 UTF-8/g' "/etc/locale.gen"
    locale-gen

    # 设置locale
    echo 'LANG=en_US.UTF-8' > "/etc/locale.conf"

    # 设置主机名
    echo "Setting hostname..." >&2
    read -p '(Enter hostname, usually uppercase with hyphens): ' hostname
    echo "$hostname" > "/etc/hostname"

    # 设置hosts
    echo "Configuring hosts file..." >&2
    echo "127.0.0.1        $hostname.localdomain $hostname" >> "/etc/hosts"

    # 设置键盘
    echo "Configuring key board..." >&2
    echo "KEYMAP=us" > "/etc/vconsole.conf"

    # 设置ROOT密码
    echo "Set root password..." >&2
    while true; do
        if passwd root; then
            break
        else
            echo "Please re-enter your root password..." >&2
        fi
    done
    
    # 配置包管理器
    echo "Configuring package manager..." >&2
    sed -i 's/#Color/Color/g' "/etc/pacman.conf"

    # 更新软件包列表
    echo "Updating package lists..." >&2
    if pacman -Syy &> /dev/null; then
        echo "Package lists updated successfully." >&2
    else
        echo "Failed to update the package list;" >&2
        echo "Please check /etc/pacman.d/mirrorlist or your network connection." >&2
        return 1
    fi
}
