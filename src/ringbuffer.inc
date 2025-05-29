/*
** ringbuffer.inc
**
** ringbuffer structure  implementations.
** optional posix thread safe.
**
**  The ringbuffer is a native C template structure (like C++ template class)
**
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

**
**  Usage example from a .c source code, non therad-safe:
**
** struct foobar { int a; char c; };
** #define RINGBUFFER_ENTRY_TYPE struct foobar
** #include "ringbuffer.inc"
**
**  Usage example from a .c source code, therad-safe:
**
** struct foobar { int a; char c; };
** #define RINGBUFFER_ENTRY_TYPE struct foobar
** #define RINGBUFFER_THREADSAFE
** #include "ringbuffer.inc"
**
**
*/


#ifndef RINGBUFFER_ENTRY_TYPE
#error "RINGBUFFER_ENTRY_TYPE must be defined! see ringbuffer.inc"
#endif


#include <stdlib.h>

/*
** Cyclic buffer == ring buffer == cyclic queue
**
**  init
**  free (uninplemented yet)
**  clear
**  add  # to the end
**  pop  # from the begining, using as queue
**  getlast
**  copy
**  move
*/


struct ringbuffer {
    size_t bufferlen;
    size_t len;
    size_t start;
    RINGBUFFER_ENTRY_TYPE * buffer;
#ifdef RINGBUFFER_THREADSAFE
    pthread_mutex_t mutex;
#endif
};


/*
**  ringbuffer_init
**      len should be positivive (no check)
**
*/

int ringbuffer_init( struct ringbuffer * head, size_t len)
{

    head->bufferlen = len;
    head->len = 0;
    head->start = 0;
    head->buffer = (RINGBUFFER_ENTRY_TYPE *) malloc( head->bufferlen * sizeof(RINGBUFFER_ENTRY_TYPE));
    if( NULL ==  head->buffer){
        return -1;
    }
    memset(head->buffer, 0xFE, head->bufferlen * sizeof(RINGBUFFER_ENTRY_TYPE)); /* some invalid magic */
#ifdef RINGBUFFER_THREADSAFE
    pthread_mutex_init(&(head->mutex), 0);
#endif
    return 0;
}

/*
**  ringbufer_clear
**      clear data but not free resources
**
*/
int ringbuffer_clear( struct ringbuffer * head)
{

#ifdef RINGBUFFER_THREADSAFE
    pthread_mutex_lock(&(head->mutex));
#endif
    head->len = 0;
    head->start = 0;
    memset(head->buffer, 0xFE, head->bufferlen * sizeof(RINGBUFFER_ENTRY_TYPE)); /* some invalid magic */
#ifdef RINGBUFFER_THREADSAFE
    pthread_mutex_unlock(&(head->mutex));
#endif
    return 0;
}


/*
**  ringbuffer_add
**      add an entry to the end of the ringbuffer
*/
void ringbuffer_add(struct ringbuffer * head, const RINGBUFFER_ENTRY_TYPE * entry )
{
    size_t nextentry;

#ifdef RINGBUFFER_THREADSAFE
    pthread_mutex_lock(&(head->mutex));
#endif

    nextentry = (head->start + head->len) % head->bufferlen;

    /*
    printf("buffer_add, start:%lu len:%lu, nextentry:%lu\n",
            head->start, head->len, nextentry);
    fflush(stdout);
    */

    if( head->len == head->bufferlen ) {
        /* cyclic buffer: drop the oldest */
        head->start = (head->start+1) % head->bufferlen;
    } else {
        head->len ++;
    }

    head->buffer[nextentry] = *entry;
#ifdef RINGBUFFER_THREADSAFE
    pthread_mutex_unlock(&(head->mutex));
#endif
}


/*
**  pop
**      get and remove an entry from the begening (oldest entry).
**      for using the ringbuffer as a queue
**  return -1 if ring was empty
**  return 0 if ring ok
*/
int ringbuffer_pop(struct ringbuffer * head, RINGBUFFER_ENTRY_TYPE * entry )
{
#ifdef RINGBUFFER_THREADSAFE
    pthread_mutex_lock(&(head->mutex));
#endif
    if( head->len < 1 ){
#ifdef RINGBUFFER_THREADSAFE
        pthread_mutex_unlock(&(head->mutex));
#endif
        return -1;
    }

    *entry = head->buffer[head->start];
    head->start = (head->start + 1) % head->bufferlen;
    head->len --;

#ifdef RINGBUFFER_THREADSAFE
    pthread_mutex_unlock(&(head->mutex));
#endif
    return 0;
}


/*
**  getlast
**      get last (latest added) element, and no change the ringbuffer.
**
**  return -1 if ring was empty
**  return 0 if ring ok
*/
int ringbuffer_getlast(struct ringbuffer * head, RINGBUFFER_ENTRY_TYPE * entry )
{
    size_t lastentry;

#ifdef RINGBUFFER_THREADSAFE
    pthread_mutex_lock(&(head->mutex));
#endif
    if( head->len < 1 ){
#ifdef RINGBUFFER_THREADSAFE
        pthread_mutex_unlock(&(head->mutex));
#endif
        return -1;
    }

    lastentry = (head->start + head->len - 1) % head->bufferlen;
    *entry = head->buffer[lastentry];

#ifdef RINGBUFFER_THREADSAFE
    pthread_mutex_unlock(&(head->mutex));
#endif
    return 0;
}


/*
** ringbuffer_copy
**   both 'from' and 'to' must be initalized
**   if 'from' is greater then coy only the newest entries to fit in 'to'
**   to is not cyclic: always start with 0
**
*/
void ringbuffer_copy(struct ringbuffer * from, struct ringbuffer * to)
{
    size_t firsthalf, secondhalf, dropsome_start;

#ifdef RINGBUFFER_THREADSAFE
    pthread_mutex_lock(&(to->mutex));
    pthread_mutex_lock(&(from->mutex));
#endif
    if( 0 != from->len){


        if( from->len > to->bufferlen){  /* silently drop some from the begining */
            dropsome_start = (from->start + (from->len - to->bufferlen)) % from->bufferlen;
            to->len = to->bufferlen;
        } else {
            dropsome_start = from->start;
            to->len = from->len;
        }

        if( dropsome_start + to->len > from->bufferlen){
            firsthalf = from->bufferlen - dropsome_start;
            secondhalf = to->len - firsthalf;
        } else {
            firsthalf = to->len;
            secondhalf = 0;
        }
        /*
        printf("buffer_copy, start:%lu len:%lu, dropsome_start:%lu, firsthalf:%lu, secondhalf:%lu\n",
            from->start, from->len, dropsome_start,   firsthalf, secondhalf);
        fflush(stdout);
        */

        /* copy dropsome_start ... firsthalf and 0...secondhalf */
        memcpy(to->buffer, from->buffer + dropsome_start, firsthalf*sizeof(RINGBUFFER_ENTRY_TYPE));
        if( 0 != secondhalf){
            memcpy(to->buffer+firsthalf, from->buffer, secondhalf*sizeof(RINGBUFFER_ENTRY_TYPE));
        }
        to->start = 0;

    } else { /* from->len */
        to->len = 0;
        to->start = 0;
    }
#ifdef RINGBUFFER_THREADSAFE
    pthread_mutex_unlock(&(from->mutex));
    pthread_mutex_unlock(&(to->mutex));
#endif
}


/*
** ringbuffer_move: like copy but the "from" is emptied.
**   redundant implementtion for performance
*/
void ringbuffer_move(struct ringbuffer * from, struct ringbuffer * to)
{
    size_t firsthalf, secondhalf, dropsome_start;

#ifdef RINGBUFFER_THREADSAFE
    pthread_mutex_lock(&(to->mutex));
    pthread_mutex_lock(&(from->mutex));
#endif
    if( 0 != from->len){


        if( from->len > to->bufferlen){  /* silently drop some from the begining */
            dropsome_start = (from->start + (from->len - to->bufferlen)) % from->bufferlen;
            to->len = to->bufferlen;
        } else {
            dropsome_start = from->start;
            to->len = from->len;
        }

        if( dropsome_start + to->len > from->bufferlen){
            firsthalf = from->bufferlen - dropsome_start;
            secondhalf = to->len - firsthalf;
        } else {
            firsthalf = to->len;
            secondhalf = 0;
        }
        /*
        printf("buffer_copy, start:%lu len:%lu, dropsome_start:%lu, firsthalf:%lu, secondhalf:%lu\n",
            from->start, from->len, dropsome_start,   firsthalf, secondhalf);
        fflush(stdout);
        */

        /* copy dropsome_start ... firsthalf and 0...secondhalf */
        memcpy(to->buffer, from->buffer + dropsome_start, firsthalf*sizeof(RINGBUFFER_ENTRY_TYPE));
        if( 0 != secondhalf){
            memcpy(to->buffer+firsthalf, from->buffer, secondhalf*sizeof(RINGBUFFER_ENTRY_TYPE));
        }
        to->start = 0;

    } else { /* from->len */
        to->len = 0;
        to->start = 0;
    }

    /* move */
    from->len = 0;
    from->start = 0;

#ifdef RINGBUFFER_THREADSAFE
    pthread_mutex_unlock(&(from->mutex));
    pthread_mutex_unlock(&(to->mutex));
#endif
}
