//
// Created by xiufeng on 23-11-7.
//
#include <libavfilter/buffersink.h>
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext_qsv.h>
#include "hw.h"

static HWDevice *hw_device_get_by_type(RunContext *run_context,enum AVHWDeviceType type)
{
    HWDevice *found = NULL;
    int i;
    for (i = 0; i < run_context->nb_hw_devices; i++) {
        if (run_context->hw_devices[i]->type == type) {
            if (found)
                return NULL;
            found = run_context->hw_devices[i];
        }
    }
    return found;
}

HWDevice *hw_device_get_by_name(RunContext *run_context,const char *name)
{
    int i;
    for (i = 0; i < run_context->nb_hw_devices; i++) {
        if (!strcmp(run_context->hw_devices[i]->name, name))
            return run_context->hw_devices[i];
    }
    return NULL;
}

static HWDevice *hw_device_add(RunContext* run_context)
{
    int err;
    err = av_reallocp_array(&run_context->hw_devices, run_context->nb_hw_devices + 1,
                            sizeof(*run_context->hw_devices));
    if (err) {
        run_context->nb_hw_devices = 0;
        return NULL;
    }
    run_context->hw_devices[run_context->nb_hw_devices] = av_mallocz(sizeof(HWDevice));
    if (!run_context->hw_devices[run_context->nb_hw_devices])
        return NULL;
    return run_context->hw_devices[run_context->nb_hw_devices++];
}


static char *hw_device_default_name(RunContext *run_context,enum AVHWDeviceType type)
{
    // Make an automatic name of the form "type%d".  We arbitrarily
    // limit at 1000 anonymous devices of the same type - there is
    // probably something else very wrong if you get to this limit.
    const char *type_name = av_hwdevice_get_type_name(type);
    char *name;
    size_t index_pos;
    int index, index_limit = 1000;
    index_pos = strlen(type_name);
    name = av_malloc(index_pos + 4);
    if (!name)
        return NULL;
    for (index = 0; index < index_limit; index++) {
        snprintf(name, index_pos + 4, "%s%d", type_name, index);
        if (!hw_device_get_by_name(run_context,name))
            break;
    }
    if (index >= index_limit) {
        av_freep(&name);
        return NULL;
    }
    return name;
}

int hw_device_init_from_string(RunContext *run_context,const char *arg, HWDevice **dev_out)
{
    // "type=name:device,key=value,key2=value2"
    // "type:device,key=value,key2=value2"
    // -> av_hwdevice_ctx_create()
    // "type=name@name"
    // "type@name"
    // -> av_hwdevice_ctx_create_derived()

    AVDictionary *options = NULL;
    const char *type_name = NULL, *name = NULL, *device = NULL;
    enum AVHWDeviceType type;
    HWDevice *dev, *src;
    AVBufferRef *device_ref = NULL;
    int err;
    const char *errmsg, *p, *q;
    size_t k;

    k = strcspn(arg, ":=@");
    p = arg + k;

    type_name = av_strndup(arg, k);
    if (!type_name) {
        err = AVERROR(ENOMEM);
        goto fail;
    }
    type = av_hwdevice_find_type_by_name(type_name);
    if (type == AV_HWDEVICE_TYPE_NONE) {
        errmsg = "unknown device type";
        goto invalid;
    }

    if (*p == '=') {
        k = strcspn(p + 1, ":@");

        name = av_strndup(p + 1, k);
        if (!name) {
            err = AVERROR(ENOMEM);
            goto fail;
        }
        if (hw_device_get_by_name(run_context,name)) {
            errmsg = "named device already exists";
            goto invalid;
        }

        p += 1 + k;
    } else {
        name = hw_device_default_name(run_context,type);
        if (!name) {
            err = AVERROR(ENOMEM);
            goto fail;
        }
    }

    if (!*p) {
        // New device with no parameters.
        err = av_hwdevice_ctx_create(&device_ref, type,
                                     NULL, NULL, 0);
        if (err < 0)
            goto fail;

    } else if (*p == ':') {
        // New device with some parameters.
        ++p;
        q = strchr(p, ',');
        if (q) {
            if (q - p > 0) {
                device = av_strndup(p, q - p);
                if (!device) {
                    err = AVERROR(ENOMEM);
                    goto fail;
                }
            }
            err = av_dict_parse_string(&options, q + 1, "=", ",", 0);
            if (err < 0) {
                errmsg = "failed to parse options";
                goto invalid;
            }
        }

        err = av_hwdevice_ctx_create(&device_ref, type,
                                     q ? device : p[0] ? p : NULL,
                                     options, 0);
        if (err < 0)
            goto fail;

    } else if (*p == '@') {
        // Derive from existing device.

        src = hw_device_get_by_name(run_context,p + 1);
        if (!src) {
            errmsg = "invalid source device name";
            goto invalid;
        }

        err = av_hwdevice_ctx_create_derived(&device_ref, type,
                                             src->device_ref, 0);
        if (err < 0)
            goto fail;
    } else {
        errmsg = "parse error";
        goto invalid;
    }

    dev = hw_device_add(run_context);
    if (!dev) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    dev->name = name;
    dev->type = type;
    dev->device_ref = device_ref;

    if (dev_out)
        *dev_out = dev;

    name = NULL;
    err = 0;
    done:
    av_freep(&type_name);
    av_freep(&name);
    av_freep(&device);
    av_dict_free(&options);
    return err;
    invalid:
    av_log(NULL, AV_LOG_ERROR,
           "tid=%s,Invalid device specification \"%s\": %s\n",run_context->trace_id, arg, errmsg);
    err = AVERROR(EINVAL);
    goto done;
    fail:
    av_log(NULL, AV_LOG_ERROR,
           "tid=%s,Device creation failed: %d.\n", run_context->trace_id,err);
    av_buffer_unref(&device_ref);
    goto done;
}

static int hw_device_init_from_type(RunContext *run_context,enum AVHWDeviceType type,
                                    const char *device,
                                    HWDevice **dev_out)
{
    AVBufferRef *device_ref = NULL;
    HWDevice *dev;
    char *name;
    int err;

    name = hw_device_default_name(run_context,type);
    if (!name) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    err = av_hwdevice_ctx_create(&device_ref, type, device, NULL, 0);
    if (err < 0) {
        av_log(NULL, AV_LOG_ERROR,
               "tid=%s,Device creation failed: %d.\n", run_context->trace_id,err);
        goto fail;
    }

    dev = hw_device_add(run_context);
    if (!dev) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    dev->name = name;
    dev->type = type;
    dev->device_ref = device_ref;

    if (dev_out)
        *dev_out = dev;

    return 0;

    fail:
    av_freep(&name);
    av_buffer_unref(&device_ref);
    return err;
}

static HWDevice *hw_device_match_by_codec(RunContext *run_context,const AVCodec *codec)
{
    const AVCodecHWConfig *config;
    HWDevice *dev;
    int i;
    for (i = 0;; i++) {
        config = avcodec_get_hw_config(codec, i);
        if (!config)
            return NULL;
        if (!(config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX))
            continue;
        dev = hw_device_get_by_type(run_context,config->device_type);
        if (dev)
            return dev;
    }
}

int hw_device_setup_for_decode(RunContext *run_context,InputStream *ist)
{
    const AVCodecHWConfig *config;
    enum AVHWDeviceType type;
    HWDevice *dev = NULL;
    int err, auto_device = 0;

    if (ist->hwaccel_device) {
        dev = hw_device_get_by_name(run_context,ist->hwaccel_device);
        if (!dev) {
            if (ist->hwaccel_id == HWACCEL_AUTO) {
                auto_device = 1;
            } else if (ist->hwaccel_id == HWACCEL_GENERIC) {
                type = ist->hwaccel_device_type;
                err = hw_device_init_from_type(run_context,type, ist->hwaccel_device,
                                               &dev);
            } else {
                // This will be dealt with by API-specific initialisation
                // (using hwaccel_device), so nothing further needed here.
                return 0;
            }
        } else {
            if (ist->hwaccel_id == HWACCEL_AUTO) {
                ist->hwaccel_device_type = dev->type;
            } else if (ist->hwaccel_device_type != dev->type) {
                av_log(ist->dec_ctx, AV_LOG_ERROR, "tid=%s,Invalid hwaccel device "
                                                   "specified for decoder: device %s of type %s is not "
                                                   "usable with hwaccel %s.\n", run_context->trace_id,dev->name,
                       av_hwdevice_get_type_name(dev->type),
                       av_hwdevice_get_type_name(ist->hwaccel_device_type));
                return AVERROR(EINVAL);
            }
        }
    } else {
        if (ist->hwaccel_id == HWACCEL_AUTO) {
            auto_device = 1;
        } else if (ist->hwaccel_id == HWACCEL_GENERIC) {
            type = ist->hwaccel_device_type;
            dev = hw_device_get_by_type(run_context,type);
            if (!dev)
                err = hw_device_init_from_type(run_context,type, NULL, &dev);
        } else {
            dev = hw_device_match_by_codec(run_context,ist->dec);
            if (!dev) {
                // No device for this codec, but not using generic hwaccel
                // and therefore may well not need one - ignore.
                return 0;
            }
        }
    }

    if (auto_device) {
        int i;
        if (!avcodec_get_hw_config(ist->dec, 0)) {
            // Decoder does not support any hardware devices.
            return 0;
        }
        for (i = 0; !dev; i++) {
            config = avcodec_get_hw_config(ist->dec, i);
            if (!config)
                break;
            type = config->device_type;
            dev = hw_device_get_by_type(run_context,type);
            if (dev) {
                av_log(ist->dec_ctx, AV_LOG_INFO, "tid=%s,Using auto "
                                                  "hwaccel type %s with existing device %s.\n",
                       run_context->trace_id,
                       av_hwdevice_get_type_name(type), dev->name);
            }
        }
        for (i = 0; !dev; i++) {
            config = avcodec_get_hw_config(ist->dec, i);
            if (!config)
                break;
            type = config->device_type;
            // Try to make a new device of this type.
            err = hw_device_init_from_type(run_context,type, ist->hwaccel_device,
                                           &dev);
            if (err < 0) {
                // Can't make a device of this type.
                continue;
            }
            if (ist->hwaccel_device) {
                av_log(ist->dec_ctx, AV_LOG_INFO, "tid=%s,Using auto "
                                                  "hwaccel type %s with new device created "
                                                  "from %s.\n",run_context->trace_id, av_hwdevice_get_type_name(type),
                       ist->hwaccel_device);
            } else {
                av_log(ist->dec_ctx, AV_LOG_INFO, "tid=%s,Using auto "
                                                  "hwaccel type %s with new default device.\n",
                       run_context->trace_id,
                       av_hwdevice_get_type_name(type));
            }
        }
        if (dev) {
            ist->hwaccel_device_type = type;
        } else {
            av_log(ist->dec_ctx, AV_LOG_INFO, "tid=%s,Auto hwaccel "
                                              "disabled: no device found.\n",run_context->trace_id);
            ist->hwaccel_id = HWACCEL_NONE;
            return 0;
        }
    }

    if (!dev) {
        av_log(ist->dec_ctx, AV_LOG_ERROR, "tid=%s,No device available "
                                           "for decoder: device type %s needed for codec %s.\n",
               run_context->trace_id,
               av_hwdevice_get_type_name(type), ist->dec->name);
        return err;
    }

    ist->dec_ctx->hw_device_ctx = av_buffer_ref(dev->device_ref);
    if (!ist->dec_ctx->hw_device_ctx)
        return AVERROR(ENOMEM);

    return 0;
}

int hw_device_setup_for_encode(RunContext *run_context,OutputStream *ost)
{
    const AVCodecHWConfig *config;
    HWDevice *dev = NULL;
    AVBufferRef *frames_ref = NULL;
    int i;

    if (ost->filter) {
        frames_ref = av_buffersink_get_hw_frames_ctx(ost->filter->filter);
        if (frames_ref &&
            ((AVHWFramesContext*)frames_ref->data)->format ==
            ost->enc_ctx->pix_fmt) {
            // Matching format, will try to use hw_frames_ctx.
        } else {
            frames_ref = NULL;
        }
    }

    for (i = 0;; i++) {
        config = avcodec_get_hw_config(ost->enc, i);
        if (!config)
            break;

        if (frames_ref &&
            config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_FRAMES_CTX &&
            (config->pix_fmt == AV_PIX_FMT_NONE ||
             config->pix_fmt == ost->enc_ctx->pix_fmt)) {
            av_log(ost->enc_ctx, AV_LOG_VERBOSE, "tid=%s,Using input "
                                                 "frames context (format %s) with %s encoder.\n",
                   run_context->trace_id,
                   av_get_pix_fmt_name(ost->enc_ctx->pix_fmt),
                   ost->enc->name);
            ost->enc_ctx->hw_frames_ctx = av_buffer_ref(frames_ref);
            if (!ost->enc_ctx->hw_frames_ctx)
                return AVERROR(ENOMEM);
            return 0;
        }

        if (!dev &&
            config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)
            dev = hw_device_get_by_type(run_context,config->device_type);
    }

    if (dev) {
        av_log(ost->enc_ctx, AV_LOG_VERBOSE, "tid=%s,Using device %s "
                                             "(type %s) with %s encoder.\n",
               run_context->trace_id,
               dev->name,
               av_hwdevice_get_type_name(dev->type), ost->enc->name);
        ost->enc_ctx->hw_device_ctx = av_buffer_ref(dev->device_ref);
        if (!ost->enc_ctx->hw_device_ctx)
            return AVERROR(ENOMEM);
    } else {
        // No device required, or no device available.
    }
    return 0;
}


static int hwaccel_retrieve_data(AVCodecContext *avctx, AVFrame *input)
{
    InputStream *ist = avctx->opaque;
    AVFrame *output = NULL;
    enum AVPixelFormat output_format = ist->hwaccel_output_format;
    int err;

    if (input->format == output_format) {
        // Nothing to do.
        return 0;
    }

    output = av_frame_alloc();
    if (!output)
        return AVERROR(ENOMEM);

    output->format = output_format;

    err = av_hwframe_transfer_data(output, input, 0);
    if (err < 0) {
        av_log(avctx, AV_LOG_ERROR, "Failed to transfer data to "
                                    "output frame: %d.\n", err);
        goto fail;
    }

    err = av_frame_copy_props(output, input);
    if (err < 0) {
        av_frame_unref(output);
        goto fail;
    }

    av_frame_unref(input);
    av_frame_move_ref(input, output);
    av_frame_free(&output);

    return 0;

    fail:
    av_frame_free(&output);
    return err;
}

int hwaccel_decode_init(AVCodecContext *avctx)
{
    InputStream *ist = avctx->opaque;

    ist->hwaccel_retrieve_data = &hwaccel_retrieve_data;

    return 0;
}

void hw_device_free_all(RunContext *run_context)
{
    int i;
    for (i = 0; i < run_context->nb_hw_devices; i++) {
        av_freep(&run_context->hw_devices[i]->name);
        av_buffer_unref(&run_context->hw_devices[i]->device_ref);
        av_freep(&run_context->hw_devices[i]);
    }
    av_freep(&run_context->hw_devices);
    run_context->nb_hw_devices = 0;
}


int hw_device_setup_for_filter(RunContext *run_context,FilterGraph *fg)
{
    HWDevice *dev;
    int i;

    // If the user has supplied exactly one hardware device then just
    // give it straight to every filter for convenience.  If more than
    // one device is available then the user needs to pick one explcitly
    // with the filter_hw_device option.
    if (run_context->filter_hw_device)
        dev = run_context->filter_hw_device;
    else if (run_context->nb_hw_devices == 1)
        dev = run_context->hw_devices[0];
    else
        dev = NULL;

    if (dev) {
        for (i = 0; i < fg->graph->nb_filters; i++) {
            fg->graph->filters[i]->hw_device_ctx =
                    av_buffer_ref(dev->device_ref);
            if (!fg->graph->filters[i]->hw_device_ctx)
                return AVERROR(ENOMEM);
        }
    }

    return 0;
}

#if CONFIG_QSV
static int qsv_device_init(RunContext *run_context,InputStream *ist)
{
    int err;
    AVDictionary *dict = NULL;

    if (run_context->qsv_device) {
        err = av_dict_set(&dict, "child_device", run_context->qsv_device, 0);
        if (err < 0)
            return err;
    }

    err = av_hwdevice_ctx_create(&run_context->hw_device_ctx, AV_HWDEVICE_TYPE_QSV,
                                 ist->hwaccel_device, dict, 0);
    if (err < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error creating a QSV device\n");
        goto err_out;
    }

    err_out:
    if (dict)
        av_dict_free(&dict);

    return err;
}
static int qsv_get_buffer(AVCodecContext *s, AVFrame *frame, int flags)
{
    InputStream *ist = s->opaque;

    return av_hwframe_get_buffer(ist->hw_frames_ctx, frame, 0);
}
static void qsv_uninit(AVCodecContext *s)
{
    InputStream *ist = s->opaque;
    av_buffer_unref(&ist->hw_frames_ctx);
}
int qsv_init(void *p_run_context,AVCodecContext *s)
{
    RunContext * run_context = p_run_context;
    InputStream *ist = s->opaque;
    AVHWFramesContext *frames_ctx;
    AVQSVFramesContext *frames_hwctx;
    int ret;

    if (!run_context->hw_device_ctx) {
        ret = qsv_device_init(run_context,ist);
        if (ret < 0)
            return ret;
    }

    av_buffer_unref(&ist->hw_frames_ctx);
    ist->hw_frames_ctx = av_hwframe_ctx_alloc(run_context->hw_device_ctx);
    if (!ist->hw_frames_ctx)
        return AVERROR(ENOMEM);

    frames_ctx   = (AVHWFramesContext*)ist->hw_frames_ctx->data;
    frames_hwctx = frames_ctx->hwctx;

    frames_ctx->width             = FFALIGN(s->coded_width,  32);
    frames_ctx->height            = FFALIGN(s->coded_height, 32);
    frames_ctx->format            = AV_PIX_FMT_QSV;
    frames_ctx->sw_format         = s->sw_pix_fmt;
    frames_ctx->initial_pool_size = 64 + s->extra_hw_frames;
    frames_hwctx->frame_type      = MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET;

    ret = av_hwframe_ctx_init(ist->hw_frames_ctx);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error initializing a QSV frame pool\n");
        return ret;
    }

    ist->hwaccel_get_buffer = qsv_get_buffer;
    ist->hwaccel_uninit     = qsv_uninit;

    return 0;
}
#endif