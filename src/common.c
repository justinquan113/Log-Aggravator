#include "common.h"
#include <string.h>
#include <stdio.h>
level_t level_parse(const char *s){
    if(s == NULL){
        return LVL_INVALID;
    }

    if (strcmp(s, "DEBUG") == 0) return LVL_DEBUG;
    if (strcmp(s, "INFO")  == 0) return LVL_INFO;
    if (strcmp(s, "WARN")  == 0) return LVL_WARN;
    if (strcmp(s, "ERROR") == 0) return LVL_ERROR;

    return LVL_INVALID;
}

const char *level_name(level_t lvl){
    if(lvl < LVL_DEBUG || lvl > LVL_ERROR){
        return "?";
    } 
    
    if(lvl == LVL_INFO) return "INFO";
    if(lvl == LVL_WARN) return "WARN";
    if(lvl == LVL_DEBUG) return "DEBUG";    
}

int msg_parse(const char *line, char *src_out, size_t src_sz,level_t *lvl_out){
    char lvlbuf[16];

    /*gets the string at the first "|" and everything after it*/
    const char *bar1 = strchr(line, '|');
    if(!bar1) return -1;

    /*gets the string at the second "|" and everything after it*/
    const char *bar2 = strchr(bar1 + 1, '|');
    if(!bar2) return  -1;

    /*get the length of the level and copy string into buffer*/
    size_t level_len = bar1 - line;
    if(level_len >= sizeof(lvlbuf)){
        return -1;
    }
    memcpy(lvlbuf, line, level_len);
    lvlbuf[level_len] = '\0';

    level_t lvl = level_parse(lvlbuf);
    if(lvl == LVL_INVALID){
        return -1;
    }
    *lvl_out = lvl;

    /*get the length of the source and copy string into buffer*/
    size_t src_len = bar2 - bar1 - 1;
    if(src_len == 0 || src_len >= src_sz){
        return -1;
    }
    memcpy(src_out, bar1 + 1, src_len);
    src_out[src_len] = '\0';

    return 0;
}   


