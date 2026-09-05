# Arch Linux Install Script Builder

这是一个用 C17 和 `ncursesw` 编写的 Arch Linux 安装脚本构造器。它在全屏 TTY 界面中探测磁盘、编辑安装方案、检查配置，然后保存一份 JSON 方案并生成可审阅的 Bash 安装脚本。

C 程序是配置前端，不在编辑方案时格式化分区或写入分区表。真正的安装操作由生成的 Shell 脚本完成；用户可以先预览脚本、保存后退出，也可以在 TUI 中选择生成后立即运行。

> [!CAUTION]
> 生成的安装脚本可以执行 `wipefs`、`sfdisk`、`mkfs.*` 和 `mkswap`。引导式整盘方案会清除对应磁盘的现有分区表与全部数据。本项目不备份、不回滚；在真实磁盘上使用前，请先在虚拟机或可丢弃磁盘上验证生成的脚本。

## 当前范围

当前实现面向 UEFI 启动的 Arch Linux Live 环境，目标系统使用 GPT、systemd 和 systemd-boot。构造器目前支持：

- 最多八块参与安装的磁盘，分区按磁盘分组管理；
- 三种系统盘布局、单分区数据盘布局，或复用已有分区；
- Ext4、XFS、F2FS、FAT32 和 Swap；
- `/`、`/boot`、`/home`、`/var`、`/usr`、`/opt` 和 Swap 用途；
- Intel、AMD 和虚拟机平台；
- `linux`、`linux-lts`、`linux-zen` 和 `linux-hardened`；
- KDE Plasma、GNOME、实验性 Hyprland，或不安装桌面；
- Intel 核显、NVIDIA Open DKMS 和蓝牙组件；
- TLP、Firewall、CUPS、中文输入法和若干预设软件组；
- systemd-boot，以及基于 shim/MOK 的可选 Secure Boot 配置；
- 方案 JSON 的保存、重新加载和无 TUI 生成。

TUI 界面只使用英文。这不影响为目标系统选择 `zh_CN.UTF-8` 或安装中文输入法。

## 依赖

构建依赖：

- 支持 C17 的 GCC 或 Clang；
- CMake 3.20 或更高版本；
- `pkg-config`；
- `ncursesw`；
- `json-c`。

在 Arch Linux 上可以安装：

```bash
sudo pacman -S --needed base-devel cmake pkgconf ncurses json-c
```

VS Code 调试配置默认使用 GDB：

```bash
sudo pacman -S --needed gdb
```

运行 TUI 时需要 `lsblk`（由 `util-linux` 提供）；从 Storage 页面启动手动分区器还需要同属 `util-linux` 的 `cfdisk`。生成的脚本面向 Arch Linux Live ISO，并使用该环境中的 `pacstrap`、`arch-chroot`、`sfdisk`、文件系统工具和 systemd-boot 等命令。本地镜像模式直接使用只读的 `F2FS-DATA` 仓库，不需要 nginx。

## 使用 CMake 构建

开发构建：

```bash
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_TESTING=ON \
    -DENABLE_SANITIZERS=ON

cmake --build build --parallel
```

运行测试：

```bash
ctest --test-dir build --output-on-failure
```

发布构建：

```bash
cmake -S . -B build-release \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF

cmake --build build-release --parallel
```

生成的程序位于：

```text
build/arch-install-builder
```

CMake 还提供：

- `ENABLE_SANITIZERS`：在 Debug 构建中启用 AddressSanitizer 和 UndefinedBehaviorSanitizer；
- `ENABLE_WARNINGS_AS_ERRORS`：将编译器警告视为错误；
- `BUILD_TESTING`：构建 CTest 测试。

## VS Code

仓库中的 `.vscode/` 已配置：

- Microsoft CMake Tools 和 C/C++ 扩展推荐；
- `build/` 作为默认构建目录；
- 打开项目时自动运行 CMake configure；
- Debug 构建默认启用测试和 Sanitizer；
- `compile_commands.json` 供 C/C++ 扩展使用；
- GDB 启动配置。

常用操作：

- `Ctrl+Shift+B`：执行 `CMake: build (Debug)`，并自动先配置；
- 命令面板中执行 `Tasks: Run Task` → `CTest: run`；
- `F5`：构建后用 GDB 启动 `build/arch-install-builder`。

## 命令行

直接启动 TUI：

```bash
./build/arch-install-builder
```

首次正常启动时，程序会在当前工作目录创建 `config/`，并写入 `config/packages.json`。方案文件的默认位置为 `config/install-plan.json`，但仍只会在用户保存方案或生成脚本时写入，不会在首次启动时自动创建。默认生成脚本仍为 `install.sh`。如果默认方案文件已存在，程序会在进入 TUI 前自动加载它。

指定方案和输出路径：

```bash
./build/arch-install-builder \
    --plan workstation.json \
    --output workstation-install.sh
```

不启动 TUI，直接验证已有方案并生成脚本：

```bash
./build/arch-install-builder \
    --generate workstation.json \
    --output workstation-install.sh
```

查看全部参数：

```bash
./build/arch-install-builder --help
```

## 软件包配置

`config/packages.json` 是生成脚本的软件包数据源。首次运行时会把程序内置默认值完整写入该文件；之后的脚本生成优先使用文件中的列表。

配置结构如下（仅为节选，首次运行会生成完整文件）：

```json
{
  "version": 1,
  "groups": {
    "bootstrap": ["base", "base-devel"],
    "core": ["zsh", "networkmanager"],
    "kernel_linux": ["linux", "linux-headers"]
  }
}
```

实际生成的文件还包含其他内核、CPU 平台、Laptop、驱动、桌面、输入法、字体、Secure Boot 和可选软件组。所有预期字段都必须存在且为字符串数组；数组可以为空，以表示用户有意不安装该组。

如果文件缺少字段、版本不支持、包名无效或 JSON 损坏，程序会在进入 TUI 前报错，并询问是否使用内置默认值覆盖。在非交互环境下不会自动覆盖。

## TUI 流程

界面需要至少 `80x24` 的终端。主页将方案拆成六个可随时返回修改的章节：

1. `Storage`：参与安装的磁盘、各盘布局、文件系统和挂载用途；
2. `Base system`：CPU 平台、内核、设备类型、Locale 和镜像；
3. `Hardware`：Intel GPU、NVIDIA 和蓝牙；
4. `Desktop & software`：桌面环境和可选软件组；
5. `Identity & boot`：hostname、username、timezone、systemd-boot 和 Secure Boot；
6. `Review & output`：验证、预览、生成或生成后运行。

全局按键：

| 按键 | 作用 |
| --- | --- |
| `↑` / `↓` | 移动选中项 |
| `Enter` / `Space` | 打开、切换或编辑当前项 |
| `Esc` | 返回上一层 |
| `F2` | 保存方案 JSON |
| `F5` | 进入审阅页 |
| `F10` | 退出；有未保存修改时要求确认 |

### 存储页

| 按键 | 作用 |
| --- | --- |
| `↑` / `↓` | 在所有已添加磁盘及其分区之间连续移动，跨过组边界时自动切换当前磁盘 |
| `D` | 随时添加磁盘；已添加的磁盘会标记为 `ADDED`，不会借此切换当前磁盘 |
| `R` | 随时重新调用 `lsblk` 刷新设备列表 |
| `X` | 选中磁盘表头时，从方案中移除该磁盘，不修改真实设备 |
| `A` | 选中磁盘表头时，为该磁盘选择引导式整盘布局 |
| `E` | 选中磁盘表头时，重新读取该磁盘的现有分区 |
| `C` | 选中非自动布局的磁盘表头时，以 root 身份启动 `cfdisk`；确认后直接修改磁盘，退出时自动刷新分区列表 |
| `U` / `Space` | 选中分区时打开用途选择框，指定 `/`、`/boot`、`/home`、`/var`、`/usr`、`/opt`、Swap 或忽略 |
| `F` / `Enter` | 选中分区时选择 `KEEP` 或 `FORMAT` 及文件系统；未分配挂载点时也可格式化 |
| `O` | 选中分区时打开 F2FS 挂载配置选择框 |

引导式布局包括：

1. EFI 1 GiB + ROOT + 推荐 Swap；
2. EFI 1 GiB + ROOT 100 GiB + HOME + 推荐 Swap；
3. EFI 1 GiB + ROOT，不创建 Swap。
4. 使用整个磁盘创建一个数据分区，默认格式化为 Ext4，但不分配挂载点。

推荐 Swap 依照旧安装脚本的习惯计算：8 GiB 及以下内存使用两倍内存，大于 8 GiB 且不超过 64 GiB 时使用与内存相同的容量，超过 64 GiB 则使用 8 GiB。

### 现有分区

选择 `Use existing partitions` 后，所有分区初始都是 `unused` + `KEEP`。先为分区指定用途，再决定是否格式化：

- `KEEP`：不重新创建文件系统，执行时会再次核对类型和文件系统 UUID；安装过程仍可能在该挂载点内新增或覆盖文件；
- `FORMAT`：生成脚本会对该分区创建新文件系统；
- `IGNORE`：不格式化、不挂载。

`unused` 与 `FORMAT` 可以同时存在，表示只创建或格式化文件系统，不挂载、不写入 fstab。多个磁盘的挂载点会合并为一棵目标文件系统树；`/boot` 和 `/` 必须位于同一块磁盘上。

只有当前模型能识别的 vfat、Ext4、XFS、F2FS 和 Swap 可用于 `KEEP`。构造器不会在 TUI 中创建任意手动分区表，也不会调整已有分区大小。
新安装中 `/`、`/var` 和 `/usr` 必须选择 `FORMAT`，避免把旧系统的包数据库或二进制文件混入新系统。

F2FS 可选择：

- `default`：使用默认挂载参数；
- `balanced`：使用 `noatime,lazytime,gc_merge,atgc,nodiscard,fsync_mode=nobarrier`；
- `compressed`：在 balanced 的基础上增加 `compress_algorithm=zstd:6,compress_chksum`。

### 审阅页

审阅页会显示阻断错误、警告和将要使用的分区操作。当前验证包括：

- 已选择磁盘和分区方案；
- 恰好一个 `/` 和一个 EFI `/boot`；
- `/` 和 `/boot` 位于同一块磁盘；
- 每个挂载用途不重复；
- `/boot` 使用 FAT32/vfat；
- Swap 用途和文件系统一致；
- `KEEP` 分区的文件系统可识别；
- hostname、username 和 timezone 格式有效；
- 100 GiB ROOT + HOME 布局有足够空间。

有阻断错误时不能生成脚本。Secure Boot 和可移动磁盘目前作为明确警告显示。

审阅页按键：

| 按键 | 作用 |
| --- | --- |
| `P` | 生成并进入 Shell 预览 |
| `G` | 保存 JSON 并生成 Shell |
| `X` | 生成，确认后退出 ncurses 并用 Bash 运行 |

Shell 预览页可用方向键、`Page Up` 和 `Page Down` 滚动，`G` 重新生成，`Esc` 返回审阅页。

## 方案 JSON

JSON 是 TUI 和 Shell 生成器之间的配置交换格式。当前格式版本为 `3`，顶层包含：

```text
version
storage
  disks[]
    disk, model, serial, size_bytes, partition_table
    removable, read_only, in_use_when_detected, mode
    partitions[]
system
  platform, kernel, locale, desktop
  timezone, hostname, username
  hardware, software, mirror and boot switches
```

每块磁盘独立保存身份、分区模式和分区数组。每个分区保存设备路径、编号、容量、起始扇区、文件系统 UUID、PARTUUID、GPT 类型、当前文件系统、是否为引导式新分区、用途、`KEEP/FORMAT`、目标文件系统和 F2FS 配置。

枚举在 JSON 中使用数字值保存，因此建议使用 TUI 编辑，而不是手工修改。加载后生成脚本前仍会执行方案验证。不支持的 `version` 会被拒绝。

方案文件不保存 root 密码、普通用户密码、MOK 私钥或 shim 软件包内容。

## 生成脚本的行为

生成器会将所有字符串值经过 Shell 引号处理，并将输出脚本权限设为 `0750`。脚本使用 `set -Eeuo pipefail`，安装日志默认位于：

```text
/tmp/arch-install.XXXXXX.log
```

也可以在运行前设置 `ARCH_INSTALL_LOG` 指定位置；为避免覆盖或符号链接攻击，该路径必须尚不存在。

执行流程为：

1. 检查 root、UEFI、命令依赖和 EFI variables；
2. 核对每个参与安装的目标都是整块磁盘，并比较容量、型号、非空序列号与 GPT 类型；
3. 核对现有分区的父磁盘、编号、起始扇区、容量、PARTUUID 和 GPT 类型；`KEEP` 还会核对文件系统 UUID 并做只读挂载探测；
4. 显示存储表；网络源要求输入 `PREPARE`，本地源要求精确输入设备和 UUID，然后才刷新 Live 包数据库并预先解析完整软件包集；
5. 软件源就绪后，要求从交互式终端输入包含 `/boot` 和 `/` 的磁盘路径，然后立即再做一次所有参与磁盘的身份核对；
6. 分别在选择了引导式布局的磁盘上重建 GPT 并核对新分区；现有分区模式不写对应磁盘的分区表；
7. 只格式化 `FORMAT`，随后按挂载路径顺序挂载到 `/mnt` 并启用指定 Swap；
8. 运行 `pacstrap`，并只为 `/mnt` 树和方案中的 Swap 生成 UUID fstab；
9. 生成临时 chroot 脚本并用 `arch-chroot` 自动运行；
10. 安装软件、写入系统配置、运行 `mkinitcpio -P`、安装 systemd-boot；
11. 创建 wheel 用户和经 `visudo -cf` 验证的 sudoers drop-in；
12. 可选创建 EFI NVRAM 启动项；
13. 删除临时脚本并同步数据。

脚本在退出时会递归卸载 `/mnt`、关闭它自己启用的 Swap、卸载临时本地仓库挂载，并恢复 Live 环境的 pacman 配置。任一关键清理失败都会使脚本以失败退出，并保留带备份的临时目录供手工恢复。

## 生成和运行

推荐先生成并审阅：

```bash
./build/arch-install-builder \
    --generate config/install-plan.json \
    --output install.sh

less install.sh
```

在 Arch Linux Live 环境中由 root 运行：

```bash
./install.sh
```

如果不是 root：

```bash
sudo ./install.sh
```

> [!IMPORTANT]
> 生成的脚本会在预检查和输出方案后，要求输入包含 `/boot` 和 `/` 的完整磁盘路径（例如 `/dev/nvme0n1`）。输入完全一致后才会进入分区和格式化阶段；该检查需要交互式 TTY。

TUI 中的 `X` 会先显示一次默认为 `No` 的运行确认，然后使用当前身份运行 `/usr/bin/bash <output>`，不会自动调用 `sudo`。因此要从 `X` 直接开始安装，应在 Arch Live 的 root shell 中启动构造器。

root 和普通用户密码在 chroot 安装过程中通过 TTY 设置，不会出现在 Shell 或 JSON 中。

## 软件安装习惯

新构造器保留了 `live/` 旧安装脚本的主要选择；以下软件包组的实际内容由 `config/packages.json` 提供：

- Intel 安装 `intel-ucode`，AMD 安装 `amd-ucode`，虚拟机不安装 microcode；
- 内核与对应 headers 成对安装；
- Laptop 安装 `sof-firmware` 和 TLP；
- 网络使用 NetworkManager 和 iwd；
- 新用户加入 wheel 组并使用 Zsh；
- Intel 图形选项安装 `vulkan-intel`、`intel-media-driver` 和 `intel-gpu-tools`；
- NVIDIA 选项安装 `nvidia-open-dkms`、`nvidia-utils` 和 `vdpauinfo`；
- 蓝牙选项安装 BlueZ 并启用服务；
- KDE、GNOME 可选安装各自的推荐软件；
- Hyprland 仍是实验选项，提供基础 Wayland、PipeWire、greetd/ReGreet 和 Thunar 组件；
- 字体预设包含 Noto、Sarasa Gothic、JetBrains Mono、DejaVu 和 Nerd Font Symbols；
- 额外软件以 Archive tools、Terminal tools、Additional tools 和 Desktop applications 四组选择。

Hyprland 选项只安装基础组件和图形登录环境，不为普通用户生成完整的 Hyprland/Waybar 个人配置。

## 存储安全模型

构造阶段和执行阶段有明确边界：

```text
lsblk read-only detection
        ↓
ncurses plan editor
        ↓
validation
        ↓
config/install-plan.json + config/packages.json + install.sh
        ↓
explicit execution
```

当前 TUI 中的磁盘探测只运行 `lsblk --json`。选择引导式布局只会改变内存中的方案；界面上的整盘警告表示未来执行 Shell 时的行为。

使用现有分区时，`KEEP`、`FORMAT` 和 `IGNORE` 在审阅页中分开显示。默认安全动作是 `KEEP`；只有用户显式切换为 `FORMAT` 的现有分区才会出现在格式化命令中。

执行脚本会逐块核对磁盘容量、型号和非空序列号，并检查现有分区仍属于各自磁盘。它还会拒绝已挂载的目标节点、活动 Swap 和有 holder 的设备，并在写入前要求手工输入包含 `/boot` 和 `/` 的磁盘路径。

仍需注意：

- JSON 中保存的仍是 `/dev/...` 路径，而不是完整的 `/dev/disk/by-id` 身份模型；
- 没有序列号的磁盘只能依靠路径、容量和可用型号核对；
- 插拔或重排磁盘后，应重新打开 TUI 检查方案；
- 可移动或 USB 磁盘不会被静默过滤，选择时会显示警告并要求二次确认；
- 当前方案验证不等同于备份或回滚机制；
- 安装前仍应逐行审阅生成的存储命令。

### 本地镜像的恢复

本地镜像模式要求恰好一个标签为 `F2FS-DATA` 的分区，其中包含 `repo/archlinux`。脚本会拒绝使用任意参与安装磁盘上的分区作为本地镜像。

确认文本必须精确输入 `UNSIGNED <device> <UUID>`。脚本会确认该分区是目标盘之外、未使用的 F2FS，然后以 `ro,nodev,nosuid,noexec` 挂载。`pacstrap` 完成且 `genfstab` 生成后，仓库目录才会临时 bind 到目标系统的 `/var/cache/arch-install-repo`，供 chroot 内的 `file://` 源使用；该挂载不会写入 fstab。这个兼容模式会临时设置 `SigLevel = Never`，因此仓库内容将被视为可以以 root 权限安装的可信输入。成功后目标系统改用选定的永久镜像。

## Secure Boot

Secure Boot 选项使用 shim + MOK。生成的脚本默认从自己所在目录下的 `live/` 查找材料：

```text
output-directory/
├── install.sh
└── live/
    ├── shim-signed.pkg.tar.zst
    └── secure-boot/
        ├── MOK.key
        ├── MOK.crt
        └── MOK.cer
```

- `shim-signed.pkg.tar.zst`：预先构建的 `shim-signed` 软件包；
- `MOK.key`：供 `sbsign` 使用的 PEM 私钥；
- `MOK.crt`：PEM 证书；
- `MOK.cer`：同一证书的 DER 版本，供 MokManager 或 `mokutil` 注册。

例如，使用默认的仓库根目录 `install.sh` 时，可以直接沿用仓库中的 `live/` 材料布局：

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

`shim-signed` 来自 AUR，应在一套已安装的 Arch Linux 中以普通用户运行 `makepkg`，然后将生成的包重命名为 `shim-signed.pkg.tar.zst` 并放入材料目录。

如果材料不在 `<script-directory>/live/`，可在运行前通过 `ARCH_INSTALL_ASSET_DIR` 指定：

```bash
sudo ARCH_INSTALL_ASSET_DIR=/path/to/assets ./install.sh
```

> [!IMPORTANT]
> `MOK.key` 是可以签署启动代码的私钥。不要将 `secure-boot/`、shim 软件包或其中的密钥提交到 Git，也不要将它们嵌入方案 JSON。

材料目录、shim 包和三个 MOK 文件必须是真实文件，不接受符号链接。开启后，脚本会从当前选定的软件源安装 `sbsigntools`，在私有 tmpfs 中校验并仅解包 shim 包中所需的 EFI 文件，不安装该 AUR 包、也不执行其 hook。完成 chroot 配置后再由 Live 环境签名。`MOK.key` 不会被复制或 bind 到目标文件系统；最终只有公开的 `MOK.cer` 会被复制到 EFI 分区。首次启动仍需在 MokManager 中手工注册证书。

Secure Boot 可以与临时本地镜像同时启用。本地镜像模式会临时关闭 pacman 包签名校验，其仓库内容与 `sbsigntools` 的可信性由用户负责。

该模式签名 EFI 可执行文件和内核，但 systemd-boot 配置中的外置 initramfs 未被这条 shim/MOK 链验证；需要完整验证启动时应改用 UKI，当前不在范围内。内核或 systemd-boot 更新后会覆盖已签署的文件，需要使用在安全外部位置保留的 MOK 重新签署。NVIDIA DKMS 模块签名也不在当前构造器的配置范围内。

## 项目结构

```text
.
├── CMakeLists.txt
├── include/                 # 数据模型和模块接口
├── src/
│   ├── main.c                 # CLI 与入口
│   ├── detect.c               # lsblk JSON 探测
│   ├── model.c                # 方案、验证和 JSON
│   ├── packages.c             # 软件包默认值与 packages.json
│   ├── ui.c                   # ncursesw TUI
│   ├── generator.c            # Bash 生成器
│   └── util.c                 # 子进程、验证与 Shell 转义
├── tests/                   # CTest 模型与生成器测试
├── .vscode/                 # CMake 构建和 GDB 调试配置
├── config/                  # 运行时方案与软件包配置
└── live/                    # 旧 Bash 安装实现，仅供参考
```

## `live/` 目录

`live/` 保留了重构前的线性 Bash 安装器，用于对照软件包、系统配置和安装习惯。它不是新架构的入口，不读取 `install-plan.json`，也不与 C 构造器共享运行状态。

旧实现在交互过程中就可能修改磁盘，而且不具备新方案模型的 `KEEP/FORMAT/IGNORE` 边界和集中验证。请将它视为迁移参考，不要把两套流程混合在同一次安装中。

## 已知限制

- 只支持 UEFI + GPT + systemd-boot；
- 最多允许八块磁盘参与同一安装方案；
- 不支持加密、LVM、RAID 和 Btrfs 格式化；
- 不提供任意分区表编辑器或分区缩放；
- 现有文件系统只识别 vfat、Ext4、XFS、F2FS 和 Swap；
- 方案中仍保存内核动态的 `/dev/...` 名称，磁盘变动后需要人工重新核对；
- Secure Boot 需要用户自行保护和注册 MOK，更新后的重签名未自动化；
- Hyprland 只提供基础软件和 greetd/ReGreet，不包含完整用户配置；
- 脚本仍需要在 TTY 中设置 root 和普通用户密码，不是完全无人值守安装器。

## License

本项目使用 [MIT License](LICENSE)。
