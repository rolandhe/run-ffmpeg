//
// Created by hexiufeng on 2023/10/31.
//

#include <libavutil/avstring.h>
#include <libavdevice/avdevice.h>
#include <libavutil/opt.h>
#include <libavutil/dict.h>
#include "cmd_options.h"
#include "cmd_util.h"
#include "run_ffmpeg.h"
#include "common.h"
#include "transcode.h"
#include "open_files.h"
#include "hw.h"


static int opt_data_frames(void *optctx, const char *opt, const char *arg);
static int opt_qscale(void *optctx, const char *opt, const char *arg);
static int opt_target(void *optctx, const char *opt, const char *arg);
static int opt_audio_codec(void *optctx, const char *opt, const char *arg);
static int opt_data_codec(void *optctx, const char *opt, const char *arg);
static int opt_video_channel(void *optctx, const char *opt, const char *arg);
static int opt_video_standard(void *optctx, const char *opt, const char *arg);
static int opt_channel_layout(void *optctx, const char *opt, const char *arg);
static int opt_subtitle_codec(void *optctx, const char *opt, const char *arg);
static int opt_old2new(void *optctx, const char *opt, const char *arg);
static int opt_audio_filters(void *optctx, const char *opt, const char *arg);
static int opt_video_filters(void *optctx, const char *opt, const char *arg);
static int opt_video_frames(void *optctx, const char *opt, const char *arg);
static int opt_default_new(OptionsContext *o, const char *opt, const char *arg);

static int opt_recording_timestamp(void *optctx, const char *opt, const char *arg);
static int opt_audio_frames(void *optctx, const char *opt, const char *arg);
static int opt_audio_qscale(void *optctx, const char *opt, const char *arg);
static int opt_video_codec(void *optctx, const char *opt, const char *arg);
static int opt_preset(void *optctx, const char *opt, const char *arg);
static int opt_sdp_file(void *optctx, const char *opt, const char *arg);
static int opt_filter_complex(void *optctx, const char *opt, const char *arg);
static int opt_filter_complex_script(void *optctx, const char *opt, const char *arg);
static int opt_streamid(void *optctx, const char *opt, const char *arg);
static int opt_timecode(void *optctx, const char *opt, const char *arg);
static int opt_map(void *optctx, const char *opt, const char *arg);
static int opt_map_channel(void *optctx, const char *opt, const char *arg);
static int opt_bitrate(void *optctx, const char *opt, const char *arg);
static int opt_vsync(void *optctx, const char *opt, const char *arg);
static int opt_init_hw_device(void *optctx, const char *opt, const char *arg);
static int opt_filter_hw_device(void *optctx, const char *opt, const char *arg);

static const char *const G_FRAME_RATES[] = { "25", "30000/1001", "24000/1001" };

#define MATCH_PER_TYPE_OPT(name, type, outvar, fmtctx, mediatype)\
{\
    int i;\
    for (i = 0; i < o->nb_ ## name; i++) {\
        char *spec = o->name[i].specifier;\
        if (!strcmp(spec, mediatype))\
            outvar = o->name[i].u.type;\
    }\
}








#define OFFSET(x) offsetof(OptionsContext, x)
#define RUN_CTX_OFFSET(x) offsetof(ParsedOptionsContext, raw_context) + offsetof(RunContext,x)
const OptionDef options[] = {
        /* main options */
        { "f",              HAS_ARG | OPT_STRING | OPT_OFFSET |
                            OPT_INPUT | OPT_OUTPUT,                      { .off       = OFFSET(format) },
          "force format", "fmt" },
        { "y",              OPT_BOOL|OPT_RUN_OFFSET,                                    {         .off = RUN_CTX_OFFSET(file_overwrite) },
          "overwrite output files" },
        { "n",              OPT_BOOL|OPT_RUN_OFFSET,                                    {          .off = RUN_CTX_OFFSET(no_file_overwrite) },
          "never overwrite output files" },
        { "ignore_unknown", OPT_BOOL|OPT_RUN_OFFSET,                                    {         .off = RUN_CTX_OFFSET(ignore_unknown_streams)},
          "Ignore unknown stream types" },
        { "copy_unknown",   OPT_BOOL | OPT_EXPERT|OPT_RUN_OFFSET,                       {          .off = RUN_CTX_OFFSET(copy_unknown_streams)},
          "Copy unknown stream types" },
        { "c",              HAS_ARG | OPT_STRING | OPT_SPEC |
                            OPT_INPUT | OPT_OUTPUT,                      { .off       = OFFSET(codec_names) },
          "codec name", "codec" },
        { "codec",          HAS_ARG | OPT_STRING | OPT_SPEC |
                            OPT_INPUT | OPT_OUTPUT,                      { .off       = OFFSET(codec_names) },
          "codec name", "codec" },
        { "pre",            HAS_ARG | OPT_STRING | OPT_SPEC |
                            OPT_OUTPUT,                                  { .off       = OFFSET(presets) },
          "preset name", "preset" },
        { "map",            HAS_ARG | OPT_EXPERT | OPT_PERFILE |
                            OPT_OUTPUT,                                  { .func_arg = opt_map },
                    "set input stream mapping",
                    "[-]input_file_id[:stream_specifier][,sync_file_id[:stream_specifier]]" },
        { "map_channel",    HAS_ARG | OPT_EXPERT | OPT_PERFILE | OPT_OUTPUT, { .func_arg = opt_map_channel },
                    "map an audio channel from one stream to another", "file.stream.channel[:syncfile.syncstream]" },
        { "map_metadata",   HAS_ARG | OPT_STRING | OPT_SPEC |
                            OPT_OUTPUT,                                  { .off       = OFFSET(metadata_map) },
          "set metadata information of outfile from infile",
          "outfile[,metadata]:infile[,metadata]" },
        { "map_chapters",   HAS_ARG | OPT_INT | OPT_EXPERT | OPT_OFFSET |
                            OPT_OUTPUT,                                  { .off = OFFSET(chapters_input_file) },
          "set chapters mapping", "input_file_index" },
        { "t",              HAS_ARG | OPT_TIME | OPT_OFFSET |
                            OPT_INPUT | OPT_OUTPUT,                      { .off = OFFSET(recording_time) },
          "record or transcode \"duration\" seconds of audio/video",
          "duration" },
        { "to",             HAS_ARG | OPT_TIME | OPT_OFFSET | OPT_INPUT | OPT_OUTPUT,  { .off = OFFSET(stop_time) },
          "record or transcode stop time", "time_stop" },
        { "fs",             HAS_ARG | OPT_INT64 | OPT_OFFSET | OPT_OUTPUT, { .off = OFFSET(limit_filesize) },
          "set the limit file size in bytes", "limit_size" },
        { "ss",             HAS_ARG | OPT_TIME | OPT_OFFSET |
                            OPT_INPUT | OPT_OUTPUT,                      { .off = OFFSET(start_time) },
          "set the start time offset", "time_off" },
        { "sseof",          HAS_ARG | OPT_TIME | OPT_OFFSET |
                            OPT_INPUT,                                   { .off = OFFSET(start_time_eof) },
          "set the start time offset relative to EOF", "time_off" },
        { "seek_timestamp", HAS_ARG | OPT_INT | OPT_OFFSET |
                            OPT_INPUT,                                   { .off = OFFSET(seek_timestamp) },
          "enable/disable seeking by timestamp with -ss" },
        { "accurate_seek",  OPT_BOOL | OPT_OFFSET | OPT_EXPERT |
                            OPT_INPUT,                                   { .off = OFFSET(accurate_seek) },
          "enable/disable accurate seeking with -ss" },
        { "itsoffset",      HAS_ARG | OPT_TIME | OPT_OFFSET |
                            OPT_EXPERT | OPT_INPUT,                      { .off = OFFSET(input_ts_offset) },
          "set the input ts offset", "time_off" },
        { "itsscale",       HAS_ARG | OPT_DOUBLE | OPT_SPEC |
                            OPT_EXPERT | OPT_INPUT,                      { .off = OFFSET(ts_scale) },
          "set the input ts scale", "scale" },
        { "timestamp",      HAS_ARG | OPT_PERFILE | OPT_OUTPUT,          { .func_arg = opt_recording_timestamp },
                    "set the recording timestamp ('now' to set the current time)", "time" },
        { "metadata",       HAS_ARG | OPT_STRING | OPT_SPEC | OPT_OUTPUT, { .off = OFFSET(metadata) },
          "add metadata", "string=string" },
        { "program",        HAS_ARG | OPT_STRING | OPT_SPEC | OPT_OUTPUT, { .off = OFFSET(program) },
          "add program with specified streams", "title=string:st=number..." },
        { "dframes",        HAS_ARG | OPT_PERFILE | OPT_EXPERT |
                            OPT_OUTPUT,                                  { .func_arg = opt_data_frames },
          "set the number of data frames to output", "number" },
//        { "benchmark",      OPT_BOOL | OPT_EXPERT,                       { &do_benchmark },
//                    "add timings for benchmarking" },
//        { "benchmark_all",  OPT_BOOL | OPT_EXPERT,                       { &do_benchmark_all },
//                    "add timings for each task" },
//        { "progress",       HAS_ARG | OPT_EXPERT,                        { .func_arg = opt_progress },
//                    "write program-readable progress information", "url" },
//        { "stdin",          OPT_BOOL | OPT_EXPERT,                       { &stdin_interaction },
//                    "enable or disable interaction on standard input" },
        { "timelimit",      HAS_ARG | OPT_EXPERT,                        { .func_arg = opt_timelimit },
          "set max runtime in seconds in CPU user time", "limit" },
//        { "dump",           OPT_BOOL | OPT_EXPERT,                       { &do_pkt_dump },
//                    "dump each input packet" },
//        { "hex",            OPT_BOOL | OPT_EXPERT,                       { &do_hex_dump },
//          "when dumping packets, also dump the payload" },
        { "re",             OPT_BOOL | OPT_EXPERT | OPT_OFFSET |
                            OPT_INPUT,                                   { .off = OFFSET(rate_emu) },
          "read input at native frame rate", "" },
        { "target",         HAS_ARG | OPT_PERFILE | OPT_OUTPUT,          { .func_arg = opt_target },
          "specify target file type (\"vcd\", \"svcd\", \"dvd\", \"dv\" or \"dv50\" "
          "with optional prefixes \"pal-\", \"ntsc-\" or \"film-\")", "type" },
        { "vsync",          HAS_ARG | OPT_EXPERT,                        { .func_arg = opt_vsync },
                    "video sync method", "" },
        { "frame_drop_threshold", HAS_ARG | OPT_FLOAT | OPT_EXPERT|OPT_RUN_OFFSET,      { .off= RUN_CTX_OFFSET(frame_drop_threshold) },
                    "frame drop threshold", "" },
        { "async",          HAS_ARG | OPT_INT | OPT_EXPERT|OPT_RUN_OFFSET,              { .off= RUN_CTX_OFFSET(audio_sync_method) },
                    "audio sync method", "" },
        { "adrift_threshold", HAS_ARG | OPT_FLOAT | OPT_EXPERT|OPT_RUN_OFFSET,          { .off= RUN_CTX_OFFSET(audio_drift_threshold) },
                    "audio drift threshold", "threshold" },
        { "copyts",         OPT_BOOL | OPT_EXPERT|OPT_RUN_OFFSET,                       { .off= RUN_CTX_OFFSET(copy_ts)},
          "copy timestamps" },
        { "start_at_zero",  OPT_BOOL | OPT_EXPERT|OPT_RUN_OFFSET,                       { .off= RUN_CTX_OFFSET(start_at_zero) },
          "shift input timestamps to start at 0 when using copyts" },
        { "copytb",         HAS_ARG | OPT_INT | OPT_EXPERT|OPT_RUN_OFFSET,              {  .off= RUN_CTX_OFFSET(copy_tb) },
          "copy input stream time base when stream copying", "mode" },
        { "shortest",       OPT_BOOL | OPT_EXPERT | OPT_OFFSET |
                            OPT_OUTPUT,                                  { .off = OFFSET(shortest) },
          "finish encoding within shortest input" },
        { "bitexact",       OPT_BOOL | OPT_EXPERT | OPT_OFFSET |
                            OPT_OUTPUT | OPT_INPUT,                      { .off = OFFSET(bitexact) },
          "bitexact mode" },
        { "apad",           OPT_STRING | HAS_ARG | OPT_SPEC |
                            OPT_OUTPUT,                                  { .off = OFFSET(apad) },
          "audio pad", "" },
//        { "dts_delta_threshold", HAS_ARG | OPT_FLOAT | OPT_EXPERT,       { &dts_delta_threshold },
//          "timestamp discontinuity delta threshold", "threshold" },
//        { "dts_error_threshold", HAS_ARG | OPT_FLOAT | OPT_EXPERT,       { &dts_error_threshold },
//          "timestamp error delta threshold", "threshold" },
//        { "xerror",         OPT_BOOL | OPT_EXPERT,                       { &exit_on_error },
//          "exit on error", "error" },
//        { "abort_on",       HAS_ARG | OPT_EXPERT,                        { .func_arg = opt_abort_on },
//          "abort on the specified condition flags", "flags" },
        { "copyinkf",       OPT_BOOL | OPT_EXPERT | OPT_SPEC |
                            OPT_OUTPUT,                                  { .off = OFFSET(copy_initial_nonkeyframes) },
          "copy initial non-keyframes" },
        { "copypriorss",    OPT_INT | HAS_ARG | OPT_EXPERT | OPT_SPEC | OPT_OUTPUT,   { .off = OFFSET(copy_prior_start) },
          "copy or discard frames before start time" },
        { "frames",         OPT_INT64 | HAS_ARG | OPT_SPEC | OPT_OUTPUT, { .off = OFFSET(max_frames) },
          "set the number of frames to output", "number" },
        { "tag",            OPT_STRING | HAS_ARG | OPT_SPEC |
                            OPT_EXPERT | OPT_OUTPUT | OPT_INPUT,         { .off = OFFSET(codec_tags) },
          "force codec tag/fourcc", "fourcc/tag" },
        { "q",              HAS_ARG | OPT_EXPERT | OPT_DOUBLE |
                            OPT_SPEC | OPT_OUTPUT,                       { .off = OFFSET(qscale) },
          "use fixed quality scale (VBR)", "q" },
        { "qscale",         HAS_ARG | OPT_EXPERT | OPT_PERFILE |
                            OPT_OUTPUT,                                  { .func_arg = opt_qscale },
          "use fixed quality scale (VBR)", "q" },
//        { "profile",        HAS_ARG | OPT_EXPERT | OPT_PERFILE | OPT_OUTPUT, { .func_arg = opt_profile },
//                    "set profile", "profile" },
        { "filter",         HAS_ARG | OPT_STRING | OPT_SPEC | OPT_OUTPUT, { .off = OFFSET(filters) },
          "set stream filtergraph", "filter_graph" },
        { "filter_threads",  HAS_ARG | OPT_INT|OPT_RUN_OFFSET,                          { .off = RUN_CTX_OFFSET(filter_nbthreads) },
          "number of non-complex filter threads" },
        { "filter_script",  HAS_ARG | OPT_STRING | OPT_SPEC | OPT_OUTPUT, { .off = OFFSET(filter_scripts) },
          "read stream filtergraph description from a file", "filename" },
        { "reinit_filter",  HAS_ARG | OPT_INT | OPT_SPEC | OPT_INPUT,    { .off = OFFSET(reinit_filters) },
          "reinit filtergraph on input parameter changes", "" },
        { "filter_complex", HAS_ARG | OPT_EXPERT,                        { .func_arg = opt_filter_complex },
          "create a complex filtergraph", "graph_description" },
        { "filter_complex_threads", HAS_ARG | OPT_INT|OPT_RUN_OFFSET,                   { .off = RUN_CTX_OFFSET(filter_complex_nbthreads)},
          "number of threads for -filter_complex" },
        { "lavfi",          HAS_ARG | OPT_EXPERT,                        { .func_arg = opt_filter_complex },
          "create a complex filtergraph", "graph_description" },
        { "filter_complex_script", HAS_ARG | OPT_EXPERT,                 { .func_arg = opt_filter_complex_script },
          "read complex filtergraph description from a file", "filename" },
        { "auto_conversion_filters", OPT_BOOL | OPT_EXPERT|OPT_RUN_OFFSET,              { .off = RUN_CTX_OFFSET(auto_conversion_filters) },
          "enable automatic conversion filters globally" },
//        { "stats",          OPT_BOOL,                                    { &print_stats },
//                    "print progress report during encoding", },
//        { "stats_period",    HAS_ARG | OPT_EXPERT,                       { .func_arg = opt_stats_period },
//                    "set the period at which ffmpeg updates stats and -progress output", "time" },
//        { "attach",         HAS_ARG | OPT_PERFILE | OPT_EXPERT |
//                            OPT_OUTPUT,                                  { .func_arg = opt_attach },
//                    "add an attachment to the output file", "filename" },
        { "dump_attachment", HAS_ARG | OPT_STRING | OPT_SPEC |
                             OPT_EXPERT | OPT_INPUT,                     { .off = OFFSET(dump_attachment) },
          "extract an attachment into a file", "filename" },
        { "stream_loop", OPT_INT | HAS_ARG | OPT_EXPERT | OPT_INPUT |
                         OPT_OFFSET,                                  { .off = OFFSET(loop) }, "set number of times input stream shall be looped", "loop count" },
//        { "debug_ts",       OPT_BOOL | OPT_EXPERT,                       { &debug_ts },
//                    "print timestamp debugging info" },
//        { "max_error_rate",  HAS_ARG | OPT_FLOAT,                        { &max_error_rate },
//                    "ratio of decoding errors (0.0: no errors, 1.0: 100% errors) above which ffmpeg returns an error instead of success.", "maximum error rate" },
        { "discard",        OPT_STRING | HAS_ARG | OPT_SPEC |
                            OPT_INPUT,                                   { .off = OFFSET(discard) },
          "discard", "" },
        { "disposition",    OPT_STRING | HAS_ARG | OPT_SPEC |
                            OPT_OUTPUT,                                  { .off = OFFSET(disposition) },
          "disposition", "" },
        { "thread_queue_size", HAS_ARG | OPT_INT | OPT_OFFSET | OPT_EXPERT | OPT_INPUT,
          { .off = OFFSET(thread_queue_size) },
          "set the maximum number of queued packets from the demuxer" },
        { "find_stream_info", OPT_BOOL | OPT_PERFILE | OPT_INPUT | OPT_EXPERT|OPT_RUN_OFFSET, { .off = RUN_CTX_OFFSET(find_stream_info) },
          "read and decode the streams to fill missing information with heuristics" },

        /* video options */
        { "vframes",      OPT_VIDEO | HAS_ARG  | OPT_PERFILE | OPT_OUTPUT,           { .func_arg = opt_video_frames },
          "set the number of video frames to output", "number" },
        { "r",            OPT_VIDEO | HAS_ARG  | OPT_STRING | OPT_SPEC |
                          OPT_INPUT | OPT_OUTPUT,                                    { .off = OFFSET(frame_rates) },
          "set frame rate (Hz value, fraction or abbreviation)", "rate" },
        { "fpsmax",       OPT_VIDEO | HAS_ARG  | OPT_STRING | OPT_SPEC |
                          OPT_OUTPUT,                                                { .off = OFFSET(max_frame_rates) },
          "set max frame rate (Hz value, fraction or abbreviation)", "rate" },
        { "s",            OPT_VIDEO | HAS_ARG | OPT_SUBTITLE | OPT_STRING | OPT_SPEC |
                          OPT_INPUT | OPT_OUTPUT,                                    { .off = OFFSET(frame_sizes) },
          "set frame size (WxH or abbreviation)", "size" },
        { "aspect",       OPT_VIDEO | HAS_ARG  | OPT_STRING | OPT_SPEC |
                          OPT_OUTPUT,                                                { .off = OFFSET(frame_aspect_ratios) },
          "set aspect ratio (4:3, 16:9 or 1.3333, 1.7777)", "aspect" },
        { "pix_fmt",      OPT_VIDEO | HAS_ARG | OPT_EXPERT  | OPT_STRING | OPT_SPEC |
                          OPT_INPUT | OPT_OUTPUT,                                    { .off = OFFSET(frame_pix_fmts) },
          "set pixel format", "format" },
        { "bits_per_raw_sample", OPT_VIDEO | OPT_INT | HAS_ARG|OPT_RUN_OFFSET,                      { .off = RUN_CTX_OFFSET(frame_bits_per_raw_sample) },
          "set the number of bits per raw sample", "number" },
        { "intra",        OPT_VIDEO | OPT_BOOL | OPT_EXPERT|OPT_RUN_OFFSET,                         { .off = RUN_CTX_OFFSET(intra_only) },
          "deprecated use -g 1" },
        { "vn",           OPT_VIDEO | OPT_BOOL  | OPT_OFFSET | OPT_INPUT | OPT_OUTPUT,{ .off = OFFSET(video_disable) },
          "disable video" },
        { "rc_override",  OPT_VIDEO | HAS_ARG | OPT_EXPERT  | OPT_STRING | OPT_SPEC |
                          OPT_OUTPUT,                                                { .off = OFFSET(rc_overrides) },
          "rate control override for specific intervals", "override" },
        { "vcodec",       OPT_VIDEO | HAS_ARG  | OPT_PERFILE | OPT_INPUT |
                          OPT_OUTPUT,                                                { .func_arg = opt_video_codec },
          "force video codec ('copy' to copy stream)", "codec" },
//        { "sameq",        OPT_VIDEO | OPT_EXPERT ,                                   { .func_arg = opt_sameq },
//          "Removed" },
//        { "same_quant",   OPT_VIDEO | OPT_EXPERT ,                                   { .func_arg = opt_sameq },
//          "Removed" },
        { "timecode",     OPT_VIDEO | HAS_ARG | OPT_PERFILE | OPT_OUTPUT,            { .func_arg = opt_timecode },
          "set initial TimeCode value.", "hh:mm:ss[:;.]ff" },
        { "pass",         OPT_VIDEO | HAS_ARG | OPT_SPEC | OPT_INT | OPT_OUTPUT,     { .off = OFFSET(pass) },
          "select the pass number (1 to 3)", "n" },
        { "passlogfile",  OPT_VIDEO | HAS_ARG | OPT_STRING | OPT_EXPERT | OPT_SPEC |
                          OPT_OUTPUT,                                                { .off = OFFSET(passlogfiles) },
          "select two pass log file name prefix", "prefix" },
          // 不再支持
//        { "deinterlace",  OPT_VIDEO | OPT_BOOL | OPT_EXPERT,                         { &do_deinterlace },
//          "this option is deprecated, use the yadif filter instead" },
        { "psnr",         OPT_VIDEO | OPT_BOOL | OPT_EXPERT|OPT_RUN_OFFSET,                         { .off = RUN_CTX_OFFSET(do_psnr)},
          "calculate PSNR of compressed frames" },
//        { "vstats",       OPT_VIDEO | OPT_EXPERT ,                                   { .func_arg = opt_vstats },
//          "dump video coding statistics to file" },
//        { "vstats_file",  OPT_VIDEO | HAS_ARG | OPT_EXPERT ,                         { .func_arg = opt_vstats_file },
//          "dump video coding statistics to file", "file" },
//        { "vstats_version",  OPT_VIDEO | OPT_INT | HAS_ARG | OPT_EXPERT ,            { &vstats_version },
//          "Version of the vstats format to use."},
        { "vf",           OPT_VIDEO | HAS_ARG  | OPT_PERFILE | OPT_OUTPUT,           { .func_arg = opt_video_filters },
          "set video filters", "filter_graph" },
        { "intra_matrix", OPT_VIDEO | HAS_ARG | OPT_EXPERT  | OPT_STRING | OPT_SPEC |
                          OPT_OUTPUT,                                                { .off = OFFSET(intra_matrices) },
          "specify intra matrix coeffs", "matrix" },
        { "inter_matrix", OPT_VIDEO | HAS_ARG | OPT_EXPERT  | OPT_STRING | OPT_SPEC |
                          OPT_OUTPUT,                                                { .off = OFFSET(inter_matrices) },
          "specify inter matrix coeffs", "matrix" },
        { "chroma_intra_matrix", OPT_VIDEO | HAS_ARG | OPT_EXPERT  | OPT_STRING | OPT_SPEC |
                                 OPT_OUTPUT,                                                { .off = OFFSET(chroma_intra_matrices) },
          "specify intra matrix coeffs", "matrix" },
        { "top",          OPT_VIDEO | HAS_ARG | OPT_EXPERT  | OPT_INT| OPT_SPEC |
                          OPT_INPUT | OPT_OUTPUT,                                    { .off = OFFSET(top_field_first) },
          "top=1/bottom=0/auto=-1 field first", "" },
        { "vtag",         OPT_VIDEO | HAS_ARG | OPT_EXPERT  | OPT_PERFILE |
                          OPT_INPUT | OPT_OUTPUT,                                    { .func_arg = opt_old2new },
          "force video tag/fourcc", "fourcc/tag" },
//        { "qphist",       OPT_VIDEO | OPT_BOOL | OPT_EXPERT ,                        { &qp_hist },
//          "show QP histogram" },
        { "force_fps",    OPT_VIDEO | OPT_BOOL | OPT_EXPERT  | OPT_SPEC |
                          OPT_OUTPUT,                                                { .off = OFFSET(force_fps) },
          "force the selected framerate, disable the best supported framerate selection" },
        { "streamid",     OPT_VIDEO | HAS_ARG | OPT_EXPERT | OPT_PERFILE |
                          OPT_OUTPUT,                                                { .func_arg = opt_streamid },
          "set the value of an outfile streamid", "streamIndex:value" },
        { "force_key_frames", OPT_VIDEO | OPT_STRING | HAS_ARG | OPT_EXPERT |
                              OPT_SPEC | OPT_OUTPUT,                                 { .off = OFFSET(forced_key_frames) },
          "force key frames at specified timestamps", "timestamps" },
        { "ab",           OPT_VIDEO | HAS_ARG | OPT_PERFILE | OPT_OUTPUT,            { .func_arg = opt_bitrate },
          "audio bitrate (please use -b:a)", "bitrate" },
        { "b",            OPT_VIDEO | HAS_ARG | OPT_PERFILE | OPT_OUTPUT,            { .func_arg = opt_bitrate },
          "video bitrate (please use -b:v)", "bitrate" },
        { "hwaccel",          OPT_VIDEO | OPT_STRING | HAS_ARG | OPT_EXPERT |
                              OPT_SPEC | OPT_INPUT,                                  { .off = OFFSET(hwaccels) },
          "use HW accelerated decoding", "hwaccel name" },
        { "hwaccel_device",   OPT_VIDEO | OPT_STRING | HAS_ARG | OPT_EXPERT |
                              OPT_SPEC | OPT_INPUT,                                  { .off = OFFSET(hwaccel_devices) },
          "select a device for HW acceleration", "devicename" },
        { "hwaccel_output_format", OPT_VIDEO | OPT_STRING | HAS_ARG | OPT_EXPERT |
                                   OPT_SPEC | OPT_INPUT,                                  { .off = OFFSET(hwaccel_output_formats) },
          "select output format used with HW accelerated decoding", "format" },
#if CONFIG_VIDEOTOOLBOX
        { "videotoolbox_pixfmt", HAS_ARG | OPT_STRING | OPT_EXPERT|OPT_RUN_OFFSET, { .off = RUN_CTX_OFFSET(videotoolbox_pixfmt)}, "" },
#endif
//        不需要支持
//        { "hwaccels",         OPT_EXIT,                                              { .func_arg = show_hwaccels },
//          "show available HW acceleration methods" },
        { "autorotate",       HAS_ARG | OPT_BOOL | OPT_SPEC |
                              OPT_EXPERT | OPT_INPUT,                                { .off = OFFSET(autorotate) },
          "automatically insert correct rotate filters" },
        { "autoscale",        HAS_ARG | OPT_BOOL | OPT_SPEC |
                              OPT_EXPERT | OPT_OUTPUT,                               { .off = OFFSET(autoscale) },
          "automatically insert a scale filter at the end of the filter graph" },

        /* audio options */
        { "aframes",        OPT_AUDIO | HAS_ARG  | OPT_PERFILE | OPT_OUTPUT,           { .func_arg = opt_audio_frames },
          "set the number of audio frames to output", "number" },
        { "aq",             OPT_AUDIO | HAS_ARG  | OPT_PERFILE | OPT_OUTPUT,           { .func_arg = opt_audio_qscale },
          "set audio quality (codec-specific)", "quality", },
        { "ar",             OPT_AUDIO | HAS_ARG  | OPT_INT | OPT_SPEC |
                            OPT_INPUT | OPT_OUTPUT,                                    { .off = OFFSET(audio_sample_rate) },
          "set audio sampling rate (in Hz)", "rate" },
        { "ac",             OPT_AUDIO | HAS_ARG  | OPT_INT | OPT_SPEC |
                            OPT_INPUT | OPT_OUTPUT,                                    { .off = OFFSET(audio_channels) },
          "set number of audio channels", "channels" },
        { "an",             OPT_AUDIO | OPT_BOOL | OPT_OFFSET | OPT_INPUT | OPT_OUTPUT,{ .off = OFFSET(audio_disable) },
          "disable audio" },
        { "acodec",         OPT_AUDIO | HAS_ARG  | OPT_PERFILE |
                            OPT_INPUT | OPT_OUTPUT,                                    { .func_arg = opt_audio_codec },
          "force audio codec ('copy' to copy stream)", "codec" },
        { "atag",           OPT_AUDIO | HAS_ARG  | OPT_EXPERT | OPT_PERFILE |
                            OPT_OUTPUT,                                                { .func_arg = opt_old2new },
          "force audio tag/fourcc", "fourcc/tag" },
        { "vol",            OPT_AUDIO | HAS_ARG  | OPT_INT|OPT_RUN_OFFSET,                            { .off = RUN_CTX_OFFSET(audio_volume) },
          "change audio volume (256=normal)" , "volume" },
        { "sample_fmt",     OPT_AUDIO | HAS_ARG  | OPT_EXPERT | OPT_SPEC |
                            OPT_STRING | OPT_INPUT | OPT_OUTPUT,                       { .off = OFFSET(sample_fmts) },
          "set sample format", "format" },
        { "channel_layout", OPT_AUDIO | HAS_ARG  | OPT_EXPERT | OPT_PERFILE |
                            OPT_INPUT | OPT_OUTPUT,                                    { .func_arg = opt_channel_layout },
          "set channel layout", "layout" },
        { "af",             OPT_AUDIO | HAS_ARG  | OPT_PERFILE | OPT_OUTPUT,           { .func_arg = opt_audio_filters },
          "set audio filters", "filter_graph" },
        { "guess_layout_max", OPT_AUDIO | HAS_ARG | OPT_INT | OPT_SPEC | OPT_EXPERT | OPT_INPUT, { .off = OFFSET(guess_layout_max) },
          "set the maximum number of channels to try to guess the channel layout" },

        /* subtitle options */
        { "sn",     OPT_SUBTITLE | OPT_BOOL | OPT_OFFSET | OPT_INPUT | OPT_OUTPUT, { .off = OFFSET(subtitle_disable) },
          "disable subtitle" },
        { "scodec", OPT_SUBTITLE | HAS_ARG  | OPT_PERFILE | OPT_INPUT | OPT_OUTPUT, { .func_arg = opt_subtitle_codec },
          "force subtitle codec ('copy' to copy stream)", "codec" },
        { "stag",   OPT_SUBTITLE | HAS_ARG  | OPT_EXPERT  | OPT_PERFILE | OPT_OUTPUT, { .func_arg = opt_old2new }
                , "force subtitle tag/fourcc", "fourcc/tag" },
        { "fix_sub_duration", OPT_BOOL | OPT_EXPERT | OPT_SUBTITLE | OPT_SPEC | OPT_INPUT, { .off = OFFSET(fix_sub_duration) },
          "fix subtitles duration" },
        { "canvas_size", OPT_SUBTITLE | HAS_ARG | OPT_STRING | OPT_SPEC | OPT_INPUT, { .off = OFFSET(canvas_sizes) },
          "set canvas size (WxH or abbreviation)", "size" },

        /* grab options */
        { "vc", HAS_ARG | OPT_EXPERT | OPT_VIDEO, { .func_arg = opt_video_channel },
          "deprecated, use -channel", "channel" },
        { "tvstd", HAS_ARG | OPT_EXPERT | OPT_VIDEO, { .func_arg = opt_video_standard },
          "deprecated, use -standard", "standard" },
        { "isync", OPT_BOOL | OPT_EXPERT, { .off = RUN_CTX_OFFSET(input_sync) }, "this option is deprecated and does nothing", "" },

        /* muxer options */
        { "muxdelay",   OPT_FLOAT | HAS_ARG | OPT_EXPERT | OPT_OFFSET | OPT_OUTPUT, { .off = OFFSET(mux_max_delay) },
          "set the maximum demux-decode delay", "seconds" },
        { "muxpreload", OPT_FLOAT | HAS_ARG | OPT_EXPERT | OPT_OFFSET | OPT_OUTPUT, { .off = OFFSET(mux_preload) },
          "set the initial demux-decode delay", "seconds" },
        { "sdp_file", HAS_ARG | OPT_EXPERT | OPT_OUTPUT, { .func_arg = opt_sdp_file },
          "specify a file in which to print sdp information", "file" },

        { "time_base", HAS_ARG | OPT_STRING | OPT_EXPERT | OPT_SPEC | OPT_OUTPUT, { .off = OFFSET(time_bases) },
          "set the desired time base hint for output stream (1:24, 1:48000 or 0.04166, 2.0833e-5)", "ratio" },
        { "enc_time_base", HAS_ARG | OPT_STRING | OPT_EXPERT | OPT_SPEC | OPT_OUTPUT, { .off = OFFSET(enc_time_bases) },
          "set the desired time base for the encoder (1:24, 1:48000 or 0.04166, 2.0833e-5). "
          "two special values are defined - "
          "0 = use frame rate (video) or sample rate (audio),"
          "-1 = match source time base", "ratio" },

        { "bsf", HAS_ARG | OPT_STRING | OPT_SPEC | OPT_EXPERT | OPT_OUTPUT, { .off = OFFSET(bitstream_filters) },
          "A comma-separated list of bitstream filters", "bitstream_filters" },
        { "absf", HAS_ARG | OPT_AUDIO | OPT_EXPERT| OPT_PERFILE | OPT_OUTPUT, { .func_arg = opt_old2new },
          "deprecated", "audio bitstream_filters" },
        { "vbsf", OPT_VIDEO | HAS_ARG | OPT_EXPERT| OPT_PERFILE | OPT_OUTPUT, { .func_arg = opt_old2new },
          "deprecated", "video bitstream_filters" },

        { "apre", HAS_ARG | OPT_AUDIO | OPT_EXPERT| OPT_PERFILE | OPT_OUTPUT,    { .func_arg = opt_preset },
          "set the audio options to the indicated preset", "preset" },
        { "vpre", OPT_VIDEO | HAS_ARG | OPT_EXPERT| OPT_PERFILE | OPT_OUTPUT,    { .func_arg = opt_preset },
          "set the video options to the indicated preset", "preset" },
        { "spre", HAS_ARG | OPT_SUBTITLE | OPT_EXPERT| OPT_PERFILE | OPT_OUTPUT, { .func_arg = opt_preset },
          "set the subtitle options to the indicated preset", "preset" },
        { "fpre", HAS_ARG | OPT_EXPERT| OPT_PERFILE | OPT_OUTPUT,                { .func_arg = opt_preset },
          "set options from indicated preset file", "filename" },

        { "max_muxing_queue_size", HAS_ARG | OPT_INT | OPT_SPEC | OPT_EXPERT | OPT_OUTPUT, { .off = OFFSET(max_muxing_queue_size) },
          "maximum number of packets that can be buffered while waiting for all streams to initialize", "packets" },
        { "muxing_queue_data_threshold", HAS_ARG | OPT_INT | OPT_SPEC | OPT_EXPERT | OPT_OUTPUT, { .off = OFFSET(muxing_queue_data_threshold) },
          "set the threshold after which max_muxing_queue_size is taken into account", "bytes" },

        /* data codec support */
        { "dcodec", HAS_ARG | OPT_DATA | OPT_PERFILE | OPT_EXPERT | OPT_INPUT | OPT_OUTPUT, { .func_arg = opt_data_codec },
          "force data codec ('copy' to copy stream)", "codec" },
        { "dn", OPT_BOOL | OPT_VIDEO | OPT_OFFSET | OPT_INPUT | OPT_OUTPUT, { .off = OFFSET(data_disable) },
          "disable data" },

#if CONFIG_VAAPI
        { "vaapi_device", HAS_ARG | OPT_EXPERT, { .func_arg = opt_vaapi_device },
        "set VAAPI hardware device (DRM path or X11 display name)", "device" },
#endif

#if CONFIG_QSV
        { "qsv_device", HAS_ARG | OPT_STRING | OPT_EXPERT, { &qsv_device },
        "set QSV hardware device (DirectX adapter index, DRM path or X11 display name)", "device"},
#endif

        { "init_hw_device", HAS_ARG | OPT_EXPERT, { .func_arg = opt_init_hw_device },
          "initialise hardware device", "args" },
        { "filter_hw_device", HAS_ARG | OPT_EXPERT, { .func_arg = opt_filter_hw_device },
          "set hardware device used when filtering", "device" },

        { NULL, },
};

static int opt_init_hw_device(void *optctx, const char *opt, const char *arg)
{
    OptionsContext  * o = optctx;
    if (!strcmp(arg, "list")) {
        enum AVHWDeviceType type = AV_HWDEVICE_TYPE_NONE;
        av_log(NULL,AV_LOG_INFO,"tid=%s,Supported hardware device types:\n",o->run_context_ref->trace_id);
        while ((type = av_hwdevice_iterate_types(type)) !=
               AV_HWDEVICE_TYPE_NONE)
            av_log(NULL,AV_LOG_INFO,"tid=%s,%s\n", o->run_context_ref->trace_id,av_hwdevice_get_type_name(type));
//        exit_program(0);
        return -1;
    } else {

        return hw_device_init_from_string(o->run_context_ref,arg, NULL);
    }
}

static int opt_filter_hw_device(void *optctx, const char *opt, const char *arg)
{
    OptionsContext  * o = optctx;
    RunContext * run_context = o->run_context_ref;
    char * trace_id = run_context->trace_id;
    if (run_context->filter_hw_device) {
        av_log(NULL, AV_LOG_ERROR, "tid=%s,Only one filter device can be used.\n",trace_id);
        return AVERROR(EINVAL);
    }
    run_context->filter_hw_device = hw_device_get_by_name(run_context,arg);
    if (!run_context->filter_hw_device) {
        av_log(NULL, AV_LOG_ERROR, "tid=%s,Invalid filter device %s.\n", trace_id,arg);
        return AVERROR(EINVAL);
    }
    return 0;
}

static int opt_map(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    StreamMap *m = NULL;
    int i, negative = 0, file_idx, disabled = 0;
    int sync_file_idx = -1, sync_stream_idx = 0;
    char *p, *sync;
    char *map;
    char *allow_unused;

    if (*arg == '-') {
        negative = 1;
        arg++;
    }
    map = av_strdup(arg);
    if (!map)
        return AVERROR(ENOMEM);

    char * trace_id =o->run_context_ref->trace_id;

    RunContext* run_context = o->run_context_ref;

    /* parse sync stream first, just pick first matching stream */
    if (sync = strchr(map, ',')) {
        *sync = 0;
        sync_file_idx = strtol(sync + 1, &sync, 0);
        if (sync_file_idx >= run_context->option_input.nb_input_files || sync_file_idx < 0) {
            av_log(NULL, AV_LOG_FATAL, "tid=%s,Invalid sync file index: %d.\n", trace_id,sync_file_idx);
//            exit_program(1);
            return -1;
        }
        if (*sync)
            sync++;
        for (i = 0; i < run_context->option_input.input_files[sync_file_idx]->nb_streams; i++)
            if (check_stream_specifier(run_context->option_input.input_files[sync_file_idx]->ctx,
                                       run_context->option_input.input_files[sync_file_idx]->ctx->streams[i], sync) == 1) {
                sync_stream_idx = i;
                break;
            }
        if (i == run_context->option_input.input_files[sync_file_idx]->nb_streams) {
            av_log(NULL, AV_LOG_FATAL, "tid=%s,Sync stream specification in map %s does not "
                                       "match any streams.\n",trace_id, arg);
//            exit_program(1);
            return -1;
        }
        if (run_context->option_input.input_streams[run_context->option_input.input_files[sync_file_idx]->ist_index + sync_stream_idx]->user_set_discard == AVDISCARD_ALL) {
            av_log(NULL, AV_LOG_FATAL, "tid=%s,Sync stream specification in map %s matches a disabled input "
                                       "stream.\n", trace_id,arg);
//            exit_program(1);
            return -1;
        }
    }


    if (map[0] == '[') {
        /* this mapping refers to lavfi output */
        const char *c = map + 1;
        int has_err = 0;
        GROW_ARRAY(trace_id,o->stream_maps, o->nb_stream_maps,has_err);
        if(has_err < 0){
            return -1;
        }
        m = &o->stream_maps[o->nb_stream_maps - 1];
        m->linklabel = av_get_token(&c, "]");
        if (!m->linklabel) {
            av_log(NULL, AV_LOG_ERROR, "tid=%s,Invalid output link label: %s.\n",trace_id, map);
//            exit_program(1);
            return  -1;
        }
    } else {
        if (allow_unused = strchr(map, '?'))
            *allow_unused = 0;
        file_idx = strtol(map, &p, 0);
        if (file_idx >= run_context->option_input.nb_input_files || file_idx < 0) {
            av_log(NULL, AV_LOG_FATAL, "tid=%s,Invalid input file index: %d.\n", trace_id,file_idx);
//            exit_program(1);
            return -1;
        }
        if (negative)
            /* disable some already defined maps */
            for (i = 0; i < o->nb_stream_maps; i++) {
                m = &o->stream_maps[i];
                if (file_idx == m->file_index &&
                    check_stream_specifier(run_context->option_input.input_files[m->file_index]->ctx,
                                           run_context->option_input.input_files[m->file_index]->ctx->streams[m->stream_index],
                                           *p == ':' ? p + 1 : p) > 0)
                    m->disabled = 1;
            }
        else
            for (i = 0; i < run_context->option_input.input_files[file_idx]->nb_streams; i++) {
                if (check_stream_specifier(run_context->option_input.input_files[file_idx]->ctx, run_context->option_input.input_files[file_idx]->ctx->streams[i],
                                           *p == ':' ? p + 1 : p) <= 0)
                    continue;
                if (run_context->option_input.input_streams[run_context->option_input.input_files[file_idx]->ist_index + i]->user_set_discard == AVDISCARD_ALL) {
                    disabled = 1;
                    continue;
                }
                int has_err = 0;
                GROW_ARRAY(trace_id,o->stream_maps, o->nb_stream_maps,has_err);
                if(has_err < 0){
                    return -1;
                }
                m = &o->stream_maps[o->nb_stream_maps - 1];

                m->file_index   = file_idx;
                m->stream_index = i;

                if (sync_file_idx >= 0) {
                    m->sync_file_index   = sync_file_idx;
                    m->sync_stream_index = sync_stream_idx;
                } else {
                    m->sync_file_index   = file_idx;
                    m->sync_stream_index = i;
                }
            }
    }

    if (!m) {
        if (allow_unused) {
            av_log(NULL, AV_LOG_VERBOSE, "tid=%s,Stream map '%s' matches no streams; ignoring.\n", trace_id,arg);
        } else if (disabled) {
            av_log(NULL, AV_LOG_FATAL, "tid=%s,Stream map '%s' matches disabled streams.\n"
                                       "To ignore this, add a trailing '?' to the map.\n",trace_id, arg);
//            exit_program(1);
            return -1;
        } else {
            av_log(NULL, AV_LOG_FATAL, "tid=%s,Stream map '%s' matches no streams.\n"
                                       "To ignore this, add a trailing '?' to the map.\n", trace_id,arg);
//            exit_program(1);
            return  -1;
        }
    }

    av_freep(&map);
    return 0;
}

static int opt_map_channel(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    int n;
    AVStream *st;
    AudioChannelMap *m;
    char *allow_unused;
    char *mapchan;
    mapchan = av_strdup(arg);
    if (!mapchan)
        return AVERROR(ENOMEM);

    RunContext * run_context = o->run_context_ref;
    char * trace_id = run_context->trace_id;
    int has_err = 0;
    GROW_ARRAY(trace_id,o->audio_channel_maps, o->nb_audio_channel_maps,has_err);
    if(has_err < 0){
        return -1;
    }
    m = &o->audio_channel_maps[o->nb_audio_channel_maps - 1];

    /* muted channel syntax */
    n = sscanf(arg, "%d:%d.%d", &m->channel_idx, &m->ofile_idx, &m->ostream_idx);
    if ((n == 1 || n == 3) && m->channel_idx == -1) {
        m->file_idx = m->stream_idx = -1;
        if (n == 1)
            m->ofile_idx = m->ostream_idx = -1;
        av_free(mapchan);
        return 0;
    }

    /* normal syntax */
    n = sscanf(arg, "%d.%d.%d:%d.%d",
               &m->file_idx,  &m->stream_idx, &m->channel_idx,
               &m->ofile_idx, &m->ostream_idx);

    if (n != 3 && n != 5) {
        av_log(NULL, AV_LOG_FATAL, "tid=%s,Syntax error, mapchan usage: "
                                   "[file.stream.channel|-1][:syncfile:syncstream]\n",trace_id);
//        exit_program(1);
        return -1;
    }

    if (n != 5) // only file.stream.channel specified
        m->ofile_idx = m->ostream_idx = -1;

    /* check input */
    if (m->file_idx < 0 || m->file_idx >= run_context->option_input.nb_input_files) {
        av_log(NULL, AV_LOG_FATAL, "tid=%s,mapchan: invalid input file index: %d\n",
               trace_id, m->file_idx);
//        exit_program(1);
        return -1;
    }
    if (m->stream_idx < 0 ||
        m->stream_idx >= run_context->option_input.input_files[m->file_idx]->nb_streams) {
        av_log(NULL, AV_LOG_FATAL, "tid=%s,mapchan: invalid input file stream index #%d.%d\n",
               trace_id, m->file_idx, m->stream_idx);
//        exit_program(1);
        return -1;
    }
    st = run_context->option_input.input_files[m->file_idx]->ctx->streams[m->stream_idx];
    if (st->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
        av_log(NULL, AV_LOG_FATAL, "tid=%s,mapchan: stream #%d.%d is not an audio stream.\n",
               trace_id, m->file_idx, m->stream_idx);
//        exit_program(1);
        return -1;
    }
    /* allow trailing ? to map_channel */
    if (allow_unused = strchr(mapchan, '?'))
        *allow_unused = 0;
    if (m->channel_idx < 0 || m->channel_idx >= st->codecpar->channels ||
        run_context->option_input.input_streams[run_context->option_input.input_files[m->file_idx]->ist_index + m->stream_idx]->user_set_discard == AVDISCARD_ALL) {
        if (allow_unused) {
            av_log(NULL, AV_LOG_VERBOSE, "mapchan: invalid audio channel #%d.%d.%d\n",
                   m->file_idx, m->stream_idx, m->channel_idx);
        } else {
            av_log(NULL, AV_LOG_FATAL,  "tid=%s,mapchan: invalid audio channel #%d.%d.%d\n"
                                        "To ignore this, add a trailing '?' to the map_channel.\n",
                   trace_id, m->file_idx, m->stream_idx, m->channel_idx);
//            exit_program(1);
            return -1;
        }

    }
    av_free(mapchan);
    return 0;
}

static int opt_bitrate(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;

    if(!strcmp(opt, "ab")){
        av_dict_set(&o->g->codec_opts, "b:a", arg, 0);
        return 0;
    } else if(!strcmp(opt, "b")){
        av_log(NULL, AV_LOG_WARNING, "tid=%s,Please use -b:a or -b:v, -b is ambiguous\n",o->run_context_ref->trace_id);
        av_dict_set(&o->g->codec_opts, "b:v", arg, 0);
        return 0;
    }
    av_dict_set(&o->g->codec_opts, opt, arg, 0);
    return 0;
}

static int opt_vsync(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    if      (!av_strcasecmp(arg, "cfr"))         o->run_context_ref->video_sync_method = VSYNC_CFR;
    else if (!av_strcasecmp(arg, "vfr"))         o->run_context_ref->video_sync_method = VSYNC_VFR;
    else if (!av_strcasecmp(arg, "passthrough")) o->run_context_ref->video_sync_method = VSYNC_PASSTHROUGH;
    else if (!av_strcasecmp(arg, "drop"))        o->run_context_ref->video_sync_method = VSYNC_DROP;

    if (o->run_context_ref->video_sync_method == VSYNC_AUTO){
        int has_error = 0;
        o->run_context_ref->video_sync_method = parse_number_or_die(o->run_context_ref->trace_id,"vsync", arg, OPT_INT, VSYNC_AUTO, VSYNC_VFR, &has_error);
        if(has_error < 0){
            return  - 1;
        }
    }
    return 0;
}

static int opt_streamid(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    int idx;
    char *p;
    char idx_str[16];

    char * trace_id =o->run_context_ref->trace_id;
    av_strlcpy(idx_str, arg, sizeof(idx_str));
    p = strchr(idx_str, ':');
    if (!p) {
        av_log(NULL, AV_LOG_FATAL,
               "tid=%s,Invalid value '%s' for option '%s', required syntax is 'index:value'\n",trace_id,
               arg, opt);
//        exit_program(1);
        return -1;
    }
    *p++ = '\0';
    int has_error = 0;
    idx = parse_number_or_die(trace_id,opt, idx_str, OPT_INT, 0, MAX_STREAMS-1,&has_error);
    if(has_error < 0){ return -1;}
    o->streamid_map = grow_array(trace_id,o->streamid_map, sizeof(*o->streamid_map), &o->nb_streamid_map, idx+1,&has_error);
    if(has_error < 0){ return -1;}
    o->streamid_map[idx] = parse_number_or_die(trace_id,opt, p, OPT_INT, 0, INT_MAX,&has_error);
    if(has_error < 0){ return -1;}
    return 0;
}

static int opt_timecode(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    int ret;
    char *tcr = av_asprintf("timecode=%s", arg);
    if (!tcr)
        return AVERROR(ENOMEM);
    ret = parse_option(o, "metadata:g", tcr, options);
    if (ret >= 0)
        ret = av_dict_set(&o->g->codec_opts, "gop_timecode", arg, 0);
    av_free(tcr);
    return ret;
}

static int opt_filter_complex_script(void *optctx, const char *opt, const char *arg)
{
    uint8_t *graph_desc = read_file(arg);
    if (!graph_desc)
        return AVERROR(EINVAL);

    OptionsContext *o = optctx;
    int has_error = 0;

    char * trace_id = o->run_context_ref->trace_id;

    GROW_ARRAY(trace_id,o->run_context_ref->filtergraphs, o->run_context_ref->nb_filtergraphs, has_error);
    if (!(o->run_context_ref->filtergraphs[o->run_context_ref->nb_filtergraphs - 1] = av_mallocz(sizeof(*o->run_context_ref->filtergraphs[0]))))
        return AVERROR(ENOMEM);
    o->run_context_ref->filtergraphs[o->run_context_ref->nb_filtergraphs - 1]->index      = o->run_context_ref->nb_filtergraphs - 1;
    o->run_context_ref->filtergraphs[o->run_context_ref->nb_filtergraphs - 1]->graph_desc = graph_desc;

    o->run_context_ref->input_stream_potentially_available = 1;

    return 0;
}

static int opt_filter_complex(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    int has_error = 0;
    GROW_ARRAY(o->run_context_ref->trace_id,o->run_context_ref->filtergraphs, o->run_context_ref->nb_filtergraphs, has_error);
    if(has_error < 0){
        return -1;
    }
    if (!(o->run_context_ref->filtergraphs[o->run_context_ref->nb_filtergraphs - 1] = av_mallocz(sizeof(*o->run_context_ref->filtergraphs[0]))))
        return AVERROR(ENOMEM);
    o->run_context_ref->filtergraphs[o->run_context_ref->nb_filtergraphs - 1]->index      = o->run_context_ref->nb_filtergraphs - 1;
    o->run_context_ref->filtergraphs[o->run_context_ref->nb_filtergraphs - 1]->graph_desc = av_strdup(arg);
    if (!o->run_context_ref->filtergraphs[o->run_context_ref->nb_filtergraphs - 1]->graph_desc)
        return AVERROR(ENOMEM);

    o->run_context_ref->input_stream_potentially_available = 1;

    return 0;
}

static int opt_preset(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    FILE *f=NULL;
    char filename[1000], line[1000], tmp_line[1000];
    const char *codec_name = NULL;

    char * trace_id = o->run_context_ref->trace_id;

    tmp_line[0] = *opt;
    tmp_line[1] = 0;
    MATCH_PER_TYPE_OPT(codec_names, str, codec_name, NULL, tmp_line);

    if (!(f = get_preset_file(filename, sizeof(filename), arg, *opt == 'f', codec_name))) {
        if(!strncmp(arg, "libx264-lossless", strlen("libx264-lossless"))){
            av_log(NULL, AV_LOG_FATAL, "tid=%s,Please use -preset <speed> -qp 0\n",trace_id);
        }else
            av_log(NULL, AV_LOG_FATAL, "tid=%s,File for preset '%s' not found\n", arg,trace_id);
        goto fail;
    }

    while (fgets(line, sizeof(line), f)) {
        char *key = tmp_line, *value, *endptr;

        if (strcspn(line, "#\n\r") == 0)
            continue;
        av_strlcpy(tmp_line, line, sizeof(tmp_line));
        if (!av_strtok(key,   "=",    &value) ||
            !av_strtok(value, "\r\n", &endptr)) {
            av_log(NULL, AV_LOG_FATAL, "tid=%s,%s: Invalid syntax: '%s'\n", trace_id,filename, line);
            goto fail;
        }
        av_log(NULL, AV_LOG_DEBUG, "tid=%s,ffpreset[%s]: set '%s' = '%s'\n",trace_id, filename, key, value);

        if      (!strcmp(key, "acodec")) opt_audio_codec   (o, key, value);
        else if (!strcmp(key, "vcodec")) opt_video_codec   (o, key, value);
        else if (!strcmp(key, "scodec")) opt_subtitle_codec(o, key, value);
        else if (!strcmp(key, "dcodec")) opt_data_codec    (o, key, value);
        else if (opt_default_new(o, key, value) < 0) {
            av_log(NULL, AV_LOG_FATAL, "tid=%s,%s: Invalid option or argument: '%s', parsed as '%s' = '%s'\n",
                   trace_id,filename, line, key, value);
            goto fail;
        }
    }

    fclose(f);

    return 0;
fail:
    return -1;
}


static int opt_sdp_file(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    av_free(o->run_context_ref->sdp_filename);
    o->run_context_ref->sdp_filename = av_strdup(arg);
    return 0;
}

static int opt_video_frames(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    return parse_option(o, "frames:v", arg, options);
}

static int opt_audio_frames(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    return parse_option(o, "frames:a", arg, options);
}

static int opt_audio_qscale(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    return parse_option(o, "q:a", arg, options);
}

static int opt_recording_timestamp(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    char buf[128];
    int has_error = 0;
    char * trace_id = o->run_context_ref->trace_id;
    int64_t recording_timestamp = parse_time_or_die(trace_id,opt, arg, 0,&has_error) / 1E6;
    if(has_error < 0){
        return -1;
    }

    struct tm time = *gmtime((time_t*)&recording_timestamp);
    if (!strftime(buf, sizeof(buf), "creation_time=%Y-%m-%dT%H:%M:%S%z", &time))
        return -1;

    parse_option(o, "metadata", buf, options);

    av_log(NULL, AV_LOG_WARNING, "tid=%s,%s is deprecated, set the 'creation_time' metadata "
                                 "tag instead.\n",trace_id, opt);
    return 0;
}

static int opt_audio_filters(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    return parse_option(o, "filter:a", arg, options);
}


static int opt_video_filters(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    return parse_option(o, "filter:v", arg, options);
}

static int opt_old2new(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    int ret;
    char *s = av_asprintf("%s:%c", opt + 1, *opt);
    if (!s)
        return AVERROR(ENOMEM);
    ret = parse_option(o, s, arg, options);
    av_free(s);
    return ret;
}

static int opt_data_frames(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    return parse_option(o, "frames:d", arg, options);
}

static int opt_qscale(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    char *s;
    int ret;
    if(!strcmp(opt, "qscale")){
        av_log(NULL, AV_LOG_WARNING, "tid=%s,Please use -q:a or -q:v, -qscale is ambiguous\n",o->run_context_ref->trace_id);
        return parse_option(o, "q:v", arg, options);
    }
    s = av_asprintf("q%s", opt + 6);
    if (!s)
        return AVERROR(ENOMEM);
    ret = parse_option(o, s, arg, options);
    av_free(s);
    return ret;
}

static int opt_video_channel(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    av_log(NULL, AV_LOG_WARNING, "tid=%s,This option is deprecated, use -channel.\n",o->run_context_ref->trace_id);
    return opt_default(optctx, "channel", arg);
}

static int opt_video_standard(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    av_log(NULL, AV_LOG_WARNING, "tid=%s,This option is deprecated, use -standard.\n",o->run_context_ref->trace_id);
    return opt_default(optctx, "standard", arg);
}


static int opt_audio_codec(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    return parse_option(o, "codec:a", arg, options);
}

static int opt_video_codec(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    return parse_option(o, "codec:v", arg, options);
}

static int opt_subtitle_codec(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    return parse_option(o, "codec:s", arg, options);
}

static int opt_default_new(OptionsContext *o, const char *opt, const char *arg)
{
    int ret;
    AVDictionary *cbak = o->run_context_ref->codec_opts;
    AVDictionary *fbak = o->run_context_ref->format_opts;
    o->run_context_ref->codec_opts = NULL;
    o->run_context_ref->format_opts = NULL;

    ret = opt_default(NULL, opt, arg);

    av_dict_copy(&o->g->codec_opts , o->run_context_ref->codec_opts, 0);
    av_dict_copy(&o->g->format_opts, o->run_context_ref->format_opts, 0);
    av_dict_free(&o->run_context_ref->codec_opts);
    av_dict_free(&o->run_context_ref->format_opts);
    o->run_context_ref->codec_opts = cbak;
    o->run_context_ref->format_opts = fbak;

    return ret;
}

static int opt_channel_layout(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    char layout_str[32];
    char *stream_str;
    char *ac_str;
    int ret, channels, ac_str_size;
    uint64_t layout;

    char * trace_id = o->run_context_ref->trace_id;

    layout = av_get_channel_layout(arg);
    if (!layout) {
        av_log(NULL, AV_LOG_ERROR, "tid=%s,Unknown channel layout: %s\n",trace_id, arg);
        return AVERROR(EINVAL);
    }
    snprintf(layout_str, sizeof(layout_str), "%"PRIu64, layout);
    ret = opt_default_new(o, opt, layout_str);
    if (ret < 0)
        return ret;

    /* set 'ac' option based on channel layout */
    channels = av_get_channel_layout_nb_channels(layout);
    snprintf(layout_str, sizeof(layout_str), "%d", channels);
    stream_str = strchr(opt, ':');
    ac_str_size = 3 + (stream_str ? strlen(stream_str) : 0);
    ac_str = av_mallocz(ac_str_size);
    if (!ac_str)
        return AVERROR(ENOMEM);
    av_strlcpy(ac_str, "ac", 3);
    if (stream_str)
        av_strlcat(ac_str, stream_str, ac_str_size);
    ret = parse_option(o, ac_str, layout_str, options);
    av_free(ac_str);

    return ret;
}

static int opt_data_codec(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    return parse_option(o, "codec:d", arg, options);
}

static int opt_target(void *optctx, const char *opt, const char *arg)
{
    OptionsContext *o = optctx;
    enum { PAL, NTSC, FILM, UNKNOWN } norm = UNKNOWN;
//    static const char *const frame_rates[] = { "25", "30000/1001", "24000/1001" };

//    char ** frame_rates = o->run_context_ref->frame_rates;
    if (!strncmp(arg, "pal-", 4)) {
        norm = PAL;
        arg += 4;
    } else if (!strncmp(arg, "ntsc-", 5)) {
        norm = NTSC;
        arg += 5;
    } else if (!strncmp(arg, "film-", 5)) {
        norm = FILM;
        arg += 5;
    } else {
        /* Try to determine PAL/NTSC by peeking in the input files */
        if (o->run_context_ref->option_input.nb_input_files) {
            int i, j;
            for (j = 0; j < o->run_context_ref->option_input.nb_input_files; j++) {
                for (i = 0; i < o->run_context_ref->option_input.input_files[j]->nb_streams; i++) {
                    AVStream *st = o->run_context_ref->option_input.input_files[j]->ctx->streams[i];
                    int64_t fr;
                    if (st->codecpar->codec_type != AVMEDIA_TYPE_VIDEO)
                        continue;
                    fr = st->time_base.den * 1000LL / st->time_base.num;
                    if (fr == 25000) {
                        norm = PAL;
                        break;
                    } else if ((fr == 29970) || (fr == 23976)) {
                        norm = NTSC;
                        break;
                    }
                }
                if (norm != UNKNOWN)
                    break;
            }
        }
        if (norm != UNKNOWN)
            av_log(NULL, AV_LOG_INFO, "Assuming %s for target.\n", norm == PAL ? "PAL" : "NTSC");
    }

    if (norm == UNKNOWN) {
        av_log(NULL, AV_LOG_FATAL, "Could not determine norm (PAL/NTSC/NTSC-Film) for target.\n");
        av_log(NULL, AV_LOG_FATAL, "Please prefix target with \"pal-\", \"ntsc-\" or \"film-\",\n");
        av_log(NULL, AV_LOG_FATAL, "or set a framerate with \"-r xxx\".\n");
        return AVERROR(EINVAL);
    }


    if (!strcmp(arg, "vcd")) {
        opt_video_codec(o, "c:v", "mpeg1video");
        opt_audio_codec(o, "c:a", "mp2");
        parse_option(o, "f", "vcd", options);

        parse_option(o, "s", norm == PAL ? "352x288" : "352x240", options);
        parse_option(o, "r", G_FRAME_RATES[norm], options);
        opt_default(NULL, "g", norm == PAL ? "15" : "18");

        opt_default(NULL, "b:v", "1150000");
        opt_default(NULL, "maxrate:v", "1150000");
        opt_default(NULL, "minrate:v", "1150000");
        opt_default(NULL, "bufsize:v", "327680"); // 40*1024*8;

        opt_default(NULL, "b:a", "224000");
        parse_option(o, "ar", "44100", options);
        parse_option(o, "ac", "2", options);

        opt_default(NULL, "packetsize", "2324");
        opt_default(NULL, "muxrate", "1411200"); // 2352 * 75 * 8;

        /* We have to offset the PTS, so that it is consistent with the SCR.
           SCR starts at 36000, but the first two packs contain only padding
           and the first pack from the other stream, respectively, may also have
           been written before.
           So the real data starts at SCR 36000+3*1200. */
        o->mux_preload = (36000 + 3 * 1200) / 90000.0; // 0.44
    } else if (!strcmp(arg, "svcd")) {

        opt_video_codec(o, "c:v", "mpeg2video");
        opt_audio_codec(o, "c:a", "mp2");
        parse_option(o, "f", "svcd", options);

        parse_option(o, "s", norm == PAL ? "480x576" : "480x480", options);
        parse_option(o, "r", G_FRAME_RATES[norm], options);
        parse_option(o, "pix_fmt", "yuv420p", options);
        opt_default(NULL, "g", norm == PAL ? "15" : "18");

        opt_default(NULL, "b:v", "2040000");
        opt_default(NULL, "maxrate:v", "2516000");
        opt_default(NULL, "minrate:v", "0"); // 1145000;
        opt_default(NULL, "bufsize:v", "1835008"); // 224*1024*8;
        opt_default(NULL, "scan_offset", "1");

        opt_default(NULL, "b:a", "224000");
        parse_option(o, "ar", "44100", options);

        opt_default(NULL, "packetsize", "2324");

    } else if (!strcmp(arg, "dvd")) {

        opt_video_codec(o, "c:v", "mpeg2video");
        opt_audio_codec(o, "c:a", "ac3");
        parse_option(o, "f", "dvd", options);

        parse_option(o, "s", norm == PAL ? "720x576" : "720x480", options);
        parse_option(o, "r", G_FRAME_RATES[norm], options);
        parse_option(o, "pix_fmt", "yuv420p", options);
        opt_default(NULL, "g", norm == PAL ? "15" : "18");

        opt_default(NULL, "b:v", "6000000");
        opt_default(NULL, "maxrate:v", "9000000");
        opt_default(NULL, "minrate:v", "0"); // 1500000;
        opt_default(NULL, "bufsize:v", "1835008"); // 224*1024*8;

        opt_default(NULL, "packetsize", "2048");  // from www.mpucoder.com: DVD sectors contain 2048 bytes of data, this is also the size of one pack.
        opt_default(NULL, "muxrate", "10080000"); // from mplex project: data_rate = 1260000. mux_rate = data_rate * 8

        opt_default(NULL, "b:a", "448000");
        parse_option(o, "ar", "48000", options);

    } else if (!strncmp(arg, "dv", 2)) {

        parse_option(o, "f", "dv", options);

        parse_option(o, "s", norm == PAL ? "720x576" : "720x480", options);
        parse_option(o, "pix_fmt", !strncmp(arg, "dv50", 4) ? "yuv422p" :
                                   norm == PAL ? "yuv420p" : "yuv411p", options);
        parse_option(o, "r", G_FRAME_RATES[norm], options);

        parse_option(o, "ar", "48000", options);
        parse_option(o, "ac", "2", options);

    } else {
        av_log(NULL, AV_LOG_ERROR, "Unknown target: %s\n", arg);
        return AVERROR(EINVAL);
    }

    av_dict_copy(&o->g->codec_opts, o->run_context_ref->codec_opts, AV_DICT_DONT_OVERWRITE);
    av_dict_copy(&o->g->format_opts, o->run_context_ref->format_opts, AV_DICT_DONT_OVERWRITE);

    return 0;
}


int parse_command(char * cmd,char * argv[]){
    char * p = cmd;
    TRIM(p)
    char * start = p;
    int count = 0;
    while(*p){
        if(*p == ' '){
            *p++ = '\0';
            argv[count++] = start;
            TRIM(p)
            start = p;
            continue;
        }
        p++;
    }
    if(start){
        argv[count++] = start;
    }
    return count;
}


int parse_cmd_options(char * cmd, ParsedOptionsContext *parent_context){
    uint8_t error[128];
    int ret;

    int argc;
    char * argv[256];

    int cmd_len = strlen(cmd);
    char * recv = calloc(cmd_len + 1,1);
    strcpy(recv,cmd);


    argc = parse_command(recv,argv);

    ParseContext *p_opctx = malloc(sizeof(ParseContext));
    memset(p_opctx, 0, sizeof(*p_opctx));
    parent_context->parse_context = p_opctx;

    p_opctx->copy_cmd = recv;

    /* split the commandline into an internal representation */
    ret = split_commandline(p_opctx, argc, argv, options, parent_context);
    if (ret < 0) {
        av_log(NULL, AV_LOG_FATAL, "Error splitting the argument list: ");
        goto fail;
    }

    /* apply global options */
    ret = parse_optgroup(parent_context, &p_opctx->global_opts);
    if (ret < 0) {
        av_log(NULL, AV_LOG_FATAL, "Error parsing global options: ");
        goto fail;
    }


fail:
    if (ret < 0) {
//        uninit_parse_context(p_opctx);
//        parent_context->parse_context = NULL;
        av_strerror(ret, error, sizeof(error));
        av_log(NULL, AV_LOG_FATAL, "%s\n", error);
    }
    return ret;
}


int show_hwaccels()
{
    enum AVHWDeviceType type = AV_HWDEVICE_TYPE_NONE;

    printf("Hardware acceleration methods:\n");
    while ((type = av_hwdevice_iterate_types(type)) !=
           AV_HWDEVICE_TYPE_NONE)
        printf("%s\n", av_hwdevice_get_type_name(type));
    printf("\n");
    return 0;
}

void init_ffmpeg(){
//    av_register_all();
#if CONFIG_AVDEVICE
    avdevice_register_all();
#endif
}

int run_ffmpeg_cmd(char * trace_id,char * cmd){
    int64_t start_time = get_timestamp();
    ParsedOptionsContext parent_context;
    memset(&parent_context, 0, sizeof(ParsedOptionsContext));
    parent_context.raw_context.trace_id = trace_id;
    int ret = parse_cmd_options(cmd, &parent_context);
    av_log(NULL, AV_LOG_INFO, "tid=%s,parse_cmd_options ret:%d\n",trace_id, ret);
    if (ret < 0) {
        ffmpegg_cleanup(&parent_context);
        return 0;
    }

    ret = open_stream(&parent_context);
    av_log(NULL, AV_LOG_INFO, "tid=%s,open_stream ret:%d\n", trace_id,ret);

    if(ret < 0){
        ffmpegg_cleanup(&parent_context);
        return 0;
    }

    if (parent_context.raw_context.option_output.nb_output_files <= 0 && parent_context.raw_context.option_input.nb_input_files == 0) {
        av_log(NULL, AV_LOG_INFO, "tid=%s,参数错误\n",trace_id);
        ffmpegg_cleanup(&parent_context);
        return 0;
    }


    ret = transcode(&parent_context.raw_context);
    av_log(NULL, AV_LOG_INFO, "tid=%s,transcode ret:%d\n", trace_id,ret);
    ffmpegg_cleanup(&parent_context);
    int64_t end_time = get_timestamp();
    av_log(NULL, AV_LOG_INFO, "tid=%s,cost %ld ms\n",trace_id,end_time - start_time);
    return ret;
}

const char * used_ext[] = {".mp3",".aac"};

int get_duration_next(char *trace_id,char *cmd,int next,int64_t * p_duration){
    * p_duration = -1;
    ParsedOptionsContext parent_context;
    memset(&parent_context, 0, sizeof(ParsedOptionsContext));
    parent_context.raw_context.trace_id = trace_id;
    int ret = parse_cmd_options(cmd, &parent_context);
    av_log(NULL, AV_LOG_INFO, "tid=%s,parse_cmd_options ret:%d\n", trace_id,ret);
    if (ret < 0) {
        ffmpegg_cleanup(&parent_context);
        return 0;
    }

    ret = open_stream(&parent_context);
    av_log(NULL, AV_LOG_INFO, "tid=%s,open_stream ret:%d\n",trace_id, ret);

    if(ret < 0){
        ffmpegg_cleanup(&parent_context);
        return 0;
    }

    *p_duration = parent_context.raw_context.option_input.duration;


    if(parent_context.raw_context.option_input.duration_estimation_method == AVFMT_DURATION_FROM_BITRATE){
        if(!next){
            return QUICK_DURATION_INCORRECT;
        }
        char * input_file = parent_context.parse_context->groups[1].groups[0].arg;
        char new_cmd[2048] = {0};
        char *ext = strchr(input_file,'.');
        int use_idx = 0;
        if(strcmp(ext,used_ext[0]) == 0){
            use_idx = 1;

        }
        *ext = '\0';
        sprintf(new_cmd,"%s %s%s",cmd,input_file,used_ext[use_idx]);
        ffmpegg_cleanup(&parent_context);

        ret =  run_ffmpeg_cmd(parent_context.raw_context.trace_id,new_cmd);
        if(ret < 0){
            *ext = '.';
            return ret;
        }
        memset(new_cmd,0,sizeof (new_cmd));
        sprintf(new_cmd,"ffmpeg -i %s%s",input_file,used_ext[use_idx]);
        *ext = '.';
        return get_duration_next(trace_id,new_cmd,1,p_duration);
    }else {
        ffmpegg_cleanup(&parent_context);
    }


    return  ret;
}

int quick_duration(char * trace_id,char *cmd, int64_t * p_duration){
    return get_duration_next(trace_id,cmd,0,p_duration);
}