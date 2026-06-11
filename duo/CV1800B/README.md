我把一个milkv duo CV1800B接到了一个面板板上,引出了GND和36引脚的3V3(OUT) 到面包
板上.面包板上另外插了一个7针的1.28寸圆形IPS屏(分辨率240*240,驱动芯片GC9A01,工作
电压3.3v-5v,引脚定义:VCC、GND、SCL、SDA、DC、CS、RST).我在CV1800B上运行了一个Lin
ux系统,它没有hdmi输出,按官方指导,使用ssh root@192.168.42.1远程到Linux系统里.我想写一个软件在CV1800B的Linux里,作为接口,接收不同的出入的时候,在IPS屏幕上显示不同的画面
我想先连接duo版子的引脚和屏幕的引脚,先点亮屏幕

目前我通过ssh root@192.168.42.1登陆后，发现它没有外部网络，而且scp命令传输文件也有问题，帮我规划一下这个如何解决，是否需要自己编译一个可以支持scp的镜像，还是搞一个网络共享给它
ssh到duo上的密码是：milkv

duo的引脚定义在这个地址：https://milkv.io/zh/docs/duo/getting-started/duo
DC接到了5号引脚，SCL接到了9号引脚，SDA接到了10号引脚，CS接到了12号引脚，RST接到了11号引脚

## 当前文档索引

- `PIN_BINDINGS.md`：最早的 GC9A01 单屏接线和 GPIO 编号。
- `NATIVE_FRAMEBUFFER_SPI_LCD.md`：原生 framebuffer SPI 屏方案，默认记录 GC9A01，也包含 ST7789V 单屏切换说明。
- `DUAL_SPI_LCD_ST7789V_CONSOLE.md`：新增 ST7789V/ST7789V2 240x320 framebuffer 控制台屏，同时保留 GC9A01 表情屏的双屏方案。
