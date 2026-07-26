#!/usr/bin/env bash
set -euo pipefail

if [[ "$EUID" -ne "0" ]]; then
    echo "Please run this script with root."
    exit 1
fi

use_local_miror() {
    # 查找本地镜像源分区。
    # 依赖该分区 LABEL 固定为 F2FS-DATA。
    devices=($(blkid -t LABEL="F2FS-DATA" -o device))

    # 同名分区检测。
    if [[ ${#devices[@]} -ne 1 ]]; then
        echo "Error: expected exactly one partition with label F2FS-DATA, found ${#devices[@]}" >&2
        exit 1
    fi

    SOURCE_PARTITION="${devices[0]}"
    echo "The disk where the detected local mirror source is located: $SOURCE_PARTITION"

    # 挂载本地镜像源分区。
    mount --mkdir "$SOURCE_PARTITION" "/run/media/root/F2FS-DATA"

    # 临时关闭签名校验。
    # Arch 的 pacman.conf 默认 SigLevel 行如果变化，这个精确替换会失效。
    sed -i 's/SigLevel    = Required DatabaseOptional/SigLevel    = Never/g' "/etc/pacman.conf"

    # 先使用 file:// 镜像源安装 nginx。
    echo 'Server = file:///run/media/root/F2FS-DATA/repo/archlinux/$repo/os/$arch' > "/etc/pacman.d/mirrorlist"
    pacman -Syy &> /dev/null

    # 配置 nginx 本地 HTTP 镜像。
    # nginx 包名、默认 nginx.conf 结构、include 插入位置都可能随 Arch 包更新变化。
    echo "Configuring Nginx..."
    pacman -S --noconfirm nginx &> /dev/null
    mkdir -p "/etc/nginx/conf.d"
    sed -i '/http {/a\    include       conf.d/*.conf;' "/etc/nginx/nginx.conf"

    echo '
types_hash_max_size 4096;
types_hash_bucket_size 128;
server {
    # 监听端口
    listen 2304;

    # 本地镜像源目录
    root /run/media/root/F2FS-DATA/repo/archlinux;

    autoindex on;
    autoindex_exact_size on;
    autoindex_localtime on;

    location / {
        try_files $uri $uri/ =404;
    }
}
' > "/etc/nginx/conf.d/local_mirror.conf"

    nginx
    echo "Nginx configuration is completed."

    # 切换到 nginx 本地镜像源，供后续 pacstrap 和 chroot 内 pacman 使用。
    echo 'Server = http://127.0.0.1:2304/$repo/os/$arch' > "/etc/pacman.d/mirrorlist"
    pacman -Syy &> /dev/null
    echo "Local mirror site set up successfully."
}

use_local_miror
