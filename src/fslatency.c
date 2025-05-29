/*
** fslatency.c
**
** measure filesystem (disk) write latency for a long period
** tested at Ubuntu 24.04 LTS
**
** gcc --static -Wall -o fslatency fslatency.c -l pthread
** strip fslatency
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

#define AGENT_VERSION_MAJOR 0
#define AGENT_VERSION_MINOR 3


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
#include <math.h>

#include "datablock.h"

/*
** Cyclic buffer routines
*/

struct bufferentry {
    struct timespec begtime;
    struct timespec endtime;
};

#define RINGBUFFER_ENTRY_TYPE struct bufferentry
#define RINGBUFFER_THREADSAFE
#include "ringbuffer.inc"

static struct ringbuffer bufferhead;
static struct ringbuffer bufferhead_copy;



const struct timespec TENTHSECOND = { 0, 100000000 }; /* sec, nanosec */

/* see man statfs(2) */
#define BTRFS_SUPER_MAGIC     0x9123683e
#define BTRFS_TEST_MAGIC      0x73727279
#define EXT_SUPER_MAGIC       0x137d     /* Linux 2.0 and earlier */
#define EXT2_OLD_SUPER_MAGIC  0xef51
#define EXT2_SUPER_MAGIC      0xef53
#define EXT3_SUPER_MAGIC      0xef53
#define EXT4_SUPER_MAGIC      0xef53
#define HFS_SUPER_MAGIC       0x4244
#define HPFS_SUPER_MAGIC      0xf995e849
#define JFFS2_SUPER_MAGIC     0x72b6
#define JFS_SUPER_MAGIC       0x3153464a
#define MINIX_SUPER_MAGIC     0x137f     /* original minix FS */
#define MINIX_SUPER_MAGIC2    0x138f     /* 30 char minix FS */
#define MINIX2_SUPER_MAGIC    0x2468     /* minix V2 FS */
#define MINIX2_SUPER_MAGIC2   0x2478     /* minix V2 FS, 30 char names */
#define MINIX3_SUPER_MAGIC    0x4d5a     /* minix V3 FS, 60 char names */
#define MSDOS_SUPER_MAGIC     0x4d44
#define NTFS_SB_MAGIC         0x5346544e
#define REISERFS_SUPER_MAGIC  0x52654973
#define XFS_SUPER_MAGIC       0x58465342
#define VXFS_SUPER_MAGIC      0xa501fcf5
#define ZFS_SUPER_MAGIC       0x2fc12fc1  /*https://bugs.freebsd.org/bugzilla/show_bug.cgi?id=220086 https://github.com/minio/minio/blob/master/internal/disk/type_linux.go */

int is_kown_local_fs(int f_type)
{
    return (
          (f_type == BTRFS_SUPER_MAGIC    )
        | (f_type == BTRFS_TEST_MAGIC     )
        | (f_type == EXT_SUPER_MAGIC      )
        | (f_type == EXT2_OLD_SUPER_MAGIC )
        | (f_type == EXT2_SUPER_MAGIC     )
        | (f_type == EXT3_SUPER_MAGIC     )
        | (f_type == EXT4_SUPER_MAGIC     )
        | (f_type == HFS_SUPER_MAGIC      )
        | (f_type == HPFS_SUPER_MAGIC     )
        | (f_type == JFFS2_SUPER_MAGIC    )
        | (f_type == JFS_SUPER_MAGIC      )
        | (f_type == MINIX_SUPER_MAGIC    )
        | (f_type == MINIX_SUPER_MAGIC2   )
        | (f_type == MINIX2_SUPER_MAGIC   )
        | (f_type == MINIX2_SUPER_MAGIC2  )
        | (f_type == MINIX3_SUPER_MAGIC   )
        | (f_type == MSDOS_SUPER_MAGIC    )
        | (f_type == NTFS_SB_MAGIC        )
        | (f_type == REISERFS_SUPER_MAGIC )
        | (f_type == XFS_SUPER_MAGIC      )
        | (f_type == VXFS_SUPER_MAGIC     )
        | (f_type == ZFS_SUPER_MAGIC      )
    );
}


/*
** time differencial utility function: returns doublefloat (seconds)
*/
static inline double diff_timespec_double(const struct timespec *endtime, const struct timespec *begtime) {
  return (double)(endtime->tv_sec - begtime->tv_sec)  +
            (double)(endtime->tv_nsec - begtime->tv_nsec)/1000000000.0;
}


/*
** command-line option processing.
** There is a static, global opt struct.
*/

#define OPT_SERVERIP 1
#define OPT_SERVERPORT 2
#define OPT_TEXT 3
#define OPT_FILE 4
#define OPT_NOCHECKFS 5
#define OPT_NOMEMLOCK 6
#define OPT_DEBUG 7
#define OPT_VERSION 101

struct option myoptions[] = {
/* name, has_arg, flag, val */
 { "serverip", 1, NULL, OPT_SERVERIP},    /* mandatory */
 { "serverport", 1, NULL, OPT_SERVERPORT},/* optional. Default is 57005 */
 { "text", 1, NULL, OPT_TEXT},            /* optional. Defualt is "" */
 { "file", 1, NULL, OPT_FILE},            /* mandatory */
 { "nocheckfs", 0, NULL, OPT_NOCHECKFS},  /* optional */
 { "nomemlock", 0, NULL, OPT_NOMEMLOCK},  /* optional */
 { "debug", 0, NULL, OPT_DEBUG},          /* optional */
 { "version", 0, NULL, OPT_VERSION},      /* optional */
 { NULL, 0, NULL, 0}
};


static struct _opt {
    char * serverip;
    char * serverport;
    char * text;
    char * filename;
    char * hostname;
    unsigned int nocheckfs;
    unsigned int nomemlock;
    unsigned int debug;
} opt;


void help()
{
    puts("Usage: fslatency --serverip a.b.c.d [--serverport PORT] --file PATH");
    puts("   [--text NAME] [--nocheckfs] [--nomemlock] [--debug] [--version]");
}


static void init_opt(void)
{
    int retval;

    opt.serverip = NULL;
    opt.serverport = "57005";
    opt.text = "";
    opt.filename =  NULL;
    opt.hostname = (char*) malloc(FSLATENCY_HOSTNAME_LEN);
    retval = gethostname(opt.hostname, FSLATENCY_HOSTNAME_LEN);
    if( -1 == retval){
        perror("Error: cannot get hostname");
        exit(3);
    }

    opt.nocheckfs = 0; /*False*/
    opt.nomemlock = 0; /*False*/
    opt.debug = 0; /*False*/
}


static int parse_opt(int argc, char * argv[])
{
    int optcode;

    /* parameter processing */
    /* --ip a.b.c.d --port PORT --text "FOO" --file  "path" */
    while( ( optcode = getopt_long(argc, argv, "", myoptions, NULL)) >= 0 ){
        switch( optcode){
            case '?':
                dprintf(2 /*stderr*/, "Error: unkown command line option\n");
                help();
                return 2;
            case OPT_SERVERIP:
                opt.serverip = strdup(optarg);
                break;
            case OPT_SERVERPORT:
                opt.serverport = strdup(optarg);
                break;
            case OPT_TEXT:
                opt.text = strdup(optarg);
                break;
            case OPT_FILE:
                opt.filename = strdup(optarg);
                break;
            case OPT_NOCHECKFS:
                opt.nocheckfs = 1;
                break;
            case OPT_NOMEMLOCK:
                opt.nomemlock = 1;
                break;
            case OPT_DEBUG:
                opt.debug = 1;
                break;
            case OPT_VERSION:
                dprintf(2, "fslatency %d.%d. UDP version %d.%d\n", AGENT_VERSION_MAJOR, AGENT_VERSION_MINOR, FSLATENCY_VERSION_MAJOR, FSLATENCY_VERSION_MINOR);
                exit(0);
                break;
            default:
                dprintf(2 /*stderr*/, "Error: command line processing error (getopt returns %d)\n", optcode);
                help();
                return 2;
        } /* end switch */
    } /* end while getopt */

    /* check mandatory parameter presence */
    if( NULL == opt.serverip){
        dprintf(2 /*stderr*/, "Error: you must specify a --serverip  (IPv4 dotted form)\n");
        return 2;
    }
    if( NULL == opt.filename ){
        dprintf(2 /*stderr*/, "Error: you must specify a --file  (filepath to a local filesystem)\n");
        return 2;
    }

    /* etc */
    if( strlen(opt.text) > FSLATENCY_TEXT_LEN){
        dprintf(2 /*stderr*/, "Warning: too long --text. Truncated to %u char.\n", FSLATENCY_TEXT_LEN);
        //opt.text[FSLATENCY_TEXT_LEN] = 0;
    }


    if( opt.debug){
        printf("DEBUG Options:\n");
        printf("    --serverip %s\n", opt.serverip);
        printf("    --serverport %s\n", opt.serverport);
        printf("    --text \"%s\"\n", opt.text);
        printf("    --file \"%s\"\n", opt.filename);
        printf("    --nocheckfs %d\n", opt.nocheckfs);
        printf("    --nomemlock %d\n", opt.nomemlock);
        printf("    --debug %d\n", opt.debug);
        printf("  hostname %s\n", opt.hostname);
    }

    return 0;
}


/*
** measuring loop: thread entry point
*/
int * measuring(int * fdp)
{
    static int retval;
    struct bufferentry timeentry;
    size_t bufflen = 300;
    char buff[bufflen];

    if( opt.debug){
        printf("Info: infinite measuring loop starts. Press ctrl-c when bored\n");
    }
    while(1){

        clock_gettime(CLOCK_REALTIME, &(timeentry.begtime));
        //printf("DEBUG new sleep at %ld.%09ld\n", begtime.tv_sec, begtime.tv_nsec);

        snprintf(buff, bufflen, "%9ld.%08ld           \n", timeentry.begtime.tv_sec, timeentry.begtime.tv_nsec/10);

        retval = lseek(*fdp, 0, SEEK_SET);
        if( retval < 0){
            perror("Error: cannot lseek");
            retval = 2;
            return &retval;
        }
        retval = write(*fdp, buff, 32);
        if( retval < 0){
            perror("Error: cannot write");
            retval = 2;
            return &retval;
        }
        retval = fsync(*fdp);
        if( retval < 0){
            perror("Error: cannot fsync");
            retval = 2;
            return &retval;
        }

        clock_gettime(CLOCK_REALTIME, &(timeentry.endtime));

        ringbuffer_add(&bufferhead, &timeentry);

        retval = nanosleep(&TENTHSECOND, NULL);
        if( retval < 0){
            perror("Error: cannot sleep");
            retval = 2;
            return &retval;
        }
    } /* end while 1 */

    retval = 0;
    return &retval; /* never reach */
}


/*
** Data sender loop: thread entry point
**
*/

struct datasenderarg {
    int socket;
    struct timespec precision;
};

int * datasender(struct datasenderarg * dsp)
{
    static int retval;
    double mint, maxt, sumx, sumxx;
    size_t i;
    struct datablock mydatablock;
    struct messageblock mymessageblock;
    mydatablock.measurementcount =0;
    mydatablock.starttime.tv_sec = 0;
    mydatablock.starttime.tv_nsec = 0;
    mydatablock.endtime.tv_sec = 0;
    mydatablock.endtime.tv_nsec = 0;
    mydatablock.min = 0.0;
    mydatablock.max = 0.0;
    mydatablock.sumx = 0.0;
    mydatablock.sumxx = 0.0;

    strncpy(mymessageblock.magic, FSLATENCY_MAGIC, FSLATENCY_MAGIC_LEN);
    strncpy(mymessageblock.hostname, opt.hostname, FSLATENCY_HOSTNAME_LEN);
    strncpy(mymessageblock.text, opt.text, FSLATENCY_TEXT_LEN);
    mymessageblock.major = FSLATENCY_VERSION_MAJOR;
    mymessageblock.minor = FSLATENCY_VERSION_MINOR;
    mymessageblock.precision = dsp->precision;
    for(i=0; i < FSLATENCY_DATABLOCKARRAY_LEN; i++){
        mymessageblock.datablockarray[i] = mydatablock;
    }

    while(1){
        sleep(1);
        ringbuffer_move(&bufferhead, &bufferhead_copy);

        /* Datablock:
            number of measurements (integer, bit)
            starttime struct timespec == 64 bit
            endtime struct timespec == 64 bit
            min (float 64bit)
            max (float 64bit)
            sumX (float 64bit) sum of all measurements in this interval can be used to calculate the average
            sumXX (float 64bit) sum of all measurements² in this interval can be used to calculate std deviation
        */
        mydatablock.measurementcount = bufferhead_copy.len;
        if( bufferhead_copy.len > 0){
            mydatablock.starttime = bufferhead_copy.buffer[0].begtime;
            mydatablock.endtime =  bufferhead_copy.buffer[bufferhead_copy.len-1].endtime;
        } else {
            mydatablock.starttime = mydatablock.endtime = (struct timespec) {0,0};
        }

        mint = FSLATENCY_EXTREMEBIGINTERVAL;
        maxt = -FSLATENCY_EXTREMEBIGINTERVAL;
        sumx = sumxx = 0.0;
        /* if len == 0, mint maxt sumx sumxx remain same.
        And this type of packet will be send. */
        for( i=0; i< bufferhead_copy.len; i++){
            double elapsedtime;
            /* some data manipulaion, see README.md */
            elapsedtime = diff_timespec_double(&(bufferhead_copy.buffer[i].endtime), &(bufferhead_copy.buffer[i].begtime));
            elapsedtime = log(elapsedtime * 1000 );  /* sec -> millisec */
            if( mint > elapsedtime){
                mint = elapsedtime;
            }
            if(maxt < elapsedtime){
                maxt = elapsedtime;
            }
            sumx += elapsedtime;
            sumxx += elapsedtime*elapsedtime;
        }
        mydatablock.min = mint;
        mydatablock.max = maxt;
        mydatablock.sumx = sumx;
        mydatablock.sumxx = sumxx;

        for(i=FSLATENCY_DATABLOCKARRAY_LEN-1; i > 0; i--){
             mymessageblock.datablockarray[i] = mymessageblock.datablockarray[i-1];
        }
        mymessageblock.datablockarray[0] = mydatablock;


        if( opt.debug){
            datablock_print( &mydatablock);
        }
        retval = send(dsp->socket, &mymessageblock, sizeof(mymessageblock), MSG_NOSIGNAL);
        if( -1 == retval ){
            if( opt.debug){
                perror("Warning: error in udp send()");
            }
        }

    } /* end while 1 */
    retval = 0;
    return &retval; /* never reach */
}



/*
**
**   M A I N
**
*/

int main(int argc, char * argv[])
{
    int fd, sfd;
    int retval;
    struct stat statit;
    struct statfs statfsit;
    struct datasenderarg dsarg;
    struct sockaddr_in clientsockstruct;
    pthread_t measuringthread, datasenderthread;

    /* parameter processing */
    init_opt();
    retval = parse_opt(argc, argv);
    if( retval != 0 ){
        return retval;
    }

    /* cyclic buffer initialization */
    retval = ringbuffer_init(&bufferhead, 503); /* 503 is prime, I like the primes */
    if( 0 != retval ){
        dprintf(2 /*stderr*/, "Error: no mem for buffer\n");
        return 2;
    }
    retval = ringbuffer_init(&bufferhead_copy, 503);
    if( 0 != retval ){
        dprintf(2 /*stderr*/, "Error: no mem for second buffer\n");
        return 2;
    }


    /* socket manipulation */
    clientsockstruct.sin_family = AF_INET;
    clientsockstruct.sin_port = htons(atoi(opt.serverport));
    if( 0 == clientsockstruct.sin_port){
        dprintf(2 /*stderr*/, "Error: invalid serverport \"%s\"\n", opt.serverport);
        return 2;
    }
    retval = inet_aton(opt.serverip, &(clientsockstruct.sin_addr));
    if( 0 == retval ){
        dprintf(2 /*stderr*/, "Error: invalid serverip \"%s\"\n", opt.serverip);
        return 2;
    }

    sfd = socket(AF_INET, SOCK_DGRAM, 0);
    if( -1 == sfd){
        perror("Error: cannot allocate socket");
        return 1;
    }
    retval = connect(sfd, (struct sockaddr *) &clientsockstruct, sizeof(clientsockstruct));
    if( -1 == retval){
        perror("Error: cannot connect to remote server");
        return 1;
    }


    /* mesuring file open and check */

    fd = open(opt.filename, O_WRONLY | O_CREAT | O_SYNC | O_DSYNC | O_NOATIME, S_IRWXU );
    if( fd < 0 ){
        perror("Error: File cannot create for write");
        return 1;
    }
    retval = fstat(fd, &statit);
    if( retval < 0){
        perror("Error: File cannot fstat");
        return 2;
    }
    if( !S_ISREG(statit.st_mode))
    {
        dprintf(2 /*stderr*/, "Error: The file is not a regular file.\n");
        return 2;
    }
    if( !opt.nocheckfs ){
        retval = fstatfs(fd, &statfsit);
        if( retval < 0){
            perror("Error: cannot determine filesystem type");
            return 2;
        }
        if( ! is_kown_local_fs(statfsit.f_type)){
            dprintf(2 /*stderr*/, "Error: unkown filesystem type 0x%X. This program is only for testing local filesystems. No NFS, CIFS nor tmpfs nor fuse.\n",
                (unsigned) statfsit.f_type);
            return 2;
        }
    }

    /* starting threads */

    retval = pthread_create(&measuringthread, NULL, (void * (*)(void *)) &measuring, &fd);
    if( 0 != retval){
        dprintf(2 /*stderr*/, "Error: cannot create measuring thread. Errno:%d\n", retval);
        return 2;
    }
    if( opt.debug ){
        printf("DEBUG measuring thread started\n");
    }

    clock_getres(CLOCK_REALTIME, &(dsarg.precision));
    if( opt.debug ){
        printf("DEBUG Time measuring precision: %ld nanoseconds\n", dsarg.precision.tv_nsec);
    }
    dsarg.socket = sfd;
    retval = pthread_create(&datasenderthread,
                            NULL,
                            (void * (*)(void *)) &datasender,
                            &dsarg);
    if( 0 != retval){
        dprintf(2 /*stderr*/, "Error: cannot create datasender thread. Errno:%d\n", retval);
        return 2;
    }
    if( opt.debug ){
        printf("DEBUG datasender thread started\n");
    }



    /* Locing all memory for emergency running. This program should run even if the system disk fails. */
    if( !opt.nomemlock ){
        sleep(1); /* stabilize thread creations and all initial paging events.*/
        retval = mlockall(MCL_CURRENT);
        if( retval < 0){
            perror("Error: cannot memlockall");
            return 2;
        }
    }

    /* just wait. forever. */
    retval = pthread_join(measuringthread, NULL);
        if( 0 != retval){
        dprintf(2 /*stderr*/, "Error: cannot wait/join measuring thread. Errno:%d\n", retval);
        return 2;
    }

    close(fd);
    return 0;
}
