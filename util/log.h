#ifndef LOG_H
#define LOG_H

#define log_level_error 0
#define log_level_warn  1
#define log_level_info  2
#define log_level_debug 3

#ifndef NDEBUG
#ifndef DEBUG_LEVEL
#define DEBUG_LEVEL 3
#endif
#define _log(level, ...) do { \
        if((level) <= DEBUG_LEVEL) {\
            printf("%s:%d:[%d](%s):" , __FILE__, __LINE__, (level), __func__ );\
            printf(__VA_ARGS__);\
        }\
    } while (0) 

#define log_err(...) _log(log_level_error, __VA_ARGS__)
#define log_warn(...) _log(log_level_warn,_VA_ARGS__)
#define log_info(...) _log(log_level_info, __VA_ARGS__)
#define log_debug(...) _log(log_level_debug,__VA_ARGS__)
#else
#define _log(level, ...)
#define log_err(...) 
#define log_warn(...) 
#define log_info(...)
#define log_debug(...) 
#endif


#ifndef log_subsys_net
#define log_subsys_net
#define log_subsys_level 3
#endif



#endif