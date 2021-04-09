**环境要求**：

- hiredis 
- libevent

**添加qos_monitor xlator步骤**：

   主要为了直接可以和其他xlator一起编译

1. 修改`glusterfs-3.0.2/configure.ac`，仿照添加`debug/qos_monitor`和`debug/qos_monitor/src`相关项：

   ```makefile
   xlators/debug/io-stats/Makefile
   xlators/debug/io-stats/src/Makefile
   xlators/debug/qos_monitor/Makefile
   xlators/debug/qos_monitor/src/Makefile
   ```

2. 在``glusterfs-3.0.2/xlators/debug`文件夹下，添加`qos_monitor`文件夹及其内容如下：

   ```
   qos_monitor
   |- Makefile.am
   |- src
   |- |- qos_monitor.c
   |- |- qos_monitor.h
   |- |- Makefile.am
   ```

3. 修改`Makefile.am`，包括：`debug/Makefile.am`，`debug/qos_monitor/Makefile.am`及`debug/qos_monitor/src/Makefile.am`，基本就是改一些名字：

   ```makefile
   # debug/Makefile.am
   SUBDIRS = trace error-gen io-stats qos_monitor
   
   CLEANFILES = 
   
   # debug/qos_monitor/Makefile.am
   SUBDIRS = src
   
   CLEANFILES =
   
   # debug/qos_monitor/src/Makefile.am
   xlator_LTLIBRARIES = qos_monitor.la
   xlatordir = $(libdir)/glusterfs/$(PACKAGE_VERSION)/xlator/debug
   
   qos_monitor_la_LDFLAGS = -module -avoidversion
   
   qos_monitor_la_SOURCES = qos_monitor.c
   qos_monitor_la_LIBADD = $(top_builddir)/libglusterfs/src/libglusterfs.la
   
   noinst_HEADERS = qos_monitor.h
   
   AM_CFLAGS = -fPIC -D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE -Wall -D$(GF_HOST_OS)\
           -I$(top_srcdir)/libglusterfs/src -shared -nostartfiles $(GF_CFLAGS)\
           -lhiredis -lpthread -levent
   
   CLEANFILES = 
   ```

4. 运行`autogen.sh`，该脚本是用aclocal和autoconf生成configure文件，以及用automake生成Makefile.in。

5. 运行`configure`, configure是用Makefile.in 生成Makefile。

6. `make && sudo make install`

7. 配置文件中加入`debug/qos_monitor` 层即可。



**配置文件example：**

```
volume qos_monitor
  type debug/qos_monitor    
  option redis-host 10.10.1.13   # 发送信息redis的主机ip, 默认为10.10.1.13
  option redis-port 6379         # 发送信息redis的端口号
  option monitor-interval 15     # 监控数据发送周期
  option publish-channel monitor # 发送数据channel名称
  option redis-publish-interval 100 # 设置publish间隔 单位ms
  subvolumes brick
end-volume
```

