//
// Created by hexiufeng on 2023/11/3.
//

#ifndef RUN_FFMPEG_COMMON_DEFINE_H
#define RUN_FFMPEG_COMMON_DEFINE_H

#define ERR_QUICK_RETURN(err) \
    if(err < 0){ \
       return  -1; \
    }

#endif //RUN_FFMPEG_COMMON_DEFINE_H
