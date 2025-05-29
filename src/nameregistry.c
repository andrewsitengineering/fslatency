/*
** nameregistry.c
**
** nameregistry structure  implementations. See nameregistry.h
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

#include <string.h>
#include <pthread.h>
#include "nameregistry.h"

/*
**  freelist usage: (how to handle the fragmantation if the registry)
**    in range 0  <= X < used freelist contains the used entries of registry
**    in range used <= X < size freelist contains the free entries of registry
**      so freelist[used] is a next avaiable free index of the registry
**  free entries = size - used
*/

int nameregistry_init(struct nameregistry * nrp, size_t size, size_t namelen)
{

    int i;

    if( size > 1048573){ /* should use a better implementation for lots of names. Btw 1048573 is a nice prime bellow 2**20 */
        return -1;
    }
    nrp->size = size;
    nrp->used = 0;
    nrp->namelen = namelen;
    nrp->freelist = (size_t *) malloc( size * sizeof(size_t));
    if( NULL == nrp->freelist){
        return -1;
    }
    for( i=0; i<size; i++){
        nrp->freelist[i] = i;
    }
    nrp->registry = malloc( namelen * size);
    if( NULL == nrp->registry){
        return -1;
    }
    /* we sugest a clearcharacter == '.' because this is invalid for any internet name */
    memset(nrp->registry, '.', namelen * size);
    pthread_mutex_init(&(nrp->mutex), 0);
    return 0;
}


int nameregistry_free(struct nameregistry * nrp)
{
    memset(nrp->registry, '.', nrp->namelen * nrp->size);
    nrp->size = 0;
    nrp->used = 0;
    nrp->namelen = 0;
    free(nrp->freelist);
    nrp->freelist = NULL;
    free(nrp->registry);
    nrp->registry = NULL;
    pthread_mutex_destroy(&(nrp->mutex));
    return 0;
}


int nameregistry_find(struct nameregistry * nrp, void * name)
{
    int i;
    int retval;
    pthread_mutex_lock(&(nrp->mutex));
    /* scan only the used entries */
    for(i=0; i < nrp->used; i++){
        if( 0 == memcmp(name, nrp->registry + (nrp->namelen * nrp->freelist[i]), nrp->namelen)){
            /* match found */
            retval =  (int) nrp->freelist[i];
            pthread_mutex_unlock(&(nrp->mutex));
            return  retval;
        }
    }
    pthread_mutex_unlock(&(nrp->mutex));
    return -1;
}


int nameregistry_add(struct nameregistry * nrp, void * name)
{
    int retval;
    /* no check. May duplicate add */
    pthread_mutex_lock(&(nrp->mutex));
    if( nrp->used == nrp->size){
        pthread_mutex_unlock(&(nrp->mutex));
        return -1;
    }
    memcpy(nrp->registry + (nrp->namelen *  nrp->freelist[nrp->used]), name, nrp->namelen);
    retval = (int) nrp->freelist[nrp->used];
    nrp->used ++;
    pthread_mutex_unlock(&(nrp->mutex));
    return retval;
}


int nameregistry_findadd(struct nameregistry * nrp, void * name)
{
    int retval;
    int i;

    pthread_mutex_lock(&(nrp->mutex));
    /* scan only the used entries */
    for(i=0; i < nrp->used; i++){
        if( 0 == memcmp(name, nrp->registry + (nrp->namelen * nrp->freelist[i]), nrp->namelen)){
            /* match found */
            retval =  (int) nrp->freelist[i];
            pthread_mutex_unlock(&(nrp->mutex));
            return retval;
        }
    }
    if( nrp->used == nrp->size){
        pthread_mutex_unlock(&(nrp->mutex));
        return -1;
    }
    memcpy(nrp->registry + (nrp->namelen *  nrp->freelist[nrp->used]), name, nrp->namelen);
    retval = (int) nrp->freelist[nrp->used];
    nrp->used ++;
    pthread_mutex_unlock(&(nrp->mutex));
    return retval;
}


int nameregistry_remove(struct nameregistry * nrp, void * name)
{
    int i;
    size_t indextmp;
    pthread_mutex_lock(&(nrp->mutex));
    /* scan only the used entries */
    for(i=0; i < nrp->used; i++){
        indextmp = nrp->freelist[i];
        if( 0 == memcmp(name, nrp->registry + (nrp->namelen * indextmp), nrp->namelen)){
            /* match found */
            memset(nrp->registry + (nrp->namelen * indextmp), '.', nrp->namelen);
            nrp->used --;
            nrp->freelist[i] = nrp->freelist[nrp->used];
            nrp->freelist[nrp->used] = indextmp;
            pthread_mutex_unlock(&(nrp->mutex));
            return  (int) indextmp;
        }
    }
    pthread_mutex_unlock(&(nrp->mutex));
    return -1; /* if not found */
}


int nameregistry_removebyid(struct nameregistry * nrp, size_t id)
{
    int i;

    pthread_mutex_lock(&(nrp->mutex));
    for(i=0; i < nrp->used; i++){
        if( id == nrp->freelist[i] ){
            memset(nrp->registry + (nrp->namelen * id), '.', nrp->namelen);
            nrp->used --;
            nrp->freelist[i] = nrp->freelist[nrp->used];
            nrp->freelist[nrp->used] = id;
            pthread_mutex_unlock(&(nrp->mutex));
            return (int) id;
        }
    }
    pthread_mutex_unlock(&(nrp->mutex));
    return -1; /* id not used */
}


int nameregistry_getbyid(struct nameregistry * nrp, size_t id, void * name)
{
    int i;

    pthread_mutex_lock(&(nrp->mutex));
    for(i=0; i < nrp->used; i++){
        if( id == nrp->freelist[i] ){
            memcpy(name, nrp->registry + (nrp->namelen * id), nrp->namelen);
            pthread_mutex_unlock(&(nrp->mutex));
            return (int) id;
        }
    }
    pthread_mutex_unlock(&(nrp->mutex));
    return -1; /* id not used */

}

