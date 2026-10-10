# OTA 固件升级指南

[English](ota-guide.md) | **中文**

ZZK-SA2 固件集成了 Nordic 官方 Secure DFU (Device Firmware Update) 蓝牙空中升级服务。在完成初次有线烧录后，你可以完全脱离 USB 线缆，直接通过电脑终端或手机 App 对设备进行无线固件升级。

---

## 1. 关键前置准备：升级 Bootloader !!

> [!WARNING]
> **Seeed XIAO BLE 原厂 Bootloader 存在缺陷**  
> Seeed Studio 出厂为 XIAO nRF52840 板卡烧录的早期 Bootloader 存在 Bug：在通过 BLE OTA 接收固件数据包时，会导致芯片死锁并丢失原有用户程序，此时必须重新连接 USB 有线烧录。关于此缺陷的详细成因与社区复现记录，请参考 [Seeed 官方论坛讨论](https://forum.seeedstudio.com/t/xiao-ble-sense-ota-dfu-from-arduino-ide-code/292567)。
> **在首次尝试 BLE OTA 升级前，请先刷入社区修复版 Bootloader.**

### 步骤 A：下载修复版 Bootloader
前往社区修复项目 [oltaco/Adafruit_nRF52_Bootloader_OTAFIX Releases](https://github.com/oltaco/Adafruit_nRF52_Bootloader_OTAFIX/releases) 下载最新修复固件：
- 文件名：`update-xiao_nrf52840_ble_sense_bootloader-0.9.2-OTAFIX2.3-BP1.4_nosd.uf2`

> [!IMPORTANT]
> **注意**：哪怕你的板卡是普通版 XIAO BLE（无板载 IMU / 麦克风），**可能也要下载带 `_sense_` 名称的文件**。具体以下一步中弹出的虚拟 U 盘名称确定。拷贝不匹配的文件板卡会拒绝升级。

### 步骤 B：刷入 UF2 修复文件
1. 使用 Type-C 数据线将 XIAO nRF52840 连接到电脑。
2. 快速**双击**板载 Reset 按钮。
3. 电脑会弹出一个名为 `XIAO-SENSE` 的虚拟 U 盘。
4. 将下载的 `.uf2` 文件直接拖拽进该 U 盘根目录中。
5.  拷贝完成后虚拟 U 盘自动弹出并重启，板载指示灯**快速闪烁红色**，Bootloader 升级完成。

> [!IMPORTANT]
> **注意**：如果板载指示灯没有闪烁红色，则升级可能并未进行。要确定升级是否完成，一种可行的方案是打开虚拟 U 盘下的 `INFO_UF2.TXT` 文件，查看该文件中的日期是不是新下载 uf2 固件的发布日期。

### 步骤 C：烧录基底固件
Bootloader 升级完成后，通过 USB 线缆执行一次常规有线烧录，将当前包含 `BLEDfu` 服务的最新固件写入芯片：

```bash
pio run -t upload
```

此后即可拔掉 USB 线，后续所有开发与迭代均可采用无线 OTA。

---

## 2. 固件包生成

每次执行 PlatformIO 编译：

```bash
pio run
```

构建系统不仅会生成本地二进制镜像，还会在以下路径自动生成符合 Nordic DFU 规范的标准升级包：
```text
.pio/build/xiaoble_adafruit/firmware.zip
```

---

## 3. 升级方式一：PC 终端一键无线烧录

项目根目录下提供了一个跨平台自动化烧录脚本 [`ota_flash.py`](../ota_flash.py)。

### 运行环境
依赖本地 Python 蓝牙低功耗库 `bleak`：

```bash
pip install bleak
```
脚本支持 Linux、Windows 10/11 以及 macOS 原生自带蓝牙适配器，无需额外的专用蓝牙抓包器或硬件 Dongle.

### 执行烧录
在工程根目录下运行：

```bash
python3 ota_flash.py
```

### 自动化工作流程
1. **设备发现**：自动扫描周围广播，根据设备名锁定 `ZZK-SA2`。
2. **引导切换**：连接设备并向 DFU 控制特征值写入触发指令，命令设备软重启进入 Bootloader DFU 引导模式。
3. **捕获 DFU 广播**：设备重启后广播名称变为 `XIAO_DFU`，脚本自动重连。
4. **包传输与校验**：分包推送 `firmware.zip` 中的 Init Packet 与 Firmware Image，实时显示传输进度。
5. **自动重启**：固件写入完成后芯片自动重启进入新版固件。全程约耗时 30 ~ 60 秒。

> [!TIP]
> 如果设备当前已经处于 `XIAO_DFU` 引导状态，直接运行脚本同样能自动识别并完成烧录。

---

## 4. 升级方式二：手机 App 升级

若不想使用电脑终端，也可以使用手机端 Nordic 官方工具进行维护升级。

### 工具准备
- Android / iOS 手机安装 Nordic 官方 App：**nRF Connect for Mobile** 或 **nRF Device Firmware Update (DFU)**。
- 将编译生成的 `.pio/build/xiaoble_adafruit/firmware.zip` 发送到手机存储中。

### 操作步骤
1. 打开手机蓝牙与 **nRF Connect** App。
2. 在扫描列表中找到 `ZZK-SA2` 并点击 **Connect**。
3. 连接成功后，点击右上角的 **DFU** 图标。
4. 文件类型选择 **Distribution packet (ZIP)**，从手机文件管理器中选中传入的 `firmware.zip`。
5. 点击 **OK** / **Start** 开始升级。

>  [!IMPORTANT]
> 进行到这里后，设备会重启并闪烁红灯（进入升级模式），但是升级可能**并不会正常进行**。你会看到 DFU 进度卡在上传固件包这个步骤或者直接报错。
>
> 这是正常的。因为 XIAO BLE 在进入 DFU 模式后蓝牙名称和 MAC 地址都会发生改变，导致 App 无法找到进入升级模式的设备。如果发生了这个问题，你需要退出当前页面，重新扫描蓝牙设备，找到名叫 `XIAO_DFU` 的设备，连接它，然后重新执行一次 DFU 流程，即可完成烧录。

6. 进度走完后设备自动重启，升级完成。
