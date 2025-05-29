/*
** datablock.h
**
** UDP communication data struture definitions
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


#ifndef __DATABLOCK_H
#define __DATABLOCK_H

#include <stdint.h>
#include <time.h>
#include <stdio.h>

#pragma pack(push,1)

/*
** zeroed datablockarray like: {0, {0,0}, {0,0}, 0.0, 0.0, 0.0, 0.0}
** empty (no valid measurement} like:
**   {0, {0,0}, {0,0}, FSLATENCY_EXTREMEBIGINTERVAL, 0.0, 0.0, 0.0}
**
*/


struct datablock {
    uint64_t measurementcount; /* number of measurements (integer, 64 bit) */
    struct timespec starttime;
    struct timespec endtime;
    double min;
    double max;
    double sumx;
    double sumxx;
};




/* for debug purposes */
static inline void datablock_print(const struct datablock * d)
{
        dprintf(2, " number of measurements: %lu\n", d->measurementcount);
        dprintf(2, "   starttime: %ld.%09ld\n", d->starttime.tv_sec, d->starttime.tv_nsec);
        dprintf(2, "   endtime  : %ld.%09ld\n",  d->endtime.tv_sec, d->endtime.tv_nsec);
        dprintf(2, "   min  : %f\n", d->min);
        dprintf(2, "   max  : %f\n", d->max);
        dprintf(2, "   sumX : %f\n", d->sumx);
        dprintf(2, "   sumXX: %f\n", d->sumxx);
}


#define FSLATENCY_MAGIC "fslatency      "  /* 15 chars+terminaing 0 = 16 byte */
#define FSLATENCY_MAGIC_LEN 16u
#define FSLATENCY_HOSTNAME_LEN 64u
#define FSLATENCY_TEXT_LEN 64u
#define FSLATENCY_VERSION_MAJOR 0u
#define FSLATENCY_VERSION_MINOR 1u
#define FSLATENCY_DATABLOCKARRAY_LEN 8u
#define FSLATENCY_EXTREMEBIGINTERVAL  1000000000.0  /* 31year must be enought for disk latency measurements :-) */

struct messageblock {
    char magic[FSLATENCY_MAGIC_LEN];
    uint16_t major;
    uint16_t minor;
    char hostname[FSLATENCY_HOSTNAME_LEN];
    char text[FSLATENCY_TEXT_LEN];
    struct timespec precision;
    struct datablock datablockarray[FSLATENCY_DATABLOCKARRAY_LEN];
};


#pragma pack(pop)
#endif /* __DATABLOCK_H */
