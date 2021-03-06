## 1.BLE控制指令
本部分描述了实现BLE模块的蓝牙通信以及透传功能的全部AT指令。

> 注意：固件在运行时可能会向用户发送一些固定格式的数据，称之为事件。下文中某些指令执行后可能会接收到相关事件。具体信息，请关注事件说明。

|序号 | 指令                          | 功能                                                |
|:---:|:---------------------------- |:--------------------------------------------------- |
|1    |[AT+LENAME](#atlename)        | 查询/设置BLE蓝牙设备名称                              |
|2    |[AT+LEMAC](#atlemac)          | 查询BLE蓝牙设备地址                                  |
|3    |[AT+LESTATE](#atlestate)      | 查询BLE蓝牙设备的运行时状态                           |
|4    |[AT+LEWLNAME](#atlewlname)    | 设置目标BLE蓝牙设备名（作为主机扫描时可以通过此名称过滤）|
|5    |[AT+LEADV](#atleadv)          | 设置当前BLE蓝牙设备为从机模式并开启广播（only从机）     |
|6    |[AT+LESCAN](#atlescan)        | 设置当前BLE蓝牙设备为主机模式并开启扫描（only主机）     |
|4    |[AT+LESEND](#atlesend)        | 通过BLE蓝牙设备发送数据包（主机or从机）                |
|5    |[AT+LECONN](#atleconn)        | 与BLE蓝牙从机设备建立连接（only主机）                  |
|6    |[AT+LEDISCONN](#atledisconn)  | 与已经连接的BLE蓝牙设备断开（主机or从机）              |

### AT+LENAME
功能：查询/设置 BLE蓝牙设备名称

|查询指令|`AT+LENAME=?`     |
|:-----:|:---------------- |
|响应   | `+LENAME=<name>` |
|参数   | 请参考设置指令    |


|设置指令 | `AT+LENAME=<name>` |
|:------:|:------------------|
|响应    | `OK`               |
|参数    | `name`  BLE设备名称，字符串长度不应该超过31字节 |

### AT+LEMAC
功能：查询 BLE蓝牙设备地址

|查询指令|`AT+LEMAC=?`|
|:------:|:-----------|
|响应   | `+LEMAC=<addr>` |
|参数   | `addr`  蓝牙设备地址，比如：`11:22:33:44:55:66` |

### AT+LESTATE
功能：查询 BLE蓝牙设备运行时状态

|查询指令|`AT+LESTATE?`|
|:------:|:------------|
|响应   | `+LESTATE=<state>` |
|参数   | `state` 当前设备运行时状态。主要取值：|
|      | -- `ADV` 广播状态 |
|      | -- `SCAN` 扫描状态 |
|      | -- `CONN` 连接状态 |
|      | -- `CONNING` 正在连接状态 |

### AT+LEWLNAME
功能：设置/获取 目标BLE蓝牙设备名
> 说明： 用户设置此参数后，当设备处于主机模式扫描设备时，可以过滤掉所有设备名与此参数不一致的设备。如果没有设置此参数，那么蓝牙设备会将所有扫描到的设备通知到用户。

|查询指令|`AT+LEWLNAME=?`|
|:------:|:--------------|
|响应   | `+LEWLNAME=<name>` |
|参数   | `name` 目标设备名称 |
|说明   | 出厂默认为空        |

|设置指令|`AT+LEWLNAME=<name>` |
|:------:|:--------------------|
|响应   | `OK`                |
|参数   | `name` 目标设备名称  |

### AT+LEADV
功能：设置 BLE蓝牙设备为从机模式并开启广播
> 说明： 进入从机模式后自动广播，除非连接到其它设备或者切换到主机模式才能停止广播。需要注意的是，当蓝牙设备处于主机模式并与其它设备保持连接状态，那么此命令无法生效，必须首先断开连接。

|设置指令|`AT+LEADV`|
|:------:|:---------|
|响应   | `OK`     |
|事件   | `+LEADV:ON` |
|说明   |此命令如果执行成功将会有`LEADV`事件返回 |

### AT+LESCAN
功能：设置 BLE蓝牙设备为主机模式并开启扫描（10s后停止扫描）。
> 说明：当设备停止扫描后，用户可以再发送此命令开启扫描。每次调用此命令，蓝牙设备扫描10s就停止。需要注意的是，当蓝牙设备处于从机模式并与其它设备保持连接状态时，那么此命令无效，必须首先断开连接。

|设置指令|`AT+LESCAN`|
|:------:|:---------|
|响应   | `OK`     |
|事件   | `+LESCAN:ON` 扫描开始 |
|      | `+LESCAN:OFF` 扫描结束 |
|说明   |此命令如果执行成功将会有`LESCAN`事件返回 |

### AT+LECONN
功能：连接 已扫描到的蓝牙设备。
> 注意：使用此命令时必须处于主机模式。因此，在发送此命令之前，必须首先执行`AT+LESCAN`指令。

|设置指令|`AT+LECONN=<addr>`|
|:------:|:---------|
|响应   | `OK`     |
|参数   | `addr` 目标设备的地址，一般是`LEREPORT`事件提供的设备地址 |
|事件   | `+LESCONN:ON,<addr>,<connection_handle>` 连接成功 |
|      | `+LESCONN:OFF` 连接失败 |
|说明   |此命令如果执行成功将会有`LESCONN`事件返回 |

### AT+LEDISCONN
功能：断开 已连接的蓝牙设备
>注意：此命令在主机模式和从机模式都可以使用，将会断开相应的连接。

|设置指令|`AT+LECONN=<connection_handle>`|
|:------:|:---------|
|响应   | `OK`     |
|参数   | `connection_handle` 当前连接HANDLE。|
|      |一般是`LESCONN`或者`LEPCONN`事件提供的HANDLE |
|事件   | `+LEPCONN:OFF` 从机连接断开 |
|      | `+LESCONN:OFF` 主机连接断开 |

### AT+LESEND
功能：发送 数据
>注意：此命令在住主机模式和从机模式都可以使用，将会向相应已连接的设备发送数据。

|执行指令：|`AT+LESEND=<length>`|
|:---|:---|
|响应：|`>`|
|参数：|`length`：将要发送的数据长度|
|说明：|当用户收到`>`响应时，应该立即将指定长度的数据通过串口发送。|
|     | 蓝牙设备将会接收并将这些数据透传到已连接的目标蓝牙设备|
|注意：|设备内部在返回`>`响应后，会在规定时间内等待用户数据。|
|     |如果已经超时，那么设备将只发送已经收到的数据。超时时间一般为6s。|

## 2.BLE事件
本部分描述了BLE设备运行时的所有事件类型以及参数。
>说明：以下列表中`<ON/OFF>`参数，如果未有特别说明，`ON`表示功能开启，`OFF`表示关闭。

| 序号 | 事件                | 参数 |功能 | 触发条件 |
|:---:|:--------------------|:--------|:-------|:-------|
|1    | `+LEADV:<ON/OFF>`   | `<ON/OFF>` 开启/关闭 | 当前设备的广播状态发生改变。 | `AT+LEADV`和`AT+LESCAN`指令，或者当前从设备被连接时 |
|2    | `+LESCAN:<ON/OFF>`  | `<ON/OFF>` 开启/关闭 | 当前设备的扫描状态发生改变。 | `AT+LESCAN`和`AT+LEADV`指令，或者设备扫描10s后停止 |
|3    | `+LEPCONN:<ON/OFF>,[addr],[handle]` | `addr` 已连接设备地址；`handle` 连接ID | 当前设备作为从机已连接成功或断开 | 远程主机与当前设备建立连接或断开 |
|4    | `+LESCONN:<ON/OFF>,[addr],[handle]` | 同上 | 当前设备作为主机已连接成功或断开 | `AT+LECONN`指令执行成功或断开 |
|5    | `+LEDATA:<length>,xxxx` | `length` 收到数据的长度；后面紧接length字节的数据 | 收到已连接设备发送的数据 | 远程已连接设备发送数据成功 |
|6    | `+LEREPORT:<name>,<addr>,<rssi>` | `name` 设备名；`addr` 设备地址；`rssi` 设备信号强度 | 已扫描一个设备 | 扫描到符合要求的设备 |


## 3.示例

### a. 蓝牙从机演示

* 烧录最新固件
* 将模块用户串口引出到PC（通过串口转接板）或其他MCU的串口端（9600,8N1,No Parity,No flow-control）
* 上电后，模块默认进入BLE从机模式，并自动开启广播；或者向用户串口发送`AT+LEADV`指令以开启广播。（此时可以看到串口端收到`+LEINIT`以及`+LEADV`事件）。
* 用户手机打开相关蓝牙APP（比如iOS平台lightblue），扫描名称为MXCHIP_BT123456的设备，并与之建立连接。此时应该可以看到`+LEPCONN`事件。
* 手机APP端可以看到一个UUID为`{7163edd8-e338-3691-324d-4b3f8a21675e}`的service以及`{e2aebab4-3521-7032-7821-1d24903e3945}`的可写characteristic和`{30f31dba-5227-4391-2a12-2e825e1a1532}`的可订阅characteristic。
* 手机APP只要向可写characteristic写入数据（最长20字节），蓝牙模块的用户串口即可收到`+LEDATA`事件，相关数据也在此事件的参数中。
* 可以向模块发送`AT+LESEND=<length>`指令发送数据。（注意，前提是手机端首先需要订阅可订阅characteristic）

### b. 蓝牙主机演示

* 模块首先断开从机连接。（可以通过手机断开或者向模块发送`AT+LEDISCONN=<handle>`指令。
* 向模块发送`AT+LESCAN`指令，此时可以收到事件`+LESCAN:ON`。
* 蓝牙模块在扫描过程中，会将扫描到的设备通过`+LEREPORT`事件发送给用户。
* 等待模块完成扫描（一般时间为5s，可以设置）后，应该可以收到`+LESCAN:OFF`事件。如果这个过程中没有收到`+LEREPORT`事件，那么可以重新发送`AT+LESCAN`指令，重新扫描。
* 根据`+LEREPORT`事件中提供的设备名以及地址，用户确认需要连接的设备，并发送指令`AT+LECONN=<addr>`指令。参数就是需要连接设备的地址。
* 如果连接成功，将收到`+LESCONN`事件。
* 可以发送`AT+LESEND=<length>`指令向已连接设备（蓝牙打印机）发送数据。

### c. 蓝牙主从模式的切换
蓝牙主从机模式的切换时通过`AT+LEADV`以及`AT+LESCAN`指令完成。

|指令 | 功能 | 前提条件 |
|:---:|:----|:--------|
|`AT+LEADV` | 蓝牙主机模式--->蓝牙从机模式 | 主机没有设备连接 |
|`AT+LESCAN` | 蓝牙从机模式--->蓝牙主机模式 | 从机设备没有设备连接 |

> 总结：无论模式如何切换，首要条件就是设备此时没有连接。如果有连接，那么请先断开当前连接。

### d. Wi-Fi演示

> 注意：Wi-Fi相关的功能以及演示请参阅庆科现有的AT2.0文档。

