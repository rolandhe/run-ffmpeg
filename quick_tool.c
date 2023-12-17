//
// Created by xiufeng on 23-12-16.
//

#include "run_ffmpeg.h"
#include <stdlib.h>
#include <string.h>
typedef struct _mem_data {
    char * buffer;
    int size;
} mem_data;

int64_t new_input_mem(char * input_data,int64_t input_len,int copy){
    mem_data * p_data = malloc(sizeof(mem_data));
    if(copy){
        char * new_data = malloc(input_len);
        memcpy(new_data,input_data,input_len);
        p_data->buffer = new_data;
    } else {
        p_data->buffer = input_data;
    }
    p_data->size = input_len;
    return (int64_t)p_data;
}

int64_t new_output_mem(){
    mem_data * p_data = malloc(sizeof(mem_data));
    memset(p_data,0,sizeof(mem_data));
    return (int64_t )p_data;
}

int64_t mem_data_len(int64_t point){
    if(!point){
        return 0;
    }
    mem_data * p = (mem_data*)point;
    return (int64_t)p->size;
}

void free_mem(int64_t point,int release_data){
    if(!point){
        return;
    }
    mem_data * p = (mem_data*)point;
    if(release_data && p->buffer){
        free(p->buffer);
    }
    free(p);
}