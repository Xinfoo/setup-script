# Arch Linux Setup Script

这是一个面向 Arch Linux Live 环境的交互式安装脚本。它负责完成磁盘分区、格式化、挂载、基础系统安装、chroot 内配置、可选的桌面环境安装以及 systemd-boot 引导配置。

这个项目不是通用安装框架，也不是无人值守安装器。它按照作者自己的安装习惯设计，适合已经了解 Linux 磁盘、分区、挂载点和 UEFI 启动方式的用户。

> [!CAUTION]
> 脚本会执行 `wipefs`、`blkdiscard`、`sfdisk`、`mkfs`、`mkswap` 等破坏性命令。选择错误的磁盘或分区会造成不可恢复的数据丢失。Live 环境是一次性的，脚本不会备份配置，也不会在失败后回滚。

## 运行环境

脚本预期在以下环境中运行：

- 以 UEFI 模式启动的 Arch Linux Live ISO；
- 使用 root 权限；
- 目标是 SATA/SCSI 或 NVMe 内部磁盘；
- 能够访问 Arch Linux 软件仓库，或者已经准备好本项目支持的本地镜像分区；
- 目标系统使用 GPT、systemd、systemd-boot 和 `/boot` EFI 分区布局。

磁盘探测器会忽略 USB 磁盘、可移动设备和没有实际设备节点的虚拟块设备。

## 使用方法

在 Arch Linux Live 环境中取得本仓库，然后进入仓库根目录：

```bash
git clone https://github.com/Xinfoo/setup-script.git
cd setup-script
./live/setup.sh
```

如果当前不是 root 用户，请使用：

```bash
sudo ./live/setup.sh
```

脚本中的确认问题使用 `[Y/n]` 形式。直接按 Enter 等同于选择 Yes，请在涉及磁盘擦除、格式化和分区表写入时仔细确认。

### Secure Boot（可选）

Secure Boot 支持依赖用户预先准备的 shim 软件包和 MOK 密钥，不会自动下载软件包、生成密钥或注册 MOK。第一次操作前应保持固件中的 Secure Boot 关闭，并备份密钥。

在运行 `./live/setup.sh` 前，仓库中需要存在以下文件：

```text
live/
├── shim-signed.pkg.tar.zst
└── secure-boot/
    ├── MOK.key
    ├── MOK.crt
    └── MOK.cer
```

- `shim-signed.pkg.tar.zst`：预先构建好的 `shim-signed` 软件包，需要重命名为该固定名称；
- `MOK.key`：PEM 格式的 RSA 私钥，供 `sbsign` 使用；
- `MOK.crt`：PEM 格式的 X.509 证书，供 `sbsign` 使用；
- `MOK.cer`：同一证书的 DER 版本，供 MokManager 或 `mokutil` 注册。

`shim-signed` 来自 AUR，应提前在另一套可用的 Arch Linux 系统中以普通用户构建，然后把生成的软件包复制并重命名到本仓库：

```bash
git clone https://aur.archlinux.org/shim-signed.git
cd shim-signed
makepkg -s
cp shim-signed-*.pkg.tar.zst /path/to/setup-script/live/shim-signed.pkg.tar.zst
```

不要使用 root 运行 `makepkg`。将示例中的 `/path/to/setup-script` 替换为本仓库的实际路径。

可以在仓库根目录生成一组新的 MOK 密钥：

```bash
mkdir -p live/secure-boot

openssl req -new -x509 -newkey rsa:2048 -sha256 -nodes \
    -days 3650 \
    -subj "/CN=Arch Linux Secure Boot/" \
    -keyout live/secure-boot/MOK.key \
    -out live/secure-boot/MOK.crt

openssl x509 -outform DER \
    -in live/secure-boot/MOK.crt \
    -out live/secure-boot/MOK.cer

chmod 600 live/secure-boot/MOK.key
chmod 644 live/secure-boot/MOK.crt live/secure-boot/MOK.cer
```

> [!CAUTION]
> `MOK.key` 是能够签署可启动代码的私钥。不要把 `shim-signed.pkg.tar.zst`、`secure-boot/` 或其中的密钥提交到 Git，也不要把私钥提供给其他人。私钥丢失后，将无法使用原密钥为更新后的内核和引导器签名。

脚本检测到 shim 软件包和密钥目录后，会在 chroot 阶段询问是否启用 Secure Boot。选择 Yes 后，脚本会：

- 安装本地 `shim-signed` 软件包；
- 将 shim、MokManager 和 fallback 安装到 EFI 分区；
- 使用 MOK 签署 systemd-boot 和当前选择的内核；
- 自动生成 `/boot/EFI/ARCH/BOOTX64.CSV`，无需手动创建 CSV；
- 将 DER 证书复制为 `/boot/Arch_Linux_Secure_Boot_Key.cer`，供 MokManager 直接读取；
- 保留 `/root/secure-boot/`，供首次注册 MOK 和后续重新签名使用。

如果缺少这些材料，脚本会跳过 Secure Boot，继续执行普通的 systemd-boot 安装。

### chroot 阶段

Live 阶段安装完基础系统后，脚本会进入目标系统的 chroot shell，并提示手动执行：

```bash
./setup.sh
```

chroot 内的配置脚本执行完成后，输入：

```bash
exit
```

Live 阶段的主脚本随后会继续清理临时文件并卸载目标文件系统。systemd-boot 由 chroot 阶段的 `bootctl install` 安装，用户可以选择是否同时注册 EFI 固件启动项。

## 安装流程

### 1. 初始化与镜像源

脚本首先检查 root 权限，然后询问是否使用本地镜像源。

选择普通网络镜像时，脚本会通过 `ping baidu.com` 检查网络连接。

选择本地镜像时，脚本要求系统中恰好存在一个卷标为 `F2FS-DATA` 的分区，并执行以下操作：

- 将该分区挂载到 `/run/media/root/F2FS-DATA`；
- 临时关闭 Live 环境中的 pacman 签名校验；
- 从该分区安装 nginx；
- 在本机 `2304` 端口启动 HTTP 镜像服务；
- 将 pacman 镜像地址切换到 `http://127.0.0.1:2304/`。

### 2. 选择目标磁盘

脚本使用 `lsblk` 展示可用磁盘、文件系统、容量和当前挂载状态，然后让用户选择主安装磁盘。

分区状态输出采用树形结构。当当前磁盘状态与脚本记录的新方案不同时，会分别显示：

- `(present)`：磁盘当前真实状态；
- `(new)`：即将格式化和挂载的新状态。

### 3. 自动分区

自动分区器提供三种 GPT 布局：

1. EFI 1 GiB + ROOT 自动占用 + SWAP；
2. EFI 1 GiB + ROOT 100 GiB + HOME 自动占用 + SWAP；
3. EFI 1 GiB + ROOT 使用剩余空间。

EFI 分区固定使用 FAT32。ROOT 和 HOME 可以选择 Ext4、XFS 或 F2FS。

自动 SWAP 大小根据内存计算：

| 内存大小 | SWAP 大小 |
| --- | --- |
| 不超过 8 GiB | 内存的 2 倍 |
| 8–64 GiB | 与内存相同 |
| 超过 64 GiB | 8 GiB |

自动分区会擦除整块目标磁盘，然后使用 `sfdisk` 写入新的 GPT 分区表。`size=-<swap MiB>` 用于为磁盘末尾的 SWAP 分区预留空间。

### 4. 手动分区

手动模式会启动 `cfdisk`。用户可以选择是否先擦除磁盘，并自行创建、删除和调整分区。

对于主安装磁盘，脚本随后要求指定：

- 根分区 `/`；
- EFI 分区 `/boot`；
- 可选的 `/home`、`/var`、`/usr`、`/opt`；
- 一个或多个 SWAP 分区。

手动模式还可以继续选择并配置其他内部磁盘。

脚本会在最终确认前显示完整方案，但不会强制验证用户是否重复选择同一分区。用户需要确认挂载点没有重复，并且方案中恰好存在一个根分区和一个 EFI 分区。

### 5. 格式化与挂载

确认分区方案后，脚本按照选择执行：

- FAT32：`mkfs.fat -F 32`；
- Ext4：`mkfs.ext4 -F`；
- XFS：`mkfs.xfs -f`；
- F2FS：启用 `extra_attr`、`inode_checksum`、`sb_checksum` 和 `compression`；
- SWAP：`mkswap -f`。

根分区挂载到 `/mnt`，其他分区依次挂载到 `/mnt/boot`、`/mnt/usr`、`/mnt/var`、`/mnt/home` 和 `/mnt/opt`。挂载前会检查目标分区是否已经挂载。

对于 F2FS，用户可以选择推荐的优化挂载参数或默认挂载参数。SWAP 分区会通过 `swapon` 启用。

### 6. 安装基础系统

脚本先展示 `genfstab -U /mnt` 的结果供用户确认，然后使用 `pacstrap -K` 安装基础系统。

可选择的平台包括：

- Intel，对应 `intel-ucode`；
- AMD，对应 `amd-ucode`；
- 虚拟机，不额外安装 CPU microcode。

可选择的内核包括：

- `linux`；
- `linux-lts`；
- `linux-zen`；
- `linux-hardened`。

脚本会同时安装对应的内核头文件。选择笔记本时，脚本还会安装 `sof-firmware` 和 TLP，并在目标系统中启用 `tlp.service`。

基础软件包括 `base`、`base-devel`、文件系统工具、编辑器以及 Arch 手册等。安装完成后，脚本将 UUID 格式的 fstab 写入 `/mnt/etc/fstab`。

### 7. chroot 内配置

在 chroot 中执行 `./setup.sh` 后，脚本按以下顺序工作：

1. 检查 root 权限；
2. 设置时区、硬件时钟、主机名、hosts 和控制台键盘，并选择 `en_US.UTF-8` 或 `zh_CN.UTF-8` 作为默认 Locale；
3. 设置 root 密码；
4. 启用 pacman 彩色输出并刷新软件包数据库；
5. 安装 Zsh、NetworkManager、iwd、网络工具和 UEFI 工具；
6. 按用户选择安装 Intel 显卡组件、NVIDIA Open DKMS 驱动和蓝牙组件；
7. 安装 KDE Plasma、GNOME、实验性的 Hyprland，或者跳过桌面环境安装；
8. 可选安装中文输入法、桌面推荐软件、Firewall、打印组件及额外工具；选择跳过桌面环境时，仍会安装字体，并继续询问是否安装这些可选软件；
9. 安装并配置 systemd-boot；
10. 配置服务、创建普通用户、设置密码并打开 `visudo`。

NVIDIA 分支会修改 `/etc/mkinitcpio.conf`，加入 NVIDIA 模块并移除默认 `kms` hook。该逻辑依赖当前 Arch Linux 的默认配置格式。

选择笔记本并安装 GNOME 时，脚本还会安装 `tlp-pd`，使 GNOME 可以通过 Power Profiles 接口使用 TLP。KDE Plasma 直接使用 TLP 作为 PowerDevil 的可选后端，因此不会额外安装 `tlp-pd`。

#### Hyprland（实验性）

选择 Hyprland 后，脚本会安装 Hyprland、UWSM、greetd、ReGreet、PipeWire、Waybar、Wofi、Mako、Hyprpaper、Thunar、桌面门户和常用的 Wayland 工具，并启用 `greetd.service`。脚本还会生成供 ReGreet 单独使用的 `/etc/greetd/hyprland.lua`，并为 ReGreet 设置 Materia GTK 主题、Papirus 图标和 Noto Sans 字体。

该选项只准备基础组件和图形登录界面，不会为普通用户生成 Hyprland 及相关组件的配置文件，也不会配置相应的 systemd 用户单元。用户需要在安装完成后自行准备这些配置。

chroot 配置结束前，脚本会询问是否写入针对中国地区的 pacman 镜像列表。该设置用于目标系统安装完成后的软件更新，不影响此前已经完成的软件安装。

> [!IMPORTANT]
> 如果 Live 阶段使用了 `F2FS-DATA` 本地镜像，目标系统会继承临时的 `http://127.0.0.1:2304/` 地址。此时必须在 chroot 结束前选择中国地区镜像，或者在重启后自行将 `/etc/pacman.d/mirrorlist` 改为其他永久可访问的镜像。Live 环境结束后，本地 nginx 服务不会继续存在。

### 8. 引导与收尾

systemd-boot 配置会根据之前选择的内核和 CPU 平台生成，不会为虚拟机写入空的 microcode 路径。

脚本生成两个启动配置：

- `Arch Linux`；
- `Arch Linux Fallback`，当前使用与默认条目相同的内核、initramfs 和内核参数。

chroot 内的 `bootctl install` 会把 systemd-boot 安装到 EFI 分区，并写入 EFI 默认/回退加载器路径。用户可以选择是否注册名为 `Linux Boot Manager` 的 EFI 固件启动项；选择不注册时，引导文件和默认/回退路径仍会正常安装。

启用 Secure Boot 时，脚本会改用 shim 作为第一阶段加载器：

```text
/boot/EFI/BOOT/BOOTX64.EFI       # shim 回退入口
/boot/EFI/BOOT/GRUBX64.EFI       # 使用 MOK 签名的 systemd-boot
/boot/EFI/BOOT/MMX64.EFI         # MokManager
/boot/EFI/BOOT/FBX64.EFI         # fallback
/boot/EFI/ARCH/SHIMX64.EFI       # 固件启动项指向的 shim
/boot/EFI/ARCH/GRUBX64.EFI       # 使用 MOK 签名的 systemd-boot
/boot/EFI/ARCH/MMX64.EFI         # MokManager
/boot/EFI/ARCH/BOOTX64.CSV       # fallback 用于恢复固件启动项
/boot/Arch_Linux_Secure_Boot_Key.cer  # 供 MokManager 注册的 DER 证书
```

此时主动创建的 EFI 固件启动项名称为 `Arch Linux`，并指向 `\EFI\ARCH\SHIMX64.EFI`。即使没有主动创建，固件也可以尝试标准回退路径；fallback 会根据 `BOOTX64.CSV` 恢复启动项。

#### 首次注册 MOK

脚本会把公开的 DER 证书复制到 EFI 分区根目录。安装完成后，可以进入固件设置启用 Secure Boot，然后尝试启动 `Arch Linux`。shim 尚不信任新签名时会进入 MokManager；选择从磁盘注册密钥，在 EFI 分区中找到：

```text
Arch_Linux_Secure_Boot_Key.cer
```

确认注册并按提示重新启动。不同版本的 MokManager 菜单文字可能略有差异，通常需要依次选择从磁盘注册密钥、选择证书、查看并确认密钥，然后重启。

`MOK.cer` 是公开证书，不包含私钥，可以保留在 EFI 分区中，以便以后重新注册。真正需要严格保护的是 `/root/secure-boot/MOK.key`。

如果 MokManager 没有自动出现，也可以先关闭 Secure Boot，正常启动一次新安装的 Arch Linux，然后提交注册请求：

```bash
sudo mokutil --import /root/secure-boot/MOK.cer
sudo mokutil --list-new
```

`mokutil --import` 会要求设置一个临时密码。重新启动后进入 MokManager，确认注册并输入该密码。完成注册并再次重启后，再进入固件设置启用 Secure Boot。

进入系统后可以检查状态和签名：

```bash
mokutil --sb-state
mokutil --test-key /root/secure-boot/MOK.cer
sbverify --list /boot/EFI/ARCH/GRUBX64.EFI
sbverify --list /boot/vmlinuz-linux
```

最后一条命令中的内核文件名需要根据实际选择改为 `vmlinuz-linux-lts`、`vmlinuz-linux-zen` 或 `vmlinuz-linux-hardened`。

#### 系统更新后的重新签名

内核更新会覆盖已经签名的 `/boot/vmlinuz-*`。每次更新内核后，都必须使用保留的 MOK 重新签名。以下示例以 `linux` 内核为例：

```bash
kernel=/boot/vmlinuz-linux
mv "$kernel" "${kernel}.unsigned"
sbsign \
    --key /root/secure-boot/MOK.key \
    --cert /root/secure-boot/MOK.crt \
    --output "$kernel" \
    "${kernel}.unsigned"
sbverify --list "$kernel"
```

systemd 更新后，也应使用新版 systemd-boot 重新生成 shim 的第二阶段加载器：

```bash
systemd_boot=/usr/lib/systemd/boot/efi/systemd-bootx64.efi

sbsign \
    --key /root/secure-boot/MOK.key \
    --cert /root/secure-boot/MOK.crt \
    --output /boot/EFI/BOOT/GRUBX64.EFI \
    "$systemd_boot"

sbsign \
    --key /root/secure-boot/MOK.key \
    --cert /root/secure-boot/MOK.crt \
    --output /boot/EFI/ARCH/GRUBX64.EFI \
    "$systemd_boot"
```

重新签名前不要删除 `/root/secure-boot/MOK.key`。如果使用 NVIDIA DKMS 或其他外部内核模块，还需要另外配置模块签名；本脚本目前不负责为 DKMS 模块签名。

最后，脚本会：

- 删除复制到目标系统根目录的安装函数和临时信息；
- 递归卸载 `/mnt` 下的文件系统；
- 提示用户手动执行 `reboot`。

## 安装完成后的系统状态

正常完成后，目标系统将具有：

- 使用 UUID 的 `/etc/fstab`；
- systemd-boot、两个 Arch Linux 启动配置，以及由用户选择是否注册的普通或 Secure Boot EFI 固件启动项；
- 用户选择的 Arch 内核和对应头文件；
- Intel/AMD microcode，或适用于虚拟机的无 microcode 配置；
- NetworkManager，并使用 iwd 作为 Wi-Fi 后端；
- systemd-timesyncd 和 fstrim timer；
- 已禁用持久化核心转储；
- KDE Plasma、GNOME、实验性的 Hyprland，或者不安装桌面环境；
- 一个属于 `wheel` 组、默认 shell 为 Zsh 的普通用户；
- 笔记本上安装并启用的 TLP；
- 根据选择启用的蓝牙、显示管理器、Firewall 和 CUPS 服务；Hyprland 使用 greetd/ReGreet，未安装桌面环境时不会启用显示管理器。

脚本会打开 `visudo`，sudo 权限的具体内容由用户自行编辑。

## 项目结构

```text
live/
├── setup.sh                         # Live 环境主安装流程
├── chroot-setup.sh                  # 目标系统内的配置流程
└── functions/
    ├── processor/                   # 检测、选择、确认和状态输出
    ├── setter/                      # 文件系统与挂载点选择
    ├── actuator/                    # 分区、格式化、挂载和基础安装
    └── chroot/                      # chroot 内配置、驱动、桌面和引导
```

各函数保持独立，由 `setup.sh` 和 `chroot-setup.sh` 按安装阶段引用并调用。

## 已知限制

- 仅面向 UEFI + GPT + systemd-boot 安装方式；
- 自动分区主要面向 SATA/SCSI 和 NVMe 设备命名；
- 手动分区方案的正确性主要由用户最终确认；
- 脚本不会保存旧分区表、旧文件系统或旧配置；
- 多处配置使用针对当前 Arch Linux 默认文件内容的精确 `sed` 替换，系统包更新后可能需要调整；
- 使用 `F2FS-DATA` 本地镜像安装时，重启前必须将目标系统从临时 localhost 镜像切换到永久镜像；
- NVIDIA 分支只安装 `nvidia-open-dkms`；
- Hyprland 选项仍处于实验阶段，只安装基础组件并配置 ReGreet；用户需要自行准备 Hyprland 配置文件和相关的 systemd 用户单元配置；
- Secure Boot 需要用户自行准备和保护 MOK、完成首次注册，并在内核或 systemd-boot 更新后重新签名；
- Secure Boot 分支只签署内核和 systemd-boot，不会把 initramfs 和内核参数封装进统一内核映像，也不会自动签署 DKMS 模块；
- 安装过程包含大量交互，不适合无人值守执行；
- 当前项目仍应先在虚拟机或可丢弃磁盘上完成实际验证，再用于保存重要数据的机器。

## License

本项目使用 [MIT License](LICENSE)。
