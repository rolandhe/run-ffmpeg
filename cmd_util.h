//
// Created by hexiufeng on 2023/10/31.
//

#ifndef RUN_FFMPEG_CMD_UTIL_H
#define RUN_FFMPEG_CMD_UTIL_H
#include "cmd_options.h"
#include <libavutil/samplefmt.h>

#define GET_SAMPLE_FMT_NAME(sample_fmt)\
    const char *name = av_get_sample_fmt_name(sample_fmt)

#define GET_SAMPLE_RATE_NAME(rate)\
    char name[16];\
    snprintf(name, sizeof(name), "%d", rate);

#define GET_CH_LAYOUT_NAME(ch_layout)\
    char name[16];\
    snprintf(name, sizeof(name), "0x%"PRIx64, ch_layout);

#define GROW_ARRAY(trace_id,array, nb_elems,has_error)\
    array = grow_array(trace_id,array, sizeof(*array), &nb_elems, nb_elems + 1,&has_error)

#define TRIM(p) \
    while(*p) {     \
        if(*p != ' '){ \
            break;   \
        }      \
        p++;   \
    }

int parse_option(void *optctx, const char *opt, const char *arg,
                 const OptionDef *options);
void *grow_array(char *trace_id,void *array, int elem_size, int *size, int new_size,int *has_error);

double parse_number_or_die(char * trace_id,const char *context, const char *numstr, int type,
                           double min, double max,int *die);
int64_t parse_time_or_die(char * trace_id,const char *context, const char *timestr,
                          int is_duration,int *die);
int opt_timelimit(void *optctx, const char *opt, const char *arg);

int opt_default(void *optctx, const char *opt, const char *arg);

void init_opts(OptionsContext *optionCtx);
void uninit_opts(OptionsContext *optionCtx);

//void ffmpeg_cleanup(ParsedOptionsContext *parsed_ctx);
//void uninit_parent_context(ParsedOptionsContext *optionCtx);

int split_commandline(ParseContext *octx, int argc, char *argv[],
                      const OptionDef *options, ParsedOptionsContext *optionCtx);

int parse_optgroup(void *optctx, OptionGroup *g);

void uninit_parse_context(ParseContext *octx);

void print_error(const char *filename, int err);

void remove_avoptions(AVDictionary **a, AVDictionary *b);
int assert_avoptions(AVDictionary *m);
int check_stream_specifier(AVFormatContext *s, AVStream *st, const char *spec);

AVDictionary **setup_find_stream_info_opts(const char * trace_id,AVFormatContext *s,AVDictionary *codec_opts,int *error);
AVDictionary *filter_codec_opts(AVDictionary *opts, enum AVCodecID codec_id,
                                AVFormatContext *s, AVStream *st, const AVCodec *codec,int *error);

FILE *get_preset_file(char *filename, size_t filename_size,
                      const char *preset_name, int is_path, const char *codec_name);

double get_rotation(const char * trace_id,AVStream *st);




#endif //RUN_FFMPEG_CMD_UTIL_H
