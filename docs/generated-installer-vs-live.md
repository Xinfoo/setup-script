# C 构造器生成脚本与 `live/` 旧安装脚本的流程和差异

本文以当前仓库代码为准，说明 C 程序生成的 `install.sh` 如何完成一次安装，并与 `live/` 中保留的旧 Bash 安装器逐阶段对照。文档关注实际行为、数据传递、软件包、存储、Secure Boot、失败恢复等细节，不把 `live/` 当作仍需兼容的接口。

相关入口：

- 当前构造器入口：[src/main.c](../src/main.c)
- 当前脚本生成入口：[src/generator/generator.c](../src/generator/generator.c)
- 当前 Bash 模板：[src/generator/templates/](../src/generator/templates/)
- 旧版 Live 入口：[live/setup.sh](../live/setup.sh)
- 旧版 chroot 入口：[live/chroot-setup.sh](../live/chroot-setup.sh)

## 1. 两套实现的根本区别

| 方面 | 当前 C 构造器 | `live/` 旧脚本 |
| --- | --- | --- |
| 交互模型 | 先在 ncurses TUI 中编辑完整方案，再验证、保存和生成 | 一条线性 Shell 问答链，选择与执行交错进行 |
| 配置载体 | `config/install-plan.json` + `config/packages.json` | 当前 Shell 进程中的变量和关联数组 |
| 写盘边界 | 常规 TUI 操作只修改内存方案；执行生成脚本后才写盘 | 自动分区和手动分区在前端问答尚未结束时就可能写盘 |
| 产物 | 一份可预览、可重复运行的 `install.sh` | `live/setup.sh`、多个 sourced 函数文件和 chroot 脚本树共同工作 |
| chroot | 自动生成内层脚本并明确执行 | 进入 chroot 后要求用户手工运行 `./setup.sh` |
| 软件选择 | 运行前已全部写进方案，包名来自可编辑 JSON | 多数选择在磁盘已经格式化后才逐项询问，包名硬编码在函数中 |
| 设备复核 | 保存设备身份，生成前和写盘前多次复核 | 主要依赖当时选择的 `/dev/...` 路径 |
| 失败处理 | 全局日志和 EXIT/信号清理，跟踪本次创建的资源 | 依赖 `set -e` 中断，没有统一回收路径 |
| Secure Boot 私钥 | 只进入 Live 环境的私有 tmpfs，不进入目标系统 | 复制到目标系统 `/root/secure-boot/`，旧流程结束后仍会留下 |

一句话概括：旧版是“边问边安装”，当前实现是“先形成可审阅计划，再生成一份带运行时复核的执行脚本”。

## 2. 当前架构的数据流

```text
lsblk JSON ──> HardwareInventory
                    │
packages.json ──> ncurses TUI ──> InstallPlan
                    │                 │
                    └──── validation ─┘
                                      │
                     install-plan.json + install.sh
                                               │
                                               └── 用户明确执行
```

### 2.1 构造阶段

1. 程序在当前工作目录检查 `config/` 和 `config/packages.json`。
2. `packages.json` 不存在时写出内建默认包组；存在时必须完整符合当前 `version = 2` 格式。其中 `local_mirror_live` 默认包含用于临时 HTTP 镜像的 `nginx`。
3. 软件包配置损坏、字段缺失或版本不一致时，交互式终端会询问是否用默认值覆盖；非交互模式不会自行覆盖。
4. 程序用 `lsblk --json --bytes --paths` 建立磁盘和分区清单，记录路径、容量、型号、序列号、传输方式、只读/可移动状态、分区表、PARTUUID、文件系统 UUID、GPT 类型和起始扇区等信息。
5. TUI 编辑 `InstallPlan`。普通编辑不会执行 `wipefs`、`sfdisk`、`mkfs`、`mount` 或 `swapon`。
6. Storage 页面主动启动 `cfdisk` 是唯一例外：它要求 root、只允许用于“使用现有分区”模式，先显示破坏性确认，退出后重新探测磁盘并清空该盘旧的分区用途分派。
7. 保存方案时写入版本 3 的 JSON。旧版 JSON 不做兼容迁移，版本不符直接拒绝。
8. 生成前运行集中验证。存在阻断错误时不会产生脚本。

构造器本身可以在普通用户环境运行。只有从 Storage 页面拉起 `cfdisk` 或实际执行生成脚本时才需要特权。

### 2.2 生成阶段

生成器不是在运行时读取 `src/generator/templates/*.sh`。CMake 会把模板按原始 UTF-8 字节嵌入可执行文件，因此修改模板后必须重新构建 Builder。

生成顺序固定为：

1. Shell 前导和从方案生成的只读变量/数组；
2. Live 环境运行函数；
3. 用于目标 chroot 的内层 Shell 脚本生成函数；
4. Secure Boot、目标软件源、EFI 启动项和主流程；
5. `main "$@"` 执行入口。

动态写入脚本的主要内容包括：

- 主系统盘、所有参与安装的磁盘及其身份；
- 每块磁盘的布局模式和引导式分区容量；
- 每个待处理分区所属磁盘、设备路径、用途、动作、文件系统、挂载点和身份；
- 内核、initramfs、平台、镜像、EFI 和 Secure Boot 开关；
- 本次方案实际会使用的所有软件包数组。

字符串在写入 Shell 前统一进行单引号转义。脚本写入同目录临时文件，设置为 `0750`，完成 `fflush` 和 `fsync` 后才用 `rename` 原子替换目标。生成器拒绝把符号链接或其他非普通文件作为输出目标，因此失败时不会留下半份新脚本。

最终脚本本身不再依赖 `install-plan.json` 或 `packages.json`；这些信息已经展开在脚本中。启用 Secure Boot 时仍需要在最终脚本同目录提供 shim 包和 MOK 材料。

## 3. 当前生成脚本的完整执行流程

当前流程的实际入口位于 [finish-main.sh](../src/generator/templates/finish-main.sh)。以下顺序不是概念示意，而是 `main()` 的调用顺序。

### 3.1 Shell 运行环境和日志

脚本首先使用：

```bash
set -Eeuo pipefail
PATH='/usr/bin'
umask 022
```

它把脚本自身所在目录作为 Secure Boot 资产目录，把 `/mnt` 作为目标根目录。脚本创建权限为 `0700` 的私有工作目录，并尽早注册 `EXIT`、`INT`、`TERM`、`HUP` 清理逻辑。

日志默认创建在 `/tmp/arch-install.XXXXXX.log`。也可以用 `ARCH_INSTALL_LOG` 指定路径，但指定路径必须尚不存在且不能是符号链接。标准输出和标准错误通过 `tee` 同时写入终端和日志；清理阶段最后才结束日志进程，以保留清理错误。

旧版没有统一日志文件，也没有捕获所有退出路径的清理函数。

### 3.2 Preflight

`preflight()` 在任何分区表或文件系统写入前检查：

- 当前进程是 root；
- Live 环境由 UEFI 启动；
- Bash、块设备工具、挂载工具、pacman、pacstrap、genfstab、arch-chroot 和文件操作命令存在；
- `/mnt` 不是符号链接且可以作为目录使用；
- 方案中的时区文件在 Live 环境存在；
- 所有磁盘数组和分区数组长度一致；
- 每个参与磁盘和已有分区仍与方案记录的身份一致；
- 每种 FORMAT 动作所需的 `mkfs`/`mkswap` 命令存在；
- 如果要求创建 EFI NVRAM 项，efivarfs 已挂载且可写；
- 如果使用本地镜像，镜像源满足约束；
- 如果启用 Secure Boot，shim 和 MOK 材料满足约束。

旧版入口只做 root 检查，没有集中检查 UEFI、efivarfs、命令集合、方案数组或目标身份。

### 3.3 磁盘和分区身份复核

当前脚本对每块参与磁盘检查：

- 路径仍是块设备且 `lsblk TYPE` 为 `disk`；
- 设备不是只读；
- 容量与生成方案时一致；
- 方案中非空的型号、序列号仍一致；
- 使用现有分区时，分区表类型仍一致且必须是 GPT。

对已有分区还会检查：

- 当前父磁盘；
- 分区编号；
- 起始扇区；
- 字节容量；
- PARTUUID；
- GPT 分区类型；
- KEEP 文件系统的类型和文件系统 UUID。

自动分区完成后，脚本会检查新分区的父盘、编号、GPT 类型、PARTUUID、起始位置、与前一分区的间隙和实际容量。填满磁盘的末分区允许最多 64 MiB 的分区表/对齐误差。

在真正写盘前，脚本还会拒绝：

- `/mnt` 或其下已有挂载；
- 被选中节点已经挂载；
- 被选中节点是活动 Swap；
- 节点仍被 device-mapper、RAID、LVM 等 holder 持有。

引导式整盘模式检查该盘及全部子节点；已有分区模式检查整盘节点和方案中实际会处理的分区。

旧版只在挂载某个分区前用 `findmnt --source` 判断它是否已经挂载，没有保存并比较磁盘容量、型号、序列号、分区几何或 UUID，也没有统一检查活动 Swap 和 holder。

### 3.4 展示计划与软件源确认

脚本打印：

- `/boot` 与 `/` 所在的主系统盘；
- 全部参与磁盘、型号、容量和布局模式；
- 本地镜像身份；
- 每个待处理分区的 ACTION、文件系统、用途和挂载点。

之后需要一次软件源确认：

- 网络源必须精确输入 `PREPARE`；
- 本地源必须精确输入 `BOOTSTRAP <device> <UUID>`，确认仅在 Live 环境中用该来源引导安装 HTTP 服务器。目标系统仍使用标准签名策略。

旧版使用默认 Yes 的 `[Y/n]` 确认，而且在选择目标磁盘之前就可能挂载本地镜像、修改 Live pacman 配置并启动 nginx。

### 3.5 软件源准备和完整软件包预解析

网络模式直接运行 `pacman -Syy --noconfirm`，不再用 `ping baidu.com` 作为联网判据。网络可达但 ICMP 被禁用的环境不会因为 ping 失败被提前拒绝；真正的 pacman 操作失败仍会中止。

本地模式先以 `ro,nodev,nosuid,noexec` 挂载 `F2FS-DATA`，备份 Live 的 pacman 配置，并临时用 `file://` 和 `SigLevel = Never` 安装 `local_mirror_live` 组中的 nginx。随后启动只监听 `127.0.0.1:2304` 的独立 nginx 配置，立即恢复 Live 原有 `pacman.conf`，将 mirrorlist 切换到 `http://127.0.0.1:2304/$repo/os/$arch` 并重新刷新数据库。由此只有 nginx 引导安装绕过验签，后续 HTTP 操作恢复 Live 的原签名策略。

随后脚本使用 `pacman -Sp --needed --noconfirm` 预解析本次方案的完整软件包并集。桌面、驱动和可选软件如果在仓库中不可解析，会在任何磁盘写入之前失败。

旧版只在 pacstrap 前刷新数据库。桌面、驱动和可选软件要等根文件系统已经格式化并完成 pacstrap 后才逐项安装，因此缺包错误出现得更晚。

启用 Secure Boot 时，当前脚本还会在 Live 环境安装 `secure_boot_live` 组，默认是 `sbsigntools`，并确认 `sbsign`、`sbverify` 和 `bsdtar` 可用。这一组同样来自 `packages.json`。

### 3.6 Secure Boot 输入快照

如果启用 Secure Boot，脚本在写盘前验证：

- `shim-signed.pkg.tar.zst` 是普通文件而非符号链接；
- `secure-boot/` 是真实目录；
- `MOK.key`、`MOK.crt`、`MOK.cer` 都是普通文件；
- pacman 能读取 shim 包信息且包名确实是 `shim-signed`；
- PEM 私钥和 PEM 证书的公钥匹配；
- PEM 与 DER 证书的 SHA-256 指纹匹配。

通过后，它在私有工作目录下挂载 `nodev,nosuid,noexec,mode=0700,size=64M` 的 tmpfs，把材料以 `0600` 复制进去并再次验证。脚本用 `bsdtar -xOf` 提取 `shimx64.efi`、`mmx64.efi` 和 `fbx64.efi`，并用 `sbverify --list` 检查它们带有签名。完成 pacstrap 后，已验证的 shim 包会临时复制到目标 `/root`，再由 chroot 内的 `pacman -U` 安装并删除临时副本。

旧版只判断 shim 文件和密钥目录是否存在，然后把整个目录复制到目标系统，并在 chroot 内用 `pacman -U` 安装 shim 包。两版都会在目标系统登记该包并执行其安装脚本和 hook；区别是新版安装经过前置验证的 tmpfs 快照副本，而旧版不比较私钥/证书、不验证 DER 证书，也不验证提取的 EFI 文件。

### 3.7 KEEP 文件系统只读探测

只要方案存在 KEEP 动作，当前脚本会在破坏性确认前实际测试其可读性：

| 文件系统 | 探测挂载选项 |
| --- | --- |
| FAT/vfat | `ro,nodev,nosuid,noexec` |
| Ext4 | `ro,noload,nodev,nosuid,noexec` |
| XFS | `ro,norecovery,nodev,nosuid,noexec` |
| F2FS | `ro,disable_roll_forward,nodev,nosuid,noexec` |
| Swap | 用 `swaplabel` 读取头部，不启用 |

每次挂载后都会用 `findmnt` 确认来源，再卸载临时探测点。KEEP 只表示不运行 `mkfs`；安装仍可能覆盖该挂载点内的文件。当前模型因此强制 `/`、`/var`、`/usr` 使用 FORMAT，而 `/home` 等数据挂载点可以 KEEP。

旧版没有 KEEP/FORMAT 区分。只要分区进入 `file_system_choices`，稍后的 `partition_formatter` 就会重新创建文件系统。

### 3.8 最终破坏性确认与写前复核

完成仓库准备、软件包预解析、Secure Boot 快照和 KEEP 探测后，当前脚本要求用户输入包含 `/boot` 和 `/` 的完整磁盘路径，例如 `/dev/nvme0n1`。输入必须逐字符相同。

确认后立即再次运行完整存储状态复核，缩短“用户确认”和“实际写盘”之间设备被替换或状态变化的窗口。

旧版自动分区的整盘警告是默认 Yes 的普通确认；更重要的是，确认后会立即执行 `disk_wiper` 和 `sfdisk`，此时后续格式化、软件选择和 chroot 配置尚未完成。

### 3.9 分区表处理

当前脚本逐块处理参与磁盘：

- `existing`：完全跳过该盘的 `wipefs` 和 `sfdisk`；
- `auto-root-swap`：EFI 1 GiB + ROOT + 推荐 Swap；
- `auto-home-swap`：EFI 1 GiB + ROOT 100 GiB + HOME + 推荐 Swap；
- `auto-root-only`：EFI 1 GiB + 占满剩余空间的 ROOT；
- `auto-data`：整盘单一 Linux data 分区，默认 FORMAT 为 Ext4，不自动分配挂载点。

引导式磁盘使用 `wipefs --all --force` 和带 `--wipe always --wipe-partitions always` 的 `sfdisk` 重建 GPT。随后调用 `partprobe` 或 `blockdev --rereadpt`，可用时等待 `udevadm settle`，并最多等待 5 秒让每个分区设备出现。

当前脚本不执行 `blkdiscard`。

旧版自动布局只有前三种系统盘布局。旧 `disk_wiper` 对非旋转、支持 discard 的设备会在 `wipefs -a` 后执行整盘 `blkdiscard -f`。旧手动模式可先选择擦除，然后直接启动 `cfdisk`；其他磁盘也可以重复进入这套流程。

两版推荐 Swap 的有效规则相同：

- 内存不超过 8 GiB：Swap 为内存两倍；
- 大于 8 GiB 且不超过 64 GiB：Swap 等于内存；
- 大于 64 GiB：Swap 为 8 GiB。

当前实现读取内存失败时回退到 8 GiB，并在模型验证中检查布局能否容纳所有分区。

### 3.10 FORMAT、挂载和 Swap

当前脚本只对 ACTION 为 FORMAT 的条目运行：

- FAT32：`mkfs.fat -F 32`；
- Ext4：`mkfs.ext4 -F`；
- XFS：`mkfs.xfs -f`；
- F2FS：`mkfs.f2fs -f -O extra_attr,inode_checksum,sb_checksum,compression`；
- Swap：`mkswap --force`。

对 KEEP 条目，格式化阶段再次核对文件系统类型和 UUID，然后跳过 `mkfs`。

分区数组在生成时按以下顺序排列：

1. 根分区 `/`；
2. 其他挂载点按路径字符串排序；
3. Swap；
4. `unused + FORMAT` 的仅格式化分区。

因此多盘挂载也会合并为一个确定的全局顺序，而不是依赖 Bash 关联数组遍历。`unused + FORMAT` 会创建文件系统但不挂载；如果它的目标文件系统是 Swap，也不会启用 Swap。

F2FS 挂载配置在方案中提前确定：

- `default`：不附加专用选项；
- `balanced`：`noatime,lazytime,gc_merge,atgc,nodiscard,fsync_mode=nobarrier`；
- `compressed`：在 balanced 基础上增加 `compress_algorithm=zstd:6,compress_chksum`。

所有普通挂载完成后，脚本才逐个 `swapon`，并把本次启用的设备记入清理数组。

旧版格式化顺序来自关联数组，顺序未定义；挂载顺序硬编码为 `/`、`/boot`、`/usr`、`/var`、`/home`、`/opt`，然后遍历关联数组启用 Swap。旧版 F2FS 挂载参数是在磁盘已经格式化后现场询问。旧界面中的“Optimized mount options(read only)”实际并不是只读挂载，而是当前所称的 balanced 参数。

### 3.11 `pacstrap` 与 `fstab`

当前脚本用以下组装结果执行 `pacstrap -K`：

- `bootstrap`；
- 所选内核及 headers；
- Intel/AMD microcode，虚拟机为空；
- Laptop 模式下的固件组。

本地镜像模式下，Live mirrorlist 此时已经指向临时 nginx。`pacstrap -K` 会把该 mirrorlist 带入目标，同时不会把 Live 为引导 nginx 临时修改过的 `pacman.conf` 复制进去，因此目标使用新安装 pacman 的标准签名策略。

挂载和 `swapon` 已经完成，因此紧接着用一次覆盖重定向写入说明文件头和 `genfstab` 输出：

```bash
{
    printf '%s\n' '# Static information about the filesystems.'
    printf '%s\n\n' '# See fstab(5) for details.'
    printf '%s\n' '# <file system> <dir> <type> <options> <dump> <pass>'
    genfstab -U "$TARGET_ROOT"
} > "$TARGET_ROOT/etc/fstab"
```

`genfstab` 全权负责普通挂载和 Swap 的 fstab 内容。当前脚本不手工追加 Swap，也不使用 `>>` 累积旧内容。

旧版在 pacstrap 之前先把 `genfstab -U /mnt` 打印到终端，要求用户确认；pacstrap 后再用 `genfstab -U /mnt >> /mnt/etc/fstab` 追加。新版取消了这次运行时 fstab 问答，改为在生成脚本和 Review 页面提前审阅方案，并以覆盖方式生成最终文件。

### 3.12 自动 chroot 配置

当前脚本在 `/mnt/root/.arch-install-chroot.sh` 写入一份 `0700` 的临时内层脚本，并明确执行：

```bash
arch-chroot "$TARGET_ROOT" /bin/bash /root/.arch-install-chroot.sh
```

内层脚本按以下顺序运行：

1. `configure_base`；
2. `install_core_packages`；
3. `install_drivers`；
4. `install_desktop`；
5. `install_optional_software`；
6. `mkinitcpio -P`；
7. `configure_bootloader`；
8. `configure_system`；
9. `configure_mirrors`。

除 root 和普通用户密码外，时区、Locale、hostname、username、硬件、桌面、镜像和软件组选项都已写进脚本，不再在 chroot 中重新询问。

本地镜像模式下，内层 pacman 通过 `http://127.0.0.1:2304` 访问 Live 侧 nginx。当前 `arch-chroot` 调用没有创建独立网络命名空间，因此该回环服务可见；目标 `pacman.conf` 没有被安装器改成 `SigLevel = Never`。`configure_mirrors` 在内层流程末尾写入永久镜像，返回 Live 后才停止临时 nginx。

旧版把函数目录、信息文件和 `chroot-setup.sh` 复制到目标，运行 `arch-chroot -S /mnt` 后要求用户手工执行 `./setup.sh`。大量选择在内层脚本中才发生；用户还必须手工退出 chroot，外层脚本才能继续。

### 3.13 目标系统基础配置

两版都完成时区、硬件时钟、`en_US.UTF-8`/`zh_CN.UTF-8` locale、hostname、US 控制台键盘和 pacman 彩色输出，但细节不同：

| 项目 | 当前脚本 | 旧脚本 |
| --- | --- | --- |
| 时区 | TUI 中选择并提前验证 zoneinfo 文件，chroot 再检查一次 | chroot 中循环输入，依赖 `ln -sf` 成败判断 |
| Locale | TUI 预选，生成脚本固定写入 | chroot 中 `select` 询问 |
| hostname | 生成前校验并 Shell 转义 | chroot 中读取任意非受控文本 |
| `/etc/hosts` | 保留发行版标准注释头，并重写完整 localhost、IPv6 localhost 和 `127.0.1.1 hostname` | 只向现有文件追加 `127.0.0.1 hostname` |
| 密码失败 | root 和用户分别最多尝试 3 次 | 无限循环直到 `passwd` 成功 |
| pacman 数据库 | 明确 `--noconfirm` | 重定向输出并依据命令状态判断 |

密码不会写入 JSON 或生成脚本，仍需要在安装 TTY 中输入。

### 3.14 软件包和驱动

当前包名来自 `config/packages.json`，而不是散落在安装函数中。默认内容基本继承旧版：

- bootstrap：base、开发工具、固件、文件系统工具、编辑器和手册；
- core：Zsh、NetworkManager/iwd、DHCP 客户端、UEFI 和签名工具；
- 四种内核分别与 headers 配对；
- Intel/AMD microcode；
- Laptop 固件、TLP，以及 GNOME Laptop 的 `tlp-pd`；
- Intel、NVIDIA、Bluetooth；
- KDE、GNOME、Hyprland、推荐软件和输入法；
- 字体、防火墙、打印、压缩、终端、附加工具和桌面应用。

当前 TUI 在所有会改变 Pacman 包集合的行上支持 Enter 查看实际包名，Space 才修改选项。弹窗直接读取当前 `packages.json`，因此预览内容与生成器数据源一致。

当前 chroot 中的软件安装统一经过 `pacman -S --needed --noconfirm`。旧版 KDE、GNOME 和 Hyprland 的主安装命令没有 `--noconfirm`，可能在中途再次进入 pacman 交互。

NVIDIA 处理也有差别：

- 当前脚本用较宽松的正则重写 `MODULES=`，并按 `kms` 位于 `HOOKS` 开头、中间、末尾或作为唯一项四种位置移除它，不在相邻 hook 之间留下连续空格；
- 旧脚本只替换两条完全匹配的默认文本，Arch 默认配置变化后可能不生效；
- 当前脚本在驱动安装后明确运行 `mkinitcpio -P`；
- 旧版没有显式重新生成全部 initramfs。

### 3.15 桌面、字体和系统服务

KDE、GNOME 和 Hyprland 的默认软件集合总体沿用旧版。当前实现仍会：

- 为 KDE/Hyprland 的中文输入安装 Fcitx5 并写入环境变量；
- 为 GNOME 中文输入安装 IBus；
- 为 Hyprland 写入 greetd、ReGreet 和 Hyprland 登录配置；
- 无论是否选择桌面，都安装字体组并写入 fontconfig；
- 根据方案启用显示管理器、Bluetooth、TLP、firewalld 和 CUPS。

当前 fontconfig 沿用旧版的完整回退顺序和多行格式，显式区分简体中文、繁体中文、日文和韩文字体族，避免中文字符错误回退到日文字形。

系统配置方面：

- NetworkManager 的 iwd 后端两版都通过独立配置文件设置；
- 当前 timesyncd 使用 `/etc/systemd/timesyncd.conf.d/custom.conf`，旧版精确替换主配置中的 `#NTP=`；
- 当前用 `install -d` 幂等创建 coredump/journald drop-in 目录，旧版直接 `mkdir`，目录已存在时可能因 `set -e` 退出；
- 当前没有旧版为展示进度而加入的多处 `sleep`；
- 当前创建用户时优先指定 `/usr/bin/zsh`，该路径不可执行时回退到 `/bin/zsh`，且不再修改 `/etc/default/useradd`；
- 当前写入 `/etc/sudoers.d/10-wheel` 并用 `visudo -cf /etc/sudoers` 非交互校验；旧版等待 10 秒后打开交互式 `visudo`，具体授权内容完全由用户手工完成。

### 3.16 systemd-boot 和 initramfs

两版都使用 `bootctl --no-variables install`，再写入普通和 fallback 两个 loader entry，以及：

```text
default arch.conf
editor no
timeout 3
console-mode keep
```

当前脚本先根据实际根分区读取并检查非空 UUID。Intel/AMD 平台在启动项中加入对应 microcode initrd，虚拟机不加。

两版 fallback entry 都引用普通的 `initramfs-<kernel>.img`，不依赖新版 mkinitcpio 默认不再生成的 `*-fallback.img`。这里的 fallback 是内核参数回退项：用户以后修改普通 `arch.conf` 的内核参数时，可以保留 `arch-fallback.conf` 中安装器生成的已知可用参数作为恢复入口。

### 3.17 Secure Boot 签名

当前 chroot 阶段先通过 `pacman -U` 安装已验证的 `shim-signed` 包并删除临时副本，再安装 systemd-boot、创建目标目录和 `BOOTX64.CSV`；私钥不进入 chroot。chroot 完成后，外层 Live 脚本才执行签名：

1. 在私有工作目录生成签名后的 systemd-boot 和 kernel 临时文件；
2. 用 `sbverify --cert` 确认签名与 MOK 证书匹配；
3. 原子保存原始未签名 kernel 为 `/boot/vmlinuz-<kernel>.bak`；
4. 在每个目标目录旁创建暂存文件；
5. 用原子 `mv -fT` 替换内核和指定 EFI 文件；
6. 安装 shim、MokManager、fallback manager 和公开的 `MOK.cer`；
7. 签名完成后卸载 tmpfs 并删除快照目录。

当前流程不会：

- 把 `MOK.key` 或 `MOK.crt` 复制/绑定到目标；
- 清空 `/boot/EFI/BOOT/` 下的所有已有文件。

旧版与当前流程一样会在 chroot 内通过 `pacman -U` 安装 shim 包，但它还会：

- 把 shim 包和整个 `secure-boot/` 目录复制到目标；
- `rm -f /boot/EFI/BOOT/*`；
- 把原内核移动为 `.bak` 后直接签名回原路径；
- 从目标 `/root/secure-boot/` 使用私钥；
- 在外层清理时不删除 `/root/secure-boot/`，因此私钥会留在安装后的系统。

两版最终都会保留同名的未签名 `.bak` 内核。区别在于旧版先移动原内核再直接签名，新版先完成签名与验证，再原子写入备份和已签名正式内核。

两版都只签名 EFI 可执行文件和内核，不把外置 initramfs 纳入完整验证链；内核或 systemd-boot 更新后的自动重签名也都不在当前实现范围内。当前模型会对此给出警告，并在同时选择 NVIDIA 时提醒 DKMS 模块未自动签名。

Secure Boot 与临时本地镜像在当前实现中可以同时启用。此组合的仓库可信性由用户负责。

### 3.18 EFI NVRAM 启动项

当前方案提前决定是否创建 NVRAM 项。脚本会：

- 用 `lsblk PARTN` 获取 EFI 分区编号；
- 用 PARTUUID 标识 EFI 分区；
- 先读取现有 `efibootmgr -v` 输出；
- 同时匹配 label、PARTUUID 和 loader，避免创建重复项；
- Secure Boot 使用 `\\EFI\\ARCH\\SHIMX64.EFI`，否则使用 systemd-boot 路径；
- 两种模式的 NVRAM label 以及 shim `BOOTX64.CSV` 中的回退注册名称统一为 `Linux Boot Manager`。

旧版在 chroot 中询问是否创建，然后用 `/boot` 设备路径的最后一个字符作为分区号。多位分区号会被截断，且没有重复项检查。旧命令中的主磁盘变量也没有加引号。

### 3.19 成功和失败清理

当前 `cleanup()` 无论正常退出、错误退出还是收到常见终止信号都会运行。它先用 `findmnt` 确认资源身份，拒绝卸载与本次记录不符的“外来挂载”，然后按状态处理：

- 卸载未完成的 KEEP 探测挂载；
- 删除 Secure Boot 暂存文件和临时 chroot 脚本；
- 递归卸载 `/mnt`；
- 只对本次启用且仍活动的 Swap 逆序执行 `swapoff`；
- 停止本次启动的临时 nginx；
- 卸载 Live 侧本地镜像；
- 恢复 Live 环境的 `pacman.conf` 和 mirrorlist；
- 卸载并删除 Secure Boot tmpfs；
- 清理私有工作目录；
- 等待日志进程并报告最终日志路径。

如果清理本身失败，原本成功的安装也会转为失败状态，并尽量保留工作目录中的恢复文件。

旧版成功路径只执行 `umount -R /mnt`。它不会关闭自己启用的 Swap，不会卸载本地镜像，不会停止 nginx，不会恢复 Live pacman 配置，也没有错误退出时的统一回收。任何中途失败都可能留下部分挂载或临时配置。

## 4. `live/` 旧流程的完整时间线

旧入口 [live/setup.sh](../live/setup.sh) 的实际执行顺序如下：

1. `source` 所有 processor、setter 和 actuator 函数；
2. 立即要求 root；
3. 询问是否使用本地镜像，否则通过 `ping baidu.com` 检查网络；
4. 从过滤后的磁盘列表选择一块主盘；
5. 选择自动或手动分区；
6. 自动模式立即执行 `wipefs`、可能的 `blkdiscard` 和 `sfdisk`；手动模式可擦盘并立即运行 `cfdisk`；
7. 手动模式可继续选择其他磁盘并分区；
8. 显示计划表并确认后，格式化关联数组中的全部分区；
9. 按固定挂载点顺序挂载，并启用 Swap；
10. 打印一次 `genfstab` 结果，要求用户确认；
11. 选择 CPU、内核和 Laptop，执行 pacstrap；
12. 用 `genfstab >>` 追加 fstab；
13. 在 `/mnt/info/` 写入内核、microcode、initramfs、root UUID 和 TLP 状态；
14. 把 chroot 函数树、chroot 入口、shim 包和密钥复制进目标；
15. 进入 chroot，用户手工执行 `/setup.sh`；
16. 内层脚本询问时区、Locale、hostname、密码、驱动、桌面、可选软件、Secure Boot、EFI 项、用户名、sudoers 和镜像；
17. 用户手工退出 chroot；
18. 外层读取 `/mnt/info/secure-boot.txt` 和 `efi-variables.txt`；
19. 可选创建 EFI NVRAM 项；
20. 删除 `/mnt/info`、复制的函数树、入口和 shim 包；
21. 递归卸载 `/mnt` 并提示 reboot。

这条时间线说明旧版的“配置完成点”非常靠后：很多系统选择发生在分区和 pacstrap 已经完成之后，无法先生成一份完整方案进行整体审阅。

## 5. 存储能力逐项对照

| 能力 | 当前脚本 | 旧脚本 |
| --- | --- | --- |
| 多盘 | 最多 8 块，每块独立保存模式和身份 | 手动模式可追加其他磁盘，无集中上限模型 |
| 可移动/USB 盘 | 显示并标记，选择时再次确认 | 探测阶段直接过滤 USB 和 removable |
| 自动系统盘 | 三种布局 | 三种布局 |
| 自动数据盘 | 支持单分区、可只格式化不挂载 | 无对应引导式布局 |
| 手动分区表 | TUI 表头可显式启动 `cfdisk` | 手动流程固定启动 `cfdisk` |
| 使用已有分区 | GPT 上支持 KEEP、FORMAT、IGNORE | 选中的分区最终全部格式化 |
| 仅格式化不挂载 | 明确支持 | 可能通过选择文件系统后不选挂载点间接形成，但界面含义不清晰 |
| 根与 EFI | 集中验证恰好各一个且在同一盘 | 主盘流程固定分别选择，但最终只以提示要求用户自行检查重复 |
| 挂载点唯一性 | 模型强制 | 依赖 setter 删除已用选项和最终人工确认 |
| 分区身份 | 编号、父盘、容量、起始扇区、PARTUUID、GPT 类型 | 设备路径 |
| 文件系统身份 | KEEP 保存并复核 UUID | 不存在 KEEP |
| 整盘清除 | `wipefs` + `sfdisk`，不 discard 数据区 | `wipefs`，满足条件时整盘 `blkdiscard` |
| 格式化顺序 | 确定顺序 | Bash 关联数组顺序 |
| 挂载顺序 | 全局路径排序 | 六个挂载点硬编码顺序 |
| Swap 清理 | 记录并 `swapoff` | 安装结束后仍可能保持活动 |

## 6. 软件包和配置的对应关系

| 旧函数 | 当前包组/模板 | 主要变化 |
| --- | --- | --- |
| `basic-software-installer.sh` | `bootstrap`、kernel、platform、laptop firmware | 选择提前进入计划；完整预解析后才写盘 |
| `critical-component-installer.sh` | `core`、`laptop_tools` | 包名移到 JSON；统一安装函数 |
| `extra-driver-installer.sh` | Intel/NVIDIA/Bluetooth 组 | 选择提前；NVIDIA 配置更不依赖默认文件的完整文本 |
| `desktop-environment-installer.sh` | KDE/GNOME/Hyprland、recommended、input、fonts | 选择提前；Pacman 非交互；包组可编辑 |
| `extra-software-installer.sh` | firewall、printer、archive、terminal、extra、desktop apps | 选择提前；TUI 可查看实际包列表 |
| `basic-setter.sh` | `chroot-base.sh` | 时区、Locale、hostname 预先验证；密码重试有上限 |
| `final-setter.sh` | `chroot-system.sh` | 使用 drop-in；自动写 wheel sudoers 并校验 |
| `bootloader-installer.sh` | `chroot-bootloader.sh` + `finish-secure-boot.sh` + `finish-firmware.sh` | 私钥移出 chroot；签名验证和 EFI 身份复核 |

`packages.json` 的数组允许为空，表示用户明确清空该组。所有预期组必须存在，避免缺失字段被静默补成默认值。由于包配置可以被用户修改，本文列出的包名描述的是内建默认值，实际生成结果应以 TUI 的 Enter 包列表或最终脚本数组为准。

## 7. 本地镜像的详细差异

两版都约定标签为 `F2FS-DATA`，仓库目录为 `repo/archlinux`，并用 nginx 把 Live 可见的仓库提供给目标 chroot。`file://` 仅用于 Live 端引导安装 nginx；目标安装流程通过 HTTP 保持标准签名策略。

### 旧版

1. 查找恰好一个同标签分区；
2. 默认读写挂载到 `/run/media/root/F2FS-DATA`；
3. 用精确字符串替换 Live `pacman.conf` 的 SigLevel，仅用于引导安装 nginx；
4. 先从 `file://` 安装 nginx；
5. 修改系统 nginx 配置并启动服务；
6. 把 mirrorlist 改为 `http://127.0.0.1:2304/...`，使 pacstrap 和 chroot 使用 HTTP；
7. 不保存或恢复 Live pacman 配置；
8. 不检查镜像是否位于即将擦除的目标盘；
9. 成功后不卸载镜像、不停止 nginx；
10. 如果 chroot 最后不选择永久镜像，目标 mirrorlist 可能继续指向重启后不存在的 localhost 服务。

### 当前实现

1. 要求恰好一个同标签设备；
2. 验证它是 F2FS、有 UUID、有父磁盘，并记录父盘序列号和容量；
3. 检查其设备祖先，拒绝位于任何参与安装的磁盘上；
4. 检查未挂载、非活动 Swap、无 holder；
5. 要求精确输入设备和 UUID 才执行 nginx 引导；
6. 以 `ro,nodev,nosuid,noexec` 挂载；
7. 备份 Live `pacman.conf` 和 mirrorlist；
8. 临时用 `file://` 和 `SigLevel = Never` 安装 `local_mirror_live` 组中的 nginx；
9. nginx 使用工作目录中的独立配置并只监听 `127.0.0.1:2304`，不修改系统 nginx 配置；
10. nginx 启动后立即恢复 Live 原有签名策略，后续 pacstrap 通过 HTTP 工作；
11. 目标 chroot 通过 localhost HTTP 使用镜像，不修改目标 `pacman.conf`，不建立目标 bind mount；
12. 内层流程结束时写入永久 China mirror，返回 Live 后停止 nginx；
13. EXIT 清理也会停止本次 nginx、恢复 Live pacman 配置并卸载源分区。

当前验证规则不允许“临时本地镜像开启但目标 China mirrors 关闭”的有效方案，这是为了避免目标系统保留重启后不存在的 localhost HTTP 地址；它不限制本地镜像与 Secure Boot 同时使用。

## 8. 哪些习惯被保留，哪些行为被有意改变

### 保留的主要习惯

- Arch Linux Live + UEFI + GPT；
- systemd、systemd-boot；
- 三种系统盘自动布局和原有 Swap 计算规则；
- Ext4、XFS、F2FS、FAT32、Swap 格式化命令；
- F2FS balanced/compressed 参数；
- 四种内核和 headers；
- Intel/AMD microcode；
- NetworkManager + iwd；
- Zsh、wheel 用户、TLP；
- Intel/NVIDIA/Bluetooth 包集合；
- KDE、GNOME、Hyprland 及原有可选软件组；
- 中国镜像、shim/MOK、MOK.cer 和 systemd-boot loader 布局。

### 明确改变的行为

- 配置与执行分离，常规 TUI 不再边问边写盘；
- 允许显式选择 USB/可移动盘，但显示警告；
- 新增多盘统一模型、自动数据盘和 FORMAT-without-mount；
- 新增 KEEP/FORMAT/IGNORE；
- 现有分区要求 GPT，并保存强身份字段；
- 删除自动 `blkdiscard`；
- 删除 `ping baidu.com` 网络判定；
- 保留本地 nginx 镜像架构，但限制为回环监听、独立临时配置并纳入退出清理；
- 软件包全部前置选择并预解析；
- `genfstab` 覆盖生成并全权处理 Swap；
- chroot 改为自动执行；
- 密码重试从无限改为最多三次；
- sudoers 从手工编辑改为生成 drop-in 并验证；
- Secure Boot 私钥不进入目标系统；
- EFI 分区编号不再截取设备名最后一个字符；
- 增加日志、原子输出和失败清理。

## 9. 阅读和维护时应注意的边界

1. `live/` 只是迁移参考。修改其中脚本不会改变 C Builder 的生成结果。
2. 修改 `src/generator/templates/` 后必须重新运行 CMake 构建，模板才会重新嵌入可执行文件。
3. 修改 `config/packages.json` 会改变下一次生成脚本中的包数组，但不会改变已经生成的旧 `install.sh`。
4. 修改 `install-plan.json` 也不会自动更新已有脚本，必须重新执行 generate。
5. 生成脚本带有中英文功能段落注释，可直接审阅；动态数组才是本次方案的最终事实来源。
6. `/dev/...` 仍是执行路径。序列号、型号、容量、PARTUUID 等检查用于降低路径重排风险，但不能代替备份。
7. KEEP 不是只读安装。它只禁止 `mkfs`，挂载后安装步骤仍可修改其中内容。
8. Secure Boot 当前没有覆盖外置 initramfs、NVIDIA DKMS 模块和软件更新后的自动重签名。
9. 任何一套安装流程都不提供事务回滚。当前清理只负责运行时资源和临时配置，不会恢复已经写入的分区表或文件系统。

## 10. 源码定位表

| 主题 | 当前实现 | 旧实现 |
| --- | --- | --- |
| 主执行顺序 | [finish-main.sh](../src/generator/templates/finish-main.sh) | [live/setup.sh](../live/setup.sh) |
| 方案序列化到 Shell | [src/generator/plan.c](../src/generator/plan.c) | 关联数组和 `/mnt/info/*.txt` |
| 硬件探测 | [src/detect.c](../src/detect.c) | [disk-detector.sh](../live/functions/processor/disk-detector.sh) |
| TUI 存储编辑/cfdisk | [src/ui/storage.c](../src/ui/storage.c) | [manual-partitioner.sh](../live/functions/actuator/manual-partitioner.sh) |
| 自动分区 | [runtime-partitioning.sh](../src/generator/templates/runtime-partitioning.sh) | [automatic-partitioner.sh](../live/functions/actuator/automatic-partitioner.sh) |
| 身份和占用复核 | [runtime-storage-validation.sh](../src/generator/templates/runtime-storage-validation.sh) | `mount-detector.sh` 的单项挂载检查 |
| KEEP 探测 | [runtime-keep-probe.sh](../src/generator/templates/runtime-keep-probe.sh) | 无 |
| 格式化和挂载 | [runtime-filesystems.sh](../src/generator/templates/runtime-filesystems.sh) | `partition-formatter.sh` + `mounter.sh` |
| 包源和 pacstrap | [runtime-package-source.sh](../src/generator/templates/runtime-package-source.sh) | `use-local-mirror.sh` + `basic-software-installer.sh` |
| chroot 入口 | [chroot-preamble.sh](../src/generator/templates/chroot-preamble.sh) | [live/chroot-setup.sh](../live/chroot-setup.sh) |
| 基础和驱动 | [chroot-base.sh](../src/generator/templates/chroot-base.sh) | `basic-setter.sh` + `critical-component-installer.sh` + `extra-driver-installer.sh` |
| 桌面和可选软件 | `chroot-desktop.sh` + `chroot-optional-software.sh` | `desktop-environment-installer.sh` + `extra-software-installer.sh` |
| 服务和用户 | [chroot-system.sh](../src/generator/templates/chroot-system.sh) | [final-setter.sh](../live/functions/chroot/final-setter.sh) |
| 引导器 | [chroot-bootloader.sh](../src/generator/templates/chroot-bootloader.sh) | [bootloader-installer.sh](../live/functions/chroot/bootloader-installer.sh) |
| Secure Boot 收尾 | [finish-secure-boot.sh](../src/generator/templates/finish-secure-boot.sh) | `bootloader-installer.sh` 内部完成 |
| EFI NVRAM | [finish-firmware.sh](../src/generator/templates/finish-firmware.sh) | `live/setup.sh` chroot 返回后的末段 |
| 日志和退出清理 | `runtime-logging.sh` + `runtime-cleanup.sh` | 无统一实现 |
