#!/usr/bin/env bash

# 基本系统安装器
basic_software_installer(){
    local choice=""
    local micro_code=""
    local kernel=""
    local kernel_headers=""
    local onboard_audio=""

    # 平台列表
    local -a platform_list=(
        "Intel"
        "AMD"
        "Virtual machine"
        )

    # 内核列表
    local -a kernel_list=(
        "linux             (Latest kernel)"
        "linux-lts         (Long-Term Support (LTS) kernel)"
        "linux-zen         (High-performance kernel optimized for desktop users)"
        "linux-hardened    (A security-focused kernel)"
    )

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
        "texinfo"
        )

    # 选择CPU平台
    select choice in "${platform_list[@]}"; do
        case "$REPLY" in
            1)
                micro_code="intel-ucode"
                break
                ;;
            2)
                micro_code="amd-ucode"
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
                break
                ;;
            2)
                kernel="linux-lts"
                kernel_headers="linux-lts-headers"
                break
                ;;
            3)
                kernel="linux-zen"
                kernel_headers="linux-zen-headers"
                break
                ;;
            4)
                kernel="linux-hardened"
                kernel_headers="linux-hardened-headers"
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
    fi

    # 安装
    if pacman -Syy &> /dev/null; then
        pacstrap -K /mnt ${package_list[@]} $kernel $kernel_headers $micro_code $onboard_audio
    else
        echo "Mirror error; unable to install software." >&2
        return 1
    fi
}
