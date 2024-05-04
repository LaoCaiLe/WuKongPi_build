本程序为按键处理程序源码，模块为SIQ-02FVS3编码器
程序：/root/key_event_service
服务：/lib/systemd/system/key-handling.service

实现功能：
1. 识别编码器前进后退，按键按下，长按，抬起事件
2. 设置编码器长按为进入/退出xfce桌面环境
3. 其他按键事件未做任何操作

日志查看：
cat /var/log/syslog | grep key_event_service --text
