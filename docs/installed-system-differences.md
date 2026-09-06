# 相同选项下旧、新安装脚本的最终系统差异

本文只比较一次**成功完成的全新安装最终留下了什么**。旧版指 [`live/`](../live/) 中的 Bash 安装器，新版指当前 C Builder 生成的安装脚本。安装过程、交互方式、写盘前检查和失败清理的完整差异见 [generated-installer-vs-live.md](generated-installer-vs-live.md)。

## 1. 比较口径

“选择相同路径”在本文中指两边尽可能选择语义相同的选项：

- 同一类磁盘布局、文件系统、挂载点和 F2FS 挂载模式；
- 相同 CPU 平台、内核、Laptop、显卡和蓝牙选项；
- 相同桌面、推荐软件、中文输入法及可选软件组；
- 相同 hostname、username、timezone、Locale、Secure Boot、中国镜像和 EFI 启动项选择；
- 新版使用未修改的内建默认 `packages.json`；
- 两次安装面对相同的软件仓库快照和 Arch 包版本。

比较对象是目标磁盘、目标文件系统和 EFI NVRAM 中的持久状态，不把 Live 环境中的日志、临时 nginx、临时挂载和安装结束后的 Swap 是否仍启用算作目标系统内容。

以下天然不可能逐字相同的值也不视为架构差异：重新格式化产生的文件系统 UUID、重新建 GPT 产生的 PARTUUID、密码哈希盐、文件时间戳以及签名工具可能写入的时间相关数据。

新版独有的 KEEP、仅格式化不挂载、自动数据盘等能力在旧版没有严格对应路径，因此不纳入主比较。

## 2. 结论概览

在默认包表未修改、Secure Boot 关闭、中国镜像关闭的最简单路径中，两边安装的显式 Pacman 软件包和启用的服务基本相同，但最终系统仍不是逐文件相同。稳定可见的差异包括：

| 领域 | 最终差异 |
| --- | --- |
| GPT | 新版自动分区写入 PARTLABEL；旧版不写 `name=` |
| `/etc/hosts` | 新版生成完整 localhost/IPv6/hostname 映射；旧版只追加 hostname 映射 |
| 时间同步 | 新版使用 timesyncd drop-in；旧版修改主配置文件 |
| sudo | 新版固定生成 wheel sudoers drop-in；旧版结果取决于人工 `visudo` |
| 新用户默认 Shell | 新版只为本次用户指定 Zsh；旧版还尝试修改 `/etc/default/useradd` |
| initramfs | 新版在全部包安装后明确运行 `mkinitcpio -P` |
| Secure Boot | 启用时两版都安装 `shim-signed` 并保留未签名内核备份；新版不留下私钥 |

与此同时，`/etc/fstab` 和 `/etc/environment` 在“全新目标 + 相同选择”前提下预期具有相同的有效内容。两边的写入方法不同，但不应把方法差异误报为成品差异。

## 3. 存储成品

### 3.1 自动分区表

两边的三个共同自动布局含义一致：

- EFI 1 GiB + ROOT + Swap；
- EFI 1 GiB + ROOT 100 GiB + HOME + Swap；
- EFI 1 GiB + 占满剩余空间的 ROOT。

EFI、Linux filesystem 和 Swap 的 GPT 类型也一致。新版额外向 GPT 条目写入：

```text
EFI System
Arch Linux root
Arch Linux home
Linux swap
```

这些是 GPT partition name，通常由工具显示为 `PARTLABEL`，不是 FAT/Ext4/XFS/F2FS 的文件系统卷标。旧版 [`automatic-partitioner.sh`](../live/functions/actuator/automatic-partitioner.sh) 没有 `name=` 字段，因此对应分区通常没有这些 PARTLABEL。

ROOT + Swap 和 ROOT + HOME + Swap 布局的精确尾部扇区也可能略有差异。旧版让 `sfdisk` 用负 size 自动为 Swap 留空间；新版先按探测容量计算 MiB，并为首扇区对齐和备份 GPT 表保留小余量后写入显式容量。名义上的 1 GiB EFI、100 GiB ROOT 和推荐 Swap 不变，但不保证两个分区表逐扇区相同。ROOT-only 布局两边都让最后一个 ROOT 填满剩余空间。

旧版在支持 discard 的 SSD 上会先运行整盘 `blkdiscard`，新版不执行。完成相同 `mkfs` 后，这通常不会改变新文件系统中可见的目录和文件，但会改变未分配空间及底层块的 discard 状态，因此数据可恢复性和设备内部状态不同。

### 3.2 文件系统、挂载参数与 Swap

对共同支持的 FORMAT 路径，两边使用相同的主要命令和特性：

- FAT32：`mkfs.fat -F 32`；
- Ext4：`mkfs.ext4 -F`；
- XFS：`mkfs.xfs -f`；
- F2FS：`extra_attr,inode_checksum,sb_checksum,compression`；
- Swap：强制重新执行 `mkswap`。

选择相同 F2FS 模式时，写入 fstab 的挂载参数也相同。旧版界面中的 `Optimized mount options(read only)` 实际并非只读，它对应新版的 balanced 参数；旧版的 `normal use` 对应新版 compressed 参数。

两边都会在运行 `genfstab` 前完成所有挂载并启用 Swap，因此普通挂载和 Swap 都由 `genfstab -U` 记录。新版结束时会关闭本次启用的 Swap，旧版不会；重启后两边仍由相同语义的 fstab 决定是否启用 Swap，所以这是 Live 会话清理差异，不是持久配置差异。

### 3.3 `/etc/fstab`

在全新 pacstrap 目标上，两边最终都应包含 Arch 标准说明头、列名、设备注释、UUID 条目和 Swap 条目：

```text
# Static information about the filesystems.
# See fstab(5) for details.

# <file system> <dir> <type> <options> <dump> <pass>
```

旧版把 `genfstab` 输出追加到 pacstrap 已有的 `/etc/fstab`；新版重建上述文件头，再把同一次 `genfstab` 输出整体覆盖写入。对于全新目标，有效条目相同；如果目标文件事先已有内容，旧版会保留并继续追加，新版会丢弃旧内容。

两版挂载兄弟目录的先后顺序不同，因此多个独立挂载点在 `genfstab` 输出中的段落顺序可能不同，但不改变挂载语义。新版生成器自身以确定顺序排列挂载点，旧版则依次处理 `/boot`、`/usr`、`/var`、`/home` 和 `/opt`。

## 4. 软件包成品

### 4.1 默认包表下相同的部分

当前 [`src/packages.c`](../src/packages.c) 的内建默认包组来自旧脚本。相同选项下，下列目标系统显式包集合保持一致：

- bootstrap、所选内核与 headers、CPU microcode、Laptop 的 `sof-firmware`；
- Zsh、NetworkManager/iwd、DHCP、UEFI 和签名工具；
- TLP、GNOME Laptop 的 `tlp-pd`；
- Intel、NVIDIA 和 Bluetooth 包；
- KDE、GNOME、Hyprland、桌面推荐包和输入法；
- 字体、Firewall、Printer、Archive、Terminal、Extra 和 Desktop Apps。

依赖包集合仍取决于仓库快照。新版允许编辑 `packages.json`，一旦用户修改包组，就不再能用本节结论推断与旧版相同。

`local_mirror_live` 中的 nginx 和 `secure_boot_live` 中的签名工具用于 Live 环境准备，不会因为属于这两个组就自动成为目标系统新增包。目标系统的 `sbsigntools` 仍由两边共同的 core 组安装。

### 4.2 Secure Boot 下的 `shim-signed` 包

开启 Secure Boot 后，两版都会在目标 chroot 中执行 `pacman -U` 安装 `shim-signed.pkg.tar.zst`。因此两边安装出的系统都会：

- 在 Pacman 本地数据库中登记 `shim-signed`；
- 保留该包安装到 `/usr/share/shim-signed/` 等位置的文件；
- 执行该 AUR 包携带的安装脚本或 hook。

新版的不同之处在于，它先在 Live 环境的私有 tmpfs 中验证软件包并提取、检查三个 EFI 文件，然后只把已验证的包副本临时放入目标 `/root`。安装完成后该临时副本会被删除。旧版则直接复制输入包和整个密钥目录。两版在 Pacman 包数据库及 `/usr/share/shim-signed/` 内容上没有预期差异。

## 5. 目标配置文件

### 5.1 `/etc/hosts`：内容不同

旧版依赖 pacstrap 创建的文件头，只追加一行，并把 hostname 放在 `127.0.0.1`：

```text
# Static table lookup for hostnames.

# See hosts(5) for details.

127.0.0.1        HOSTNAME.localdomain HOSTNAME
```

新版按当前模板完整覆盖为：

```text
# Static table lookup for hostnames.
# See hosts(5) for details.

127.0.0.1 localhost
::1 localhost
127.0.1.1 HOSTNAME.localdomain HOSTNAME
```

因此新版明确提供 IPv4/IPv6 localhost，并把主机名映射放到 `127.0.1.1`；空行和字段间空格也与旧成品不同。

### 5.2 `/etc/environment`：全新安装时内容相同

KDE 或 Hyprland 启用 Fcitx 时，当前新版完整写出：

```text
#
# This file is parsed by pam_env module
#
# Syntax: simple "KEY=VAL" pairs on separate lines
#

XMODIFIERS=@im=fcitx
SDL_IM_MODULE=fcitx
GLFW_IM_MODULE=ibus
```

旧版向 pacstrap 提供的 Arch 默认 `/etc/environment` 追加相同三项；在默认文件未被改动的全新安装中，最终正文与上述内容一致。区别只在重复执行或目标已有自定义内容时出现：旧版可能重复变量并保留其他内容，新版会用上述固定成品覆盖。

GNOME 输入法路径两边都只安装 IBus 包，不写这三个 Fcitx 变量。

### 5.3 Locale、hostname、控制台和 pacman

相同输入下，下列持久配置有效内容相同：

- `/etc/localtime` 指向相同时区；
- `/etc/adjtime` 由 `hwclock --systohc` 生成；
- `en_US.UTF-8` 和 `zh_CN.UTF-8` 在 `/etc/locale.gen` 中启用；
- `/etc/locale.conf` 写入相同 `LANG=`；
- `/etc/hostname` 写入相同 hostname；
- `/etc/vconsole.conf` 为 `KEYMAP=us`；
- `/etc/pacman.conf` 启用 `Color`。

新版的匹配表达式和输入校验更严格，但在旧版替换恰好成功且输入相同的前提下，不造成额外目标配置差异。

### 5.4 NetworkManager、coredump 和 journal

两边最终都写入相同语义的文件：

- `/etc/NetworkManager/conf.d/wifi_backend.conf`：使用 iwd；
- `/etc/systemd/coredump.conf.d/custom.conf`：禁用 core dump 存储；
- `/etc/systemd/journald.conf.d/custom.conf`：总量 500M、单文件 50M。

目录创建方式不同，但成功完成后文件内容相同。

### 5.5 timesyncd：有效设置相同，文件落点不同

两边使用相同的 NTP 列表：

```text
cn.ntp.org.cn time.windows.com cn.pool.ntp.org time.cloudflare.com
```

旧版用 `sed` 修改 `/etc/systemd/timesyncd.conf` 中恰好匹配的 `#NTP=`；新版不修改主文件，而是创建：

```text
/etc/systemd/timesyncd.conf.d/custom.conf
```

只要旧版替换成功，systemd-timesyncd 的有效配置相同，但文件系统 diff 会显示不同。新版也不依赖上游主配置中注释行的精确格式。

### 5.6 用户、默认 Shell 和 sudo

两边都创建同名用户、建立 home、加入 wheel，并把该用户登录 Shell 设为 Zsh。差异是：

- 旧版还尝试把 `/etc/default/useradd` 中精确的 `SHELL=/usr/bin/bash` 改成 Zsh；如果该文本匹配，今后由 `useradd` 创建的其他用户默认也会使用 Zsh；
- 旧版为本次账户固定传递 `-s /bin/zsh`；
- 新版不修改全局 useradd 默认值，只为本次账户传递探测结果：优先 `/usr/bin/zsh`，该路径不可执行时回退到 `/bin/zsh`。

sudo 结果不能简单视为相同。旧版打开交互式 `visudo`，最终权限完全取决于操作者保存了什么；新版固定创建：

```text
/etc/sudoers.d/10-wheel
```

内容为 `%wheel ALL=(ALL:ALL) ALL`，模式为 `0440`，随后校验完整 sudoers。若旧版操作者手工启用了等价 wheel 规则，两边有效授权相同，但规则所在文件不同；若旧版没有修改 sudoers，则新版 wheel 用户会多出 sudo 权限。

## 6. 桌面相关成品

### 6.1 字体配置

两边默认安装的字体包和 `/etc/fonts/local.conf` 均相同，完整回退链显式列出：

- Noto CJK 的 SC、TC、JP、KR；
- Sarasa Mono 的 SC、TC、J、K；
- Noto、Emoji、Nerd Font 和 DejaVu 回退。

新版保留旧版的多行 XML 格式以及上述顺序，没有再精简 TC、JP、KR 或 Sarasa TC/J/K 项。这能让 fontconfig 按语言族选择更合适的 CJK 字形，减少中文字符错误回退到日文字形的情况。

### 6.2 KDE 与 GNOME

使用默认包表并选择相同推荐软件和输入法时，KDE 与 GNOME 的显式包集合相同。显示管理器分别仍为 SDDM 和 GDM；中文输入分别仍为 Fcitx5 和 IBus。

除公共的 hosts、timesyncd、sudoers 等差异外，两边没有额外的 KDE/GNOME 专用配置文件差异。

### 6.3 Hyprland

默认 Hyprland 包集合、greetd 主配置、ReGreet GTK 主题选择和启动命令保持一致。`/etc/greetd/hyprland.lua` 也恢复为旧版的多行结构与缩进格式；在相同选择下，这几份登录管理器配置没有预期的最终内容差异。

## 7. 服务启用状态

相同选项下，两边都会启用：

- `NetworkManager.service`；
- `systemd-timesyncd.service`；
- `fstrim.timer`；
- 选中蓝牙时的 `bluetooth.service`；
- Laptop 模式下的 `tlp.service`；
- 选中 Firewall 时的 `firewalld.service`；
- 选中 Printer 时的 `cups.socket`；
- KDE/GNOME/Hyprland 对应的 `sddm.service`、`gdm.service` 或 `greetd.service`。

因此在所有相关命令都成功的前提下，systemd enable 生成的目标 unit 链接集合没有有意差异。

## 8. NVIDIA 与 initramfs

两边在选中 NVIDIA 时都希望得到：

```text
MODULES=(nvidia nvidia_modeset nvidia_uvm nvidia_drm)
```

并从 `HOOKS` 中移除 `kms`。旧版只在上游文件与一整行硬编码文本完全相同时生效；新版匹配任意 `MODULES=` 和 `HOOKS=` 行，并分别处理 `kms` 位于开头、中间、末尾或作为唯一项的情况，避免在相邻 hook 之间留下连续空格。因此在旧版精确替换成功时，两边配置语义相同；上游默认行变化时，旧系统可能保留未修改配置，而新系统仍会修改。

新版在驱动、桌面和全部可选软件安装完成后明确执行一次 `mkinitcpio -P`。旧版没有对应的最终统一调用，只依靠内核、DKMS 或其他软件包 hook。即使两边使用相同配置，initramfs 二进制也不保证逐字节相同；新版的确定性在于最终一定按完成后的配置重新生成全部 preset。

## 9. systemd-boot 和 EFI NVRAM

### 9.1 普通启动项

两边都会运行 `bootctl --no-variables install`，普通 `arch.conf` 的 title、kernel、microcode、root UUID 和内核参数相同，`loader.conf` 也相同。

两边的 `arch-fallback.conf` 都与旧版设计保持一致，引用普通的 `initramfs-<kernel>.img`，不引用 `initramfs-<kernel>-fallback.img`。这个条目不是另一套 initramfs，而是内核参数回退入口：用户以后修改普通 `arch.conf` 时，可以保留 fallback entry 中安装器生成的参数，在普通项因参数错误无法启动时进入系统修复。

### 9.2 EFI NVRAM

新版无论是否启用 Secure Boot，EFI NVRAM label 都统一为 `Linux Boot Manager`；Secure Boot 的 shim `BOOTX64.CSV` 回退注册名称也使用该名称。旧版普通模式使用 `Linux Boot Manager`，Secure Boot 模式则使用 `Arch Linux`，其 CSV 描述中还包含 `Arch Linux Secure Boot`。两版仍根据模式选择不同的 loader 路径。

已有启动项或多位分区号会导致不同结果：

- 旧版用设备路径最后一个字符推断分区号，并且总是创建新项；
- 新版从 `lsblk PARTN` 读取完整编号，并按 label、PARTUUID 和 loader 检测已有项，匹配时不重复创建。

这部分差异位于固件 NVRAM，不在目标根文件系统中，但会影响最终机器的启动菜单。

## 10. Secure Boot 成品

使用相同 shim 包和 MOK 时，两边最终都计划在 ESP 中提供：

- `EFI/BOOT/BOOTX64.EFI`：shim；
- `EFI/BOOT/GRUBX64.EFI`：用 MOK 签名的 systemd-boot；
- `EFI/BOOT/MMX64.EFI` 和 `FBX64.EFI`；
- `EFI/ARCH/` 下的对应文件和 `BOOTX64.CSV`；
- `/boot/Arch_Linux_Secure_Boot_Key.cer`；
- 用 MOK 签名、仍位于原文件名的 kernel。

但最终目标中有以下明确差异：

| 项目 | 新版 | 旧版 |
| --- | --- | --- |
| `shim-signed` Pacman 包 | 安装并登记到目标包数据库 | 安装并登记到目标包数据库 |
| `/usr/share/shim-signed/` | 由该包安装并保留 | 由该包安装并保留 |
| `/root/secure-boot/` | 不存在 | 保留 MOK 私钥、PEM/DER 证书，目录 `0500`、文件 `0400` |
| 未签名 kernel 备份 | `/boot/vmlinuz-<kernel>.bak`，签名验证后原子保存 | 同一路径，先移动原内核再签名 |
| 公开 CER 模式 | 明确安装为 `0644` | `cp -a` 保留源文件被改成的 `0400` 模式 |
| ESP 既有文件 | 只替换安装器管理的目标 | 先执行 `rm -f /boot/EFI/BOOT/*` |
| 签名提交 | 验证后逐文件原子替换 | 直接生成到最终路径 |

在全新格式化的 EFI 分区上，“是否清空既有文件”不会产生可见差异；使用保留内容的 EFI 分区时才会表现出来。旧版本身没有 KEEP 语义，所以这也不是完全对称的选择路径。

两边都没有把外置 initramfs 纳入这条 shim/MOK 验证链，也都没有建立内核或 systemd-boot 更新后的自动重签名机制。

## 11. 中国镜像与本地镜像

### 11.1 最终 mirrorlist

关闭中国镜像且不使用本地镜像时，两边通常保留 pacstrap 从同一 Live 环境带入的 mirrorlist，因此没有有意差异。

开启中国镜像时，新版已恢复旧版完整内容：两边都写入相同的三行 banner、21 个服务器及相同排列顺序。因此相同选择下，最终 `/etc/pacman.d/mirrorlist` 没有有意差异。

### 11.2 临时本地镜像

在两边都使用本地 F2FS-DATA 镜像并最终选择中国镜像时，本地 nginx 只服务安装过程；目标系统不会安装 nginx，也不会保存 nginx 服务。两边最终都会写入相同的永久 China mirrorlist。

旧版允许“临时本地镜像 + 不写永久中国镜像”，这种路径可能把目标 mirrorlist 留在 `http://127.0.0.1:2304/...`，重启后不可用。新版把这一组合判定为错误，因此没有相同的新路径可供比较。

## 12. 临时文件与安装残留

未启用 Secure Boot 且两边都正常完成时：

- 旧版会删除 `/info`、`/setup-script-functions`、`/setup.sh` 和复制进去的 shim 包；
- 新版会删除 `/root/.arch-install-chroot.sh`；
- 两边都不应在目标中留下各自的常规安装脚本树。

启用 Secure Boot 时，两版都会保留 `vmlinuz-*.bak` 作为未签名内核的人工恢复材料。旧版还会留下 `/root/secure-boot/` 私钥目录，新版不会。

新版的安装日志留在 Live 环境 `/tmp` 或用户指定的 Live 路径，不会自动复制进目标系统。

## 13. “相同结果”和“仅实现不同”的清单

在本文假设下，可以把核对结果压缩为以下两组。

### 最终有效结果相同

- 默认 Secure Boot 关闭时的显式目标软件包集合；
- 同语义选择下的文件系统类型和 F2FS 挂载参数；
- fstab 中普通挂载与 Swap 的有效条目；
- Fcitx 路径的 `/etc/environment` 正文；
- 字体包及完整的 `/etc/fonts/local.conf` 回退链；
- 启用中国镜像时的完整 21 项 mirrorlist；
- timezone、Locale、hostname、vconsole 和 pacman Color；
- NetworkManager iwd、coredump 和 journal 配置；
- 相同选项对应的 systemd enabled units；
- 普通 systemd-boot entry 和 loader.conf；
- 使用普通 initramfs 的参数回退启动项；
- Secure Boot 下同路径的未签名 kernel `.bak` 备份；
- 本次创建用户的 home、wheel 组和 Zsh 登录 Shell。

### 最终内容或行为不同

- GPT PARTLABEL、部分自动布局的精确分区边界、SSD 未分配区 discard 状态；
- `/etc/hosts`；
- timesyncd 配置文件落点；
- `/etc/default/useradd` 的处理；
- sudoers 文件和旧版不确定的人工编辑结果；
- NVIDIA 配置对上游文件变化的适应性及最终 `mkinitcpio -P`；
- Secure Boot 的私钥留存、备份生成顺序、证书权限和 ESP 旧文件处理；
- EFI NVRAM 的重复项与多位分区号行为。

## 14. 维护时如何更新本文

下列文件改变后，应重新检查本文：

| 主题 | 新版 | 旧版 |
| --- | --- | --- |
| 默认包组 | [`src/packages.c`](../src/packages.c) | [`live/functions/chroot/`](../live/functions/chroot/) 与 [`basic-software-installer.sh`](../live/functions/actuator/basic-software-installer.sh) |
| 分区与格式化 | [`runtime-partitioning.sh`](../src/generator/templates/runtime-partitioning.sh)、[`runtime-filesystems.sh`](../src/generator/templates/runtime-filesystems.sh) | [`automatic-partitioner.sh`](../live/functions/actuator/automatic-partitioner.sh)、[`partition-formatter.sh`](../live/functions/actuator/partition-formatter.sh)、[`mounter.sh`](../live/functions/actuator/mounter.sh) |
| 基础配置 | [`chroot-base.sh`](../src/generator/templates/chroot-base.sh) | [`basic-setter.sh`](../live/functions/chroot/basic-setter.sh) |
| 桌面与字体 | [`chroot-desktop.sh`](../src/generator/templates/chroot-desktop.sh)、[`chroot-optional-software.sh`](../src/generator/templates/chroot-optional-software.sh) | [`desktop-environment-installer.sh`](../live/functions/chroot/desktop-environment-installer.sh) |
| 服务与用户 | [`chroot-system.sh`](../src/generator/templates/chroot-system.sh) | [`final-setter.sh`](../live/functions/chroot/final-setter.sh) |
| 引导与 Secure Boot | [`chroot-bootloader.sh`](../src/generator/templates/chroot-bootloader.sh)、[`finish-secure-boot.sh`](../src/generator/templates/finish-secure-boot.sh)、[`finish-firmware.sh`](../src/generator/templates/finish-firmware.sh) | [`bootloader-installer.sh`](../live/functions/chroot/bootloader-installer.sh) 与 [`live/setup.sh`](../live/setup.sh) |
| fstab 与包源 | [`runtime-package-source.sh`](../src/generator/templates/runtime-package-source.sh)、[`chroot-system.sh`](../src/generator/templates/chroot-system.sh) | [`live/setup.sh`](../live/setup.sh)、[`live/chroot-setup.sh`](../live/chroot-setup.sh) |

本文描述的是当前代码行为，不承诺未来版本继续与 `live/` 保持结果兼容；`live/` 仍只是迁移参考。
