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

#endif //RUN_FFMPEG_PARSE_CMD_H
