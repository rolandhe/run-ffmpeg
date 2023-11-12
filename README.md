# run_ffmpeg
移植ffmpeg的能力，可以在线程内执行ffmpeg指令。ffempeg是一个可执行的工具，使用时需要调起进程，可控性和资源消耗都需要提升，run_ffmpeg
移植了ffempeg的大部分的能力，以动态库的形式存在，可以被在线程内调用，其调用指令与ffmpeg完全兼容， 比如：

```
   ffmpeg -i s95.mp3 mmm.aac
```

# 安装
## 前置条件
run_ffmpeg移植了ffmpeg的能力，因此它依赖与ffmpeg相同的包，因此是使用run_ffmpeg之前需要先安装这些包。可以通过编译ffmpeg源码或者直接安装ffmpeg来
获取这些依赖包。安装ffmpeg的步骤可以参考ffmpeg的文档。

## 注意事项
run_ffmpeg中依赖的config.h文件由编译ffmpeg时自动生成后copy过来的，默认是来自ffmpeg4.4版本，如果你的ffmpeg版本不同，需要自行copy对应版本的config.h,
使用自定义的config.h可能会存在问题，需要自行解决，当前版本的run_ffmpeg是依赖ffmpeg4.4测试的。

## 编译安装

下载源码并安装好ffmpeg后，直接执行build.sh进行安装，默认安装到/usr/local/lib下，安装过程中使用了sudo，需要你输入密码。

```
  sh build.sh
```

# 使用

run_ffmpeg的头文件是/usr/local/include/run_ffmpeg.h，引入后即可使用

## 初始化函数
在使用前必须调用 void init_ffmpeg() 函数进行初始化

## 调用ffmpeg指令函数
int run_ffmpeg_cmd(char * trace_id,char * cmd)

trace_id: 需要业务传入的这次调用唯一的跟踪id，日志中会输出这个trace_id,方便调试
cmd： ffmpeg执行指令，与原生ffmpeg的使用方式完全相同

不支持的选项：

* benchmark
* benchmark_all
* progress"
* stdin
* dump
* hex
* dts_delta_threshold
* dts_error_threshold
* xerror
* abort_on
* profile
* stats
* stats_period
* attach
* debug_ts
* max_error_rate
* sameq
* same_quant
* deinterlace
* vstats
* vstats_file
* vstats_version
* qphist
* hwaccels

## 读取指定输入的时长

int quick_duration(char * trace_id,char *cmd, int64_t * p_duration);

cmd： 只有输入的指令，比如 ffmpeg -i xx.mp3
p_duration： 返回的时长
ret: 0 是成功，< 0错误


