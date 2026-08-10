#!/usr/bin/env bash

# 基本系统安装器
basic_software_installer(){
    local choice=""
    local micro_code=""
    local kernel=""
    local kernel_headers=""
    local onboard_audio=""
    local PS3="Enter a number: "

    # 平台列表
    local -a platform_list=(
        "Intel"
        "AMD"
        "Virtual machine")

    # 内核列表
    local -a kernel_list=(
        "linux             (Latest kernel)"
        "linux-lts         (Long-Term Support (LTS) kernel)"
        "linux-zen         (High-performance kernel optimized for desktop users)"
        "linux-hardened    (A security-focused kernel)")

    # 基本软件包列表
    local -a package_list=(
        "base"
        "base-devel"
        "linux-firmware"
        "dosfstools"
        "xfsprogs"
        "f2fs-tools"
        "exfatprogs"
        "btrfs-progs"
        "ntfsprogs"
        "nano"
        "vi"
        "man-db"
        "man-pages"
        "texinfo")

    # 选择CPU平台
    select choice in "${platform_list[@]}"; do
        case "$REPLY" in
            1)
                micro_code="intel-ucode"
                MICRO_CODE="intel-ucode.img"
                break
                ;;
            2)
                micro_code="amd-ucode"
                MICRO_CODE="amd-ucode.img"
                break
                ;;
            3)
                break
                ;;
            *)
                echo "Invalid selection, please choose a number from the list." >&2
                ;;
        esac
    done

    # 选择内核
    select choice in "${kernel_list[@]}"; do
        case "$REPLY" in
            1)
                kernel="linux"
                kernel_headers="linux-headers"
                KERNEL="vmlinuz-linux"
                INITRAMFS="initramfs-linux.img"
                break
                ;;
            2)
                kernel="linux-lts"
                kernel_headers="linux-lts-headers"
                KERNEL="vmlinuz-linux-lts"
                INITRAMFS="initramfs-linux-lts.img"
                break
                ;;
            3)
                kernel="linux-zen"
                kernel_headers="linux-zen-headers"
                KERNEL="vmlinuz-linux-zen"
                INITRAMFS="initramfs-linux-zen.img"
                break
                ;;
            4)
                kernel="linux-hardened"
                kernel_headers="linux-hardened-headers"
                KERNEL="vmlinuz-linux-hardened"
                INITRAMFS="initramfs-linux-hardened.img"
                break
                ;;
            *)
                echo "Invalid selection, please choose a number from the list." >&2
                ;;
        esac
    done

    # 是否是笔记本
    if confirm "Is this device a laptop?"; then
        onboard_audio="sof-firmware"
        TLP="yes"
    fi

    # 安装
    if pacman -Syy &> /dev/null; then
        pacstrap -K /mnt ${package_list[@]} $kernel $kernel_headers $micro_code $onboard_audio
    else
        echo "Mirror error; unable to install software." >&2
        return 1
    fi
}
