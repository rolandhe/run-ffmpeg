//
// Created by hexiufeng on 2023/11/5.
//

#ifndef RUN_FFMPEG_INTERNAL_H
#define RUN_FFMPEG_INTERNAL_H
#ifdef DEBUG
#   define ff_dlog(ctx, ...) av_log(ctx, AV_LOG_DEBUG, __VA_ARGS__)
#else
#   define ff_dlog(ctx, ...) do { if (0) av_log(ctx, AV_LOG_DEBUG, __VA_ARGS__); } while (0)
#endif
#endif //RUN_FFMPEG_INTERNAL_H
