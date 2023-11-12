//
// Created by hexiufeng on 2023/10/31.
//

#include "cmd_util.h"
#include "config.h"
#include <libavutil/avstring.h>
#include <libavutil/eval.h>
#include <libavutil/parseutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/display.h>
#include <libavutil/time.h>

#if HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif

extern const OptionDef options[];
static const OptionGroupDef const_groups[] = {
        [GROUP_OUTFILE] = { "output url",  NULL, OPT_OUTPUT },
        [GROUP_INFILE]  = { "input url",   "i",  OPT_INPUT },
};



static const OptionGroupDef global_group = { "global" };

static const OptionDef *find_option(const OptionDef *po, const char *name) {
    while (po->name) {
        const char *end;
        if (av_strstart(name, po->name, &end) && (!*end || *end == ':'))
            break;
        po++;
    }
    return po;
}

static int write_option(void *optctx, const OptionDef *po, const char *opt,
                        const char *arg) {
    /* new-style options contain an offset into optctx, old-style address of
     * a global var*/

    void *dst = po->flags & (OPT_OFFSET | OPT_SPEC|OPT_RUN_OFFSET) ?
                (uint8_t *) optctx + po->u.off : po->u.dst_ptr;
    int *dstcount;

    OptionsContext * o = optctx;

    char * trace_id = o->run_context_ref->trace_id;

    if (po->flags & OPT_SPEC) {
        SpecifierOpt **so = dst;
        char *p = strchr(opt, ':');
        char *str;

        dstcount = (int *) (so + 1);
        int has_error = 0;
        *so = grow_array(trace_id,*so, sizeof(**so), dstcount, *dstcount + 1, &has_error);
        if (has_error) {
            return AVERROR(ENOMEM);
        }
        str = av_strdup(p ? p + 1 : "");
        if (!str)
            return AVERROR(ENOMEM);
        (*so)[*dstcount - 1].specifier = str;
        dst = &(*so)[*dstcount - 1].u;
    }

    if (po->flags & OPT_STRING) {
        char *str;
        str = av_strdup(arg);
        av_freep(dst);
        if (!str)
            return AVERROR(ENOMEM);
        *(char **) dst = str;
    } else if (po->flags & OPT_BOOL || po->flags & OPT_INT) {
        int die = 0;
        *(int *) dst = parse_number_or_die(trace_id,opt, arg, OPT_INT64, INT_MIN, INT_MAX, &die);
        if (die) {
            return AVERROR(ENOMEM);
        }
    } else if (po->flags & OPT_INT64) {
        int die = 0;
        *(int64_t *) dst = parse_number_or_die(trace_id,opt, arg, OPT_INT64, INT64_MIN, INT64_MAX, &die);
        if (die) {
            return AVERROR(ENOMEM);
        }
    } else if (po->flags & OPT_TIME) {
        int die = 0;
        *(int64_t *) dst = parse_time_or_die(trace_id,opt, arg, 1, &die);
        if (die) {
            return AVERROR(ENOMEM);
        }
    } else if (po->flags & OPT_FLOAT) {
        int die = 0;
        *(float *) dst = parse_number_or_die(trace_id,opt, arg, OPT_FLOAT, -INFINITY, INFINITY, &die);
        if (die) {
            return AVERROR(ENOMEM);
        }
    } else if (po->flags & OPT_DOUBLE) {
        int die = 0;
        *(double *) dst = parse_number_or_die(trace_id,opt, arg, OPT_DOUBLE, -INFINITY, INFINITY, &die);
        if (die) {
            return AVERROR(ENOMEM);
        }
    } else if (po->u.func_arg) {
        int ret = po->u.func_arg(optctx, opt, arg);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR,
                   "tid=%s,Failed to set value '%s' for option '%s': %s\n",
                   trace_id,arg, opt, av_err2str(ret));
            return ret;
        }
    }
//    if (po->flags & OPT_EXIT)
//        exit_program(0);

    return 0;
}

int parse_option(void *optctx, const char *opt, const char *arg,
                 const OptionDef *options) {
    const OptionDef *po;
    int ret;
    OptionsContext *o = optctx;
    char * trace_id = o->run_context_ref->trace_id;

    po = find_option(options, opt);
    if (!po->name && opt[0] == 'n' && opt[1] == 'o') {
        /* handle 'no' bool option */
        po = find_option(options, opt + 2);
        if ((po->name && (po->flags & OPT_BOOL)))
            arg = "0";
    } else if (po->flags & OPT_BOOL)
        arg = "1";

    if (!po->name)
        po = find_option(options, "default");
    if (!po->name) {
        av_log(NULL, AV_LOG_ERROR, "tid=%s,Unrecognized option '%s'\n", trace_id,opt);
        return AVERROR(EINVAL);
    }
    if (po->flags & HAS_ARG && !arg) {
        av_log(NULL, AV_LOG_ERROR, "tid=%s,Missing argument for option '%s'\n",trace_id, opt);
        return AVERROR(EINVAL);
    }

    ret = write_option(optctx, po, opt, arg);
    if (ret < 0)
        return ret;

    return !!(po->flags & HAS_ARG);
}

void *grow_array(char *trace_id,void *array, int elem_size, int *size, int new_size, int *has_error) {
    if (new_size >= INT_MAX / elem_size) {
        av_log(NULL, AV_LOG_ERROR, "tid=%s,Array too big.\n",trace_id);
        *has_error = 1;
        return NULL;
    }
    if (*size < new_size) {
        uint8_t *tmp = av_realloc_array(array, new_size, elem_size);
        if (!tmp) {
            av_log(NULL, AV_LOG_ERROR, "tid=%s,Could not alloc buffer.\n",trace_id);
            *has_error = 1;
            return NULL;
        }
        memset(tmp + *size * elem_size, 0, (new_size - *size) * elem_size);
        *size = new_size;
        return tmp;
    }
    return array;
}

double parse_number_or_die(char * trace_id,const char *context, const char *numstr, int type,
                           double min, double max, int *die) {
    char *tail;
    const char *error;
    double d = av_strtod(numstr, &tail);
    *die = 0;
    if (*tail)
        error = "tid=%s,Expected number for %s but found: %s\n";
    else if (d < min || d > max)
        error = "tid=%s,The value for %s was %s which is not within %f - %f\n";
    else if (type == OPT_INT64 && (int64_t) d != d)
        error = "tid=%s,Expected int64 for %s but found %s\n";
    else if (type == OPT_INT && (int) d != d)
        error = "tid=%s,Expected int for %s but found %s\n";
    else
        return d;
    av_log(NULL, AV_LOG_FATAL, error,trace_id, context, numstr, min, max);

    *die = 1;
    return 0;
}

int64_t parse_time_or_die(char * trace_id,const char *context, const char *timestr,
                          int is_duration, int *die) {
    int64_t us;
    *die = 0;
    if (av_parse_time(&us, timestr, is_duration) < 0) {
        av_log(NULL, AV_LOG_FATAL, "tid=%s,Invalid %s specification for %s: %s\n",trace_id,
               is_duration ? "duration" : "date", context, timestr);
        *die = 1;
        return -1;
    }
    return us;
}

int opt_timelimit(void *optctx, const char *opt, const char *arg) {
    OptionsContext * o = optctx;
#if HAVE_SETRLIMIT
    int die = 0;
    int lim = parse_number_or_die(o->run_context_ref->trace_id,opt, arg, OPT_INT64, 0, INT_MAX, &die);
    if (die) {
        return 0;
    }
    struct rlimit rl = {lim, lim + 1};
    if (setrlimit(RLIMIT_CPU, &rl))
        perror("setrlimit");
#else
    av_log(NULL, AV_LOG_WARNING, "tid=%s,-%s not implemented on this OS\n", o->run_context_ref->trace_id,opt);
#endif
    return 0;
}

static const AVOption *opt_find(void *obj, const char *name, const char *unit,
                                int opt_flags, int search_flags) {
    const AVOption *o = av_opt_find(obj, name, unit, opt_flags, search_flags);
    if (o && !o->flags)
        return NULL;
    return o;
}

#define FLAGS (o->type == AV_OPT_TYPE_FLAGS && (arg[0]=='-' || arg[0]=='+')) ? AV_DICT_APPEND : 0

int opt_default(void *optctx, const char *opt, const char *arg) {
    const AVOption *o;
    int consumed = 0;
    char opt_stripped[128];
    const char *p;
    const AVClass *cc = avcodec_get_class(), *fc = avformat_get_class();

    OptionsContext *ctx = optctx;
#if CONFIG_AVRESAMPLE
    const AVClass *rc = avresample_get_class();
#endif
#if CONFIG_SWSCALE
    const AVClass *sc = sws_get_class();
#endif
#if CONFIG_SWRESAMPLE
    const AVClass *swr_class = swr_get_class();
#endif

    if (!strcmp(opt, "debug") || !strcmp(opt, "fdebug"))
        av_log_set_level(AV_LOG_DEBUG);

    if (!(p = strchr(opt, ':')))
        p = opt + strlen(opt);
    av_strlcpy(opt_stripped, opt, FFMIN(sizeof(opt_stripped), p - opt + 1));

    if ((o = opt_find(&cc, opt_stripped, NULL, 0,
                      AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ)) ||
        ((opt[0] == 'v' || opt[0] == 'a' || opt[0] == 's') &&
         (o = opt_find(&cc, opt + 1, NULL, 0, AV_OPT_SEARCH_FAKE_OBJ)))) {
        av_dict_set(&ctx->run_context_ref->codec_opts, opt, arg, FLAGS);
        consumed = 1;
    }
    if ((o = opt_find(&fc, opt, NULL, 0,
                      AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ))) {
        av_dict_set(&ctx->run_context_ref->format_opts, opt, arg, FLAGS);
        if (consumed)
            av_log(NULL, AV_LOG_VERBOSE, "Routing option %s to both codec and muxer layer\n", opt);
        consumed = 1;
    }
#if CONFIG_SWSCALE
    if (!consumed && (o = opt_find(&sc, opt, NULL, 0,
                                   AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ))) {
        struct SwsContext *sws = sws_alloc_context();
        int ret = av_opt_set(sws, opt, arg, 0);
        sws_freeContext(sws);
        if (!strcmp(opt, "srcw") || !strcmp(opt, "srch") ||
            !strcmp(opt, "dstw") || !strcmp(opt, "dsth") ||
            !strcmp(opt, "src_format") || !strcmp(opt, "dst_format")) {
            av_log(NULL, AV_LOG_ERROR,
                   "Directly using swscale dimensions/format options is not supported, please use the -s or -pix_fmt options\n");
            return AVERROR(EINVAL);
        }
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error setting option %s.\n", opt);
            return ret;
        }

        av_dict_set(&ctx->run_context_ref->sws_dict, opt, arg, FLAGS);

        consumed = 1;
    }
#else
    if (!consumed && !strcmp(opt, "sws_flags")) {
        av_log(NULL, AV_LOG_WARNING, "Ignoring %s %s, due to disabled swscale\n", opt, arg);
        consumed = 1;
    }
#endif
#if CONFIG_SWRESAMPLE
    if (!consumed && (o = opt_find(&swr_class, opt, NULL, 0,
                                   AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ))) {
        struct SwrContext *swr = swr_alloc();
        int ret = av_opt_set(swr, opt, arg, 0);
        swr_free(&swr);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error setting option %s.\n", opt);
            return ret;
        }
        av_dict_set(&ctx->run_context_ref->swr_opts, opt, arg, FLAGS);
        consumed = 1;
    }
#endif
#if CONFIG_AVRESAMPLE
    if ((o=opt_find(&rc, opt, NULL, 0,
                       AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ))) {
        av_dict_set(&resample_opts, opt, arg, FLAGS);
        consumed = 1;
    }
#endif

    if (consumed)
        return 0;
    return AVERROR_OPTION_NOT_FOUND;
}

void init_opts(OptionsContext *optionCtx) {
    av_dict_set(&optionCtx->run_context_ref->sws_dict, "flags", "bicubic", 0);
}

void uninit_opts(OptionsContext *o) {
    const OptionDef *po = options;
    int i;

    /* all OPT_SPEC and OPT_STRING can be freed in generic way */
    while (po->name) {
        void *dst = (uint8_t*)o + po->u.off;

        if (po->flags & OPT_SPEC) {
            SpecifierOpt **so = dst;
            int i, *count = (int*)(so + 1);
            for (i = 0; i < *count; i++) {
                av_freep(&(*so)[i].specifier);
                if (po->flags & OPT_STRING)
                    av_freep(&(*so)[i].u.str);
            }
            av_freep(so);
            *count = 0;
        } else if (po->flags & OPT_OFFSET && po->flags & OPT_STRING)
            av_freep(dst);
        po++;
    }
    for (i = 0; i < o->nb_stream_maps; i++)
        av_freep(&o->stream_maps[i].linklabel);
    av_freep(&o->stream_maps);
    av_freep(&o->audio_channel_maps);
    av_freep(&o->streamid_map);
    av_freep(&o->attachments);
}



static int init_parse_context(ParseContext *octx,
                              ParsedOptionsContext  * parent_context) {
    int i;

    OptionsContext * o = (OptionsContext*)parent_context;

    o->run_context_ref = &parent_context->raw_context;

    octx->nb_groups = sizeof(const_groups)/sizeof(*const_groups);
    octx->groups = av_mallocz_array(octx->nb_groups, sizeof(*octx->groups));
    if (!octx->groups)
        return -1;

    for (i = 0; i < octx->nb_groups; i++){
        OptionGroupDef *ndef = malloc(sizeof(OptionGroupDef));
        memcpy(ndef,&const_groups[i],sizeof(OptionGroupDef));
        octx->groups[i].group_def = ndef;
    }

    octx->global_opts.group_def = malloc(sizeof(OptionGroupDef));
    memcpy(octx->global_opts.group_def,&global_group,sizeof(OptionGroupDef));
    octx->global_opts.arg = "";

    parent_context->raw_context.audio_drift_threshold = 0.1;
    parent_context->raw_context.dts_delta_threshold   = 10;
    parent_context->raw_context.dts_error_threshold   = 3600 * 30;

    parent_context->raw_context.audio_volume      = 256;
    parent_context->raw_context.audio_sync_method = 0;
    parent_context->raw_context.video_sync_method = VSYNC_AUTO;
    parent_context->raw_context.frame_drop_threshold = 0;
//    parent_context->raw_context.do_deinterlace    = 0;
    parent_context->raw_context.do_benchmark      = 0;
    parent_context->raw_context.do_benchmark_all  = 0;
    parent_context->raw_context.do_hex_dump       = 0;
    parent_context->raw_context.do_pkt_dump       = 0;
    parent_context->raw_context.copy_ts           = 0;
    parent_context->raw_context.start_at_zero     = 0;
    parent_context->raw_context.copy_tb           = -1;
//    parent_context->raw_context.debug_ts          = 0;
    parent_context->raw_context.exit_on_error     = 0;
    parent_context->raw_context.abort_on_flags    = 0;
//    parent_context->raw_context.print_stats       = -1;
//    parent_context->raw_context.qp_hist           = 0;
//    parent_context->raw_context.stdin_interaction = 1;
    parent_context->raw_context.frame_bits_per_raw_sample = 0;
    parent_context->raw_context.max_error_rate  = 2.0 / 3;
    parent_context->raw_context.filter_nbthreads = 0;
    parent_context->raw_context.filter_complex_nbthreads = 0;
//    parent_context->raw_context.vstats_version = 2;
    parent_context->raw_context.auto_conversion_filters = 1;
//    parent_context->raw_context.stats_period = 500000;

    parent_context->raw_context.find_stream_info = 1;

    parent_context->raw_context.want_sdp = 1;
    parent_context->raw_context.transcode_init_done = ATOMIC_VAR_INIT(0);

    parent_context->raw_context.dup_warning = 1000;
//    memcpy(parent_context->raw_context.frame_rates,G_FRAME_RATES,sizeof (G_FRAME_RATES));


    init_opts(parent_context);

    o->stop_time = INT64_MAX;
    o->mux_max_delay  = 0.7;
    o->start_time     = AV_NOPTS_VALUE;
    o->start_time_eof = AV_NOPTS_VALUE;
    o->recording_time = INT64_MAX;
    o->limit_filesize = UINT64_MAX;
    o->chapters_input_file = INT_MAX;
    o->accurate_seek  = 1;
    o->thread_queue_size = -1;


    return 0;
}

static int add_opt(char * trace_id,ParseContext *octx, const OptionDef *opt,
                   const char *key, const char *val) {
    int global = !(opt->flags & (OPT_PERFILE | OPT_SPEC | OPT_OFFSET));
    OptionGroup *g = global ? &octx->global_opts : &octx->cur_group;

    int has_error = 0;
    GROW_ARRAY(trace_id,g->opts, g->nb_opts, has_error);
    if (has_error) {
        return -1;
    }
    g->opts[g->nb_opts - 1].opt = opt;
    g->opts[g->nb_opts - 1].key = key;
    g->opts[g->nb_opts - 1].val = val;
    return 0;
}

static int finish_group(ParseContext *octx, int group_idx,
                        const char *arg, OptionsContext *optionCtx) {
    OptionGroupList *l = &octx->groups[group_idx];
    OptionGroup *g;
    char *trace_id = optionCtx->run_context_ref->trace_id;

    int has_error = 0;
    GROW_ARRAY(trace_id,l->groups, l->nb_groups, has_error);
    if (has_error) {
        return -1;
    }
    g = &l->groups[l->nb_groups - 1];

    *g = octx->cur_group;
    g->arg = arg;
    g->group_def = l->group_def;
    g->sws_dict = optionCtx->run_context_ref->sws_dict;
    g->swr_opts = optionCtx->run_context_ref->swr_opts;
    g->codec_opts = optionCtx->run_context_ref->codec_opts;
    g->format_opts = optionCtx->run_context_ref->format_opts;
    g->resample_opts = optionCtx->run_context_ref->resample_opts;

    optionCtx->run_context_ref->codec_opts = NULL;
    optionCtx->run_context_ref->format_opts = NULL;
    optionCtx->run_context_ref->resample_opts = NULL;
    optionCtx->run_context_ref->sws_dict = NULL;
    optionCtx->run_context_ref->swr_opts = NULL;
    init_opts(optionCtx);

    memset(&octx->cur_group, 0, sizeof(octx->cur_group));
    return 0;
}

static int match_group_separator(const OptionGroupList *groups, int nb_groups,
                                 const char *opt) {
    int i;

    for (i = 0; i < nb_groups; i++) {
        const OptionGroupList *p = &groups[i];
        if (p->group_def->sep && !strcmp(p->group_def->sep, opt))
            return i;
    }

    return -1;
}

int split_commandline(ParseContext *octx, int argc, char *argv[],
                      const OptionDef *options, ParsedOptionsContext  *optionCtx) {
    int optindex = 1;
    int dashdash = -2;

    init_parse_context(octx, optionCtx);
    av_log(NULL, AV_LOG_DEBUG, "tid=%s,Splitting the commandline.\n",optionCtx->raw_context.trace_id);

    char * trace_id =  optionCtx->raw_context.trace_id;
    while (optindex < argc) {
        const char *opt = argv[optindex++], *arg;
        const OptionDef *po;
        int ret;

        av_log(NULL, AV_LOG_DEBUG, "tid=%s,Reading option '%s' ...", trace_id,opt);

        if (opt[0] == '-' && opt[1] == '-' && !opt[2]) {
            dashdash = optindex;
            continue;
        }
        /* unnamed group separators, e.g. output filename */
        if (opt[0] != '-' || !opt[1] || dashdash + 1 == optindex) {
            if (finish_group(octx, 0, opt, optionCtx) != 0) {
                av_log(NULL, AV_LOG_ERROR, "tid=%s,finish_group error for option '%s'.\n", trace_id,opt);
                return AVERROR(EINVAL);
            }
            av_log(NULL, AV_LOG_DEBUG, "tid=%s, matched as %s.\n",trace_id, octx->groups[0].group_def->name);
            continue;
        }
        opt++;

#define GET_ARG(arg)                                                           \
do {                                                                           \
    (arg) = argv[optindex++];                                                    \
    if (!arg) {                                                                \
        av_log(NULL, AV_LOG_ERROR, "tid=%s,Missing argument for option '%s'.\n",trace_id, opt);\
        return AVERROR(EINVAL);                                                \
    }                                                                          \
} while (0)

        /* named group separators, e.g. -i */
        if ((ret = match_group_separator(octx->groups, octx->nb_groups, opt)) >= 0) {
            GET_ARG(arg);
            if (0 != finish_group(octx, ret, arg, optionCtx)) {
                av_log(NULL, AV_LOG_ERROR, "tid=%s,finish_group error for option '%s'.\n",trace_id, opt);
                return AVERROR(EINVAL);
            }
            av_log(NULL, AV_LOG_DEBUG, "tid=%s, matched as %s with argument '%s'.\n",
                   trace_id,octx->groups[ret].group_def->name, arg);
            continue;
        }

        /* normal options */
        po = find_option(options, opt);
        if (po->name) {
//            if (po->flags & OPT_EXIT) {
//                /* optional argument, e.g. -h */
//                arg = argv[optindex++];
//            } else
            if (po->flags & HAS_ARG) {
                GET_ARG(arg);
            } else {
                arg = "1";
            }

            if (0 != add_opt(trace_id,octx, po, opt, arg)) {
                av_log(NULL, AV_LOG_ERROR, "tid=%s,add_opt error for option '%s'.\n",trace_id, opt);
                return AVERROR(EINVAL);
            }
            av_log(NULL, AV_LOG_DEBUG, "tid=%s, matched as option '%s' (%s) with "
                                       "argument '%s'.\n", trace_id,po->name, po->help, arg);
            continue;
        }

        /* AVOptions */
        if (argv[optindex]) {
            ret = opt_default(NULL, opt, argv[optindex]);
            if (ret >= 0) {
                av_log(NULL, AV_LOG_DEBUG, "tid=%s, matched as AVOption '%s' with "
                                           "argument '%s'.\n",trace_id, opt, argv[optindex]);
                optindex++;
                continue;
            } else if (ret != AVERROR_OPTION_NOT_FOUND) {
                av_log(NULL, AV_LOG_ERROR, "tid=%s,Error parsing option '%s' "
                                           "with argument '%s'.\n", trace_id,opt, argv[optindex]);
                return ret;
            }
        }

        /* boolean -nofoo options */
        if (opt[0] == 'n' && opt[1] == 'o' &&
            (po = find_option(options, opt + 2)) &&
            po->name && po->flags & OPT_BOOL) {
            if (0 != add_opt(trace_id,octx, po, opt, "0")) {
                av_log(NULL, AV_LOG_ERROR, "tid=%s,add_opt error for option '%s'.\n", trace_id,opt);
                return AVERROR(EINVAL);
            }
            av_log(NULL, AV_LOG_DEBUG, "tid=%s, matched as option '%s' (%s) with "
                                       "argument 0.\n",trace_id, po->name, po->help);
            continue;
        }

        av_log(NULL, AV_LOG_ERROR, "tid=%s,Unrecognized option '%s'.\n", trace_id,opt);
        return AVERROR_OPTION_NOT_FOUND;
    }

    RunContext * run_context_ref = optionCtx->options_context.run_context_ref;
    if (octx->cur_group.nb_opts || run_context_ref->codec_opts || run_context_ref->format_opts ||
        run_context_ref->resample_opts)
        av_log(NULL, AV_LOG_WARNING, "tid=%s,Trailing option(s) found in the "
                                     "command: may be ignored.\n",trace_id);

    av_log(NULL, AV_LOG_DEBUG, "tid=%s,Finished splitting the commandline.\n",trace_id);

    return 0;
}

int parse_optgroup(void *optctx, OptionGroup *g) {
    int i, ret;

    OptionsContext *ctx = (OptionsContext *)optctx;
    char * trace_id = ctx->run_context_ref->trace_id;

    av_log(NULL, AV_LOG_DEBUG, "tid=%s,Parsing a group of options: %s %s.\n",
           trace_id,g->group_def->name, g->arg);

    for (i = 0; i < g->nb_opts; i++) {
        Option *o = &g->opts[i];

        if (g->group_def->flags &&
            !(g->group_def->flags & o->opt->flags)) {
            av_log(NULL, AV_LOG_ERROR, "tid=%s,Option %s (%s) cannot be applied to "
                                       "%s %s -- you are trying to apply an input option to an "
                                       "output file or vice versa. Move this option before the "
                                       "file it belongs to.\n", trace_id,o->key, o->opt->help,
                   g->group_def->name, g->arg);
            return AVERROR(EINVAL);
        }

        av_log(NULL, AV_LOG_DEBUG, "tid=%s,Applying option %s (%s) with argument %s.\n",
               trace_id, o->key, o->opt->help, o->val);

        ret = write_option(optctx, o->opt, o->key, o->val);
        if (ret < 0)
            return ret;
    }

    av_log(NULL, AV_LOG_DEBUG, "tid=%s,Successfully parsed a group of options.\n",trace_id);

    return 0;
}

void uninit_parse_context(ParseContext *octx) {
    int i, j;

    free(octx->copy_cmd);

    for (i = 0; i < octx->nb_groups; i++) {
        OptionGroupList *l = &octx->groups[i];

        for (j = 0; j < l->nb_groups; j++) {
            av_freep(&l->groups[j].opts);
            av_freep(&l->groups[j].group_def);
            av_dict_free(&l->groups[j].codec_opts);
            av_dict_free(&l->groups[j].format_opts);
            av_dict_free(&l->groups[j].resample_opts);

            av_dict_free(&l->groups[j].sws_dict);
            av_dict_free(&l->groups[j].swr_opts);
        }
        av_freep(&l->groups);
    }
    av_freep(&octx->groups);

    av_freep(&octx->cur_group.opts);
    av_freep(&octx->global_opts.opts);
    av_freep(&octx->global_opts.group_def);
    free(octx);
}

void print_error(const char *filename, int err)
{
    char errbuf[128];
    const char *errbuf_ptr = errbuf;

    if (av_strerror(err, errbuf, sizeof(errbuf)) < 0)
        errbuf_ptr = strerror(AVUNERROR(err));
    av_log(NULL, AV_LOG_ERROR, "%s: %s\n", filename, errbuf_ptr);
}

void remove_avoptions(AVDictionary **a, AVDictionary *b)
{
    AVDictionaryEntry *t = NULL;

    while ((t = av_dict_get(b, "", t, AV_DICT_IGNORE_SUFFIX))) {
        av_dict_set(a, t->key, NULL, AV_DICT_MATCH_CASE);
    }
}

int assert_avoptions(AVDictionary *m)
{
    AVDictionaryEntry *t;
    if ((t = av_dict_get(m, "", NULL, AV_DICT_IGNORE_SUFFIX))) {
        av_log(NULL, AV_LOG_FATAL, "Option %s not found.\n", t->key);
        return -1;
    }
    return 0;
}
int check_stream_specifier(AVFormatContext *s, AVStream *st, const char *spec)
{
    int ret = avformat_match_stream_specifier(s, st, spec);
    if (ret < 0)
        av_log(s, AV_LOG_ERROR, "Invalid stream specifier: %s.\n", spec);
    return ret;
}

AVDictionary **setup_find_stream_info_opts(const char * trace_id,AVFormatContext *s,AVDictionary *codec_opts,int *error){
    int i;
    AVDictionary **opts;

    if (!s->nb_streams)
        return NULL;
    opts = av_mallocz_array(s->nb_streams, sizeof(*opts));
    if (!opts) {
        av_log(NULL, AV_LOG_ERROR,
               "tid=%s,Could not alloc memory for stream options.\n",trace_id);
        return NULL;
    }
    for (i = 0; i < s->nb_streams; i++)
        opts[i] = filter_codec_opts(codec_opts, s->streams[i]->codecpar->codec_id,
                                    s, s->streams[i], NULL,error);
        if(*error){
            return NULL;
        }
    return opts;
}
AVDictionary *filter_codec_opts(AVDictionary *opts, enum AVCodecID codec_id,
                                AVFormatContext *s, AVStream *st, const AVCodec *codec,int *error)
{
    AVDictionary    *ret = NULL;
    AVDictionaryEntry *t = NULL;
    int            flags = s->oformat ? AV_OPT_FLAG_ENCODING_PARAM
                                      : AV_OPT_FLAG_DECODING_PARAM;
    char          prefix = 0;
    const AVClass    *cc = avcodec_get_class();

    if (!codec)
        codec            = s->oformat ? avcodec_find_encoder(codec_id)
                                      : avcodec_find_decoder(codec_id);

    switch (st->codecpar->codec_type) {
        case AVMEDIA_TYPE_VIDEO:
            prefix  = 'v';
            flags  |= AV_OPT_FLAG_VIDEO_PARAM;
            break;
        case AVMEDIA_TYPE_AUDIO:
            prefix  = 'a';
            flags  |= AV_OPT_FLAG_AUDIO_PARAM;
            break;
        case AVMEDIA_TYPE_SUBTITLE:
            prefix  = 's';
            flags  |= AV_OPT_FLAG_SUBTITLE_PARAM;
            break;
    }

    while (t = av_dict_get(opts, "", t, AV_DICT_IGNORE_SUFFIX)) {
        const AVClass *priv_class;
        char *p = strchr(t->key, ':');

        /* check stream specification in opt name */
        if (p)
            switch (check_stream_specifier(s, st, p + 1)) {
                case  1: *p = 0; break;
                case  0:         continue;
                default:
                    *error = 1;
                    return NULL;
            }

        if (av_opt_find(&cc, t->key, NULL, flags, AV_OPT_SEARCH_FAKE_OBJ) ||
            !codec ||
            ((priv_class = codec->priv_class) &&
             av_opt_find(&priv_class, t->key, NULL, flags,
                         AV_OPT_SEARCH_FAKE_OBJ)))
            av_dict_set(&ret, t->key, t->value, 0);
        else if (t->key[0] == prefix &&
                 av_opt_find(&cc, t->key + 1, NULL, flags,
                             AV_OPT_SEARCH_FAKE_OBJ))
            av_dict_set(&ret, t->key + 1, t->value, 0);

        if (p)
            *p = ':';
    }
    return ret;
}

FILE *get_preset_file(char *filename, size_t filename_size,
                      const char *preset_name, int is_path,
                      const char *codec_name)
{
    FILE *f = NULL;
    int i;
    const char *base[3] = { getenv("FFMPEG_DATADIR"),
                            getenv("HOME"),
                            FFMPEG_DATADIR, };

    if (is_path) {
        av_strlcpy(filename, preset_name, filename_size);
        f = fopen(filename, "r");
    } else {
#if HAVE_GETMODULEHANDLE && defined(_WIN32)
        char datadir[MAX_PATH], *ls;
        base[2] = NULL;

        if (GetModuleFileNameA(GetModuleHandleA(NULL), datadir, sizeof(datadir) - 1))
        {
            for (ls = datadir; ls < datadir + strlen(datadir); ls++)
                if (*ls == '\\') *ls = '/';

            if (ls = strrchr(datadir, '/'))
            {
                *ls = 0;
                strncat(datadir, "/ffpresets",  sizeof(datadir) - 1 - strlen(datadir));
                base[2] = datadir;
            }
        }
#endif
        for (i = 0; i < 3 && !f; i++) {
            if (!base[i])
                continue;
            snprintf(filename, filename_size, "%s%s/%s.ffpreset", base[i],
                     i != 1 ? "" : "/.ffmpeg", preset_name);
            f = fopen(filename, "r");
            if (!f && codec_name) {
                snprintf(filename, filename_size,
                         "%s%s/%s-%s.ffpreset",
                         base[i], i != 1 ? "" : "/.ffmpeg", codec_name,
                         preset_name);
                f = fopen(filename, "r");
            }
        }
    }

    return f;
}
double get_rotation(const char * trace_id,AVStream *st)
{
    uint8_t* displaymatrix = av_stream_get_side_data(st,
                                                     AV_PKT_DATA_DISPLAYMATRIX, NULL);
    double theta = 0;
    if (displaymatrix)
        theta = -av_display_rotation_get((int32_t*) displaymatrix);

    theta -= 360*floor(theta/360 + 0.9/360);

    if (fabs(theta - 90*round(theta/90)) > 2)
        av_log(NULL, AV_LOG_WARNING, "tid=%s,Odd rotation angle.\n"
                                     "If you want to help, upload a sample "
                                     "of this file to https://streams.videolan.org/upload/ "
                                     "and contact the ffmpeg-devel mailing list. (ffmpeg-devel@ffmpeg.org)",trace_id);

    return theta;
}


