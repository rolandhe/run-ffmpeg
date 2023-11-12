//
// Created by hexiufeng on 2023/11/1.
//

#ifndef RUN_FFMPEG_COMMON_H
#define RUN_FFMPEG_COMMON_H

#include "cmd_options.h"
int64_t get_timestamp();
int guess_input_channel_layout(const char * trace_id,InputStream *ist);
uint8_t *read_file(const char *filename);
int parse_meta_type(char *arg, char *type, int *index, const char **stream_spec);
int get_preset_file_2(const char *preset_name, const char *codec_name, AVIOContext **s);
uint8_t *get_line(AVIOContext *s,int *has_error);
void sub2video_update(InputStream *ist, int64_t heartbeat_pts, AVSubtitle *sub);



#endif //RUN_FFMPEG_COMMON_H
