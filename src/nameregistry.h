/*
** nameregistry.h
**
** nameregistry structure definitions and implementations
**
**  registers a fixed-length name and assigns it an ID. The ID is a small (20bit) integer.
**  useable a name <-> id mapping.
**
** NOT multithrad safe
**
**  functions:
**      - init     the constructor
**      - free     the destructor
**      - find     find a name in the registry. Returns an ID.
**      - add      insert a perviously unknown name to the registry. Returns an ID.
**      - findadd  Returns an ID either find or add.
**      - remove   Remove a name from the registry. No error if not found.
**  attributes:
**      - size      total length of registry
**      - used      used entryes in the registry
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
*/


#ifndef __NAMEREGISTRY_H
#define __NAMEREGISTRY_H

#include <stdlib.h>
#include <pthread.h>

/*
**  freelist usage: (how to handle the fragmantation if the registry)
**    in range 0  <= X < used freelist contains the used entries of registry
**    in range used <= X < size freelist contains the free entries of registry
**      so freelist[used] is a next avaiable free index of the registry
**  free entries = size - used
*/

struct nameregistry {
    size_t size;
    size_t used;
    size_t namelen;
    size_t * freelist;
    void * registry;
    pthread_mutex_t mutex;
};


int nameregistry_init(struct nameregistry * nrp, size_t size, size_t namelen);
int nameregistry_free(struct nameregistry * nrp);
int nameregistry_find(struct nameregistry * nrp, void * name);
int nameregistry_add(struct nameregistry * nrp, void * name);
int nameregistry_findadd(struct nameregistry * nrp, void * name);
int nameregistry_remove(struct nameregistry * nrp, void * name);
int nameregistry_removebyid(struct nameregistry * nrp, size_t id);
int nameregistry_getbyid(struct nameregistry * nrp, size_t id, void * name);

#endif /* __NAMEREGISTRY_H */
