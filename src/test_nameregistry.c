/*
** test_nameregistry.c
**
**  nameregistry functionality testing
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



#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nameregistry.h"

void randomstring(char * name, size_t namelen)
{
    int i;
    for(i=0; i< namelen; i++){
        name[i] = (unsigned char)(random() % 105)  + (unsigned char)'!'; /* random printable ascii */
    }
}

int main(int argc, char * argv[])
{
    int retval;
    int i;
    size_t size;
    size_t namelen;
    struct nameregistry nr;
    char * name;

    if( argc != 3){
        puts("Incorrect number of parameters. Usage:");
        puts("  test_nameregistry  <registry_size> <name_len>");
        return 2;
    }

    size = atol(argv[1]);
    namelen = atol(argv[2]);
    name = (char *) malloc(namelen);

    printf("test_nameregistry %lu %lu\n", size, namelen);
    retval = nameregistry_init(&nr, size, namelen);
    printf("init returns: %d\n", retval);
    i = 0;
    while((nr.used < nr.size) && (i < size * 60)){
        randomstring(name, namelen);
        retval = nameregistry_findadd(&nr, name);
        if( -1 == retval){
            printf("Error in fillup. i=%i, name=%.*s\n", i, (int)namelen, name);
            return 2;
        }
        i ++;
    }
    if( nr.used < nr.size ){
        printf("Crazy in fillup. it was not enough %d step to fill up %lu size.\n", i, size);
    } else { /* filled up */
        randomstring(name, namelen);
        retval = nameregistry_find(&nr, name);
        if( -1 != retval){
            printf("Crazy Random string found?? ID=%d name=%.*s\n", retval, (int)namelen, name);
        } else {
            retval = nameregistry_add(&nr, name);
            if( -1 != retval){
                printf("Error: successfully addition after successfully fillup name=%.*s ID=%d size=%lu used=%lu\n",
                    (int)namelen, name, retval, nr.size, nr.used);
                return 2;
            }
        }
    }

    for(i=0; i < size * 60; i++){
        retval = nameregistry_getbyid(&nr, random() % size, name);
        if( 0 == memcmp(name, ".........................................", namelen<40?namelen:40) ){
            printf("Error: got a '.........' by id %d\n", retval);
            return 2;
        }
        if( -1 == retval){
             printf("Crazy not found by id. size=%lu used=%lu\n", nr.size, nr.used);
        } else {
            if( 0 == random() % 2){
                retval  = nameregistry_removebyid(&nr, retval);
                if( -1 == retval){
                    printf("Crazy cannot remove after found by id??\n");
                } else{
                    retval = nameregistry_add(&nr, name);
                    if( -1 == retval){
                        printf("Crazy cannot add after removebyid?? name=%.*s size=%lu used=%lu\n", (int)namelen, name, nr.size, nr.used);
                    }
                }
            }
        }
        if( nr.used == 0){
            printf("Crazy registry is emptied");
            break;
        }
    }/* end for i */

    printf("Last line\n");
    return 0;

}
