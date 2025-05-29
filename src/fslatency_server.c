/*
** fslatency_server.c
**
** collect filesystem (disk) write latency measurements for a long period
** tested at Ubuntu 24.04 LTS
**
** gcc --static -Wall -o fslatency_server fslatency_server.c
** strip fslatency_server
**
** Copyright by Adam Maulis maulis@andrews.hu 2025

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/


#define SERVER_VERSION_MAJOR 0
#define SERVER_VERSION_MINOR 4

# define _GNU_SOURCE 1 /* O_NOATIME */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <pthread.h>
#include <sys/vfs.h>
#include <sys/mman.h>
#include <getopt.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <math.h>

#include "datablock.h"
#include "nameregistry.h"


#ifdef DEBUG

static int my_pthread_mutex_lock(pthread_mutex_t *mutex, int line)
{
    int retval;
    dprintf(2, "[L%p %d.", mutex, line);
    retval = pthread_mutex_lock(mutex);
    dprintf(2, "%p]", mutex);
    return retval;
}

static int my_pthread_mutex_unlock(pthread_mutex_t *mutex, int line)
{
    dprintf(2, "[U%p %d]", mutex, line);
    return pthread_mutex_unlock(mutex);
}

static int my_pthread_mutex_init(pthread_mutex_t *mutex, const  pthread_mutexattr_t *mutexattr, int line)
{
    dprintf(2, "[I%p %d]", mutex, line);
    return pthread_mutex_init(mutex, mutexattr);
}

#define pthread_mutex_lock(a) my_pthread_mutex_lock(a, __LINE__)
#define pthread_mutex_unlock(a) my_pthread_mutex_unlock(a, __LINE__)
#define pthread_mutex_init(a, b) my_pthread_mutex_init(a,b, __LINE__)
#endif


#define TIMEFORMAT "%Y-%m-%dT%H:%M:%S%z"  /* iso-8601, like "2006-08-14T02:34:56-0600" */
#define TIMEFORMAT_LEN 26                 /*                 1234567890123456789012345 */

/*
** Alarm definitions
**  set and unset via alarm_set(), alarm_unset(), alarm_clear()
*/

#define ALARM_NOALARM 0
#define ALARM_STATISTICALALARM_LOW 1
#define ALARM_STATISTICALALARM_HIGH 2
#define ALARM_STATISTICALALARM_EMPTYDATABLOCK 4
#define ALARM_UDPTIMEOUT 8


/*
**  timespec_gt(left, right)
**    left > right
**    left .GT. right
**    left Greater Than right
**    the left is later than the right
**        retrun 1 if true
**        return 0 if false
*/
static inline int timespec_gt(const struct timespec *left, const struct timespec *right)
{
    if( left->tv_sec > right->tv_sec){
        return 1;
    }
    if( (left->tv_sec == right->tv_sec) && (left->tv_nsec > right->tv_nsec)){
        return 1;
    }
    return 0;
}


/*
**  timespec_zero(left)
**      return 1(true) if left is zero
*/
static inline int timespec_zero(const struct timespec *left)
{
    return ( left->tv_sec == 0) && (left->tv_nsec == 0);
}


/*
** command-line option processing.
** There is a static, global opt struct.
*/

#define OPT_BIND 1
#define OPT_PORT 2
#define OPT_MAXCLIENT 3
#define OPT_TIMETOFORGET 4
#define OPT_UDPTIMEOUT 5
#define OPT_ALARMTIMEOUT 6
#define OPT_STATUSPERIOD 7
#define OPT_ALARMSTATUSPERIOD 8
#define OPT_LATENCYTHRESHOLDFACTOR 9
#define OPT_ROLLINGWINDOW 10
#define OPT_MINIMUMMEASUREMENTCOUNT 11
#define OPT_GRAPHITEBASE 12
#define OPT_GRAPHITEIP 13
#define OPT_GRAPHITEPORT 14

#define OPT_NOMEMLOCK 99
#define OPT_DEBUG 100
#define OPT_VERSION 101

struct option myoptions[] = {
/* name, has_arg, flag, val */
 { "bind", 1, NULL, OPT_BIND},
 { "port", 1, NULL, OPT_PORT},
 { "maxclient", 1, NULL, OPT_MAXCLIENT},
 { "timetoforget", 1, NULL, OPT_TIMETOFORGET},
 { "udptimeout", 1, NULL, OPT_UDPTIMEOUT},
 { "alarmtimeout", 1, NULL, OPT_ALARMTIMEOUT},
 { "statusperiod", 1, NULL, OPT_STATUSPERIOD},
 { "alarmstatusperiod", 1, NULL, OPT_ALARMSTATUSPERIOD},
 { "latencythresholdfactor", 1, NULL, OPT_LATENCYTHRESHOLDFACTOR},
 { "rollingwindow", 1,  NULL, OPT_ROLLINGWINDOW},
 { "minimummeasurementcount", 1, NULL, OPT_MINIMUMMEASUREMENTCOUNT},
 { "graphitebase", 1, NULL, OPT_GRAPHITEBASE},
 { "graphiteip", 1, NULL, OPT_GRAPHITEIP},
 { "graphiteport", 1, NULL, OPT_GRAPHITEPORT},
 { "nomemlock", 0, NULL, OPT_NOMEMLOCK},
 { "debug", optional_argument , NULL, OPT_DEBUG},
 { "version", 0, NULL, OPT_VERSION},
 { NULL, 0, NULL, 0}
};


static struct _opt {
    char * bind;
    unsigned short int port;
    int maxclient;
    int timetoforget;
    int udptimeout;
    int alarmtimeout;
    int statusperiod;
    int alarmstatusperiod;
    double latencythresholdfactor;
    int rollingwindow;
    int minimummeasurementcount;
    char * graphitebase;
    char * graphiteip;
    unsigned short int graphiteport;
    struct sockaddr_in graphiteaddr;
    unsigned int nomemlock;
    unsigned int debug;
} opt;


static void init_opt(void)
{
    opt.bind = "0.0.0.0";
    opt.port = 57005;
    opt.maxclient = 509;
    opt.timetoforget = 600;
    opt.udptimeout = 3;
    opt.statusperiod = 300;
    opt.alarmstatusperiod = 1;
    opt.alarmtimeout = 8;
    opt.latencythresholdfactor = 15.0;
    opt.rollingwindow = 60;
    opt.minimummeasurementcount = 60;
    opt.graphitebase = NULL;
    opt.graphiteip = NULL;
    opt.graphiteport = 2003;
    opt.nomemlock = 0; /*False*/
    opt.debug = 0; /*False*/
}


void help()
{   /*   "01234567890123456789012345678901234567890123456789012345678901234567890123456789" */
    puts("Usage: fslatency_server [--bind a.b.c.d] [--port PORT] [--maxclient 509]");
    puts("   [--timetoforget 600] [--udptimeout 3] [--alarmstatusperiod 1]");
    puts("   [--statusperiod 300] [--alarmtimeout 8] [--latencythresholdfactor 15.0]");
    puts("   [--rollingwindow 60] [--minimummeasurementcount 60]");
    puts("   [--graphitebase metric.path.base --graphiteip 1.2.3.4 [--graphiteport 2003]]");
    puts("   [--nomemlock] [--debug[=1]] [--version]");
}


static int parse_opt(int argc, char * argv[])
{
    int optcode;

    /* parameter processing */
    /* --ip a.b.c.d --port PORT --text "FOO" --file  "path" */
    while( (optcode = getopt_long(argc, argv, "", myoptions, NULL)) >= 0 ){
        switch(optcode){
            case '?':
                dprintf(2 /*stderr*/, "Error: unkown command line option\n");
                help();
                return 2;
            case OPT_BIND:
                opt.bind = strdup(optarg);
                break;
            case OPT_PORT:
                opt.port = atoi(optarg);
                break;
            case OPT_MAXCLIENT:
                opt.maxclient = atoi(optarg);
                break;
            case OPT_TIMETOFORGET:
                opt.timetoforget = atoi(optarg);
                break;
            case OPT_UDPTIMEOUT:
                opt.udptimeout = atoi(optarg);
                break;
            case OPT_ALARMTIMEOUT:
                opt.alarmtimeout = atoi(optarg);
                break;
            case OPT_STATUSPERIOD:
                opt.statusperiod = atoi(optarg);
                break;
            case OPT_ALARMSTATUSPERIOD:
                opt.alarmstatusperiod = atoi(optarg);
                break;
            case OPT_LATENCYTHRESHOLDFACTOR:
                opt.latencythresholdfactor =  atof(optarg);
                break;
            case OPT_ROLLINGWINDOW:
                opt.rollingwindow = atoi(optarg);
                break;
            case OPT_MINIMUMMEASUREMENTCOUNT:
                opt.minimummeasurementcount = atoi(optarg);
                break;
            case OPT_GRAPHITEBASE:
                opt.graphitebase = strdup(optarg);
                break;
            case OPT_GRAPHITEIP:
                opt.graphiteip = strdup(optarg);
                break;
            case OPT_GRAPHITEPORT:
                opt.graphiteport = atoi(optarg);
                break;
            case OPT_NOMEMLOCK:
                opt.nomemlock = 1;
                break;
            case OPT_DEBUG:
                if( NULL == optarg){
                    opt.debug = 1;
                }else{
                    opt.debug = atoi(optarg);
                }
                break;
            case OPT_VERSION:
                dprintf(2, "fslatency_server %d.%d. UDP version %d.%d\n", SERVER_VERSION_MAJOR, SERVER_VERSION_MINOR, FSLATENCY_VERSION_MAJOR, FSLATENCY_VERSION_MINOR);
                exit(0);
                break;
            default:
                dprintf(2 /*stderr*/, "Error: command line processing error (getopt returns %d)\n", optcode);
                help();
                return 2;
        } /* end switch */
    } /* end while getopt */

    if( 0 == opt.port){
        dprintf(2 /*stderr*/, "Error: invalid port number\n");
        return 2;
    }
    if( 0 == opt.maxclient){
        dprintf(2 /*stderr*/, "Error: invalid maxclient number\n");
        return 2;
    }
    if( (3 > opt.timetoforget) || (opt.udptimeout >= opt.timetoforget)) {
        dprintf(2 /*stderr*/, "Error: invalid timetoforget number (min 3 and must be greather than udptimeout)\n");
        return 2;
    }
    if( 2 > opt.udptimeout){
        dprintf(2 /*stderr*/, "Error: invalid udptimeout number (min 2)\n");
        return 2;
    }
    if( 0 == opt.alarmtimeout){
        dprintf(2 /*stderr*/, "Error: invalid alarmtimeout number\n");
        return 2;
    }
    if( 0 == opt.statusperiod){
        dprintf(2 /*stderr*/, "Error: invalid statusperiod number\n");
        return 2;
    }
    if( 0 == opt.alarmstatusperiod){
        dprintf(2 /*stderr*/, "Error: invalid alarmstatusperiod number\n");
        return 2;
    }
    if( 0.0 >= opt.latencythresholdfactor){
        dprintf(2 /*stderr*/, "Error: invalid latencythresholdfactor value (must be positive float)\n");
        return 2;
    }
    if( 8 > opt.rollingwindow){
        dprintf(2 /*stderr*/, "Error: invalid rollingwindow number. Min 8.\n");
        return 2;
    }
    if( (opt.rollingwindow-1) *9 < opt.minimummeasurementcount){
        dprintf(2 /*stderr*/, "Error: minimummeasurementcount is too high or rollingwindow is too low. \n");
        return 2;
    }
    if( NULL != opt.graphitebase && NULL == opt.graphiteip){
        dprintf(2 /*stderr*/, "Warning: you shuld specify graphite server ip address (--graphiteip). Printing to stdout.\n");
    }
    if( NULL == opt.graphitebase && NULL != opt.graphiteip){
        dprintf(2 /*stderr*/, "Warning: you should not specify --graphiteip when no graphite base string (--graphitebase)\n");
    }



    if( opt.debug){
        dprintf(2,"DEBUG Options:\n");
        dprintf(2, "    --bind                    %s\n", opt.bind);
        dprintf(2, "    --port                    %u\n", opt.port);
        dprintf(2, "    --maxclient               %d\n", opt.maxclient);
        dprintf(2, "    --timetoforget            %d\n", opt.timetoforget);
        dprintf(2, "    --udptimeout              %d\n", opt.udptimeout);
        dprintf(2, "    --alarmtimeout            %d\n", opt.alarmtimeout);
        dprintf(2, "    --statusperiod            %d\n", opt.statusperiod);
        dprintf(2, "    --alarmstatusperiod       %d\n", opt.alarmstatusperiod);
        dprintf(2, "    --latencythresholdfactor  %f\n", opt.latencythresholdfactor);
        dprintf(2, "    --rollingwindow           %d\n", opt.rollingwindow);
        dprintf(2, "    --graphitebase            %s\n", opt.graphitebase);
        dprintf(2, "    --graphiteip              %s\n", opt.graphiteip);
        dprintf(2, "    --graphiteport            %u\n", opt.graphiteport);
        dprintf(2, "    --nomemlock %d\n", opt.nomemlock);
        dprintf(2, "    --debug %d\n", opt.debug);
    }

    return 0;
}


/*
** databases: namedb and statusdb
**   some static global variables and functions
**
*/

static struct nameregistry namedb;

/* sometimes we need to modify booth statusdb and namedb in a single transaction */
static pthread_mutex_t global_addremove_lock;

/* we need something that signals all subsystem the alarm status */
static int global_alarmstatus; /* bool */
static pthread_mutex_t global_alarmstatus_lock;

static pthread_cond_t global_alarmstatus_cond;  /* see alarmstatus_loop() */
static pthread_cond_t global_normalstatus_cond; /* see normalstatus_loop() */


#define RINGBUFFER_ENTRY_TYPE struct datablock
#include "ringbuffer.inc"  /* ringbuffer.inc is a C surce code that implements a template typed ringbuffer */


struct statusentry {
    uint32_t alarm;
    struct timespec lastalarmtime;
    struct timespec lastarrival;
    struct ringbuffer datablockbuffer;
    pthread_mutex_t mutex;
};


static struct statusentry * statusdb;


static void statusentry_init(struct statusentry * sep)
{
    int retval;

    sep->alarm = ALARM_NOALARM;
    sep->lastalarmtime =  (struct timespec) {0,0};
    sep->lastarrival = (struct timespec) {0,0};
    pthread_mutex_init(&(sep->mutex), 0);
    retval = ringbuffer_init(&(sep->datablockbuffer), opt.rollingwindow);
    if( 0 != retval ){
        dprintf(2 /*stderr*/, "Error: no mem for datablock buffer\n");
        exit(2);
    }
}


static void statusentry_clear(struct statusentry * sep)
{
    pthread_mutex_lock(&(sep->mutex));
    sep->alarm = ALARM_NOALARM;
    sep->lastalarmtime =  (struct timespec) {0,0};
    sep->lastarrival = (struct timespec) {0,0};
    ringbuffer_clear(&(sep->datablockbuffer));
    pthread_mutex_unlock(&(sep->mutex));
}


int init_databases(size_t clientnum)
{
    int retval;
    int i;

    /* dirty and guick hack. Since the hostname and the text come directly after each other, they can be used as one. */
    retval = nameregistry_init(&namedb, clientnum, FSLATENCY_HOSTNAME_LEN + FSLATENCY_TEXT_LEN);
    if( 0 != retval){
        if( opt.debug){
            dprintf(2 /*stderr*/, "Error: cannot allocate memory for namedb\n");
        }
        return -1;
    }
    statusdb = (struct statusentry *) malloc(clientnum * sizeof(struct statusentry));
    if( NULL == statusdb){
        if( opt.debug){
            dprintf(2 /*stderr*/, "Error: cannot allocate memory for statusdb\n");
        }
        return -1;
    }
    for(i=0; i< clientnum; i++){
        statusentry_init( statusdb+i );
    }

    global_alarmstatus = 0;
    pthread_mutex_init(&global_addremove_lock, 0);
    pthread_mutex_init(&global_alarmstatus_lock, 0);
    pthread_cond_init(&global_alarmstatus_cond, 0);
    pthread_cond_init(&global_normalstatus_cond, 0);
    return 0;
}


/*
** it mus be call under the lock of statusdb entry!
**  both the alarmstatus of statusdb's entry and the global alarmstatus set here.
**  the global one unset in the alarmsilencer_loop()
*/
static inline void alarm_set(int msgid, const unsigned int alarm_name)
{
    //dprintf(2, "DEBUG alarm_set(%d, %d) begin\n", msgid, alarm_name);
    statusdb[msgid].alarm |= alarm_name;
    clock_gettime(CLOCK_REALTIME, &(statusdb[msgid].lastalarmtime));
    if( opt.debug >1){
        dprintf(2, "DEBUG alarm set for msgid=%d global_alarmstatus=%d\n", msgid, global_alarmstatus);
    }
    pthread_mutex_lock(&global_alarmstatus_lock);
    if( !global_alarmstatus){
        if( opt.debug){
            dprintf(2, "DEBUG Global alarm status set. msgid=%d alarm_name=%d\n", msgid, alarm_name);
        }
        global_alarmstatus = 1;
        pthread_cond_signal(&global_alarmstatus_cond);
    }
    pthread_mutex_unlock(&global_alarmstatus_lock);
    //dprintf(2, "DEBUG alarm_set(%d, %d) end\n", msgid, alarm_name);
}

static inline void alarm_unset(int msgid, const unsigned int alarm_name)
{
    statusdb[msgid].alarm &= ~alarm_name;
}

static inline void alarm_clear(int msgid)
{
    statusdb[msgid].alarm = ALARM_NOALARM;
    statusdb[msgid].lastalarmtime = (struct timespec) {0,0};
}

/*
**  alarmer threads
**
**
**
*/


struct statnumbers {
    double minx, maxx, sumx, sumxx, mean, std;
    uint64_t sumN;
};


static void statnumbers_init( struct statnumbers * snp)
{
    snp->minx = FSLATENCY_EXTREMEBIGINTERVAL;
    snp->maxx = -FSLATENCY_EXTREMEBIGINTERVAL;
    snp->mean = snp->std = snp->sumx = snp->sumxx = 0.0;
    snp->sumN = 0;
}


static struct statnumbers global_stat;
static pthread_mutex_t global_stat_lock = PTHREAD_MUTEX_INITIALIZER;


static inline double standard_deviation(uint64_t sumN, double sumx, double sumxx){
        return sqrt((sumxx - sumx*sumx/(double)sumN)/(sumN-1.0));
}


static int statistical_alarmer(int msgid, struct statnumbers * csp)
{
    /* csp: cumulative statnumbers pointer */

    struct ringbuffer * rbp;
    struct datablock * dbp;
    int i;
    struct statnumbers stat;

    statnumbers_init(&stat);

    pthread_mutex_lock(&(statusdb[msgid].mutex));
    /* rbp RingBufferPointer points to the ringbuffer of current msgid */
    rbp = &(statusdb[msgid].datablockbuffer);
    if( 0 == rbp->len){ /* empty */
        pthread_mutex_unlock(&(statusdb[msgid].mutex));
        return 0;
    }

    /* calculate statnumbers for this msgid */
    for(i =0; i < rbp->len; i++){
        /* dbp DatBlockPointer points to the the curent datablock of current msgid */
        dbp = &(rbp->buffer[(i + rbp->start) % rbp->bufferlen]);
        if( dbp->min <= FSLATENCY_EXTREMEBIGINTERVAL){
            stat.sumN += dbp->measurementcount;
            if( dbp->min < stat.minx){
                stat.minx = dbp->min;
            }
            if( dbp->max > stat.maxx){
                stat.maxx = dbp->max;
            }
            stat.sumx += dbp->sumx;
            stat.sumxx += dbp->sumxx;
        }else{
            if(opt.debug){
                /* program flow error, please ignore */
                dprintf(2, "DEBUG empty datablock arrived for statisctic alarmer.\n");
            }
        }
    }

    /* update cumulative_stat that is thread-local*/

    csp->sumN += stat.sumN;
    csp->sumx += stat.sumx;
    csp->sumxx += stat.sumxx;
    if( csp->minx > stat.minx){
        csp->minx = stat.minx;
    }
    if( csp->maxx < stat.maxx){
        csp->maxx = stat.maxx;
    }

    /* after the loop, dbp points to the last datablock. Max/min check only for last datablock*/
    if( stat.sumN > opt.minimummeasurementcount){
        stat.mean = stat.sumx / stat.sumN;
        stat.std = standard_deviation(stat.sumN, stat.sumx, stat.sumxx);
        if( opt.debug > 1){
            dprintf(2, "DEBUG statistic msgid=%d sumN=%lu [%f < min=%f max=%f < %f] avg=%f std=%f\n", msgid, stat.sumN,
            stat.mean - stat.std * opt.latencythresholdfactor, stat.minx, stat.maxx,
            stat.mean + stat.std * opt.latencythresholdfactor, stat.mean, stat.std);
        }
        if( dbp->min < (stat.mean - stat.std * opt.latencythresholdfactor)){
            alarm_set(msgid, ALARM_STATISTICALALARM_LOW);
        } else {
            alarm_unset(msgid, ALARM_STATISTICALALARM_LOW);
        }
        if( dbp->max > (stat.mean + stat.std * opt.latencythresholdfactor)){
            alarm_set(msgid, ALARM_STATISTICALALARM_HIGH);
        } else {
            alarm_unset(msgid, ALARM_STATISTICALALARM_HIGH);
        }


    }else{
        if( opt.debug > 1){
            dprintf(2, "DEBUG statistic (low on N) msgid=%d sumN=%lu min=%f max=%f \n", msgid, stat.sumN, stat.minx, stat.maxx);
        }
    }
    pthread_mutex_unlock(&(statusdb[msgid].mutex));
    return 0;
}


static void * statistical_alarmer_loop( void * arg)
{
    int msgid;
    struct statnumbers cumulative_stat;

    while(1){
        statnumbers_init(&cumulative_stat);  /* zero it */
        for(msgid = 0; msgid < opt.maxclient; msgid++){
            statistical_alarmer(msgid, &cumulative_stat);  /* note: there is a room for performance tunning */
        }

        pthread_mutex_lock(&global_stat_lock);
        global_stat = cumulative_stat;
        global_stat.mean = global_stat.sumx / (double)global_stat.sumN;
        global_stat.std = standard_deviation(global_stat.sumN, global_stat.sumx, global_stat.sumxx);
        pthread_mutex_unlock(&global_stat_lock);
        sleep(1);
    }
    return NULL;
}



static void * udptimeout_loop( void * arg)
{
    int msgid;
    struct timespec deadline;

    while(1){
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec -= opt.udptimeout;
        for(msgid = 0; msgid < opt.maxclient; msgid++){
            /* some quickie without lock */
            if( timespec_zero(&(statusdb[msgid].lastarrival))){ /* empty slot*/
                continue;
            }
            if( timespec_gt(&(statusdb[msgid].lastarrival), &deadline)){ /*fresh*/
                continue;
            }
            /*set alarm for this*/
            if( opt.debug >1){
                dprintf(2, "DEBUG udptimeout, msgid=%d\n", msgid);
            }
            pthread_mutex_lock(&(statusdb[msgid].mutex));
            clock_gettime(CLOCK_REALTIME, &deadline);
            deadline.tv_sec -= opt.udptimeout;
            if( timespec_gt(&(statusdb[msgid].lastarrival), &deadline)){ /*fresh*/
                alarm_unset(msgid, ALARM_UDPTIMEOUT);
                pthread_mutex_unlock(&(statusdb[msgid].mutex));
                continue;
            }
            alarm_set(msgid, ALARM_UDPTIMEOUT);
            pthread_mutex_unlock(&(statusdb[msgid].mutex));
        }/* end for msgid*/
        sleep(1);
    } /* end while 1*/
    return NULL;
}


/*
** housekeeping threads: timetoforget_loop, alarmsilencer_loop
**
*/

static void * timetoforget_loop( void * arg)
{
    int msgid;
    struct timespec deadline;
    int retval;
    char buff[FSLATENCY_HOSTNAME_LEN + FSLATENCY_TEXT_LEN];

    while(1){
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec -= opt.timetoforget;
        for(msgid = 0; msgid < opt.maxclient; msgid++){
            /* some quickie without lock */
            if( timespec_zero(&(statusdb[msgid].lastarrival))){ /* empty slot*/
                continue;
            }
            if( timespec_gt(&(statusdb[msgid].lastarrival), &deadline)){ /*fresh*/
                continue;
            }
            pthread_mutex_lock(&global_addremove_lock);
            clock_gettime(CLOCK_REALTIME, &deadline); /* ujra kell kerni, mert a lock() barmeddig tarthat */
            deadline.tv_sec -= opt.timetoforget;
            /* itt nem lehet empty, mert ez az egyetlen thread, ami torol.
            De lehet fresh, mert lehet, hogy ido kozben a receiver rakott bele.
            */
            if( timespec_gt(&(statusdb[msgid].lastarrival), &deadline)){ /*fresh*/
                pthread_mutex_unlock(&global_addremove_lock);
                continue;
            }
            retval = nameregistry_getbyid(&namedb, msgid, buff);
            if( -1 == retval){
                dprintf(2 /*stderr*/, "Error: programing flow error: namedb does not contain an entry for statusdb msgid=%d\n. Clear this orphaned statusdb entry.\n", msgid);
                statusentry_clear(statusdb + msgid);
            } else {
                dprintf(2 /*stderr*/, "Notice: timetoforget, client removed from database. msgid=%d hostname=%.*s text=%.*s\n",
                msgid, FSLATENCY_HOSTNAME_LEN, buff, FSLATENCY_TEXT_LEN, buff+FSLATENCY_HOSTNAME_LEN);
                /* clear it */
                statusentry_clear(statusdb + msgid);
                retval = nameregistry_removebyid(&namedb, msgid);
                if( -1 == retval){
                    dprintf(2 /*stderr*/, "Error: programing flow error: possibly inconsistent namedb. %s %d\n", __FILE__, __LINE__);
                }
            }
            pthread_mutex_unlock(&global_addremove_lock);
        } /* end for msgid */
        sleep(1);
    } /* end while 1 */
    return NULL;
}



static void * alarmsilencer_loop( void * arg)
/* only this function switches off alarms. All others just set. */
{
    int msgid;
    struct timespec deadline;
    int some_alarm;

    while(1){
        clock_gettime(CLOCK_REALTIME, &deadline);
        deadline.tv_sec -= opt.alarmtimeout;
        some_alarm = 0;
        for(msgid = 0; msgid < opt.maxclient; msgid++){
            /* some quickie without lock */
            if( timespec_zero(&(statusdb[msgid].lastarrival))){ /* empty slot*/
                continue;
            }
            if( opt.debug > 2){
                dprintf(2, "DEBUG in alarmsilencer_loop global_alarmstatus=%d msgid=%d lastalarmtime=%ld.%09ld deadline=%ld.%09ld alarm=%d\n",
                    global_alarmstatus, msgid, statusdb[msgid].lastalarmtime.tv_sec, statusdb[msgid].lastalarmtime.tv_nsec,
                    deadline.tv_sec, deadline.tv_nsec, statusdb[msgid].alarm);
            }
            pthread_mutex_lock(&(statusdb[msgid].mutex));
            clock_gettime(CLOCK_REALTIME, &deadline);
            deadline.tv_sec -= opt.alarmtimeout;
            if( timespec_gt(&(statusdb[msgid].lastalarmtime), &deadline)){
                pthread_mutex_unlock(&(statusdb[msgid].mutex));
                some_alarm = 1;
                continue;
            }
            if(opt.debug >1){
                dprintf(2, "DEBUG alarm status cleared for msgid=%d\n", msgid);
            }
            alarm_clear(msgid);
            pthread_mutex_unlock(&(statusdb[msgid].mutex));
        } /* end for msgid */
        /* if there no more alarm, but global_alarmstatus is set, clear it.*/
        pthread_mutex_lock(&global_alarmstatus_lock);
        if( !some_alarm && global_alarmstatus){
            dprintf(2, "Info: global status set to normal.\n");
            global_alarmstatus = 0;
            pthread_cond_signal(&global_normalstatus_cond);
        }
        pthread_mutex_unlock(&global_alarmstatus_lock);
        sleep(1);
    }
    return NULL;
}



/*
** periodic reporting loops: normalstatus_loop, alarmstatus_loop
**
**
*/

void * normalstatus_loop(void *arg)
{
    time_t tmp;
    char timebuff[TIMEFORMAT_LEN]; /* "2025-01-31T14:45:20+01:00" */

    while(1){
        sleep(opt.statusperiod);
        pthread_mutex_lock(&global_alarmstatus_lock);
        if( global_alarmstatus){
            pthread_cond_wait(&global_normalstatus_cond, &global_alarmstatus_lock);
        }
        tmp = time(NULL);
        strftime(timebuff, sizeof(timebuff), TIMEFORMAT, localtime(&tmp));
        pthread_mutex_lock(&global_stat_lock);
        dprintf(1, "%s Status: normal. Clients: %lu ln_ltncy:(N:%lu min:%f max:%f avg:%f std:%f)\n",
            timebuff, namedb.used,
            global_stat.sumN,global_stat.minx, global_stat.maxx, global_stat.mean, global_stat.std);
        pthread_mutex_unlock(&global_stat_lock);
        pthread_mutex_unlock(&global_alarmstatus_lock);

    }
}


void * alarmstatus_loop(void *arg)
{
    time_t tmp;
    char timebuff[TIMEFORMAT_LEN]; /* "2025-01-31T14:45:20+01:00" */
    unsigned int cnt_statlow, cnt_stathigh, cnt_empty, cnt_udptmo, cnt_alarm;
    int msgid;

    while(1){
        sleep(opt.alarmstatusperiod);
        pthread_mutex_lock(&global_alarmstatus_lock);
        if( !global_alarmstatus){
            pthread_cond_wait(&global_alarmstatus_cond, &global_alarmstatus_lock);
        }
        cnt_alarm = cnt_statlow = cnt_stathigh = cnt_empty = cnt_udptmo = 0;
        for(msgid = 0; msgid < opt.maxclient; msgid++){
            if( statusdb[msgid].alarm){
                cnt_alarm++;
            }
            if( statusdb[msgid].alarm & ALARM_STATISTICALALARM_LOW){
                cnt_statlow++;
            }
            if( statusdb[msgid].alarm & ALARM_STATISTICALALARM_HIGH){
                cnt_stathigh ++;
            }
            if( statusdb[msgid].alarm & ALARM_STATISTICALALARM_EMPTYDATABLOCK){
                cnt_empty++;
            }
            if( statusdb[msgid].alarm & ALARM_UDPTIMEOUT){
                cnt_udptmo++;
            }
        }
        tmp = time(NULL);
        strftime(timebuff, sizeof(timebuff), TIMEFORMAT, localtime(&tmp));
        pthread_mutex_lock(&global_stat_lock);
        dprintf(1, "%s ALARM Clients: %lu w/alarms: %d (ltncy lo:%d ltncy hi:%d stuck:%d lost:%d) ln_ltncy:(N:%lu min:%f max:%f avg:%f std:%f)\n",
            timebuff, namedb.used,
            cnt_alarm, cnt_statlow, cnt_stathigh, cnt_empty, cnt_udptmo,
            global_stat.sumN, global_stat.minx, global_stat.maxx, global_stat.mean, global_stat.std);
        pthread_mutex_unlock(&global_stat_lock);
        pthread_mutex_unlock(&global_alarmstatus_lock);
    }
}


void * graphite_loop(void *arg)
/* send status and data to graphite server in graphithe plaintext input format*/
{
    time_t curtime;
    unsigned int cnt_statlow, cnt_stathigh, cnt_empty, cnt_udptmo, cnt_alarm;
    double minx, maxx, mean, std;
    uint64_t sumN;
    int msgid;
    int retval;
    int gfd;

    while(1){
        sleep(60);
        curtime = time(NULL);
        cnt_alarm = cnt_statlow = cnt_stathigh = cnt_empty = cnt_udptmo = 0;
        for(msgid = 0; msgid < opt.maxclient; msgid++){
            if( statusdb[msgid].alarm){
                cnt_alarm++;
            }
            if( statusdb[msgid].alarm & ALARM_STATISTICALALARM_LOW){
                cnt_statlow++;
            }
            if( statusdb[msgid].alarm & ALARM_STATISTICALALARM_HIGH){
                cnt_stathigh ++;
            }
            if( statusdb[msgid].alarm & ALARM_STATISTICALALARM_EMPTYDATABLOCK){
                cnt_empty++;
            }
            if( statusdb[msgid].alarm & ALARM_UDPTIMEOUT){
                cnt_udptmo++;
            }
        }
        pthread_mutex_lock(&global_stat_lock);
        minx = global_stat.minx;
        maxx = global_stat.maxx;
        mean = global_stat.mean;
        std = global_stat.std;
        sumN = global_stat.sumN;
        pthread_mutex_unlock(&global_stat_lock);


        if( NULL != opt.graphiteip){
            gfd = socket(AF_INET, SOCK_STREAM, 0);
            if( -1 == gfd){
                perror("Error: cannot allocate socket to graphite");
                continue;
            }

            retval = connect(gfd, (struct sockaddr *) &(opt.graphiteaddr), sizeof(opt.graphiteaddr));
            if( -1 == retval){
                perror("Error: cannot connect to graphite");
                close(gfd);
                continue;
            }
            if( opt.debug >1){
                dprintf(2, "DEBUG graphite connection established to %s:%u via fd=%d\n", 
                    inet_ntoa(opt.graphiteaddr.sin_addr), ntohs(opt.graphiteaddr.sin_port), gfd);
            }
        }else{
            gfd = 1;
        }

        dprintf(gfd, "%s.totalclients %lu %ld\n", opt.graphitebase, namedb.used, curtime);
        dprintf(gfd, "%s.alarmedclients %u %ld\n", opt.graphitebase, cnt_alarm, curtime);
        dprintf(gfd, "%s.latencylow %u %ld\n", opt.graphitebase, cnt_statlow, curtime);
        dprintf(gfd, "%s.latencyhigh %u %ld\n", opt.graphitebase, cnt_stathigh, curtime);
        dprintf(gfd, "%s.stuckedclients %u %ld\n", opt.graphitebase, cnt_empty, curtime);
        dprintf(gfd, "%s.lostclients %u %ld\n", opt.graphitebase, cnt_udptmo, curtime);
        dprintf(gfd, "%s.ln_latency.datapoints %lu %ld\n", opt.graphitebase, sumN, curtime);
        dprintf(gfd, "%s.ln_latency.min %f %ld\n", opt.graphitebase, minx, curtime);
        dprintf(gfd, "%s.ln_latency.max %f %ld\n", opt.graphitebase, maxx, curtime);
        dprintf(gfd, "%s.ln_latency.mean %f %ld\n", opt.graphitebase, mean, curtime);
        dprintf(gfd, "%s.ln_latency.std %f %ld\n", opt.graphitebase, std, curtime);
        if(  NULL != opt.graphiteip){
            shutdown(gfd, SHUT_RDWR);
            close(gfd);
        }
    }
    return NULL;
}

/*
** receiver_loop (not a child threaded one)
**  implements UDP socket receiver handling via select()
**  based on https://stackoverflow.com/questions/15592089/implementing-udp-sockets-with-select-in-c
**  does not return
**
*/
void receiver_loop(int sfd)
{
    struct messageblock mymessageblock;
    struct datablock lastdatablock;
    struct timespec rectime;
    int msgid;
    size_t retsize;
    int retval;
    int i;

    while(1){
        retsize = recv(sfd, &mymessageblock, sizeof(mymessageblock), 0);
        if( retsize != sizeof(mymessageblock)){
            if(opt.debug){
                dprintf(2, "DEBUG received packed dropped because of wrong size.\n");
            }
            continue; /*silently drop*/
        }
        clock_gettime(CLOCK_REALTIME, &rectime);
        if( opt.debug > 2  ){ /* undocumented --debug=3 */
            dprintf(2, "Received:\n");
            dprintf(2, "  magic %s\n", mymessageblock.magic);
            dprintf(2, "  hostname %.*s\n", FSLATENCY_HOSTNAME_LEN, mymessageblock.hostname);
            dprintf(2, "  text %.*s\n", FSLATENCY_TEXT_LEN, mymessageblock.text);
            dprintf(2, "  version: %d.%d\n", mymessageblock.major, mymessageblock.minor);
            dprintf(2, "  precision: %ld.%09ld sec\n", mymessageblock.precision.tv_sec, mymessageblock.precision.tv_nsec);
            datablock_print(&mymessageblock.datablockarray[0]);
            datablock_print(&mymessageblock.datablockarray[1]);
        }
        /* magic and version processing */
        if( (FSLATENCY_VERSION_MAJOR != mymessageblock.major) || (FSLATENCY_VERSION_MINOR != mymessageblock.minor)){
            if(opt.debug){
                dprintf(2, "DEBUG received packed dropped because of wrong version. Requires: %d.%d received: %d.%d\n",
                    FSLATENCY_VERSION_MAJOR,FSLATENCY_VERSION_MINOR, mymessageblock.major, mymessageblock.minor);
            }
            continue; /*silently drop*/
        }
        if( 0!= memcmp(mymessageblock.magic, FSLATENCY_MAGIC, FSLATENCY_MAGIC_LEN)){
            if(opt.debug){
                dprintf(2, "DEBUG received packed dropped because of wrong magic.\n");
            }
            continue; /*silently drop*/
        }

        pthread_mutex_lock(&global_addremove_lock);
        msgid = nameregistry_find(&namedb, mymessageblock.hostname); /* hostname+text both */
        if( -1 == msgid){
            /* new client */
            msgid = nameregistry_add(&namedb, mymessageblock.hostname); /* hostname+text both */
            if( -1 == msgid){
                dprintf(2 /*stderr*/, "Warning: received packed from hostname=%.*s text=%.*s is dropped because nameregistry is full.\n",
                    FSLATENCY_HOSTNAME_LEN, mymessageblock.hostname, FSLATENCY_TEXT_LEN, mymessageblock.text);
                pthread_mutex_unlock(&global_addremove_lock);
                continue;
            }
            dprintf(2 /*stderr*/, "Info: client added. msgid=%d hostname=%.*s text=%.*s\n",
                msgid, FSLATENCY_HOSTNAME_LEN, mymessageblock.hostname, FSLATENCY_TEXT_LEN, mymessageblock.text);
            pthread_mutex_lock(&(statusdb[msgid].mutex));
            statusdb[msgid].lastarrival = rectime;
            alarm_clear(msgid); /* new client: no alarm */
            for( i = FSLATENCY_DATABLOCKARRAY_LEN-1; i>=0 ; i--){
                if( 0 != mymessageblock.datablockarray[i].measurementcount){
                    /* it won't add empty datablocks */
                    ringbuffer_add(&(statusdb[msgid].datablockbuffer), &(mymessageblock.datablockarray[i]));
                }
            }
            pthread_mutex_unlock(&(statusdb[msgid].mutex));
        } else { /* end if new entry added. else: kown entry will be updated*/
            if( opt.debug >1){
                dprintf(2, "DEBUG known client msgid=%d\n", msgid);
            }

            /* note received packet */
            pthread_mutex_lock(&(statusdb[msgid].mutex));
            statusdb[msgid].lastarrival = rectime;
            retval = ringbuffer_getlast(&(statusdb[msgid].datablockbuffer), &lastdatablock);
            if( -1 == retval){ /* there was no datablock in th ringbuffer, but it is a known client.  */
                /* unmature but known client */
                dprintf(2 /*stderr*/, "Warning: Why is the buffer for the known client empty? msgid=%d\n", msgid);
                if( 0 != mymessageblock.datablockarray[i].measurementcount){
                    /* it won't add empty datablocks */
                    ringbuffer_add(&(statusdb[msgid].datablockbuffer), &(mymessageblock.datablockarray[i]));
                }
            } else {
                /*mature and kown client */
                for( i = FSLATENCY_DATABLOCKARRAY_LEN-1; i>=0 ; i--){
                    /* autmatically discard out-of-order packets. And automatically replace the data of dropped packages.
                       That's why we have repeated datablocks in each UDP packet. */
                    if( timespec_gt(&(mymessageblock.datablockarray[i].starttime), &(lastdatablock.starttime))){
                         ringbuffer_add(&(statusdb[msgid].datablockbuffer), &(mymessageblock.datablockarray[i]));
                    }
                }
                /* the "empty datablock alarm" is set only for mature and known client */
                if( mymessageblock.datablockarray[0].min == FSLATENCY_EXTREMEBIGINTERVAL){
                    alarm_set(msgid, ALARM_STATISTICALALARM_EMPTYDATABLOCK);
                } else {
                    alarm_unset(msgid, ALARM_STATISTICALALARM_EMPTYDATABLOCK);
                }
            }
            if( opt.debug > 1){
                dprintf(2, "DEBUG receiver: this msgid=%d 's ringbufer size: %lu of %lu\n",
                        msgid, statusdb[msgid].datablockbuffer.len, statusdb[msgid].datablockbuffer.bufferlen);
            }
            pthread_mutex_unlock(&(statusdb[msgid].mutex));
        }
        pthread_mutex_unlock(&global_addremove_lock);
        /* ... */

    } /* end while1 */
}


/*
**
**   M A I N
**
*/

int main(int argc, char * argv[])
{
    int sfd;
    int retval;
    struct sockaddr_in serversockstruct;
    pthread_t statistical_alarmer_thread;
    pthread_t timetoforget_thread;
    pthread_t alarmsilencer_thread;
    pthread_t udptimeout_thread;
    pthread_t alarmstatus_thread;
    pthread_t normalstatus_thread;
    pthread_t graphite_thread;

    /* parameter processing */
    init_opt();
    retval = parse_opt(argc, argv);
    if( retval != 0 ){
        return retval;
    }

    /* socket manipulation */

    if( NULL != opt.graphitebase && NULL != opt.graphiteip){
        opt.graphiteaddr.sin_family = AF_INET;
        opt.graphiteaddr.sin_port = htons(opt.graphiteport);
        retval = inet_aton(opt.graphiteip, &(opt.graphiteaddr.sin_addr));
        if( 0 == retval ){
            dprintf(2 /*stderr*/, "Error: invalid graphiteip \"%s\"\n", opt.graphiteip);
            return 2;
        }
    }

    serversockstruct.sin_family = AF_INET;
    serversockstruct.sin_port = htons(opt.port);
    retval = inet_aton(opt.bind, &(serversockstruct.sin_addr));
    if( 0 == retval ){
        dprintf(2 /*stderr*/, "Error: invalid bindip \"%s\"\n", opt.bind);
        return 2;
    }

    sfd = socket(AF_INET, SOCK_DGRAM, 0);
    if( -1 == sfd){
        perror("Error: cannot allocate socket");
        return 1;
    }

    retval = bind(sfd, (struct sockaddr *) &serversockstruct, sizeof(serversockstruct));
    if( -1 == retval){
        perror("Error: cannot bind");
        return 1;
    }

    /* initializations */

    retval = init_databases(opt.maxclient);
    if( -1 == retval){
        dprintf(2 /*stderr*/, "Error: cannot initialize databases\n");
        return 1;
    }
    if( opt.debug > 2){
        dprintf(2, "DEBUG initialization done for %d clients\n", opt.maxclient);
    }

    /* various threads */

    retval = pthread_create(&statistical_alarmer_thread, NULL, &statistical_alarmer_loop, NULL);
        if( 0 != retval){
        dprintf(2 /*stderr*/, "Error: cannot create statistical_alarmer thread. Errno:%d\n", retval);
        return 2;
    }
    if( opt.debug > 2){
        dprintf(2, "DEBUG thread start: statistical_alarmer\n");
    }

    retval = pthread_create(&timetoforget_thread, NULL, &timetoforget_loop, NULL);
        if( 0 != retval){
        dprintf(2 /*stderr*/, "Error: cannot create timetoforget (client auto remove) thread. Errno:%d\n", retval);
        return 2;
    }
    if( opt.debug > 2){
        dprintf(2, "DEBUG thread start: timetoforget\n");
    }

    retval = pthread_create(&alarmsilencer_thread, NULL, &alarmsilencer_loop, NULL);
        if( 0 != retval){
        dprintf(2 /*stderr*/, "Error: cannot create alarmsilencer (obsoleted alarm purger) thread. Errno:%d\n", retval);
        return 2;
    }
    if( opt.debug > 2){
        dprintf(2, "DEBUG thread start: alarmsilencer\n");
    }

    retval = pthread_create(&udptimeout_thread, NULL, &udptimeout_loop, NULL);
        if( 0 != retval){
        dprintf(2 /*stderr*/, "Error: cannot create udptimeout detection thread. Errno:%d\n", retval);
        return 2;
    }
    if( opt.debug > 2){
        dprintf(2, "DEBUG thread start: udptimeout\n");
    }

    retval = pthread_create(&alarmstatus_thread, NULL, &alarmstatus_loop, NULL);
        if( 0 != retval){
        dprintf(2 /*stderr*/, "Error: cannot create thread to report alarm periodically. Errno:%d\n", retval);
        return 2;
    }
    if( opt.debug > 2){
        dprintf(2, "DEBUG thread start: alarmstatus\n");
    }

    retval = pthread_create(&normalstatus_thread, NULL, &normalstatus_loop, NULL);
        if( 0 != retval){
        dprintf(2 /*stderr*/, "Error: cannot create thread to report normal status periodically. Errno:%d\n", retval);
        return 2;
    }
    if( opt.debug > 2){
        dprintf(2, "DEBUG thread start: normalstatus\n");
    }

    if( NULL != opt.graphitebase){
        retval = pthread_create(&graphite_thread, NULL, &graphite_loop, NULL);
            if( 0 != retval){
            dprintf(2 /*stderr*/, "Error: cannot create thread to report status to graphite. Errno:%d\n", retval);
            return 2;
        }
        if( opt.debug > 2){
            dprintf(2, "DEBUG thread start: graphite\n");
        }


    }


    /* Locking all memory for emergency running. This program should run even if the system disk fails. */
    if( !opt.nomemlock ){
        sleep(1);
        retval = mlockall(MCL_CURRENT);
        if( retval < 0){
            perror("Error: cannot memlockall");
            return 2;
        }
    }

    /* starting receiver */
    receiver_loop(sfd);

    close(sfd);
    return 0;
}
