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

### chroot 阶段

Live 阶段安装完基础系统后，脚本会进入目标系统的 chroot shell，并提示手动执行：

```bash
./setup.sh
```

chroot 内的配置脚本执行完成后，输入：

```bash
exit
```

Live 阶段的主脚本随后会继续清理临时文件并卸载目标文件系统。systemd-boot 的安装和 EFI 固件启动项注册由 chroot 阶段的 `bootctl install` 完成。

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
7. 安装 KDE Plasma、GNOME，或者跳过桌面环境安装；
8. 可选安装中文输入法、桌面推荐软件、Firewall、打印组件及额外工具；选择跳过桌面环境时，仍会安装字体，并继续询问是否安装这些可选软件；
9. 安装并配置 systemd-boot；
10. 配置服务、创建普通用户、设置密码并打开 `visudo`。

NVIDIA 分支会修改 `/etc/mkinitcpio.conf`，加入 NVIDIA 模块并移除默认 `kms` hook。该逻辑依赖当前 Arch Linux 的默认配置格式。

选择笔记本并安装 GNOME 时，脚本还会安装 `tlp-pd`，使 GNOME 可以通过 Power Profiles 接口使用 TLP。KDE Plasma 直接使用 TLP 作为 PowerDevil 的可选后端，因此不会额外安装 `tlp-pd`。

chroot 配置结束前，脚本会询问是否写入针对中国地区的 pacman 镜像列表。该设置用于目标系统安装完成后的软件更新，不影响此前已经完成的软件安装。

> [!IMPORTANT]
> 如果 Live 阶段使用了 `F2FS-DATA` 本地镜像，目标系统会继承临时的 `http://127.0.0.1:2304/` 地址。此时必须在 chroot 结束前选择中国地区镜像，或者在重启后自行将 `/etc/pacman.d/mirrorlist` 改为其他永久可访问的镜像。Live 环境结束后，本地 nginx 服务不会继续存在。

### 8. 引导与收尾

systemd-boot 配置会根据之前选择的内核和 CPU 平台生成，不会为虚拟机写入空的 microcode 路径。

脚本生成两个启动配置：

- `Arch Linux`；
- `Arch Linux Fallback`，当前使用与默认条目相同的内核、initramfs 和内核参数。

chroot 内的 `bootctl install` 负责把 systemd-boot 安装到 EFI 分区、写入 EFI 默认/回退加载器路径，并注册名为 `Linux Boot Manager` 的固件启动项。

最后，脚本会：

- 删除复制到目标系统根目录的安装函数和临时信息；
- 递归卸载 `/mnt` 下的文件系统；
- 提示用户手动执行 `reboot`。

## 安装完成后的系统状态

正常完成后，目标系统将具有：

- 使用 UUID 的 `/etc/fstab`；
- systemd-boot 及两个 Arch Linux 启动配置；
- 用户选择的 Arch 内核和对应头文件；
- Intel/AMD microcode，或适用于虚拟机的无 microcode 配置；
- NetworkManager，并使用 iwd 作为 Wi-Fi 后端；
- systemd-timesyncd 和 fstrim timer；
- 已禁用持久化核心转储；
- KDE Plasma、GNOME，或者不安装桌面环境；
- 一个属于 `wheel` 组、默认 shell 为 Zsh 的普通用户；
- 笔记本上安装并启用的 TLP；
- 根据选择启用的蓝牙、显示管理器、Firewall 和 CUPS 服务；未安装桌面环境时不会启用显示管理器。

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
- 安装过程包含大量交互，不适合无人值守执行；
- 当前项目仍应先在虚拟机或可丢弃磁盘上完成实际验证，再用于保存重要数据的机器。

## License

本项目使用 [MIT License](LICENSE)。
