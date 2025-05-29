/* test for condition variables */



#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>


static int global_alarmstate;
static pthread_mutex_t global_alarmstatus_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t global_alarmstatus_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t global_normalstatus_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t global_normalstatus_cond = PTHREAD_COND_INITIALIZER;


static void * loop_normal(void *p)
{
    while(1){
        sleep(3);
        pthread_mutex_lock(&global_normalstatus_lock);
        if( global_alarmstate ){
            pthread_cond_wait(&global_normalstatus_cond, &global_normalstatus_lock);
        }
        printf("normal\n"); fflush(stdout);
        pthread_mutex_unlock(&global_normalstatus_lock);
    }
    return NULL;
}


static void * loop_alarm(void *p)
{
    while(1){
        sleep(3);
        pthread_mutex_lock(&global_normalstatus_lock);
        if( !global_alarmstate ){
            pthread_cond_wait(&global_alarmstatus_cond, &global_normalstatus_lock);
        }
        printf("alarm\n"); fflush(stdout);
        pthread_mutex_unlock(&global_normalstatus_lock);
    }
    return NULL;
}

int main(void)
{
    char buff[100];
    pthread_t thread_normal, thread_alarm;

    global_alarmstate = 0;
    pthread_create(&thread_normal, NULL, loop_normal, NULL);
    pthread_create(&thread_alarm, NULL, loop_alarm, NULL);

    printf("Prompt [n/a]:\n");

    while(1){
        fgets(buff, 100, stdin);
        switch( buff[0]){
            case 'n':
                printf("waiting for normlock\n");
                pthread_mutex_lock(&global_normalstatus_lock);
                printf("Switch norm\n"); fflush(stdout);
                global_alarmstate = 0;
                pthread_cond_signal(&global_normalstatus_cond);
                pthread_mutex_unlock(&global_normalstatus_lock);
                break;
            case 'a':
                printf("waiting for normlock\n");
                pthread_mutex_lock(&global_normalstatus_lock);
                printf("Switch to ala\n"); fflush(stdout);
                global_alarmstate = 1;
                pthread_cond_signal(&global_alarmstatus_cond);
                pthread_mutex_unlock(&global_normalstatus_lock);
                break;
            default:
                printf("Kilép\n"); fflush(stdout);
                return 0;
        }
    }/* end while 1 */
    return 0;
}
