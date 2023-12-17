//
// Created by hexiufeng on 2023/10/31.
//

#ifndef RUN_FFMPEG_PARSE_CMD_H
#define RUN_FFMPEG_PARSE_CMD_H

#include <stdint.h>



int show_hwaccels();
void init_ffmpeg();
int run_ffmpeg_cmd(char * trace_id,char * cmd);

int quick_duration(char * trace_id,char *cmd, int64_t * p_duration);

int64_t new_input_mem(char * input_data,int64_t input_len,int copy);
int64_t new_output_mem();
int64_t mem_data_len(int64_t point);
int8_t * get_mem_info(int64_t point,int * data_len);
void free_mem(int64_t point,int release_data);

#endif //RUN_FFMPEG_PARSE_CMD_H
