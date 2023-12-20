//
// Created by xiufeng on 23-11-7.
//

#ifndef EFFMPEG_HW_H
#define EFFMPEG_HW_H

#include "cmd_options.h"

HWDevice *hw_device_get_by_name(RunContext *run_context,const char *name);
int hw_device_init_from_string(RunContext *run_context,const char *arg, HWDevice **dev);
void hw_device_free_all(RunContext *run_context);

int hw_device_setup_for_decode(RunContext *run_context,InputStream *ist);
int hw_device_setup_for_encode(RunContext *run_context,OutputStream *ost);
int hw_device_setup_for_filter(RunContext *run_context,FilterGraph *fg);

int hwaccel_decode_init(AVCodecContext *avctx);


int qsv_init(void *run_context,AVCodecContext *s);
#endif //EFFMPEG_HW_H
