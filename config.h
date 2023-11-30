#ifndef RUN_FFMPEG_CONFIG_H

#if CONF_MAC_X86
#include "conf/config-mac-x64.h"
#elif CONF_MAC_ARM
#include "conf/config-mac-arm64.h"
#elif CONF_LINUX_X86
#include "conf/config-linux-x64.h"
#elif CONF_LINUX_ARM
#include "conf/config-linux-arm64.h"
#elif

#endif

#endif /* RUN_FFMPEG_CONFIG_H */
