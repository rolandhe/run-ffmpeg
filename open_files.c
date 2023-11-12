//
// Created by hexiufeng on 2023/11/1.
//
#include "open_files.h"
#include <libavutil/opt.h>
#include <libavutil/avstring.h>
#include <libavutil/intreadwrite.h>
#include <libavutil/parseutils.h>
#include <libavutil/pixdesc.h>
#include "cmd_util.h"
#include "common.h"
#include "filter.h"

#define DEFAULT_PASS_LOGFILENAME_PREFIX "ffmpeg2pass"

#define SPECIFIER_OPT_FMT_str  "%s"
#define SPECIFIER_OPT_FMT_i    "%i"
#define SPECIFIER_OPT_FMT_i64  "%"PRId64
#define SPECIFIER_OPT_FMT_ui64 "%"PRIu64
#define SPECIFIER_OPT_FMT_f    "%f"
#define SPECIFIER_OPT_FMT_dbl  "%lf"

static const char *const opt_name_codec_names[] = {"c", "codec", "acodec", "vcodec", "scodec", "dcodec", NULL};
static const char *const opt_name_audio_channels[] = {"ac", NULL};
static const char *const opt_name_audio_sample_rate[] = {"ar", NULL};
static const char *const opt_name_frame_rates[] = {"r", NULL};
static const char *const opt_name_max_frame_rates[] = {"fpsmax", NULL};
static const char *const opt_name_frame_sizes[] = {"s", NULL};
static const char *const opt_name_frame_pix_fmts[] = {"pix_fmt", NULL};
static const char *const opt_name_ts_scale[] = {"itsscale", NULL};
static const char *const opt_name_hwaccels[] = {"hwaccel", NULL};
static const char *const opt_name_hwaccel_devices[] = {"hwaccel_device", NULL};
static const char *const opt_name_hwaccel_output_formats[] = {"hwaccel_output_format", NULL};
static const char *const opt_name_autorotate[] = {"autorotate", NULL};
static const char *const opt_name_autoscale[] = {"autoscale", NULL};
static const char *const opt_name_max_frames[] = {"frames", "aframes", "vframes", "dframes", NULL};
static const char *const opt_name_bitstream_filters[] = {"bsf", "absf", "vbsf", NULL};
static const char *const opt_name_codec_tags[] = {"tag", "atag", "vtag", "stag", NULL};
static const char *const opt_name_sample_fmts[] = {"sample_fmt", NULL};
static const char *const opt_name_qscale[] = {"q", "qscale", NULL};
static const char *const opt_name_forced_key_frames[] = {"forced_key_frames", NULL};
static const char *const opt_name_force_fps[] = {"force_fps", NULL};
static const char *const opt_name_frame_aspect_ratios[] = {"aspect", NULL};
static const char *const opt_name_rc_overrides[] = {"rc_override", NULL};
static const char *const opt_name_intra_matrices[] = {"intra_matrix", NULL};
static const char *const opt_name_inter_matrices[] = {"inter_matrix", NULL};
static const char *const opt_name_chroma_intra_matrices[] = {"chroma_intra_matrix", NULL};
static const char *const opt_name_top_field_first[] = {"top", NULL};
static const char *const opt_name_presets[] = {"pre", "apre", "vpre", "spre", NULL};
static const char *const opt_name_copy_initial_nonkeyframes[] = {"copyinkfr", NULL};
static const char *const opt_name_copy_prior_start[] = {"copypriorss", NULL};
static const char *const opt_name_filters[] = {"filter", "af", "vf", NULL};
static const char *const opt_name_filter_scripts[] = {"filter_script", NULL};
static const char *const opt_name_reinit_filters[] = {"reinit_filter", NULL};
static const char *const opt_name_fix_sub_duration[] = {"fix_sub_duration", NULL};
static const char *const opt_name_canvas_sizes[] = {"canvas_size", NULL};
static const char *const opt_name_pass[] = {"pass", NULL};
static const char *const opt_name_passlogfiles[] = {"passlogfile", NULL};
static const char *const opt_name_max_muxing_queue_size[] = {"max_muxing_queue_size", NULL};
static const char *const opt_name_muxing_queue_data_threshold[] = {"muxing_queue_data_threshold", NULL};
static const char *const opt_name_guess_layout_max[] = {"guess_layout_max", NULL};
static const char *const opt_name_apad[] = {"apad", NULL};
static const char *const opt_name_discard[] = {"discard", NULL};
static const char *const opt_name_disposition[] = {"disposition", NULL};
static const char *const opt_name_time_bases[] = {"time_base", NULL};
static const char *const opt_name_enc_time_bases[] = {"enc_time_base", NULL};

#define MATCH_PER_TYPE_OPT(name, type, outvar, fmtctx, mediatype)\
{\
    int i;\
    for (i = 0; i < o->nb_ ## name; i++) {\
        char *spec = o->name[i].specifier;\
        if (!strcmp(spec, mediatype))\
            outvar = o->name[i].u.type;\
    }\
}

#define WARN_MULTIPLE_OPT_USAGE(name, type, so, st)\
{\
    char namestr[128] = "";\
    const char *spec = so->specifier && so->specifier[0] ? so->specifier : "";\
    for (i = 0; opt_name_##name[i]; i++)\
        av_strlcatf(namestr, sizeof(namestr), "-%s%s", opt_name_##name[i], opt_name_##name[i+1] ? (opt_name_##name[i+2] ? ", " : " or ") : "");\
    av_log(NULL, AV_LOG_WARNING, "Multiple %s options specified for stream %d, only the last option '-%s%s%s "SPECIFIER_OPT_FMT_##type"' will be used.\n",\
           namestr, st->index, opt_name_##name[0], spec[0] ? ":" : "", spec, so->u.type);\
}

#define MATCH_PER_STREAM_OPT(name, type, outvar, fmtctx, st, fail_goto)\
{\
    int i, ret, matches = 0;\
    SpecifierOpt *so;\
    for (i = 0; i < o->nb_ ## name; i++) {\
        char *spec = o->name[i].specifier;\
        if ((ret = check_stream_specifier(fmtctx, st, spec)) > 0) {\
            outvar = o->name[i].u.type;\
            so = &o->name[i];\
            matches++;\
        } else if (ret < 0)\
            goto fail_goto;\
    }\
    if (matches > 1)\
       WARN_MULTIPLE_OPT_USAGE(name, type, so, st);\
}

const HWAccel hwaccels[] = {
#if CONFIG_VIDEOTOOLBOX
        { "videotoolbox", videotoolbox_init, HWACCEL_VIDEOTOOLBOX, AV_PIX_FMT_VIDEOTOOLBOX },
#endif
#if CONFIG_LIBMFX
        { "qsv",   qsv_init,   HWACCEL_QSV,   AV_PIX_FMT_QSV },
#endif
        { 0 },
};

extern  const OptionDef options[];

static AVCodec *find_codec_or_die(const char *name, enum AVMediaType type, int encoder, int *error) {
    const AVCodecDescriptor *desc;
    const char *codec_string = encoder ? "encoder" : "decoder";
    AVCodec *codec;

    codec = encoder ?
            avcodec_find_encoder_by_name(name) :
            avcodec_find_decoder_by_name(name);

    if (!codec && (desc = avcodec_descriptor_get_by_name(name))) {
        codec = encoder ? avcodec_find_encoder(desc->id) :
                avcodec_find_decoder(desc->id);
        if (codec)
            av_log(NULL, AV_LOG_VERBOSE, "Matched %s '%s' for codec '%s'.\n",
                   codec_string, codec->name, desc->name);
    }

    if (!codec) {
        av_log(NULL, AV_LOG_FATAL, "Unknown %s '%s'\n", codec_string, name);
        *error = -1;
        return NULL;
    }
    if (codec->type != type) {
        av_log(NULL, AV_LOG_FATAL, "Invalid %s type '%s'\n", codec_string, name);
        *error = -1;
        return NULL;
    }
    return codec;
}


static const AVCodec *choose_decoder(OptionsContext *o, AVFormatContext *s, AVStream *st) {
    char *codec_name = NULL;

    MATCH_PER_STREAM_OPT(codec_names, str, codec_name, s, st, fail)
    if (codec_name) {
        int die = 0;
        const AVCodec *codec = find_codec_or_die(codec_name, st->codecpar->codec_type, 0,&die);
        if(die < 0){
            goto fail;
        }
        st->codecpar->codec_id = codec->id;
        return codec;
    } else
        return avcodec_find_decoder(st->codecpar->codec_id);
fail:
    return NULL;
}

static int add_input_streams(OptionsContext *o, AVFormatContext *ic)
{
    int i, ret;

    o->run_context_ref->option_input.duration = ic->duration;
    o->run_context_ref->option_input.duration_estimation_method = ic->duration_estimation_method;

    char * trace_id = o->run_context_ref->trace_id;

    for (i = 0; i < ic->nb_streams; i++) {
        AVStream *st = ic->streams[i];
        AVCodecParameters *par = st->codecpar;
        InputStream *ist = av_mallocz(sizeof(*ist));
        char *framerate = NULL, *hwaccel_device = NULL;
        const char *hwaccel = NULL;
        char *hwaccel_output_format = NULL;
        char *codec_tag = NULL;
        char *next;
        char *discard_str = NULL;
        const AVClass *cc = avcodec_get_class();
        const AVOption *discard_opt = av_opt_find(&cc, "skip_frame", NULL, 0, 0);

        if (!ist)
        {
            return  -1;
        }
        int has_error = 0;

        GROW_ARRAY(trace_id,o->run_context_ref->option_input.input_streams, o->run_context_ref->option_input.nb_input_streams, has_error);
        if(has_error < 0){
            return has_error;
        }
        o->run_context_ref->option_input.input_streams[o->run_context_ref->option_input.nb_input_streams - 1] = ist;

        ist->st = st;
        ist->file_index = o->run_context_ref->option_input.nb_input_files;
        ist->discard = 1;
        st->discard  = AVDISCARD_ALL;
        ist->nb_samples = 0;
        ist->min_pts = INT64_MAX;
        ist->max_pts = INT64_MIN;

        ist->ts_scale = 1.0;
        MATCH_PER_STREAM_OPT(ts_scale, dbl, ist->ts_scale, ic, st,fail);

        ist->autorotate = 1;
        MATCH_PER_STREAM_OPT(autorotate, i, ist->autorotate, ic, st,fail);

        MATCH_PER_STREAM_OPT(codec_tags, str, codec_tag, ic, st,fail);
        if (codec_tag) {
            uint32_t tag = strtol(codec_tag, &next, 0);
            if (*next)
                tag = AV_RL32(codec_tag);
            st->codecpar->codec_tag = tag;
        }

        ist->dec = choose_decoder(o, ic, st);
        ist->decoder_opts = filter_codec_opts(o->g->codec_opts, ist->st->codecpar->codec_id, ic, st, ist->dec,&has_error);

        if(has_error){
            goto fail;
        }
        ist->reinit_filters = -1;
        MATCH_PER_STREAM_OPT(reinit_filters, i, ist->reinit_filters, ic, st,fail);

        MATCH_PER_STREAM_OPT(discard, str, discard_str, ic, st,fail);
        ist->user_set_discard = AVDISCARD_NONE;

        if ((o->video_disable && ist->st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) ||
            (o->audio_disable && ist->st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) ||
            (o->subtitle_disable && ist->st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) ||
            (o->data_disable && ist->st->codecpar->codec_type == AVMEDIA_TYPE_DATA))
            ist->user_set_discard = AVDISCARD_ALL;

        if (discard_str && av_opt_eval_int(&cc, discard_opt, discard_str, &ist->user_set_discard) < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error parsing discard %s.\n",
                   discard_str);
            goto  fail;
        }

        ist->filter_in_rescale_delta_last = AV_NOPTS_VALUE;

        ist->dec_ctx = avcodec_alloc_context3(ist->dec);
        if (!ist->dec_ctx) {
            av_log(NULL, AV_LOG_ERROR, "tid=%s,Error allocating the decoder context.\n",trace_id);
            goto fail;
        }

        ret = avcodec_parameters_to_context(ist->dec_ctx, par);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "tid=%s,Error initializing the decoder context.\n",trace_id);
            goto fail;
        }

        if (o->bitexact)
            ist->dec_ctx->flags |= AV_CODEC_FLAG_BITEXACT;

        switch (par->codec_type) {
            case AVMEDIA_TYPE_VIDEO:
                if(!ist->dec)
                    ist->dec = avcodec_find_decoder(par->codec_id);

                // avformat_find_stream_info() doesn't set this for us anymore.
                ist->dec_ctx->framerate = st->avg_frame_rate;

                MATCH_PER_STREAM_OPT(frame_rates, str, framerate, ic, st,fail);
                if (framerate && av_parse_video_rate(&ist->framerate,
                                                     framerate) < 0) {
                    av_log(NULL, AV_LOG_ERROR, "tid=%s,Error parsing framerate %s.\n",
                           trace_id, framerate);
                    goto  fail;
                }

                ist->top_field_first = -1;
                MATCH_PER_STREAM_OPT(top_field_first, i, ist->top_field_first, ic, st,fail);

//                MATCH_PER_STREAM_OPT(hwaccels, str, hwaccel, ic, st,fail);
//                MATCH_PER_STREAM_OPT(hwaccel_output_formats, str,
//                                     hwaccel_output_format, ic, st,fail);

                if (!hwaccel_output_format && hwaccel && !strcmp(hwaccel, "cuvid")) {
                    av_log(NULL, AV_LOG_WARNING,
                           "tid=%s,WARNING: defaulting hwaccel_output_format to cuda for compatibility "
                           "with old commandlines. This behaviour is DEPRECATED and will be removed "
                           "in the future. Please explicitly set \"-hwaccel_output_format cuda\".\n",trace_id);
                    ist->hwaccel_output_format = AV_PIX_FMT_CUDA;
                } else if (hwaccel_output_format) {
                    ist->hwaccel_output_format = av_get_pix_fmt(hwaccel_output_format);
                    if (ist->hwaccel_output_format == AV_PIX_FMT_NONE) {
                        av_log(NULL, AV_LOG_FATAL, "tid=%s,Unrecognised hwaccel output "
                                                   "format: %s", trace_id,hwaccel_output_format);
                    }
                } else {
                    ist->hwaccel_output_format = AV_PIX_FMT_NONE;
                }

                if (hwaccel) {
                    // The NVDEC hwaccels use a CUDA device, so remap the name here.
                    if (!strcmp(hwaccel, "nvdec") || !strcmp(hwaccel, "cuvid"))
                        hwaccel = "cuda";

                    if (!strcmp(hwaccel, "none"))
                        ist->hwaccel_id = HWACCEL_NONE;
                    else if (!strcmp(hwaccel, "auto"))
                        ist->hwaccel_id = HWACCEL_AUTO;
                    else {
//                        enum AVHWDeviceType type;
//                        int i;
//                        for (i = 0; hwaccels[i].name; i++) {
//                            if (!strcmp(hwaccels[i].name, hwaccel)) {
//                                ist->hwaccel_id = hwaccels[i].id;
//                                break;
//                            }
//                        }
//
//                        if (!ist->hwaccel_id) {
//                            type = av_hwdevice_find_type_by_name(hwaccel);
//                            if (type != AV_HWDEVICE_TYPE_NONE) {
//                                ist->hwaccel_id = HWACCEL_GENERIC;
//                                ist->hwaccel_device_type = type;
//                            }
//                        }
//
//                        if (!ist->hwaccel_id) {
//                            av_log(NULL, AV_LOG_FATAL, "Unrecognized hwaccel: %s.\n",
//                                   hwaccel);
//                            av_log(NULL, AV_LOG_FATAL, "Supported hwaccels: ");
//                            type = AV_HWDEVICE_TYPE_NONE;
//                            while ((type = av_hwdevice_iterate_types(type)) !=
//                                   AV_HWDEVICE_TYPE_NONE)
//                                av_log(NULL, AV_LOG_FATAL, "%s ",
//                                       av_hwdevice_get_type_name(type));
//                            av_log(NULL, AV_LOG_FATAL, "\n");
//                            goto fail;
//                        }
                        av_log(NULL, AV_LOG_ERROR, "tid=%s,don't Supported hwaccels ",trace_id);
                    }
                }

//                MATCH_PER_STREAM_OPT(hwaccel_devices, str, hwaccel_device, ic, st,fail);
                if (hwaccel_device) {
                    ist->hwaccel_device = av_strdup(hwaccel_device);
                    if (!ist->hwaccel_device)
                        goto  fail;
                }

                ist->hwaccel_pix_fmt = AV_PIX_FMT_NONE;

                break;
            case AVMEDIA_TYPE_AUDIO:
                ist->guess_layout_max = INT_MAX;
                MATCH_PER_STREAM_OPT(guess_layout_max, i, ist->guess_layout_max, ic, st,fail);
                guess_input_channel_layout(trace_id,ist);
                break;
            case AVMEDIA_TYPE_DATA:
            case AVMEDIA_TYPE_SUBTITLE: {
                char *canvas_size = NULL;
                if(!ist->dec)
                    ist->dec = avcodec_find_decoder(par->codec_id);
                MATCH_PER_STREAM_OPT(fix_sub_duration, i, ist->fix_sub_duration, ic, st,fail);
                MATCH_PER_STREAM_OPT(canvas_sizes, str, canvas_size, ic, st,fail);
                if (canvas_size &&
                    av_parse_video_size(&ist->dec_ctx->width, &ist->dec_ctx->height, canvas_size) < 0) {
                    av_log(NULL, AV_LOG_FATAL, "tid=%s,Invalid canvas size: %s.\n", trace_id,canvas_size);
                    goto fail;
                }
                break;
            }
            case AVMEDIA_TYPE_ATTACHMENT:
            case AVMEDIA_TYPE_UNKNOWN:
                break;
            default:
                goto fail;
        }

        ret = avcodec_parameters_from_context(par, ist->dec_ctx);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "tid=%s,Error initializing the decoder context.\n",trace_id);
            goto fail;
        }
    }
    return 0;

fail:
    return -1;
}

static AVDictionary *strip_specifiers(AVDictionary *dict)
{
    AVDictionaryEntry *e = NULL;
    AVDictionary    *ret = NULL;

    while ((e = av_dict_get(dict, "", e, AV_DICT_IGNORE_SUFFIX))) {
        char *p = strchr(e->key, ':');

        if (p)
            *p = 0;
        av_dict_set(&ret, e->key, e->value, 0);
        if (p)
            *p = ':';
    }
    return ret;
}


static int dump_attachment(OptionsContext *o,AVStream *st, const char *filename)
{
    int ret;
    AVIOContext *out = NULL;
    AVDictionaryEntry *e;

    char * trace_id = o->run_context_ref->trace_id;

    if (!st->codecpar->extradata_size) {
        av_log(NULL, AV_LOG_WARNING, "tid=%s,No extradata to dump in stream #%d:%d.\n",trace_id,
               o->run_context_ref->option_input.nb_input_files - 1, st->index);
        return - 1;
    }
    if (!*filename && (e = av_dict_get(st->metadata, "filename", NULL, 0)))
        filename = e->value;
    if (!*filename) {
        av_log(NULL, AV_LOG_FATAL, "tid=%s,No filename specified and no 'filename' tag"
                                   "in stream #%d:%d.\n",trace_id, o->run_context_ref->option_input.nb_input_files - 1, st->index);
        return -1;
    }
    ret = avio_open2(&out, filename, AVIO_FLAG_WRITE, NULL, NULL);

    if (ret < 0) {
        av_log(NULL, AV_LOG_FATAL, "tid=%s,Could not open file %s for writing.\n",trace_id,
               filename);
        return -1;
    }

    avio_write(out, st->codecpar->extradata, st->codecpar->extradata_size);
    avio_flush(out);
    avio_close(out);
    return 0;
}

static int open_input_file(OptionsContext *o, const char *filename) {
    InputFile *f;
    AVFormatContext *ic;
    AVInputFormat *file_iformat = NULL;
    int err, i, ret;
    int64_t timestamp;
    AVDictionary *unused_opts = NULL;
    AVDictionaryEntry *e = NULL;
    char *video_codec_name = NULL;
    char *audio_codec_name = NULL;
    char *subtitle_codec_name = NULL;
    char *data_codec_name = NULL;
    int scan_all_pmts_set = 0;

    char * trace_id = o->run_context_ref->trace_id;

    if (o->stop_time != INT64_MAX && o->recording_time != INT64_MAX) {
        o->stop_time = INT64_MAX;
        av_log(NULL, AV_LOG_WARNING, "tid=%s,-t and -to cannot be used together; using -t.\n",trace_id);
    }

    if (o->stop_time != INT64_MAX && o->recording_time == INT64_MAX) {
        int64_t start_time = o->start_time == AV_NOPTS_VALUE ? 0 : o->start_time;
        if (o->stop_time <= start_time) {
            av_log(NULL, AV_LOG_ERROR, "tid=%s,-to value smaller than -ss; aborting.\n",trace_id);
            return AVERROR(EINVAL);
        } else {
            o->recording_time = o->stop_time - start_time;
        }
    }

    if (o->format) {
        if (!(file_iformat = av_find_input_format(o->format))) {
            av_log(NULL, AV_LOG_FATAL, "tid=%s,Unknown input format: '%s'\n", trace_id,o->format);
            return AVERROR(EINVAL);
        }
    }


    /* get default parameters from command line */
    ic = avformat_alloc_context();
    if (!ic) {
        print_error(filename, AVERROR(ENOMEM));
        return AVERROR(ENOMEM);
    }
    if (o->nb_audio_sample_rate) {
        av_dict_set_int(&o->g->format_opts, "sample_rate", o->audio_sample_rate[o->nb_audio_sample_rate - 1].u.i, 0);
    }
    if (o->nb_audio_channels) {
        /* because we set audio_channels based on both the "ac" and
         * "channel_layout" options, we need to check that the specified
         * demuxer actually has the "channels" option before setting it */
        if (file_iformat && file_iformat->priv_class &&
            av_opt_find(&file_iformat->priv_class, "channels", NULL, 0,
                        AV_OPT_SEARCH_FAKE_OBJ)) {
            av_dict_set_int(&o->g->format_opts, "channels", o->audio_channels[o->nb_audio_channels - 1].u.i, 0);
        }
    }
    if (o->nb_frame_rates) {
        /* set the format-level framerate option;
         * this is important for video grabbers, e.g. x11 */
        if (file_iformat && file_iformat->priv_class &&
            av_opt_find(&file_iformat->priv_class, "framerate", NULL, 0,
                        AV_OPT_SEARCH_FAKE_OBJ)) {
            av_dict_set(&o->g->format_opts, "framerate",
                        o->frame_rates[o->nb_frame_rates - 1].u.str, 0);
        }
    }
    if (o->nb_frame_sizes) {
        av_dict_set(&o->g->format_opts, "video_size", o->frame_sizes[o->nb_frame_sizes - 1].u.str, 0);
    }
    if (o->nb_frame_pix_fmts)
        av_dict_set(&o->g->format_opts, "pixel_format", o->frame_pix_fmts[o->nb_frame_pix_fmts - 1].u.str, 0);

    MATCH_PER_TYPE_OPT(codec_names, str, video_codec_name, ic, "v");
    MATCH_PER_TYPE_OPT(codec_names, str, audio_codec_name, ic, "a");
    MATCH_PER_TYPE_OPT(codec_names, str, subtitle_codec_name, ic, "s");
    MATCH_PER_TYPE_OPT(codec_names, str, data_codec_name, ic, "d");

    if (video_codec_name) {
        int error = 0;
        ic->video_codec = find_codec_or_die(video_codec_name, AVMEDIA_TYPE_VIDEO, 0, &error);
        if (error < 0) {
            goto fail;
        }
    }

    if (audio_codec_name) {
        int error = 0;
        ic->audio_codec = find_codec_or_die(audio_codec_name, AVMEDIA_TYPE_AUDIO, 0, &error);
        if (error < 0) {
            goto fail;
        }
    }
    if (subtitle_codec_name) {
        int error = 0;
        ic->subtitle_codec = find_codec_or_die(subtitle_codec_name, AVMEDIA_TYPE_SUBTITLE, 0, &error);
        if (error < 0) {
            goto fail;
        }
    }
    if (data_codec_name) {
        int error = 0;
        ic->data_codec = find_codec_or_die(data_codec_name, AVMEDIA_TYPE_DATA, 0, &error);
        if (error < 0) {
            goto fail;
        }
    }

    ic->video_codec_id = video_codec_name ? ic->video_codec->id : AV_CODEC_ID_NONE;
    ic->audio_codec_id = audio_codec_name ? ic->audio_codec->id : AV_CODEC_ID_NONE;
    ic->subtitle_codec_id = subtitle_codec_name ? ic->subtitle_codec->id : AV_CODEC_ID_NONE;
    ic->data_codec_id = data_codec_name ? ic->data_codec->id : AV_CODEC_ID_NONE;

    ic->flags |= AVFMT_FLAG_NONBLOCK;
    if (o->bitexact)
        ic->flags |= AVFMT_FLAG_BITEXACT;
    // 不用这个功能
//    ic->interrupt_callback = int_cb;

    if (!av_dict_get(o->g->format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE)) {
        av_dict_set(&o->g->format_opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
        scan_all_pmts_set = 1;
    }
    /* open the input file with generic avformat function */
    err = avformat_open_input(&ic, filename, file_iformat, &o->g->format_opts);
    if (err < 0) {
        print_error(filename, err);
        if (err == AVERROR_PROTOCOL_NOT_FOUND)
            av_log(NULL, AV_LOG_ERROR, "tid=%s,Did you mean file:%s?\n", trace_id,filename);
        goto fail;
    }
    if (scan_all_pmts_set)
        av_dict_set(&o->g->format_opts, "scan_all_pmts", NULL, AV_DICT_MATCH_CASE);
    remove_avoptions(&o->g->format_opts, o->g->codec_opts);
    if (0 > assert_avoptions(o->g->format_opts)) {
        goto fail;
    }

    /* apply forced codec ids */
    for (i = 0; i < ic->nb_streams; i++)
        choose_decoder(o, ic, ic->streams[i]);

    if (o->run_context_ref->find_stream_info) {
        int error = 0;
        AVDictionary **opts = setup_find_stream_info_opts(trace_id,ic, o->g->codec_opts,&error);
        if(error){
            av_log(NULL, AV_LOG_ERROR, "tid=%s,find codec error.\n",trace_id,filename);
            goto fail;
        }
        int orig_nb_streams = ic->nb_streams;

        /* If not enough info to get the stream parameters, we decode the
           first frames to get it. (used in mpeg case for example) */
        ret = avformat_find_stream_info(ic, opts);

        for (i = 0; i < orig_nb_streams; i++)
            av_dict_free(&opts[i]);
        av_freep(&opts);

        if (ret < 0) {
            av_log(NULL, AV_LOG_FATAL, "tid=%s,%s: could not find codec parameters\n", trace_id,filename);
            if (ic->nb_streams == 0) {
                avformat_close_input(&ic);
                goto fail;
            }
        }
    }

    if (o->start_time != AV_NOPTS_VALUE && o->start_time_eof != AV_NOPTS_VALUE) {
        av_log(NULL, AV_LOG_WARNING, "tid=%s,Cannot use -ss and -sseof both, using -ss for %s\n",trace_id, filename);
        o->start_time_eof = AV_NOPTS_VALUE;
    }

    if (o->start_time_eof != AV_NOPTS_VALUE) {
        if (o->start_time_eof >= 0) {
            av_log(NULL, AV_LOG_ERROR, "tid=%s,-sseof value must be negative; aborting\n",trace_id);
            goto fail;
        }
        if (ic->duration > 0) {
            o->start_time = o->start_time_eof + ic->duration;
            if (o->start_time < 0) {
                av_log(NULL, AV_LOG_WARNING, "tid=%s,-sseof value seeks to before start of file %s; ignored\n", trace_id,filename);
                o->start_time = AV_NOPTS_VALUE;
            }
        } else
            av_log(NULL, AV_LOG_WARNING, "tid=%s,Cannot use -sseof, duration of %s not known\n", trace_id,filename);
    }
    timestamp = (o->start_time == AV_NOPTS_VALUE) ? 0 : o->start_time;
    /* add the stream start time */
    if (!o->seek_timestamp && ic->start_time != AV_NOPTS_VALUE)
        timestamp += ic->start_time;

    /* if seeking requested, we execute it */
    if (o->start_time != AV_NOPTS_VALUE) {
        int64_t seek_timestamp = timestamp;

        if (!(ic->iformat->flags & AVFMT_SEEK_TO_PTS)) {
            int dts_heuristic = 0;
            for (i = 0; i < ic->nb_streams; i++) {
                const AVCodecParameters *par = ic->streams[i]->codecpar;
                if (par->video_delay) {
                    dts_heuristic = 1;
                    break;
                }
            }
            if (dts_heuristic) {
                seek_timestamp -= 3 * AV_TIME_BASE / 23;
            }
        }
        ret = avformat_seek_file(ic, -1, INT64_MIN, seek_timestamp, seek_timestamp, 0);
        if (ret < 0) {
            av_log(NULL, AV_LOG_WARNING, "tid=%s,%s: could not seek to position %0.3f\n",
                   trace_id, filename, (double) timestamp / AV_TIME_BASE);
        }
    }

    /* update the current parameters so that they match the one of the input stream */
    add_input_streams(o, ic);

    /* dump the file content */
//    av_dump_format(ic, nb_input_files, filename, 0);

    int has_error = 0;
    GROW_ARRAY(trace_id,o->run_context_ref->option_input.input_files, o->run_context_ref->option_input.nb_input_files, has_error);
    if(has_error < 0){
        goto fail;
    }
    f = av_mallocz(sizeof(*f));
    if (!f)
        goto  fail;
    o->run_context_ref->option_input.input_files[o->run_context_ref->option_input.nb_input_files - 1] = f;

    f->ctx = ic;
    f->ist_index = o->run_context_ref->option_input.nb_input_streams - ic->nb_streams;
    f->start_time = o->start_time;
    f->recording_time = o->recording_time;
    f->input_ts_offset = o->input_ts_offset;
    f->ts_offset = o->input_ts_offset -
                   (o->run_context_ref->copy_ts ? (o->run_context_ref->start_at_zero && ic->start_time != AV_NOPTS_VALUE ? ic->start_time : 0) : timestamp);
    f->nb_streams = ic->nb_streams;
    f->rate_emu = o->rate_emu;
    f->accurate_seek = o->accurate_seek;
    f->loop = o->loop;
    f->duration = 0;
    f->time_base = (AVRational) {1, 1};
    f->pkt = av_packet_alloc();
    if (!f->pkt)
        goto  fail;
#if HAVE_THREADS
    f->thread_queue_size = o->thread_queue_size;
#endif

    /* check if all codec options have been used */
    unused_opts = strip_specifiers(o->g->codec_opts);
    for (i = f->ist_index; i < o->run_context_ref->option_input.nb_input_streams; i++) {
        e = NULL;
        while ((e = av_dict_get(o->run_context_ref->option_input.input_streams[i]->decoder_opts, "", e,
                                AV_DICT_IGNORE_SUFFIX)))
            av_dict_set(&unused_opts, e->key, NULL, 0);
    }

    e = NULL;
    while ((e = av_dict_get(unused_opts, "", e, AV_DICT_IGNORE_SUFFIX))) {
        const AVClass *class = avcodec_get_class();
        const AVOption *option = av_opt_find(&class, e->key, NULL, 0,
                                             AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ);
        const AVClass *fclass = avformat_get_class();
        const AVOption *foption = av_opt_find(&fclass, e->key, NULL, 0,
                                              AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ);
        if (!option || foption)
            continue;


        if (!(option->flags & AV_OPT_FLAG_DECODING_PARAM)) {
            av_log(NULL, AV_LOG_ERROR, "tid=%s,Codec AVOption %s (%s) specified for "
                                       "input file #%d (%s) is not a decoding option.\n",trace_id, e->key,
                   option->help ? option->help : "", o->run_context_ref->option_input.nb_input_files - 1,
                   filename);
            goto fail;
        }

        av_log(NULL, AV_LOG_WARNING, "tid=%s,Codec AVOption %s (%s) specified for "
                                     "input file #%d (%s) has not been used for any stream. The most "
                                     "likely reason is either wrong type (e.g. a video option with "
                                     "no video streams) or that it is a private option of some decoder "
                                     "which was not actually used for any stream.\n", trace_id,e->key,
               option->help ? option->help : "", o->run_context_ref->option_input.nb_input_files - 1, filename);
    }
    av_dict_free(&unused_opts);

    for (i = 0; i < o->nb_dump_attachment; i++) {
        int j;

        for (j = 0; j < ic->nb_streams; j++) {
            AVStream *st = ic->streams[j];

            if (check_stream_specifier(ic, st, o->dump_attachment[i].specifier) == 1)
                dump_attachment(o,st, o->dump_attachment[i].u.str);
        }
    }

    o->run_context_ref->input_stream_potentially_available = 1;

    return 0;

fail:
    // destroy resource
    avformat_free_context(ic);
    return -1;
}



static void uninit_options(OptionsContext *o)
{
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



static void init_options(ParsedOptionsContext *parent, OptionsContext *o)
{
    memset(o, 0, sizeof(*o));

    o->stop_time = INT64_MAX;
    o->mux_max_delay  = 0.7;
    o->start_time     = AV_NOPTS_VALUE;
    o->start_time_eof = AV_NOPTS_VALUE;
    o->recording_time = INT64_MAX;
    o->limit_filesize = UINT64_MAX;
    o->chapters_input_file = INT_MAX;
    o->accurate_seek  = 1;
    o->thread_queue_size = -1;


    o->run_context_ref = parent->options_context.run_context_ref;
}

static int open_files(ParsedOptionsContext *parent, OptionGroupList *l, const char *inout,
                      int (*open_file)(OptionsContext *, const char *)) {
    int i, ret;

    char * trace_id = parent->raw_context.trace_id;

    for (i = 0; i < l->nb_groups; i++) {
        OptionGroup *g = &l->groups[i];
        OptionsContext o;

        init_options(parent,&o);
        o.g = g;

        ret = parse_optgroup(&o, g);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "tid=%s,Error parsing options for %s file "
                                       "%s.\n", trace_id,inout, g->arg);
            uninit_options(&o);
            return ret;
        }

        av_log(NULL, AV_LOG_DEBUG, "Opening an %s file: %s.\n", inout, g->arg);
        ret = open_file(&o, g->arg);

        uninit_options(&o);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "tid=%s,Error opening %s file %s.\n",
                   trace_id,  inout, g->arg);
            return ret;
        }
        av_log(NULL, AV_LOG_DEBUG, "tid=%s,Successfully opened the file.\n",trace_id);
    }

    return 0;
}

static int init_complex_filters(RunContext *run_context)
{
    int i, ret = 0;

    for (i = 0; i < run_context->nb_filtergraphs; i++) {
        ret = init_complex_filtergraph(run_context,run_context->filtergraphs[i]);
        if (ret < 0)
            return ret;
    }
    return 0;
}

static int choose_encoder(OptionsContext *o, AVFormatContext *s, OutputStream *ost)
{
    enum AVMediaType type = ost->st->codecpar->codec_type;
    char *codec_name = NULL;
    char * trace_id = o->run_context_ref->trace_id;

    if (type == AVMEDIA_TYPE_VIDEO || type == AVMEDIA_TYPE_AUDIO || type == AVMEDIA_TYPE_SUBTITLE) {
        MATCH_PER_STREAM_OPT(codec_names, str, codec_name, s, ost->st,fail);
        if (!codec_name) {
            ost->st->codecpar->codec_id = av_guess_codec(s->oformat, NULL, s->url,
                                                         NULL, ost->st->codecpar->codec_type);
            ost->enc = avcodec_find_encoder(ost->st->codecpar->codec_id);
            if (!ost->enc) {
                av_log(NULL, AV_LOG_FATAL, "tid=%s,Automatic encoder selection failed for "
                                           "output stream #%d:%d. Default encoder for format %s (codec %s) is "
                                           "probably disabled. Please choose an encoder manually.\n",
                       trace_id,ost->file_index, ost->index, s->oformat->name,
                       avcodec_get_name(ost->st->codecpar->codec_id));
                return AVERROR_ENCODER_NOT_FOUND;
            }
        } else if (!strcmp(codec_name, "copy"))
            ost->stream_copy = 1;
        else {
            int has_err = 0;
            ost->enc = find_codec_or_die(codec_name, ost->st->codecpar->codec_type, 1,&has_err);
            if(has_err < 0){
                return -1;
            }
            ost->st->codecpar->codec_id = ost->enc->id;
        }
        ost->encoding_needed = !ost->stream_copy;
    } else {
        /* no encoding supported for other media types */
        ost->stream_copy     = 1;
        ost->encoding_needed = 0;
    }
    return 0;
    fail:
    return  -1;
}



static OutputStream *new_output_stream(OptionsContext *o, AVFormatContext *oc, enum AVMediaType type, int source_index,int * has_err)
{
    OutputStream *ost;
    AVStream *st = avformat_new_stream(oc, NULL);
    int idx      = oc->nb_streams - 1, ret = 0;
    const char *bsfs = NULL, *time_base = NULL;
    char *next, *codec_tag = NULL;
    double qscale = -1;
    int i;

    char * trace_id = o->run_context_ref->trace_id;

    *has_err = 0;
    if (!st) {
        av_log(NULL, AV_LOG_FATAL, "tid=%s,Could not alloc stream.\n",trace_id);
//        exit_program(1);
        *has_err = 1;
        return NULL;
    }

    if (oc->nb_streams - 1 < o->nb_streamid_map)
        st->id = o->streamid_map[oc->nb_streams - 1];

    GROW_ARRAY(trace_id,o->run_context_ref->option_output.output_streams, o->run_context_ref->option_output.nb_output_streams, *has_err);
    if (!(ost = av_mallocz(sizeof(*ost)))){

//        exit_program(1);
        *has_err = 1;
        return NULL;
    }
    o->run_context_ref->option_output.output_streams[o->run_context_ref->option_output.nb_output_streams - 1] = ost;

    ost->file_index = o->run_context_ref->option_output.nb_output_files - 1;
    ost->index      = idx;
    ost->st         = st;
    ost->forced_kf_ref_pts = AV_NOPTS_VALUE;
    st->codecpar->codec_type = type;

    ret = choose_encoder(o, oc, ost);
    if (ret < 0) {
        av_log(NULL, AV_LOG_FATAL, "tid=%s,Error selecting an encoder for stream "
                                   "%d:%d\n",trace_id, ost->file_index, ost->index);
        //        exit_program(1);
        *has_err = 1;
        return NULL;
    }

    ost->enc_ctx = avcodec_alloc_context3(ost->enc);
    if (!ost->enc_ctx) {
        av_log(NULL, AV_LOG_ERROR, "tid=%s,Error allocating the encoding context.\n",trace_id);
        //        exit_program(1);
        *has_err = 1;
        return NULL;
    }
    ost->enc_ctx->codec_type = type;

    ost->ref_par = avcodec_parameters_alloc();
    if (!ost->ref_par) {
        av_log(NULL, AV_LOG_ERROR, "tid=%s,Error allocating the encoding parameters.\n",trace_id);
        //        exit_program(1);
        *has_err = 1;
        return NULL;
    }

    if (ost->enc) {
        AVIOContext *s = NULL;
        char *buf = NULL, *arg = NULL, *preset = NULL;

        ost->encoder_opts  = filter_codec_opts(o->g->codec_opts, ost->enc->id, oc, st, ost->enc,has_err);
        if(*has_err < 0){
            return NULL;
        }
        MATCH_PER_STREAM_OPT(presets, str, preset, oc, st,fail);
        ost->autoscale = 1;
        MATCH_PER_STREAM_OPT(autoscale, i, ost->autoscale, oc, st,fail);
        if (preset && (!(ret = get_preset_file_2(preset, ost->enc->name, &s)))) {
            do  {

                buf = get_line(s,has_err);
                if(*has_err < 0){
                    return NULL;
                }
                if (!buf[0] || buf[0] == '#') {
                    av_free(buf);
                    continue;
                }
                if (!(arg = strchr(buf, '='))) {
                    av_log(NULL, AV_LOG_FATAL, "tid=%s,Invalid line found in the preset file.\n",trace_id);
                    //        exit_program(1);
                    *has_err = 1;
                    return NULL;
                }
                *arg++ = 0;
                av_dict_set(&ost->encoder_opts, buf, arg, AV_DICT_DONT_OVERWRITE);
                av_free(buf);
            } while (!s->eof_reached);
            avio_closep(&s);
        }
        if (ret) {
            av_log(NULL, AV_LOG_FATAL,
                   "tid=%s,Preset %s specified for stream %d:%d, but could not be opened.\n",
                   trace_id,preset, ost->file_index, ost->index);
            //        exit_program(1);
            *has_err = 1;
            return NULL;
        }
    } else {
        ost->encoder_opts = filter_codec_opts(o->g->codec_opts, AV_CODEC_ID_NONE, oc, st, NULL,has_err);
        if(*has_err < 0){
            return  NULL;
        }
    }


    if (o->bitexact)
        ost->enc_ctx->flags |= AV_CODEC_FLAG_BITEXACT;

    MATCH_PER_STREAM_OPT(time_bases, str, time_base, oc, st,fail);
    if (time_base) {
        AVRational q;
        if (av_parse_ratio(&q, time_base, INT_MAX, 0, NULL) < 0 ||
            q.num <= 0 || q.den <= 0) {
            av_log(NULL, AV_LOG_FATAL, "tid=%s,Invalid time base: %s\n", trace_id,time_base);
            //        exit_program(1);
            *has_err = 1;
            return NULL;
        }
        st->time_base = q;
    }

    MATCH_PER_STREAM_OPT(enc_time_bases, str, time_base, oc, st,fail);
    if (time_base) {
        AVRational q;
        if (av_parse_ratio(&q, time_base, INT_MAX, 0, NULL) < 0 ||
            q.den <= 0) {
            av_log(NULL, AV_LOG_FATAL, "tid=%s,Invalid time base: %s\n",trace_id, time_base);
            //        exit_program(1);
            *has_err = 1;
            return NULL;
        }
        ost->enc_timebase = q;
    }

    ost->max_frames = INT64_MAX;
    MATCH_PER_STREAM_OPT(max_frames, i64, ost->max_frames, oc, st,fail);
    for (i = 0; i<o->nb_max_frames; i++) {
        char *p = o->max_frames[i].specifier;
        if (!*p && type != AVMEDIA_TYPE_VIDEO) {
            av_log(NULL, AV_LOG_WARNING, "tid=%s,Applying unspecific -frames to non video streams, maybe you meant -vframes ?\n",trace_id);
            break;
        }
    }

    ost->copy_prior_start = -1;
    MATCH_PER_STREAM_OPT(copy_prior_start, i, ost->copy_prior_start, oc ,st,fail);

    MATCH_PER_STREAM_OPT(bitstream_filters, str, bsfs, oc, st,fail);
    if (bsfs && *bsfs) {
        ret = av_bsf_list_parse_str(bsfs, &ost->bsf_ctx);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "tid=%s,Error parsing bitstream filter sequence '%s': %s\n", trace_id,bsfs, av_err2str(ret));
            //        exit_program(1);
            *has_err = 1;
            return NULL;
        }
    }

    MATCH_PER_STREAM_OPT(codec_tags, str, codec_tag, oc, st,fail);
    if (codec_tag) {
        uint32_t tag = strtol(codec_tag, &next, 0);
        if (*next)
            tag = AV_RL32(codec_tag);
        ost->st->codecpar->codec_tag =
        ost->enc_ctx->codec_tag = tag;
    }

    MATCH_PER_STREAM_OPT(qscale, dbl, qscale, oc, st,fail);
    if (qscale >= 0) {
        ost->enc_ctx->flags |= AV_CODEC_FLAG_QSCALE;
        ost->enc_ctx->global_quality = FF_QP2LAMBDA * qscale;
    }

    MATCH_PER_STREAM_OPT(disposition, str, ost->disposition, oc, st,fail);
    ost->disposition = av_strdup(ost->disposition);

    ost->max_muxing_queue_size = 128;
    MATCH_PER_STREAM_OPT(max_muxing_queue_size, i, ost->max_muxing_queue_size, oc, st,fail);
    ost->max_muxing_queue_size *= sizeof(ost->pkt);

    ost->muxing_queue_data_size = 0;

    ost->muxing_queue_data_threshold = 50*1024*1024;
    MATCH_PER_STREAM_OPT(muxing_queue_data_threshold, i, ost->muxing_queue_data_threshold, oc, st,fail);

    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        ost->enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    av_dict_copy(&ost->sws_dict, o->g->sws_dict, 0);

    av_dict_copy(&ost->swr_opts, o->g->swr_opts, 0);
    if (ost->enc && av_get_exact_bits_per_sample(ost->enc->id) == 24)
        av_dict_set(&ost->swr_opts, "output_sample_bits", "24", 0);

    av_dict_copy(&ost->resample_opts, o->g->resample_opts, 0);

    ost->source_index = source_index;
    if (source_index >= 0) {
        ost->sync_ist = o->run_context_ref->option_input.input_streams[source_index];
        o->run_context_ref->option_input.input_streams[source_index]->discard = 0;
        o->run_context_ref->option_input.input_streams[source_index]->st->discard = o->run_context_ref->option_input.input_streams[source_index]->user_set_discard;
    }
    ost->last_mux_dts = AV_NOPTS_VALUE;

    ost->muxing_queue = av_fifo_alloc(8 * sizeof(AVPacket));
    if (!ost->muxing_queue){
        //        exit_program(1);
        *has_err = 1;
        return NULL;
    }

    return ost;

fail:
    *has_err = 1;
    return NULL;
}


static int parse_matrix_coeffs(char * trace_id,uint16_t *dest, const char *str)
{
    int i;
    const char *p = str;
    for (i = 0;; i++) {
        dest[i] = atoi(p);
        if (i == 63)
            break;
        p = strchr(p, ',');
        if (!p) {
            av_log(NULL, AV_LOG_FATAL, "tid=%s,Syntax error in matrix \"%s\" at coeff %d\n",trace_id, str, i);
//            exit_program(1);
            return -1;
        }
        p++;
    }
}

static char *get_ost_filters(OptionsContext *o, AVFormatContext *oc,
                             OutputStream *ost)
{
    AVStream *st = ost->st;

    if (ost->filters_script && ost->filters) {
        av_log(NULL, AV_LOG_ERROR, "Both -filter and -filter_script set for "
                                   "output stream #%d:%d.\n", o->run_context_ref->option_output.nb_output_files, st->index);
//        exit_program(1);
        return NULL;
    }

    if (ost->filters_script)
        return read_file(ost->filters_script);
    else if (ost->filters)
        return av_strdup(ost->filters);

    return av_strdup(st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO ?
                     "null" : "anull");
}

static int check_streamcopy_filters(OptionsContext *o, AVFormatContext *oc,
                                     const OutputStream *ost, enum AVMediaType type)
{
    if (ost->filters_script || ost->filters) {
        av_log(NULL, AV_LOG_ERROR,
               "%s '%s' was defined for %s output stream %d:%d but codec copy was selected.\n"
               "Filtering and streamcopy cannot be used together.\n",
               ost->filters ? "Filtergraph" : "Filtergraph script",
               ost->filters ? ost->filters : ost->filters_script,
               av_get_media_type_string(type), ost->file_index, ost->index);
        return -1;
    }
    return  0;
}

static OutputStream *new_video_stream(OptionsContext *o, AVFormatContext *oc, int source_index,int *has_err)
{
    AVStream *st;
    OutputStream *ost;
    AVCodecContext *video_enc;
    char *frame_rate = NULL, *max_frame_rate = NULL, *frame_aspect_ratio = NULL;

    ost = new_output_stream(o, oc, AVMEDIA_TYPE_VIDEO, source_index,has_err);
    if(has_err < 0){
        return NULL;
    }
    char * trace_id = o->run_context_ref->trace_id;
    st  = ost->st;
    video_enc = ost->enc_ctx;

    MATCH_PER_STREAM_OPT(frame_rates, str, frame_rate, oc, st,fail);
    if (frame_rate && av_parse_video_rate(&ost->frame_rate, frame_rate) < 0) {
        av_log(NULL, AV_LOG_FATAL, "tid=%s,Invalid framerate value: %s\n", trace_id,frame_rate);
//        exit_program(1);
        *has_err = -1;
        return NULL;
    }

    MATCH_PER_STREAM_OPT(max_frame_rates, str, max_frame_rate, oc, st,fail);
    if (max_frame_rate && av_parse_video_rate(&ost->max_frame_rate, max_frame_rate) < 0) {
        av_log(NULL, AV_LOG_FATAL, "tid=%s,Invalid maximum framerate value: %s\n", trace_id,max_frame_rate);
//        exit_program(1);
        *has_err = -1;
        return NULL;
    }

    if (frame_rate && max_frame_rate) {
        av_log(NULL, AV_LOG_ERROR, "tid=%s,Only one of -fpsmax and -r can be set for a stream.\n",trace_id);
        //        exit_program(1);
        *has_err = -1;
        return NULL;
    }

    if ((frame_rate || max_frame_rate) &&
        o->run_context_ref->video_sync_method == VSYNC_PASSTHROUGH)
        av_log(NULL, AV_LOG_ERROR, "tid=%s,Using -vsync 0 and -r/-fpsmax can produce invalid output files\n",trace_id);

    MATCH_PER_STREAM_OPT(frame_aspect_ratios, str, frame_aspect_ratio, oc, st,fail);
    if (frame_aspect_ratio) {
        AVRational q;
        if (av_parse_ratio(&q, frame_aspect_ratio, 255, 0, NULL) < 0 ||
            q.num <= 0 || q.den <= 0) {
            av_log(NULL, AV_LOG_FATAL, "tid=%s,Invalid aspect ratio: %s\n",trace_id, frame_aspect_ratio);
            //        exit_program(1);
            *has_err = -1;
            return NULL;
        }
        ost->frame_aspect_ratio = q;
    }

    MATCH_PER_STREAM_OPT(filter_scripts, str, ost->filters_script, oc, st,fail);
    MATCH_PER_STREAM_OPT(filters,        str, ost->filters,        oc, st,fail);

    if (!ost->stream_copy) {
        const char *p = NULL;
        char *frame_size = NULL;
        char *frame_pix_fmt = NULL;
        char *intra_matrix = NULL, *inter_matrix = NULL;
        char *chroma_intra_matrix = NULL;
        int do_pass = 0;
        int i;

        MATCH_PER_STREAM_OPT(frame_sizes, str, frame_size, oc, st,fail);
        if (frame_size && av_parse_video_size(&video_enc->width, &video_enc->height, frame_size) < 0) {
            av_log(NULL, AV_LOG_FATAL, "tid=%s,Invalid frame size: %s.\n", trace_id,frame_size);
            //        exit_program(1);
            *has_err = -1;
            return NULL;
        }

        video_enc->bits_per_raw_sample = o->run_context_ref->frame_bits_per_raw_sample;
        MATCH_PER_STREAM_OPT(frame_pix_fmts, str, frame_pix_fmt, oc, st,fail);
        if (frame_pix_fmt && *frame_pix_fmt == '+') {
            ost->keep_pix_fmt = 1;
            if (!*++frame_pix_fmt)
                frame_pix_fmt = NULL;
        }
        if (frame_pix_fmt && (video_enc->pix_fmt = av_get_pix_fmt(frame_pix_fmt)) == AV_PIX_FMT_NONE) {
            av_log(NULL, AV_LOG_FATAL, "tid=%s,Unknown pixel format requested: %s.\n", trace_id,frame_pix_fmt);
            //        exit_program(1);
            *has_err = -1;
            return NULL;
        }
        st->sample_aspect_ratio = video_enc->sample_aspect_ratio;

        if (o->run_context_ref->intra_only)
            video_enc->gop_size = 0;
        MATCH_PER_STREAM_OPT(intra_matrices, str, intra_matrix, oc, st,fail);
        if (intra_matrix) {
            if (!(video_enc->intra_matrix = av_mallocz(sizeof(*video_enc->intra_matrix) * 64))) {
                av_log(NULL, AV_LOG_FATAL, "tid=%s,Could not allocate memory for intra matrix.\n",trace_id);
                //        exit_program(1);
                *has_err = -1;
                return NULL;
            }
            if(0 > parse_matrix_coeffs(trace_id,video_enc->intra_matrix, intra_matrix)){
                *has_err = -1;
                return NULL;
            }
        }
        MATCH_PER_STREAM_OPT(chroma_intra_matrices, str, chroma_intra_matrix, oc, st,fail);
        if (chroma_intra_matrix) {
            uint16_t *p = av_mallocz(sizeof(*video_enc->chroma_intra_matrix) * 64);
            if (!p) {
                av_log(NULL, AV_LOG_FATAL, "tid=%s,Could not allocate memory for intra matrix.\n",trace_id);
                //        exit_program(1);
                *has_err = -1;
                return NULL;
            }
            video_enc->chroma_intra_matrix = p;
            parse_matrix_coeffs(trace_id,p, chroma_intra_matrix);
        }
        MATCH_PER_STREAM_OPT(inter_matrices, str, inter_matrix, oc, st,fail);
        if (inter_matrix) {
            if (!(video_enc->inter_matrix = av_mallocz(sizeof(*video_enc->inter_matrix) * 64))) {
                av_log(NULL, AV_LOG_FATAL, "tid=%s,Could not allocate memory for inter matrix.\n",trace_id);
                //        exit_program(1);
                *has_err = -1;
                return NULL;
            }
            parse_matrix_coeffs(trace_id,video_enc->inter_matrix, inter_matrix);
        }

        MATCH_PER_STREAM_OPT(rc_overrides, str, p, oc, st,fail);
        for (i = 0; p; i++) {
            int start, end, q;
            int e = sscanf(p, "%d,%d,%d", &start, &end, &q);
            if (e != 3) {
                av_log(NULL, AV_LOG_FATAL, "tid=%s,error parsing rc_override\n",trace_id);
                //        exit_program(1);
                *has_err = -1;
                return NULL;
            }
            video_enc->rc_override =
                    av_realloc_array(video_enc->rc_override,
                                     i + 1, sizeof(RcOverride));
            if (!video_enc->rc_override) {
                av_log(NULL, AV_LOG_FATAL, "tid=%s,Could not (re)allocate memory for rc_override.\n",trace_id);
                //        exit_program(1);
                *has_err = -1;
                return NULL;
            }
            video_enc->rc_override[i].start_frame = start;
            video_enc->rc_override[i].end_frame   = end;
            if (q > 0) {
                video_enc->rc_override[i].qscale         = q;
                video_enc->rc_override[i].quality_factor = 1.0;
            }
            else {
                video_enc->rc_override[i].qscale         = 0;
                video_enc->rc_override[i].quality_factor = -q/100.0;
            }
            p = strchr(p, '/');
            if (p) p++;
        }
        video_enc->rc_override_count = i;

        if (o->run_context_ref->do_psnr)
            video_enc->flags|= AV_CODEC_FLAG_PSNR;

        /* two pass mode */
        MATCH_PER_STREAM_OPT(pass, i, do_pass, oc, st,fail);
        if (do_pass) {
            if (do_pass & 1) {
                video_enc->flags |= AV_CODEC_FLAG_PASS1;
                av_dict_set(&ost->encoder_opts, "flags", "+pass1", AV_DICT_APPEND);
            }
            if (do_pass & 2) {
                video_enc->flags |= AV_CODEC_FLAG_PASS2;
                av_dict_set(&ost->encoder_opts, "flags", "+pass2", AV_DICT_APPEND);
            }
        }

        MATCH_PER_STREAM_OPT(passlogfiles, str, ost->logfile_prefix, oc, st,fail);
        if (ost->logfile_prefix &&
            !(ost->logfile_prefix = av_strdup(ost->logfile_prefix))){
            //        exit_program(1);
            *has_err = -1;
            return NULL;
        }

        if (do_pass) {
            char logfilename[1024];
            FILE *f;

            snprintf(logfilename, sizeof(logfilename), "%s-%d.log",
                     ost->logfile_prefix ? ost->logfile_prefix :
                     DEFAULT_PASS_LOGFILENAME_PREFIX,
                     i);
            if (!strcmp(ost->enc->name, "libx264")) {
                av_dict_set(&ost->encoder_opts, "stats", logfilename, AV_DICT_DONT_OVERWRITE);
            } else {
                if (video_enc->flags & AV_CODEC_FLAG_PASS2) {
                    char  *logbuffer = read_file(logfilename);

                    if (!logbuffer) {
                        av_log(NULL, AV_LOG_FATAL, "tid=%s,Error reading log file '%s' for pass-2 encoding\n",
                               trace_id, logfilename);
                        //        exit_program(1);
                        *has_err = -1;
                        return NULL;
                    }
                    video_enc->stats_in = logbuffer;
                }
                if (video_enc->flags & AV_CODEC_FLAG_PASS1) {
                    f = av_fopen_utf8(logfilename, "wb");
                    if (!f) {
                        av_log(NULL, AV_LOG_FATAL,
                               "tid=%s,Cannot write log file '%s' for pass-1 encoding: %s\n",
                               trace_id,logfilename, strerror(errno));
                        //        exit_program(1);
                        *has_err = -1;
                        return NULL;
                    }
                    ost->logfile = f;
                }
            }
        }

        MATCH_PER_STREAM_OPT(forced_key_frames, str, ost->forced_keyframes, oc, st,fail);
        if (ost->forced_keyframes)
            ost->forced_keyframes = av_strdup(ost->forced_keyframes);

        MATCH_PER_STREAM_OPT(force_fps, i, ost->force_fps, oc, st,fail);

        ost->top_field_first = -1;
        MATCH_PER_STREAM_OPT(top_field_first, i, ost->top_field_first, oc, st,fail);


        ost->avfilter = get_ost_filters(o, oc, ost);
        if (!ost->avfilter){
            //  exit_program(1);
            *has_err = -1;
            return NULL;

        }
    } else {
        MATCH_PER_STREAM_OPT(copy_initial_nonkeyframes, i, ost->copy_initial_nonkeyframes, oc ,st,fail);
    }

    if (ost->stream_copy){
        if (0 > check_streamcopy_filters(o, oc, ost, AVMEDIA_TYPE_VIDEO)){
            *has_err = -1;
            return NULL;
        }
    }

    *has_err = 0;
    return ost;

    fail:

    *has_err = -1;
    return NULL;
}
static OutputStream *new_audio_stream(OptionsContext *o, AVFormatContext *oc, int source_index,int *has_err)
{
    int n;
    AVStream *st;
    OutputStream *ost;
    AVCodecContext *audio_enc;

    ost = new_output_stream(o, oc, AVMEDIA_TYPE_AUDIO, source_index,has_err);
    if(*has_err < 0){
        return NULL;
    }
    st  = ost->st;

    audio_enc = ost->enc_ctx;
    audio_enc->codec_type = AVMEDIA_TYPE_AUDIO;

    MATCH_PER_STREAM_OPT(filter_scripts, str, ost->filters_script, oc, st,fail);
    MATCH_PER_STREAM_OPT(filters,        str, ost->filters,        oc, st,fail);


    char * trace_id = o->run_context_ref->trace_id;

    if (!ost->stream_copy) {
        char *sample_fmt = NULL;

        MATCH_PER_STREAM_OPT(audio_channels, i, audio_enc->channels, oc, st,fail);

        MATCH_PER_STREAM_OPT(sample_fmts, str, sample_fmt, oc, st,fail);
        if (sample_fmt &&
            (audio_enc->sample_fmt = av_get_sample_fmt(sample_fmt)) == AV_SAMPLE_FMT_NONE) {
            av_log(NULL, AV_LOG_FATAL, "tid=%s,Invalid sample format '%s'\n", trace_id,sample_fmt);
//            exit_program(1);
            *has_err = -1;
            return NULL;
        }

        MATCH_PER_STREAM_OPT(audio_sample_rate, i, audio_enc->sample_rate, oc, st,fail);

        MATCH_PER_STREAM_OPT(apad, str, ost->apad, oc, st,fail);
        ost->apad = av_strdup(ost->apad);

        ost->avfilter = get_ost_filters(o, oc, ost);
        if (!ost->avfilter){
//           exit_program(1);
            *has_err = -1;
            return NULL;
        }

        /* check for channel mapping for this audio stream */
        for (n = 0; n < o->nb_audio_channel_maps; n++) {
            AudioChannelMap *map = &o->audio_channel_maps[n];
            if ((map->ofile_idx   == -1 || ost->file_index == map->ofile_idx) &&
                (map->ostream_idx == -1 || ost->st->index  == map->ostream_idx)) {
                InputStream *ist;

                if (map->channel_idx == -1) {
                    ist = NULL;
                } else if (ost->source_index < 0) {
                    av_log(NULL, AV_LOG_FATAL, "tid=%s,Cannot determine input stream for channel mapping %d.%d\n",
                           trace_id,ost->file_index, ost->st->index);
                    continue;
                } else {
                    ist = o->run_context_ref->option_input.input_streams[ost->source_index];
                }

                if (!ist || (ist->file_index == map->file_idx && ist->st->index == map->stream_idx)) {
                    if (av_reallocp_array(&ost->audio_channels_map,
                                          ost->audio_channels_mapped + 1,
                                          sizeof(*ost->audio_channels_map)
                    ) < 0 ){
//                        exit_program(1);
                        *has_err = -1;
                        return NULL;
                    }


                    ost->audio_channels_map[ost->audio_channels_mapped++] = map->channel_idx;
                }
            }
        }
    }

    if (ost->stream_copy)
        check_streamcopy_filters(o, oc, ost, AVMEDIA_TYPE_AUDIO);

    return ost;

    fail:
    *has_err = -1;
    return NULL;
}
static int init_output_filter(OutputFilter *ofilter, OptionsContext *o,
                               AVFormatContext *oc)
{
    OutputStream *ost;

    char * trace_id = o->run_context_ref->trace_id;

    int has_err = 0;
    switch (ofilter->type) {
        case AVMEDIA_TYPE_VIDEO: ost = new_video_stream(o, oc, -1,&has_err); break;
        case AVMEDIA_TYPE_AUDIO: ost = new_audio_stream(o, oc, -1,&has_err); break;
        default:
            av_log(NULL, AV_LOG_FATAL, "tid=%s,Only video and audio filters are supported "
                                       "currently.\n",trace_id);
//            exit_program(1);
            has_err = -1;
    }
    if(has_err < 0){
        return  -1;
    }

    ost->source_index = -1;
    ost->filter       = ofilter;

    ofilter->ost      = ost;
    ofilter->format   = -1;

    if (ost->stream_copy) {
        av_log(NULL, AV_LOG_ERROR, "tid=%s,Streamcopy requested for output stream %d:%d, "
                                   "which is fed from a complex filtergraph. Filtering and streamcopy "
                                   "cannot be used together.\n", trace_id,ost->file_index, ost->index);
//        exit_program(1);
        return  -1;
    }

    if (ost->avfilter && (ost->filters || ost->filters_script)) {
        const char *opt = ost->filters ? "-vf/-af/-filter" : "-filter_script";
        av_log(NULL, AV_LOG_ERROR,
               "tid=%s,%s '%s' was specified through the %s option "
               "for output stream %d:%d, which is fed from a complex filtergraph.\n"
               "%s and -filter_complex cannot be used together for the same stream.\n",
               trace_id,ost->filters ? "Filtergraph" : "Filtergraph script",
               ost->filters ? ost->filters : ost->filters_script,
               opt, ost->file_index, ost->index, opt);
//        exit_program(1);
        return  -1;
    }

    avfilter_inout_free(&ofilter->out_tmp);
}


static OutputStream *new_subtitle_stream(OptionsContext *o, AVFormatContext *oc, int source_index,int *has_err)
{
    AVStream *st;
    OutputStream *ost;
    AVCodecContext *subtitle_enc;

    *has_err = 0;
    ost = new_output_stream(o, oc, AVMEDIA_TYPE_SUBTITLE, source_index,has_err);
    if(*has_err < 0){
        return NULL;
    }
    st  = ost->st;
    subtitle_enc = ost->enc_ctx;

    subtitle_enc->codec_type = AVMEDIA_TYPE_SUBTITLE;

    char * trace_id = o->run_context_ref->trace_id;

    MATCH_PER_STREAM_OPT(copy_initial_nonkeyframes, i, ost->copy_initial_nonkeyframes, oc, st,fail);

    if (!ost->stream_copy) {
        char *frame_size = NULL;

        MATCH_PER_STREAM_OPT(frame_sizes, str, frame_size, oc, st,fail);
        if (frame_size && av_parse_video_size(&subtitle_enc->width, &subtitle_enc->height, frame_size) < 0) {
            av_log(NULL, AV_LOG_FATAL, "tid=%s,Invalid frame size: %s.\n", trace_id,frame_size);
//            exit_program(1);
            *has_err = -1;
            return NULL;
        }
    }

    return ost;

    fail:
    *has_err = -1;
    return NULL;
}

static OutputStream *new_data_stream(OptionsContext *o, AVFormatContext *oc, int source_index,int *has_err)
{
    OutputStream *ost;

    *has_err = 0;
    ost = new_output_stream(o, oc, AVMEDIA_TYPE_DATA, source_index,has_err);
    if(*has_err < 0){
        return NULL;
    }
    if (!ost->stream_copy) {
        av_log(NULL, AV_LOG_FATAL, "Data stream encoding not supported yet (only streamcopy)\n");
//        exit_program(1);
        *has_err = -1;
        return NULL;
    }

    return ost;
}

static OutputStream *new_unknown_stream(OptionsContext *o, AVFormatContext *oc, int source_index,int *has_err)
{
    OutputStream *ost;

    ost = new_output_stream(o, oc, AVMEDIA_TYPE_UNKNOWN, source_index,has_err);
    if(*has_err < 0){
        return NULL;
    }
    char * trace_id = o->run_context_ref->trace_id;
    if (!ost->stream_copy) {
        av_log(NULL, AV_LOG_FATAL, "tid=%s,Unknown stream encoding not supported yet (only streamcopy)\n",trace_id);
//        exit_program(1);
        *has_err = -1;
        return NULL;
    }

    return ost;
}

static int copy_chapters(InputFile *ifile, OutputFile *ofile, int copy_metadata)
{
    AVFormatContext *is = ifile->ctx;
    AVFormatContext *os = ofile->ctx;
    AVChapter **tmp;
    int i;

    tmp = av_realloc_f(os->chapters, is->nb_chapters + os->nb_chapters, sizeof(*os->chapters));
    if (!tmp)
        return AVERROR(ENOMEM);
    os->chapters = tmp;

    for (i = 0; i < is->nb_chapters; i++) {
        AVChapter *in_ch = is->chapters[i], *out_ch;
        int64_t start_time = (ofile->start_time == AV_NOPTS_VALUE) ? 0 : ofile->start_time;
        int64_t ts_off   = av_rescale_q(start_time - ifile->ts_offset,
                                        AV_TIME_BASE_Q, in_ch->time_base);
        int64_t rt       = (ofile->recording_time == INT64_MAX) ? INT64_MAX :
                           av_rescale_q(ofile->recording_time, AV_TIME_BASE_Q, in_ch->time_base);


        if (in_ch->end < ts_off)
            continue;
        if (rt != INT64_MAX && in_ch->start > rt + ts_off)
            break;

        out_ch = av_mallocz(sizeof(AVChapter));
        if (!out_ch)
            return AVERROR(ENOMEM);

        out_ch->id        = in_ch->id;
        out_ch->time_base = in_ch->time_base;
        out_ch->start     = FFMAX(0,  in_ch->start - ts_off);
        out_ch->end       = FFMIN(rt, in_ch->end   - ts_off);

        if (copy_metadata)
            av_dict_copy(&out_ch->metadata, in_ch->metadata, 0);

        os->chapters[os->nb_chapters++] = out_ch;
    }
    return 0;
}

static OutputStream *new_attachment_stream(OptionsContext *o, AVFormatContext *oc, int source_index,int *has_err)
{
    OutputStream *ost = new_output_stream(o, oc, AVMEDIA_TYPE_ATTACHMENT, source_index,has_err);
    if(*has_err < 0){
        return NULL;
    }
    ost->stream_copy = 1;
    ost->finished    = 1;
    return ost;
}

static int copy_metadata(char *outspec, char *inspec, AVFormatContext *oc, AVFormatContext *ic, OptionsContext *o)
{
    AVDictionary **meta_in = NULL;
    AVDictionary **meta_out = NULL;
    int i, ret = 0;
    char type_in, type_out;
    const char *istream_spec = NULL, *ostream_spec = NULL;
    int idx_in = 0, idx_out = 0;

    ret = parse_meta_type(inspec,  &type_in,  &idx_in,  &istream_spec);
    if(ret < 0){
        return -1;
    }
    ret=parse_meta_type(outspec, &type_out, &idx_out, &ostream_spec);
    if(ret < 0){
        return -1;
    }
    if (!ic) {
        if (type_out == 'g' || !*outspec)
            o->metadata_global_manual = 1;
        if (type_out == 's' || !*outspec)
            o->metadata_streams_manual = 1;
        if (type_out == 'c' || !*outspec)
            o->metadata_chapters_manual = 1;
        return 0;
    }

    if (type_in == 'g' || type_out == 'g')
        o->metadata_global_manual = 1;
    if (type_in == 's' || type_out == 's')
        o->metadata_streams_manual = 1;
    if (type_in == 'c' || type_out == 'c')
        o->metadata_chapters_manual = 1;

    /* ic is NULL when just disabling automatic mappings */
    if (!ic)
        return 0;

    char * trace_id = o->run_context_ref->trace_id;
#define METADATA_CHECK_INDEX(index, nb_elems, desc)\
    if ((index) < 0 || (index) >= (nb_elems)) {\
        av_log(NULL, AV_LOG_FATAL, "tid=%s,Invalid %s index %d while processing metadata maps.\n",\
                trace_id,(desc), (index));\
        return -1;\
    }

#define SET_DICT(type, meta, context, index)\
        switch (type) {\
        case 'g':\
            meta = &context->metadata;\
            break;\
        case 'c':\
            METADATA_CHECK_INDEX(index, context->nb_chapters, "chapter")\
            meta = &context->chapters[index]->metadata;\
            break;\
        case 'p':\
            METADATA_CHECK_INDEX(index, context->nb_programs, "program")\
            meta = &context->programs[index]->metadata;\
            break;\
        case 's':\
            break; /* handled separately below */ \
        default: return -1;\
        }\

    SET_DICT(type_in, meta_in, ic, idx_in);
    SET_DICT(type_out, meta_out, oc, idx_out);

    /* for input streams choose first matching stream */
    if (type_in == 's') {
        for (i = 0; i < ic->nb_streams; i++) {
            if ((ret = check_stream_specifier(ic, ic->streams[i], istream_spec)) > 0) {
                meta_in = &ic->streams[i]->metadata;
                break;
            } else if (ret < 0) {
//                exit_program(1);
                return -1;
            }
        }
        if (!meta_in) {
            av_log(NULL, AV_LOG_FATAL, "tid=%s,Stream specifier %s does not match  any streams.\n", trace_id,istream_spec);
//            exit_program(1);
            return -1;
        }
    }

    if (type_out == 's') {
        for (i = 0; i < oc->nb_streams; i++) {
            if ((ret = check_stream_specifier(oc, oc->streams[i], ostream_spec)) > 0) {
                meta_out = &oc->streams[i]->metadata;
                av_dict_copy(meta_out, *meta_in, AV_DICT_DONT_OVERWRITE);
            } else if (ret < 0) {
//                exit_program(1);
                return -1;
            }
        }
    } else
        av_dict_copy(meta_out, *meta_in, AV_DICT_DONT_OVERWRITE);

    return 0;
}

static int open_output_file(OptionsContext *o, const char *filename)
{
    AVFormatContext *oc;
    int i, j, err;
    OutputFile *of;
    OutputStream *ost;
    InputStream  *ist;
    AVDictionary *unused_opts = NULL;
    AVDictionaryEntry *e = NULL;
    int format_flags = 0;

    char * trace_id = o->run_context_ref->trace_id;

    if (o->stop_time != INT64_MAX && o->recording_time != INT64_MAX) {
        o->stop_time = INT64_MAX;
        av_log(NULL, AV_LOG_WARNING, "tid=%s,-t and -to cannot be used together; using -t.\n",trace_id);
    }

    if (o->stop_time != INT64_MAX && o->recording_time == INT64_MAX) {
        int64_t start_time = o->start_time == AV_NOPTS_VALUE ? 0 : o->start_time;
        if (o->stop_time <= start_time) {
            av_log(NULL, AV_LOG_ERROR, "tid=%s,-to value smaller than -ss; aborting.\n",trace_id);
//            exit_program(1);
            return -1;
        } else {
            o->recording_time = o->stop_time - start_time;
        }
    }

    int has_err = 0;
    GROW_ARRAY(trace_id,o->run_context_ref->option_output.output_files, o->run_context_ref->option_output.nb_output_files, has_err);
    of = av_mallocz(sizeof(*of));
    if (!of){

//        exit_program(1);
        return  -1;
    }
    o->run_context_ref->option_output.output_files[o->run_context_ref->option_output.nb_output_files - 1] = of;

    of->ost_index      = o->run_context_ref->option_output.nb_output_streams;
    of->recording_time = o->recording_time;
    of->start_time     = o->start_time;
    of->limit_filesize = o->limit_filesize;
    of->shortest       = o->shortest;
    av_dict_copy(&of->opts, o->g->format_opts, 0);

    if (!strcmp(filename, "-")){
        return -1;
    }
//        filename = "pipe:";

    err = avformat_alloc_output_context2(&oc, NULL, o->format, filename);
    if (!oc) {
        print_error(filename, err);
//        exit_program(1);
        return -1;
    }

    of->ctx = oc;
    if (o->recording_time != INT64_MAX)
        oc->duration = o->recording_time;
//    不使用该功能
//    oc->interrupt_callback = int_cb;

    e = av_dict_get(o->g->format_opts, "fflags", NULL, 0);
    if (e) {
        const AVOption *o = av_opt_find(oc, "fflags", NULL, 0, 0);
        av_opt_eval_flags(oc, o, e->value, &format_flags);
    }
    if (o->bitexact) {
        format_flags |= AVFMT_FLAG_BITEXACT;
        oc->flags    |= AVFMT_FLAG_BITEXACT;
    }

    /* create streams for all unlabeled output pads */
    for (i = 0; i < o->run_context_ref->nb_filtergraphs; i++) {
        FilterGraph *fg = o->run_context_ref->filtergraphs[i];
        for (j = 0; j < fg->nb_outputs; j++) {
            OutputFilter *ofilter = fg->outputs[j];

            if (!ofilter->out_tmp || ofilter->out_tmp->name)
                continue;

            switch (ofilter->type) {
                case AVMEDIA_TYPE_VIDEO:    o->video_disable    = 1; break;
                case AVMEDIA_TYPE_AUDIO:    o->audio_disable    = 1; break;
                case AVMEDIA_TYPE_SUBTITLE: o->subtitle_disable = 1; break;
            }
            init_output_filter(ofilter, o, oc);
        }
    }

    if (!o->nb_stream_maps) {
        char *subtitle_codec_name = NULL;
        /* pick the "best" stream of each type */

        /* video: highest resolution */
        if (!o->video_disable && av_guess_codec(oc->oformat, NULL, filename, NULL, AVMEDIA_TYPE_VIDEO) != AV_CODEC_ID_NONE) {
            int best_score = 0, idx = -1;
            int qcr = avformat_query_codec(oc->oformat, oc->oformat->video_codec, 0);
            for (i = 0; i < o->run_context_ref->option_input.nb_input_streams; i++) {
                int score;
                ist = o->run_context_ref->option_input.input_streams[i];
                score = ist->st->codecpar->width * ist->st->codecpar->height
                        + 100000000 * !!(ist->st->event_flags & AVSTREAM_EVENT_FLAG_NEW_PACKETS)
                        + 5000000*!!(ist->st->disposition & AV_DISPOSITION_DEFAULT);
                if (ist->user_set_discard == AVDISCARD_ALL)
                    continue;
                if((qcr!=MKTAG('A', 'P', 'I', 'C')) && (ist->st->disposition & AV_DISPOSITION_ATTACHED_PIC))
                    score = 1;
                if (ist->st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
                    score > best_score) {
                    if((qcr==MKTAG('A', 'P', 'I', 'C')) && !(ist->st->disposition & AV_DISPOSITION_ATTACHED_PIC))
                        continue;
                    best_score = score;
                    idx = i;
                }
            }

            if (idx >= 0){
                int has_err = 0;
                new_video_stream(o, oc, idx,&has_err);
                if(has_err < 0){
                    return  -1;
                }
            }
        }

        /* audio: most channels */
        if (!o->audio_disable && av_guess_codec(oc->oformat, NULL, filename, NULL, AVMEDIA_TYPE_AUDIO) != AV_CODEC_ID_NONE) {
            int best_score = 0, idx = -1;
            for (i = 0; i < o->run_context_ref->option_input.nb_input_streams; i++) {
                int score;
                ist = o->run_context_ref->option_input.input_streams[i];
                score = ist->st->codecpar->channels + 100000000*!!ist->st->codec_info_nb_frames
                        + 5000000*!!(ist->st->disposition & AV_DISPOSITION_DEFAULT);
                if (ist->user_set_discard == AVDISCARD_ALL)
                    continue;
                if (ist->st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
                    score > best_score) {
                    best_score = score;
                    idx = i;
                }
            }
            if (idx >= 0){
                int has_err = 0;
                new_audio_stream(o, oc, idx,&has_err);
                if(has_err < 0){
                    return  -1;
                }
            }

        }

        /* subtitles: pick first */
        MATCH_PER_TYPE_OPT(codec_names, str, subtitle_codec_name, oc, "s");
        if (!o->subtitle_disable && (avcodec_find_encoder(oc->oformat->subtitle_codec) || subtitle_codec_name)) {
            for (i = 0; i < o->run_context_ref->option_input.nb_input_streams; i++)
                if (o->run_context_ref->option_input.input_streams[i]->st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
                    AVCodecDescriptor const *input_descriptor =
                            avcodec_descriptor_get(o->run_context_ref->option_input.input_streams[i]->st->codecpar->codec_id);
                    AVCodecDescriptor const *output_descriptor = NULL;
                    AVCodec const *output_codec =
                            avcodec_find_encoder(oc->oformat->subtitle_codec);
                    int input_props = 0, output_props = 0;
                    if (o->run_context_ref->option_input.input_streams[i]->user_set_discard == AVDISCARD_ALL)
                        continue;
                    if (output_codec)
                        output_descriptor = avcodec_descriptor_get(output_codec->id);
                    if (input_descriptor)
                        input_props = input_descriptor->props & (AV_CODEC_PROP_TEXT_SUB | AV_CODEC_PROP_BITMAP_SUB);
                    if (output_descriptor)
                        output_props = output_descriptor->props & (AV_CODEC_PROP_TEXT_SUB | AV_CODEC_PROP_BITMAP_SUB);
                    if (subtitle_codec_name ||
                        input_props & output_props ||
                        // Map dvb teletext which has neither property to any output subtitle encoder
                        input_descriptor && output_descriptor &&
                        (!input_descriptor->props ||
                         !output_descriptor->props)) {
                        int has_err = 0;
                        new_subtitle_stream(o, oc, i,&has_err);
                        if(has_err < 0){
                            return -1;
                        }
                        break;
                    }
                }
        }
        /* Data only if codec id match */
        if (!o->data_disable ) {
            enum AVCodecID codec_id = av_guess_codec(oc->oformat, NULL, filename, NULL, AVMEDIA_TYPE_DATA);
            for (i = 0; codec_id != AV_CODEC_ID_NONE && i < o->run_context_ref->option_input.nb_input_streams; i++) {
                if (o->run_context_ref->option_input.input_streams[i]->user_set_discard == AVDISCARD_ALL)
                    continue;
                if (o->run_context_ref->option_input.input_streams[i]->st->codecpar->codec_type == AVMEDIA_TYPE_DATA
                    && o->run_context_ref->option_input.input_streams[i]->st->codecpar->codec_id == codec_id ){
                    int has_err = 0;
                    new_data_stream(o, oc, i,&has_err);
                    if(has_err < 0){
                        return  -1;
                    }
                }
            }
        }
    } else {
        for (i = 0; i < o->nb_stream_maps; i++) {
            StreamMap *map = &o->stream_maps[i];

            if (map->disabled)
                continue;

            if (map->linklabel) {
                FilterGraph *fg;
                OutputFilter *ofilter = NULL;
                int j, k;

                for (j = 0; j < o->run_context_ref->nb_filtergraphs; j++) {
                    fg = o->run_context_ref->filtergraphs[j];
                    for (k = 0; k < fg->nb_outputs; k++) {
                        AVFilterInOut *out = fg->outputs[k]->out_tmp;
                        if (out && !strcmp(out->name, map->linklabel)) {
                            ofilter = fg->outputs[k];
                            goto loop_end;
                        }
                    }
                }
                loop_end:
                if (!ofilter) {
                    av_log(NULL, AV_LOG_FATAL, "tid=%s,Output with label '%s' does not exist "
                                               "in any defined filter graph, or was already used elsewhere.\n",trace_id, map->linklabel);
//                    exit_program(1);
                    return -1;
                }
                if(0>init_output_filter(ofilter, o, oc)){
                    return -1;
                }
            } else {
                int src_idx = o->run_context_ref->option_input.input_files[map->file_index]->ist_index + map->stream_index;

                ist = o->run_context_ref->option_input.input_streams[o->run_context_ref->option_input.input_files[map->file_index]->ist_index + map->stream_index];
                if (ist->user_set_discard == AVDISCARD_ALL) {
                    av_log(NULL, AV_LOG_FATAL, "tid=%s,Stream #%d:%d is disabled and cannot be mapped.\n",
                           trace_id,  map->file_index, map->stream_index);
//                    exit_program(1);
                    return -1;
                }
                if(o->subtitle_disable && ist->st->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE)
                    continue;
                if(o->   audio_disable && ist->st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
                    continue;
                if(o->   video_disable && ist->st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
                    continue;
                if(o->    data_disable && ist->st->codecpar->codec_type == AVMEDIA_TYPE_DATA)
                    continue;

                ost = NULL;
                int has_err = 0;
                switch (ist->st->codecpar->codec_type) {
                    case AVMEDIA_TYPE_VIDEO:      ost = new_video_stream     (o, oc, src_idx,&has_err); break;
                    case AVMEDIA_TYPE_AUDIO:      ost = new_audio_stream     (o, oc, src_idx,&has_err); break;
                    case AVMEDIA_TYPE_SUBTITLE:   ost = new_subtitle_stream  (o, oc, src_idx,&has_err); break;
                    case AVMEDIA_TYPE_DATA:       ost = new_data_stream      (o, oc, src_idx,&has_err); break;
                    case AVMEDIA_TYPE_ATTACHMENT: ost = new_attachment_stream(o, oc, src_idx,&has_err); break;
                    case AVMEDIA_TYPE_UNKNOWN:
                        if (o->run_context_ref->copy_unknown_streams) {
                            ost = new_unknown_stream   (o, oc, src_idx,&has_err);
                            break;
                        }
                    default:
                        av_log(NULL, o->run_context_ref->ignore_unknown_streams ? AV_LOG_WARNING : AV_LOG_FATAL,
                               "tid=%s,Cannot map stream #%d:%d - unsupported type.\n",
                               trace_id,map->file_index, map->stream_index);
                        if (!o->run_context_ref->ignore_unknown_streams) {
                            av_log(NULL, AV_LOG_FATAL,
                                   "tid=%s,If you want unsupported types ignored instead "
                                   "of failing, please use the -ignore_unknown option\n"
                                   "If you want them copied, please use -copy_unknown\n",trace_id);
//                            exit_program(1);
                            return -1;
                        }
                }
                if(has_err < 0){
                    return -1;
                }
                if (ost)
                    ost->sync_ist = o->run_context_ref->option_input.input_streams[o->run_context_ref->option_input.input_files[map->sync_file_index]->ist_index
                                                                                   + map->sync_stream_index];
            }
        }
    }

    /* handle attached files */
    for (i = 0; i < o->nb_attachments; i++) {
        AVIOContext *pb;
        uint8_t *attachment;
        const char *p;
        int64_t len;

        // &int_cb , 不使用
        if ((err = avio_open2(&pb, o->attachments[i], AVIO_FLAG_READ, NULL, NULL)) < 0) {
            av_log(NULL, AV_LOG_FATAL, "tid=%s,Could not open attachment file %s.\n",
                   trace_id,   o->attachments[i]);
//            exit_program(1);
            return -1;
        }
        if ((len = avio_size(pb)) <= 0) {
            av_log(NULL, AV_LOG_FATAL, "tid=%s,Could not get size of the attachment %s.\n",
                   trace_id, o->attachments[i]);
//            exit_program(1);
            return -1;
        }
        if (len > INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE ||
            !(attachment = av_malloc(len + AV_INPUT_BUFFER_PADDING_SIZE))) {
            av_log(NULL, AV_LOG_FATAL, "tid=%s,Attachment %s too large.\n",
                   trace_id, o->attachments[i]);
//            exit_program(1);
            return -1;
        }
        avio_read(pb, attachment, len);
        memset(attachment + len, 0, AV_INPUT_BUFFER_PADDING_SIZE);

        int has_err = 0;
        ost = new_attachment_stream(o, oc, -1,&has_err);
        if(has_err < 0){
            return -1;
        }
        ost->stream_copy               = 0;
        ost->attachment_filename       = o->attachments[i];
        ost->st->codecpar->extradata      = attachment;
        ost->st->codecpar->extradata_size = len;

        p = strrchr(o->attachments[i], '/');
        av_dict_set(&ost->st->metadata, "filename", (p && *p) ? p + 1 : o->attachments[i], AV_DICT_DONT_OVERWRITE);
        avio_closep(&pb);
    }

#if FF_API_LAVF_AVCTX
    for (i = o->run_context_ref->option_output.nb_output_streams - oc->nb_streams; i < o->run_context_ref->option_output.nb_output_streams; i++) { //for all streams of this output file
        AVDictionaryEntry *e;
        ost = o->run_context_ref->option_output.output_streams[i];

        if ((ost->stream_copy || ost->attachment_filename)
            && (e = av_dict_get(o->g->codec_opts, "flags", NULL, AV_DICT_IGNORE_SUFFIX))
            && (!e->key[5] || check_stream_specifier(oc, ost->st, e->key+6)))
            if (av_opt_set(ost->st->codec, "flags", e->value, 0) < 0){

//                exit_program(1);
                return -1;
            }
    }
#endif

    if (!oc->nb_streams && !(oc->oformat->flags & AVFMT_NOSTREAMS)) {
        av_dump_format(oc, o->run_context_ref->option_output.nb_output_files - 1, oc->url, 1);
        av_log(NULL, AV_LOG_ERROR, "tid=%s,Output file #%d does not contain any stream\n", trace_id,o->run_context_ref->option_output.nb_output_files - 1);
//        exit_program(1);
        return -1;
    }

    /* check if all codec options have been used */
    unused_opts = strip_specifiers(o->g->codec_opts);
    for (i = of->ost_index; i < o->run_context_ref->option_output.nb_output_streams; i++) {
        e = NULL;
        while ((e = av_dict_get(o->run_context_ref->option_output.output_streams[i]->encoder_opts, "", e,
                                AV_DICT_IGNORE_SUFFIX)))
            av_dict_set(&unused_opts, e->key, NULL, 0);
    }

    e = NULL;
    while ((e = av_dict_get(unused_opts, "", e, AV_DICT_IGNORE_SUFFIX))) {
        const AVClass *class = avcodec_get_class();
        const AVOption *option = av_opt_find(&class, e->key, NULL, 0,
                                             AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ);
        const AVClass *fclass = avformat_get_class();
        const AVOption *foption = av_opt_find(&fclass, e->key, NULL, 0,
                                              AV_OPT_SEARCH_CHILDREN | AV_OPT_SEARCH_FAKE_OBJ);
        if (!option || foption)
            continue;


        if (!(option->flags & AV_OPT_FLAG_ENCODING_PARAM)) {
            av_log(NULL, AV_LOG_ERROR, "tid=%s,Codec AVOption %s (%s) specified for "
                                       "output file #%d (%s) is not an encoding option.\n", trace_id,e->key,
                   option->help ? option->help : "", o->run_context_ref->option_output.nb_output_files - 1,
                   filename);
//            exit_program(1);
            return -1;
        }

        // gop_timecode is injected by generic code but not always used
        if (!strcmp(e->key, "gop_timecode"))
            continue;

        av_log(NULL, AV_LOG_WARNING, "tid=%s,Codec AVOption %s (%s) specified for "
                                     "output file #%d (%s) has not been used for any stream. The most "
                                     "likely reason is either wrong type (e.g. a video option with "
                                     "no video streams) or that it is a private option of some encoder "
                                     "which was not actually used for any stream.\n", trace_id,e->key,
               option->help ? option->help : "", o->run_context_ref->option_output.nb_output_files - 1, filename);
    }
    av_dict_free(&unused_opts);

    /* set the decoding_needed flags and create simple filtergraphs */
    for (i = of->ost_index; i < o->run_context_ref->option_output.nb_output_streams; i++) {
        OutputStream *ost = o->run_context_ref->option_output.output_streams[i];

        if (ost->encoding_needed && ost->source_index >= 0) {
            InputStream *ist = o->run_context_ref->option_input.input_streams[ost->source_index];
            ist->decoding_needed |= DECODING_FOR_OST;

            if (ost->st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO ||
                ost->st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                err = init_simple_filtergraph(o->run_context_ref,ist, ost);
                if (err < 0) {
                    av_log(NULL, AV_LOG_ERROR,
                           "tid=%s,Error initializing a simple filtergraph between streams "
                           "%d:%d->%d:%d\n",trace_id, ist->file_index, ost->source_index,
                           o->run_context_ref->option_output.nb_output_files - 1, ost->st->index);
//                    exit_program(1);
                    return -1;
                }
            }
        }

        /* set the filter output constraints */
        if (ost->filter) {
            OutputFilter *f = ost->filter;
            int count;
            switch (ost->enc_ctx->codec_type) {
                case AVMEDIA_TYPE_VIDEO:
                    f->frame_rate = ost->frame_rate;
                    f->width      = ost->enc_ctx->width;
                    f->height     = ost->enc_ctx->height;
                    if (ost->enc_ctx->pix_fmt != AV_PIX_FMT_NONE) {
                        f->format = ost->enc_ctx->pix_fmt;
                    } else if (ost->enc->pix_fmts) {
                        count = 0;
                        while (ost->enc->pix_fmts[count] != AV_PIX_FMT_NONE)
                            count++;
                        f->formats = av_mallocz_array(count + 1, sizeof(*f->formats));
                        if (!f->formats){
//                            exit_program(1);
                            return -1;
                        }
                        memcpy(f->formats, ost->enc->pix_fmts, (count + 1) * sizeof(*f->formats));
                    }
                    break;
                case AVMEDIA_TYPE_AUDIO:
                    if (ost->enc_ctx->sample_fmt != AV_SAMPLE_FMT_NONE) {
                        f->format = ost->enc_ctx->sample_fmt;
                    } else if (ost->enc->sample_fmts) {
                        count = 0;
                        while (ost->enc->sample_fmts[count] != AV_SAMPLE_FMT_NONE)
                            count++;
                        f->formats = av_mallocz_array(count + 1, sizeof(*f->formats));
                        if (!f->formats){
//                            exit_program(1);
                            return -1;
                        }
                        memcpy(f->formats, ost->enc->sample_fmts, (count + 1) * sizeof(*f->formats));
                    }
                    if (ost->enc_ctx->sample_rate) {
                        f->sample_rate = ost->enc_ctx->sample_rate;
                    } else if (ost->enc->supported_samplerates) {
                        count = 0;
                        while (ost->enc->supported_samplerates[count])
                            count++;
                        f->sample_rates = av_mallocz_array(count + 1, sizeof(*f->sample_rates));
                        if (!f->sample_rates){
//                            exit_program(1);
                            return -1;
                        }
                        memcpy(f->sample_rates, ost->enc->supported_samplerates,
                               (count + 1) * sizeof(*f->sample_rates));
                    }
                    if (ost->enc_ctx->channels) {
                        f->channel_layout = av_get_default_channel_layout(ost->enc_ctx->channels);
                    } else if (ost->enc->channel_layouts) {
                        count = 0;
                        while (ost->enc->channel_layouts[count])
                            count++;
                        f->channel_layouts = av_mallocz_array(count + 1, sizeof(*f->channel_layouts));
                        if (!f->channel_layouts){
//                            exit_program(1);
                            return -1;
                        }
                        memcpy(f->channel_layouts, ost->enc->channel_layouts,
                               (count + 1) * sizeof(*f->channel_layouts));
                    }
                    break;
            }
        }
    }

    /* check filename in case of an image number is expected */
    if (oc->oformat->flags & AVFMT_NEEDNUMBER) {
        if (!av_filename_number_test(oc->url)) {
            print_error(oc->url, AVERROR(EINVAL));
//            exit_program(1);
            return -1;
        }
    }

    if (!(oc->oformat->flags & AVFMT_NOSTREAMS) && !o->run_context_ref->input_stream_potentially_available) {
        av_log(NULL, AV_LOG_ERROR,
               "tid=%s,No input streams but output needs an input stream\n",trace_id);
//        exit_program(1);
        return -1;
    }

    if (!(oc->oformat->flags & AVFMT_NOFILE)) {
        /* test if it already exists to avoid losing precious files */
        // 直接覆盖，不用断言
//        assert_file_overwrite(filename);

        /* open the file */
        if ((err = avio_open2(&oc->pb, filename, AVIO_FLAG_WRITE,
                              &oc->interrupt_callback,
                              &of->opts)) < 0) {
            print_error(filename, err);
//            exit_program(1);
            return -1;
        }
    } else if (strcmp(oc->oformat->name, "image2")==0 && !av_filename_number_test(filename)){
        // 直接覆盖，不用断言
//        assert_file_overwrite(filename);
    }

    if (o->mux_preload) {
        av_dict_set_int(&of->opts, "preload", o->mux_preload*AV_TIME_BASE, 0);
    }
    oc->max_delay = (int)(o->mux_max_delay * AV_TIME_BASE);

    /* copy metadata */
    for (i = 0; i < o->nb_metadata_map; i++) {
        char *p;
        int in_file_index = strtol(o->metadata_map[i].u.str, &p, 0);

        if (in_file_index >= o->run_context_ref->option_input.nb_input_files) {
            av_log(NULL, AV_LOG_FATAL, "tid=%s,Invalid input file index %d while processing metadata maps\n", trace_id,in_file_index);
//            exit_program(1);
            return -1;
        }
        int ret = copy_metadata(o->metadata_map[i].specifier, *p ? p + 1 : p, oc,
                      in_file_index >= 0 ?
                      o->run_context_ref->option_input.input_files[in_file_index]->ctx : NULL, o);
        if(ret < 0) {
            return  -1;
        }
    }

    /* copy chapters */
    if (o->chapters_input_file >= o->run_context_ref->option_input.nb_input_files) {
        if (o->chapters_input_file == INT_MAX) {
            /* copy chapters from the first input file that has them*/
            o->chapters_input_file = -1;
            for (i = 0; i < o->run_context_ref->option_input.nb_input_files; i++)
                if (o->run_context_ref->option_input.input_files[i]->ctx->nb_chapters) {
                    o->chapters_input_file = i;
                    break;
                }
        } else {
            av_log(NULL, AV_LOG_FATAL, "tid=%s,Invalid input file index %d in chapter mapping.\n",
                   trace_id, o->chapters_input_file);
//            exit_program(1);
            return -1;
        }
    }
    if (o->chapters_input_file >= 0){
        int ret = copy_chapters(o->run_context_ref->option_input.input_files[o->chapters_input_file], of,
                                !o->metadata_chapters_manual);
        if(ret < 0){
            return -1;
        }
    }

    /* copy global metadata by default */
    if (!o->metadata_global_manual && o->run_context_ref->option_input.nb_input_files){
        av_dict_copy(&oc->metadata, o->run_context_ref->option_input.input_files[0]->ctx->metadata,
                     AV_DICT_DONT_OVERWRITE);
        if(o->recording_time != INT64_MAX)
            av_dict_set(&oc->metadata, "duration", NULL, 0);
        av_dict_set(&oc->metadata, "creation_time", NULL, 0);
        av_dict_set(&oc->metadata, "company_name", NULL, 0);
        av_dict_set(&oc->metadata, "product_name", NULL, 0);
        av_dict_set(&oc->metadata, "product_version", NULL, 0);
    }
    if (!o->metadata_streams_manual)
        for (i = of->ost_index; i < o->run_context_ref->option_output.nb_output_streams; i++) {
            InputStream *ist;
            if (o->run_context_ref->option_output.output_streams[i]->source_index < 0)         /* this is true e.g. for attached files */
                continue;
            ist = o->run_context_ref->option_input.input_streams[o->run_context_ref->option_output.output_streams[i]->source_index];
            av_dict_copy(&o->run_context_ref->option_output.output_streams[i]->st->metadata, ist->st->metadata, AV_DICT_DONT_OVERWRITE);
            if (!o->run_context_ref->option_output.output_streams[i]->stream_copy) {
                av_dict_set(&o->run_context_ref->option_output.output_streams[i]->st->metadata, "encoder", NULL, 0);
            }
        }

    /* process manually set programs */
    for (i = 0; i < o->nb_program; i++) {
        const char *p = o->program[i].u.str;
        int progid = i+1;
        AVProgram *program;

        while(*p) {
            const char *p2 = av_get_token(&p, ":");
            const char *to_dealloc = p2;
            char *key;
            if (!p2)
                break;

            if(*p) p++;

            key = av_get_token(&p2, "=");
            if (!key || !*p2) {
                av_freep(&to_dealloc);
                av_freep(&key);
                break;
            }
            p2++;

            if (!strcmp(key, "program_num"))
                progid = strtol(p2, NULL, 0);
            av_freep(&to_dealloc);
            av_freep(&key);
        }

        program = av_new_program(oc, progid);

        p = o->program[i].u.str;
        while(*p) {
            const char *p2 = av_get_token(&p, ":");
            const char *to_dealloc = p2;
            char *key;
            if (!p2)
                break;
            if(*p) p++;

            key = av_get_token(&p2, "=");
            if (!key) {
                av_log(NULL, AV_LOG_FATAL,
                       "tid=%s,No '=' character in program string %s.\n",trace_id,
                       p2);
//                exit_program(1);
                return -1;
            }
            if (!*p2){
//                exit_program(1);
                return -1;
            }
            p2++;

            if (!strcmp(key, "title")) {
                av_dict_set(&program->metadata, "title", p2, 0);
            } else if (!strcmp(key, "program_num")) {
            } else if (!strcmp(key, "st")) {
                int st_num = strtol(p2, NULL, 0);
                av_program_add_stream_index(oc, progid, st_num);
            } else {
                av_log(NULL, AV_LOG_FATAL, "tid=%s,Unknown program key %s.\n", trace_id,key);
//                exit_program(1);
                return -1;
            }
            av_freep(&to_dealloc);
            av_freep(&key);
        }
    }

    /* process manually set metadata */
    for (i = 0; i < o->nb_metadata; i++) {
        AVDictionary **m;
        char type, *val;
        const char *stream_spec;
        int index = 0, j, ret = 0;

        val = strchr(o->metadata[i].u.str, '=');
        if (!val) {
            av_log(NULL, AV_LOG_FATAL, "tid=%s,No '=' character in metadata string %s.\n",
                   trace_id,   o->metadata[i].u.str);
//            exit_program(1);
            return -1;
        }
        *val++ = 0;

        parse_meta_type(o->metadata[i].specifier, &type, &index, &stream_spec);
        if (type == 's') {
            for (j = 0; j < oc->nb_streams; j++) {
                ost = o->run_context_ref->option_output.output_streams[o->run_context_ref->option_output.nb_output_streams - oc->nb_streams + j];
                if ((ret = check_stream_specifier(oc, oc->streams[j], stream_spec)) > 0) {
                    if (!strcmp(o->metadata[i].u.str, "rotate")) {
                        char *tail;
                        double theta = av_strtod(val, &tail);
                        if (!*tail) {
                            ost->rotate_overridden = 1;
                            ost->rotate_override_value = theta;
                        }
                    } else {
                        av_dict_set(&oc->streams[j]->metadata, o->metadata[i].u.str, *val ? val : NULL, 0);
                    }
                } else if (ret < 0){
//                    exit_program(1);
                    return -1;
                }
            }
        }
        else {
            switch (type) {
                case 'g':
                    m = &oc->metadata;
                    break;
                case 'c':
                    if (index < 0 || index >= oc->nb_chapters) {
                        av_log(NULL, AV_LOG_FATAL, "tid=%s,Invalid chapter index %d in metadata specifier.\n", trace_id,index);
//                        exit_program(1);
                        return -1;
                    }
                    m = &oc->chapters[index]->metadata;
                    break;
                case 'p':
                    if (index < 0 || index >= oc->nb_programs) {
                        av_log(NULL, AV_LOG_FATAL, "tid=%s,Invalid program index %d in metadata specifier.\n", trace_id,index);
//                        exit_program(1);
                        return -1;
                    }
                    m = &oc->programs[index]->metadata;
                    break;
                default:
                    av_log(NULL, AV_LOG_FATAL, "tid=%s,Invalid metadata specifier %s.\n", trace_id,o->metadata[i].specifier);
//                    exit_program(1);
                    return -1;
            }
            av_dict_set(m, o->metadata[i].u.str, *val ? val : NULL, 0);
        }
    }

    return 0;
}

int open_stream(ParsedOptionsContext *p_ctx) {
    int ret;
    char *  trace_id = p_ctx->raw_context.trace_id;

    ret = open_files(p_ctx,&p_ctx->parse_context->groups[GROUP_INFILE], "input", open_input_file);
    if (ret < 0) {
        av_log(NULL, AV_LOG_FATAL, "tid=%s,Error opening input files: ",trace_id);
        goto fail;
    }
    ret = init_complex_filters(p_ctx->options_context.run_context_ref);
    if (ret < 0) {
        av_log(NULL, AV_LOG_FATAL, "tid=%s,Error initializing complex filters.\n",trace_id);
        goto fail;
    }

    ret = open_files(p_ctx,&p_ctx->parse_context->groups[GROUP_OUTFILE], "output", open_output_file);
    if (ret < 0) {
        av_log(NULL, AV_LOG_FATAL, "tid=%s,Error opening output files: ",trace_id);
        goto fail;
    }
    return ret;
    fail:

    return -1;
}