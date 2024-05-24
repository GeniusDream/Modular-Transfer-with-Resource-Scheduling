# 项目说明文档

## 一、项目概述

将数据传输模块化，分为读入模块、压缩模块、传输模块，三个模块的块大小及线程数可单独调节，压缩所使用的算法可单独调节。

联动测试可实现各模块线程数、压缩算法的实时调节。可根据需求，自主编写资源监测代码及自适应策略。

项目会给出测试的运行时间和平均 cpu 使用率，还可根据起始时间和辅助设备计算功率和能耗。

## 二、安装说明

### 环境要求

- CMake 3.20 或更高版本
- C++17 编译器
- zlib 库

### 步骤

1. **获取源代码**

   下载或克隆项目源代码到本地机器。

2. **创建构建目录**

   进入到项目目录，在项目文件夹下创建一个构建目录。

   ```
   cd /path/Flexible Modular Transfer with Resource Scheduling
   mkdir build
   cd build
   ```

3. **运行 CMake**

   在构建目录中运行 CMake，并指定源代码目录的路径。

   ```
   cmake ../source
   ```

   这将生成构建所需的 Makefile。

4. **构建项目**

   使用你 make 来构建项目。

   ```
   make
   ```

## 三、使用说明

请完成各文件配置后再重新编译运行，详细见 [四、配置](#4)

### 单模块测试可执行文件

1. 切换到 root 用户

   ```
   sudo su
   ```

2. 进入build目录，运行编译好的可执行文件

   ```
   cd build
   ./test_single compressType threadNum
   ```

   其中， compressType 替换为你所测试的压缩算法，取值范围 0-4

   > 0：lz4；1：zlib；2：bzip2；3：none；4：gz

   threadNum 替换为你所测试的各模块线程数，取值范围 1-4，项目里三个模块给同一个值，可根据需要自行更改源文件。

3. 结果将会输出到终端

### 联动测试可执行文件

1. 切换到 root 用户

   ```
   sudo su
   ```

2. 进入build目录，运行编译好的可执行文件

   ```
   cd build
   ./test_queue
   ```

3. 结果将会输出到终端

### 自主编写脚本运行可执行文件的注意事项

由于本项目中的 cpuTest.h 使用 ``sudo killall bash`` 命令来终止 cpu 测试脚本，所以项目运行同时使用 .sh 脚本时，需要使用非 bash 命令，例如 ``source xxx.sh `` 来运行脚本，否则会被项目误杀。

<h2 id="4"> 四、配置 </h2>

### cpu 测试脚本配置

1. 打开 /shell/testcpu.sh 文件，
   将 "/home/c508/cpu.log" 改为自定义的本机 cpu.log 输出的绝对路径。
2. 打开 /source/cpuTest.h 文件，
   将 "/home/c508/testcpu.sh" 改为本机 testcpu.sh 的绝对路径 "/path/Flexible Modular Transfer with Resource Scheduling/shell/testcpu.sh"
   将 "/home/c508/cpu.log" 改为自定义的本机 cpu.log 输出的绝对路径。

### main(single) 文件配置

1. 数据集路径：更改 #define FOLDER_IN
2. 数据集总大小：更改 \#define FILE_MAX_SIZE ，单位字节
3. 数据集文件个数：更改 \#define FILE_MAX_NUM
4. 各模块块大小：更改 \#define BLOCK1_MAX_SIZE 等
   同时需要更改相应的块个数 \#define BLOCK1_MAX_NUM 使得其相乘结果大于数据集总大小
5. 传输服务器的 IP 地址：更改 \#define ServerIP，本项目采用的是 http 协议
6. 模块间暂停开关：更改 const bool switch_begin ，为 true 的话，各模块结束后输入任意键继续下一模块。

### main(queue) 文件配置

1-5同 main(single) 文件配置

设置监测/策略时间间隔：\#define TIMESPAN ，单位 μs

编写资源分配策略：自主编写函数 void monitor()

## 五、项目目录

Flexible Modular Transfer with Resource Scheduling  
├── README.md  
├── shell  
│   └── testcpu.sh  
├── source  
│   ├── CMakeLists.txt  
│   ├── ThreadPool.h  
│   ├── cpuTest.h  
│   ├── gzcompress.cpp  
│   ├── gzcompress.h  
│   ├── httplib.h  
│   ├── main(queue).cpp  
│   ├── main(single).cpp  
│   ├── thpool.cpp  
│   ├── thpool.h  
│   └── thread_safe_queue.h  
└── tree.md  
