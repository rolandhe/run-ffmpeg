//
// Created by xiufeng on 23-12-16.
//

#include "run_ffmpeg.h"
#include <stdlib.h>
#include <string.h>
#include <libavutil/log.h>

typedef struct _mem_data {
    char * buffer;
    int size;
} mem_data;

int64_t new_input_mem(char * input_data,int64_t input_len,int copy){
    mem_data * p_data = av_malloc(sizeof(mem_data));
    if(copy){
        char * new_data = av_malloc(input_len);
        memcpy(new_data,input_data,input_len);
        p_data->buffer = new_data;
    } else {
        p_data->buffer = input_data;
    }
    p_data->size = input_len;
    return (int64_t)p_data;
}

int64_t new_output_mem(){
    mem_data * p_data = av_malloc(sizeof(mem_data));
    p_data->buffer = NULL;
    p_data->size = 0;
    int64_t  ret = (int64_t )p_data;

    av_log(NULL, AV_LOG_INFO, "new_output_mem:0x%lx\n",ret);
    return ret;
}

int64_t mem_data_len(int64_t point){
    if(!point){
        return 0;
    }
    mem_data * p = (mem_data*)point;
    av_log(NULL, AV_LOG_INFO, "get data len:%d\n",p->size);

//    FILE * f = fopen("/home/xiufeng/github/ffmpego/inc.pcm","wb");
//    fwrite(p->buffer,1,p->size,f);
//    fclose(f);
    return (int64_t)p->size;
}

int8_t * get_mem_info(int64_t point,int * data_len){
    *data_len = 0;
    if(!point){
        return NULL;
    }
    mem_data * p = (mem_data*)point;
    av_log(NULL, AV_LOG_INFO, "get data len:%d\n",p->size);
    *data_len = p->size;
    return (int8_t*)p->buffer;
}

void free_mem(int64_t point,int release_data){
    if(!point){
        return;
    }
    mem_data * p = (mem_data*)point;
    if(release_data && p->buffer){
        av_free(p->buffer);
    }
    av_free(p);
}