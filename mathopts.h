//
// Created by hexiufeng on 2023/11/5.
//

#ifndef RUN_FFMPEG_MATHOPTS_H
#define RUN_FFMPEG_MATHOPTS_H

#include "config.h"
#include <libavutil/attributes.h>

#if ARCH_X86
#if HAVE_I686
/* median of 3 */
#define mid_pred mid_pred
static inline av_const int mid_pred(int a, int b, int c)
{
int i=b;
__asm__ (
        "cmp    %2, %1 \n\t"
        "cmovg  %1, %0 \n\t"
        "cmovg  %2, %1 \n\t"
        "cmp    %3, %1 \n\t"
        "cmovl  %3, %1 \n\t"
        "cmp    %1, %0 \n\t"
        "cmovg  %1, %0 \n\t"
        :"+&r"(i), "+&r"(a)
        :"r"(b), "r"(c)
        );
return i;
}
#endif
#elif ARCH_ARM
#define mid_pred mid_pred
static inline av_const int mid_pred(int a, int b, int c)
{
    int m;
    __asm__ (
        "mov   %0, %2  \n\t"
        "cmp   %1, %2  \n\t"
        "itt   gt      \n\t"
        "movgt %0, %1  \n\t"
        "movgt %1, %2  \n\t"
        "cmp   %1, %3  \n\t"
        "it    le      \n\t"
        "movle %1, %3  \n\t"
        "cmp   %0, %1  \n\t"
        "it    gt      \n\t"
        "movgt %0, %1  \n\t"
        : "=&r"(m), "+r"(a)
        : "r"(b), "r"(c)
        : "cc");
    return m;
}
#endif
#ifndef mid_pred
#define mid_pred mid_pred
static inline av_const int mid_pred(int a, int b, int c)
{
    if(a>b){
        if(c>b){
            if(c>a) b=a;
            else    b=c;
        }
    }else{
        if(b>c){
            if(c>a) b=c;
            else    b=a;
        }
    }
    return b;
}
#endif
#endif //RUN_FFMPEG_MATHOPTS_H
