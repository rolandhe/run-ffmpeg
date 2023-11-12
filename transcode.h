//
// Created by hexiufeng on 2023/11/3.
//

#ifndef RUN_FFMPEG_TRANSCODE_H
#define RUN_FFMPEG_TRANSCODE_H

#include "cmd_options.h"

#define ABORT_ON_FLAG_EMPTY_OUTPUT        (1 <<  0)
#define ABORT_ON_FLAG_EMPTY_OUTPUT_STREAM (1 <<  1)

int transcode(RunContext *run_context);
//int get_duration_from_stream(RunContext *run_context,int64_t * p_duration);
static int reap_filters(RunContext *run_context,int flush);
static int do_subtitle_out(RunContext *run_context,OutputFile *of,
                            OutputStream *ost,
                            AVSubtitle *sub);
static int output_packet(RunContext *run_context,OutputFile *of, AVPacket *pkt,
                         OutputStream *ost, int eof);
static void close_output_stream(RunContext *run_context,OutputStream *ost);

#if HAVE_THREADS
static void *input_thread(void *arg);
static void free_input_thread(RunContext  *run_context,int i);
static void free_input_threads(RunContext  *run_context);
static int init_input_thread(RunContext *run_context,int i);
static int init_input_threads(RunContext *run_context);
static int get_input_packet_mt(InputFile *f, AVPacket **pkt);
#endif

void ffmpegg_cleanup(ParsedOptionsContext *parsed_ctx);
#endif //RUN_FFMPEG_TRANSCODE_H
