//
// Created by hexiufeng on 2023/11/2.
//

#ifndef RUN_FFMPEG_FILTER_H
#define RUN_FFMPEG_FILTER_H

#include "common_define.h"
#include "cmd_options.h"


int init_complex_filtergraph(RunContext *run_context,FilterGraph *fg);
int init_simple_filtergraph(RunContext *run_context,InputStream *ist, OutputStream *ost);
int filtergraph_is_simple(FilterGraph *fg);
int configure_filtergraph(RunContext *run_context,FilterGraph *fg);
int ifilter_parameters_from_frame(InputFilter *ifilter, const AVFrame *frame);
int ifilter_has_all_input_formats(FilterGraph *fg);
#endif //RUN_FFMPEG_FILTER_H
