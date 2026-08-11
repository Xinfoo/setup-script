#!/usr/bin/env bash

# 最终设置器
final_setter() {
    local tlp="$(cat "/info/tlp.txt")"
    local user_name

    # 让NetworkManager使用iwd作为wifi后端
    echo "Configuring NetworkManager..." >&2
    sleep 1
    # NetworkManager 配置项如果改名，WiFi 后端设置会失效。
    echo '[device]
wifi.backend=iwd' > "/etc/NetworkManager/conf.d/wifi_backend.conf"

    # 设置时间同步
    echo "Configuring time synchronization..." >&2
    sleep 1
    # systemd-timesyncd.conf 默认 NTP 行如果变化，这个替换会失效。
    sed -i 's/#NTP=/NTP=cn.ntp.org.cn time.windows.com cn.pool.ntp.org time.cloudflare.com/g' "/etc/systemd/timesyncd.conf"

    # 关闭核心转储
    echo "Disabling core dumps..." >&2
    sleep 1
    mkdir "/etc/systemd/coredump.conf.d/"
    sleep 1
    echo '[Coredump]
Storage=none
ProcessSizeMax=0' > "/etc/systemd/coredump.conf.d/custom.conf"

    # 启用服务
    echo "enabling services..." >&2
    systemctl enable NetworkManager.service
    systemctl enable systemd-timesyncd.service
    systemctl enable fstrim.timer

    if [[ "$BLUETOOTH" == "yes" ]]; then
        systemctl enable bluetooth.service
    fi

    if [[ "$DESKTOP_ENVIRONMENT" == "KDE" ]]; then
        systemctl enable sddm.service
    fi

    if [[ "$DESKTOP_ENVIRONMENT" == "Gnome" ]]; then
        systemctl enable gdm.service
    fi

    if [[ "$DESKTOP_ENVIRONMENT" == "Hyprland" ]]; then
        systemctl enable greetd.service
    fi

    if [[ "$tlp" == "yes" ]]; then
        systemctl enable tlp.service
    fi

    if [[ "$FIREWALL" == "yes" ]]; then
        systemctl enable firewalld.service
    fi

    if [[ "$PRINTER" == "yes" ]]; then
        systemctl enable cups.socket
    fi

    # 将新建用户的默认shell改成zsh
    sed -i 's|SHELL=/usr/bin/bash|SHELL=/usr/bin/zsh|g' "/etc/default/useradd"

    # 设置用户名
    echo "Creating user account..." >&2
    while true; do
        read -p 'Enter username: ' user_name

        if [[ -z "$user_name" ]]; then
            echo "The username cannot be empty." >&2
        else
            break
        fi
    done

    useradd -m -G wheel -s /bin/zsh "$user_name"

    echo "Set user password:" >&2
    while true; do
        if passwd "$user_name"; then
            break
        else
            echo "Please re-enter your password..." >&2
        fi
    done

    echo "Edit sudoers file by own (10s delay)..." >&2
    sleep 10
    visudo
}
