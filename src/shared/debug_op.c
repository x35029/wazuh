/* Copyright (C) 2009 Trend Micro Inc.
 * All rights reserved.
 *
 * This program is a free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License (version 2) as published by the FSF - Free Software
 * Foundation
 */

#include "headers/shared.h"
#include <external/cJSON/cJSON.h>

static int dbg_flag = 0;
static int chroot_flag = 0;
static int daemon_flag = 0;
//static int read_flag = 0;
struct{
  unsigned int log_plain:1;
  unsigned int log_json:1;
  unsigned int read:1;
} flags;

static void _log(int level, const char *tag, const char *msg, va_list args) __attribute__((format(printf, 2, 0))) __attribute__((nonnull));

#ifdef WIN32
void WinSetError();
#endif

static void _log(int level, const char *tag, const char *msg, va_list args)
{
    time_t tm;
    struct tm *p;
    va_list args2; /* For the stderr print */
    va_list args3; /* For the JSON output */
    FILE *fp;
    FILE *fp2;
    char timestamp[OS_MAXSTR];
    char jsonstr[OS_MAXSTR];
    char *output;
    const char *strlevel[5]={
      "DEBUG",
      "INFO",
      "WARNING",
      "ERROR",
      "CRITICAL",
    };
    const char *strleveljson[5]={
      "debug",
      "info",
      "warning",
      "error",
      "critical"
    };

    tm = time(NULL);
    p = localtime(&tm);
    /* Duplicate args */
    va_copy(args2, args);
    va_copy(args3, args);

    if (!flags.read){
      os_logging_config();
    }

    if (flags.log_json){

      /* If under chroot, log directly to /logs/ossec.json */
      if (chroot_flag == 1) {
          fp2 = fopen(LOGJSONFILE, "a");
      } else {
          char _logjsonfile[256];
  #ifndef WIN32
          snprintf(_logjsonfile, 256, "%s%s", DEFAULTDIR, LOGJSONFILE);
  #else
          snprintf(_logjsonfile, 256, "%s", LOGJSONFILE);
  #endif
          fp2 = fopen(_logjsonfile, "a");
      }

      if (fp2) {

          cJSON *json_log = cJSON_CreateObject();

          snprintf(timestamp,OS_MAXSTR,"%d/%02d/%02d %02d:%02d:%02d",
                        p->tm_year + 1900, p->tm_mon + 1,
                        p->tm_mday, p->tm_hour, p->tm_min, p->tm_sec);

          vsnprintf(jsonstr, OS_MAXSTR, msg, args3);

          cJSON_AddStringToObject(json_log, "timestamp", timestamp);
          cJSON_AddStringToObject(json_log, "tag", tag);
          cJSON_AddStringToObject(json_log, "level", strleveljson[level]);
          cJSON_AddStringToObject(json_log, "description", jsonstr);

          output = cJSON_PrintUnformatted(json_log);

          (void)fprintf(fp2, "%s", output);
          (void)fprintf(fp2, "\n");

          cJSON_Delete(json_log);
          free(output);
          fflush(fp2);
          fclose(fp2);
      }
    }

    if(flags.log_plain){
      /* If under chroot, log directly to /logs/ossec.log */
      if (chroot_flag == 1) {
          fp = fopen(LOGFILE, "a");
      } else {
          char _logfile[256];
  #ifndef WIN32
          snprintf(_logfile, 256, "%s%s", DEFAULTDIR, LOGFILE);
  #else
          snprintf(_logfile, 256, "%s", LOGFILE);
  #endif
          fp = fopen(_logfile, "a");
      }

      /* Maybe log to syslog if the log file is not available */
      if (fp) {
          (void)fprintf(fp, "%d/%02d/%02d %02d:%02d:%02d ",
                        p->tm_year + 1900, p->tm_mon + 1,
                        p->tm_mday, p->tm_hour, p->tm_min, p->tm_sec);
          (void)fprintf(fp, "%s: ", tag);
          (void)fprintf(fp, "%s: ", strlevel[level]);
          (void)vfprintf(fp, msg, args);
  #ifdef WIN32
          (void)fprintf(fp, "\r\n");
  #else
          (void)fprintf(fp, "\n");
  #endif
          fflush(fp);
          fclose(fp);
      }
    }


    /* Only if not in daemon mode */
    if (daemon_flag == 0) {
        /* Print to stderr */
        (void)fprintf(stderr, "%d/%02d/%02d %02d:%02d:%02d ",
                      p->tm_year + 1900, p->tm_mon + 1 , p->tm_mday,
                      p->tm_hour, p->tm_min, p->tm_sec);
        (void)fprintf(stderr, "%s: ", tag);
        (void)fprintf(stderr, "%s: ", strlevel[level]);
        (void)vfprintf(stderr, msg, args2);
#ifdef WIN32
        (void)fprintf(stderr, "\r\n");
#else
        (void)fprintf(stderr, "\n");
#endif
    }

    /* args must be ended here */
    va_end(args2);
    va_end(args3);
}

void os_logging_config(){
  OS_XML xml;
  const char * xmlf[] = {"ossec_config", "logging", "log_format", NULL};
  char * logformat;
  char ** parts = NULL;
  int i, j;

  if (OS_ReadXML(chroot_flag ? OSSECCONF : DEFAULTCPATH, &xml) < 0){
    flags.log_plain = 1;
    flags.log_json = 0;
    flags.read = 1;
    OS_ClearXML(&xml);
    merror_exit(XML_ERROR, "/etc/ossec.conf", xml.err, xml.err_line);
  }

  logformat = OS_GetOneContentforElement(&xml, xmlf);

  if (!logformat || logformat[0] == '\0'){

    flags.log_plain = 1;
    flags.log_json = 0;
    flags.read = 1;

    free(logformat);
    OS_ClearXML(&xml);
    mdebug1(XML_NO_ELEM, "log_format");

  }else{

    parts = OS_StrBreak(',', logformat, 2);
    char * part;
    if (parts){
      for (i=0; parts[i]; i++){
        part = w_strtrim(parts[i]);
        if (!strcmp(part, "plain")){
          flags.log_plain = 1;
        }else if(!strcmp(part, "json")){
          flags.log_json = 1;
        }else{
          flags.log_plain = 1;
          flags.log_json = 0;
          flags.read = 1;
          for (j=0; parts[j]; j++){
            free(parts[j]);
          }
          free(parts);
          free(logformat);
          OS_ClearXML(&xml);
          merror(CONFIG_ERROR, DEFAULTCPATH);
        }
      }
      for (i=0; parts[i]; i++){
        free(parts[i]);
      }
      free(parts);
    }

    free(logformat);
    OS_ClearXML(&xml);
    flags.read = 1;
  }
}

void mdebug1(const char *msg, ...)
{
    if (dbg_flag >= 1) {
        va_list args;
        int level = LOGLEVEL_DEBUG;
        const char *tag = __local_name;
        va_start(args, msg);
        _log(level, tag, msg, args);
        va_end(args);
    }
}

void mtdebug1(const char *tag, const char *msg, ...)
{
    if (dbg_flag >= 1) {
        va_list args;
        int level = LOGLEVEL_DEBUG;
        va_start(args, msg);
        _log(level, tag, msg, args);
        va_end(args);
    }
}

void mdebug2(const char *msg, ...)
{
    if (dbg_flag >= 2) {
        va_list args;
        int level = LOGLEVEL_DEBUG;
        const char *tag = __local_name;
        va_start(args, msg);
        _log(level, tag, msg, args);
        va_end(args);
    }
}

void mtdebug2(const char *tag, const char *msg, ...)
{
    if (dbg_flag >= 2) {
        va_list args;
        int level = LOGLEVEL_DEBUG;
        va_start(args, msg);
        _log(level, tag, msg, args);
        va_end(args);
    }
}

void merror(const char *msg, ... )
{
    va_list args;
    int level = LOGLEVEL_ERROR;
    const char *tag = __local_name;

    va_start(args, msg);
    _log(level, tag, msg, args);
    va_end(args);
}

void mterror(const char *tag, const char *msg, ... )
{
    va_list args;
    int level = LOGLEVEL_ERROR;

    va_start(args, msg);
    _log(level, tag, msg, args);
    va_end(args);
}

void mwarn(const char *msg, ... )
{
    va_list args;
    int level = LOGLEVEL_WARNING;
    const char *tag = __local_name;

    va_start(args, msg);
    _log(level, tag, msg, args);
    va_end(args);
}

void mtwarn(const char *tag, const char *msg, ... )
{
    va_list args;
    int level = LOGLEVEL_WARNING;

    va_start(args, msg);
    _log(level, tag, msg, args);
    va_end(args);
}

void minfo(const char *msg, ... )
{
    va_list args;
    int level = LOGLEVEL_INFO;
    const char *tag = __local_name;

    va_start(args, msg);
    _log(level, tag, msg, args);
    va_end(args);
}

void mtinfo(const char *tag, const char *msg, ... )
{
    va_list args;
    int level = LOGLEVEL_INFO;

    va_start(args, msg);
    _log(level, tag, msg, args);
    va_end(args);
}

/* Only logs to a file */
void mferror(const char *msg, ... )
{
    int level = LOGLEVEL_ERROR;
    const char *tag = __local_name;
    int dbg_tmp;
    va_list args;
    va_start(args, msg);

    /* We set daemon flag to 1, so nothing is printed to the terminal */
    dbg_tmp = daemon_flag;
    daemon_flag = 1;
    _log(level, tag, msg, args);

    daemon_flag = dbg_tmp;

    va_end(args);
}

/* Only logs to a file */
void mtferror(const char *tag, const char *msg, ... )
{
    int level = LOGLEVEL_ERROR;
    int dbg_tmp;
    va_list args;
    va_start(args, msg);

    /* We set daemon flag to 1, so nothing is printed to the terminal */
    dbg_tmp = daemon_flag;
    daemon_flag = 1;
    _log(level, tag, msg, args);

    daemon_flag = dbg_tmp;

    va_end(args);
}

void merror_exit(const char *msg, ...)
{
    va_list args;
    int level = LOGLEVEL_CRITICAL;
    const char *tag = __local_name;

#ifdef WIN32
    /* If not MA */
#ifndef MA
    WinSetError();
#endif
#endif

    va_start(args, msg);
    _log(level, tag, msg, args);
    va_end(args);

    exit(1);
}

void mterror_exit(const char *tag, const char *msg, ...)
{
    va_list args;
    int level = LOGLEVEL_CRITICAL;

#ifdef WIN32
    /* If not MA */
#ifndef MA
    WinSetError();
#endif
#endif

    va_start(args, msg);
    _log(level, tag, msg, args);
    va_end(args);

    exit(1);
}

void nowChroot()
{
    chroot_flag = 1;
}

void nowDaemon()
{
    daemon_flag = 1;
}

void print_out(const char *msg, ...)
{
    va_list args;
    va_start(args, msg);

    /* Print to stderr */
    (void)vfprintf(stderr, msg, args);

#ifdef WIN32
    (void)fprintf(stderr, "\r\n");
#else
    (void)fprintf(stderr, "\n");
#endif

    va_end(args);
}

void nowDebug()
{
    dbg_flag++;
}

int isDebug(void)
{
    return dbg_flag;
}

int isChroot()
{
    return (chroot_flag);
}
