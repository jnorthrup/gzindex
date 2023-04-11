/* gzindex -- build an index for a gzip file and then test it
* Copyright (C) 2009 Mark Adler
* For conditions of distribution and use, see copyright notice in zlib.h
* Version 1.0  20 December 2009  Mark Adler
*/

/* This code demonstrates the use of new capabilities in zlib 1.2.3.4 or later
to create and use a random access index.  It is called with the name of a
gzip file on the command line.  That file is then indexed and the index is
tested by accessing the blocks in reverse order. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include "zlib.h"

/* data structure for each random access entry point */
struct point {
    off_t head;         /* starting bit of coded header in input stream (could
replace with the actual header bits -- the average
dynamic block header length is about 80 bytes), or
-1 if not a coded entry point */
    off_t start;        /* starting bit of compressed data in input stream --
for a coded block, this may be the start of a code
that generates bytes before the entry point, and so
those bytes need to be decoded and discarded to get
to the entry point */
    unsigned offset;    /* offset of the entry point in output data for a coded
block (i.e. the output bytes to discard), or number
of bytes remaining in stored block, or zero for an
entry at the start of a block if head == -1 */
    int last;           /* true if this access point is in the last block --
needed for stored header construction */
};

/* maximum dynamic block header span is less than this -- this also provides
enough space to read six bytes in and produce 258 bytes out */
#define MAXHEAD 289

/* set up the inflate state and the file pointer to decompress with the output
starting at entry, where strm is the state to set (assumed to be already
initialized for raw inflation), the deflate stream is in file gz starting
at offset, index is the list of index points, entry is the element of index,
and sofar is the uncompressed data from the beginning of the stream at least
as far as the entry point -- inflate_entry() will return Z_OK on success, or
zlib error code on failure, where Z_ERRNO is an error reading or seeking the
file gz */
int inflate_entry(z_stream *strm, FILE *gz, off_t offset, struct point *index, size_t entry, unsigned char *sofar,
                  size_t chunkSz) {
    int ret;
    unsigned len;
    size_t edge;
    struct point *point;
    unsigned char buf[MAXHEAD];

/* prepare the inflate stream to start anew (assume it's set up for raw) */
    ret = inflateReset(strm);
    if (ret != Z_OK)
        return ret;

/* set the dictionary history for decompression */
    point = index + entry;
    edge = chunkSz * entry;
    if (point->head != -1)          /* back up for coded block */
        edge -= point->offset;
    len = edge < 32768U ? (unsigned) edge : 32768U;
    ret = inflateSetDictionary(strm, sofar + edge - len, len);
    if (ret != Z_OK)
        return ret;

/* set up the inflate state and file pointer to start inflation at the
entry point */
    if (point->head == -1)
        if (point->offset == 0) {
/* entry point is the start of a deflate block (first block) */
            ret = fseeko(gz, offset + (point->start >> 3), SEEK_SET);
            if (ret)
                return Z_ERRNO;
            ret = fread(buf, 1, 1, gz);
            if (ret < 0)
                return Z_ERRNO;
            if (ret == 0)
                return Z_BUF_ERROR;
            ret = inflatePrime(strm, 8 - ((int) (point->start) & 7),
                               buf[0] >> ((int) (point->start) & 7));
            if (ret != Z_OK)
                return ret;
        } else {
/* entry point is inside a stored block -- build a stored block
header to start off with the bytes left in that block at the
entry point */
            buf[0] = (unsigned char) (point->last ? 1 : 0);
            buf[1] = (unsigned char) (point->offset);
            buf[2] = (unsigned char) (point->offset >> 8);
            buf[3] = (unsigned char) (~(point->offset));
            buf[4] = (unsigned char) (~(point->offset) >> 8);
            strm->avail_in = 5;
            strm->next_in = buf;
            strm->avail_out = 0;
            strm->next_out = buf + 5;
            ret = inflate(strm, Z_NO_FLUSH);
            assert(ret != Z_STREAM_ERROR);
            if (ret == Z_DATA_ERROR || ret == Z_MEM_ERROR)
                return ret;

/* position input file at next byte to read */
            ret = fseeko(gz, offset + (point->start >> 3), SEEK_SET);
            if (ret)
                return Z_ERRNO;
        }
    else {
/* entry point is inside a coded block -- run the header through
inflate first */
        ret = fseeko(gz, offset + (point->head >> 3), SEEK_SET);
        if (ret)
            return Z_ERRNO;
        ret = fread(buf, 1, MAXHEAD, gz);
        if (ret < 0)
            return Z_ERRNO;
        if (ret == 0)
            return Z_BUF_ERROR;
        strm->avail_in = (unsigned) ret - 1;
        strm->next_in = buf + 1;
        ret = inflatePrime(strm, 8 - ((int) (point->head) & 7),
                           buf[0] >> ((int) (point->head) & 7));
        if (ret != Z_OK)
            return ret;
        strm->avail_out = 0;
        strm->next_out = buf + MAXHEAD;
        ret = inflate(strm, Z_TREES);
        assert(ret != Z_STREAM_ERROR);
        if (ret == Z_DATA_ERROR || ret == Z_MEM_ERROR)
            return ret;
        if ((strm->data_type & 256) != 256)
            return Z_DATA_ERROR;
        ret = inflatePrime(strm, -1, 0);    /* discard remaining bits */
        if (ret != Z_OK)
            return ret;

/* set up to inflate, loading the initial 1..8 bits */
        ret = fseeko(gz, offset + (point->start >> 3), SEEK_SET);
        if (ret)
            return Z_ERRNO;
        ret = fread(buf, 1, 1, gz);
        if (ret < 0)
            return Z_ERRNO;
        if (ret == 0)
            return Z_BUF_ERROR;
        ret = inflatePrime(strm, 8 - ((int) (point->start) & 7),
                           buf[0] >> ((int) (point->start) & 7));
        if (ret != Z_OK)
            return ret;

/* discard extra output bytes from this code, if any, to get to the
entry point (max length/distance pair is six bytes) -- move file
pointer back to first unused byte */
        if (point->offset) {
            ret = fread(buf, 1, 6, gz);
            if (ret < 0)
                return Z_ERRNO;
            strm->avail_in = (unsigned) ret;
            strm->next_in = buf;
            strm->avail_out = point->offset;
            strm->next_out = buf + 6;
            ret = inflate(strm, Z_NO_FLUSH);
            assert(ret != Z_STREAM_ERROR);
            if (ret == Z_DATA_ERROR || ret == Z_MEM_ERROR)
                return ret;
            if (strm->avail_out != 0)
                return Z_DATA_ERROR;
            ret = fseeko(gz, -(long) (strm->avail_in), SEEK_CUR);
            if (ret)
                return Z_ERRNO;
        }
    }

/* return with strm and next byte from gz prepared to decompress starting
at the requested entry point (reset buffers to make user sets them) */
    strm->avail_in = 0;
    strm->next_in = NULL;
    strm->avail_out = 0;
    strm->next_out = NULL;
    return Z_OK;
}

/* index a raw deflate stream from the file gz starting at offset, and return
an index with number entries -- the uncompressed data is also returned in
data[0..length-1] for use in dictionaries and for comparison */
int inflate_index(FILE *gz, off_t offset, struct point **index, size_t *number, unsigned char **data, size_t *length,
                  size_t chunkSz) {
    int ret, last;
    size_t size, num, max, n;
    off_t pos, head, here;
    long left, back;
    struct point *list, *list2;
    unsigned char *out, *out2;
    z_stream strm;
    unsigned char in[16384];
    size_t input_stride = sizeof(in);

/* position input file */
    ret = fseeko(gz, offset, SEEK_SET);
    if (ret)
        return Z_ERRNO;

/* allocate output space to save the data, grow as needed later */
    size = 131072L;
    out = malloc(size);
    if (out == NULL)
        return Z_MEM_ERROR;

/* allocate space for index list -- grow as needed later */
    max = 512;
    list = malloc(max * sizeof(struct point));
    if (list == NULL) {
        free(out);
        return Z_MEM_ERROR;
    }

/* allocate inflate state for raw decoding */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    ret = inflateInit2(&strm, -15);
    if (ret != Z_OK) {
        free(list);
        free(out);
        return ret;
    }

/* make first index entry to simply start decompressing at beginning */
    list[0].head = -1;
    list[0].start = 0;
    list[0].offset = 0;
    list[0].last = 0;      /* doesn't matter since not inside stored */
    num = 1;

/* inflate the input data, CHUNK output bytes at a time, saving enough
information to randomly access the input data */
    pos = head = 0;
    last = 0;
    strm.next_out = out;
    do {
/* if needed, allocate more output space */
        if ((size_t) (strm.next_out - out) > size - chunkSz) {
            size <<= 1;
            if (size <= 0 || (out2 = realloc(out, size)) == NULL) {
                (void) inflateEnd(&strm);
                free(list);
                free(out);
                return Z_MEM_ERROR;
            }
            strm.next_out = out2 + (strm.next_out - out);
            out = out2;
        }

/* decompress CHUNK more output bytes */
        strm.avail_out = chunkSz;

/* for each output CHUNK, feed input data until there is no progress */
        do {
/* if needed, get more input data */
            if (strm.avail_in == 0) {
                strm.avail_in = fread(in, 1, input_stride, gz);
                if (ferror(gz)) {
                    (void) inflateEnd(&strm);
                    free(list);
                    free(out);
                    return Z_ERRNO;
                }
/* if we get to EOF here, then Houston, we have a problem */
                if (strm.avail_in == 0) {
                    (void) inflateEnd(&strm);
                    free(list);
                    free(out);
                    return Z_DATA_ERROR;
                }
                pos += strm.avail_in;
                strm.next_in = in;
            }

/* inflate available input data to fill block, but return early if
we get to a block boundary */
            ret = inflate(&strm, Z_BLOCK);
            assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
            if (ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
                (void) inflateEnd(&strm);
                free(list);
                free(out);
                return ret;
            }

/* if at a block boundary, note the location of the header */
            if (strm.data_type & 128) {
                size_t out_alignment = (pos - strm.avail_in) & 7;
                if (out_alignment) {
                    input_stride = 1;
                } else {
                    input_stride = sizeof(in);
                }
                head = ((pos - strm.avail_in) << 3) - (strm.data_type & 63);
                last = strm.data_type & 64; /* true at end of last block */
            }
        } while (strm.avail_out != 0 && !last);

/* if got to end of stream, no more entry points needed */
        if (last)
            break;

/* filled up a block and there's more -- make a new entry */
        if (num == max) {       /* make more room in list if needed */
            max <<= 1;
            n = max * sizeof(struct point);
            if (n / max != sizeof(struct point) ||
                (list2 = realloc(list, n)) == NULL) {
                (void) inflateEnd(&strm);
                free(list);
                free(out);
                return Z_MEM_ERROR;
            }
            list = list2;
        }
        here = ((pos - strm.avail_in) << 3) - (strm.data_type & 63);
        left = inflateMark(&strm);
        back = left >> 16;
        left &= 0xffff;
        if ((back & 0xffff) == 0xffff) {    /* signed shift not portable */
            list[num].head = -1;
            list[num].start = here;
        } else {
            list[num].head = head;
            list[num].start = here - back;
        }
        list[num].offset = left;
        list[num].last = strm.data_type & 64;
        num++;
    } while (1);
    (void) inflateEnd(&strm);

/* return results */
    *index = list;
    *number = num;
    if (data == NULL || length == NULL)
        free(out);
    else {
        *data = out;
        *length = strm.next_out - out;
    }
    return Z_OK;
}

/* return the offset of the start of deflate data -- return 0 on failure */
 off_t raw_start(FILE *gz) {
    int ret;
    off_t pos = 0;
    z_stream strm;
    unsigned char in[512];

/* set up to decode zlib or gzip header */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    ret = inflateInit2(&strm, 47);
    if (ret != Z_OK)
        return 0;

/* decode header */
    rewind(gz);
    strm.avail_out = 0;
    strm.next_out = in;
    do {
        if (strm.avail_in == 0) {
            ret = fread(in, 1, sizeof(in), gz);
            if (ret <= 0) {
                (void) inflateEnd(&strm);
                return 0;
            }
            pos += ret;
            strm.avail_in = (unsigned) ret;
            strm.next_in = in;
            ret = inflate(&strm, Z_BLOCK);
            assert(ret != Z_STREAM_ERROR);
            if (ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
                (void) inflateEnd(&strm);
                return 0;
            }
        }
    } while ((strm.data_type & 128) != 128);
    (void) inflateEnd(&strm);
    return pos - strm.avail_in;
}

/* create an index for the file on the command line and test it */
 int main(int argc, char **argv) {
    uInt CHUNK = 1024;
    int ret;
    FILE *gz;
    off_t start;
    struct point *index = NULL;
    unsigned char *data = NULL;
    size_t n, number = 0, length = 0;
    z_stream strm;
    unsigned char in[512], out[CHUNK];

/* set up input file, find start of deflate stream */
    if (argc == 1) {
        fputs("Usage: gzindex <gzipfile>\n", stderr);
        return 1;
    }
    gz = fopen(argv[1], "rb");
    if (gz == NULL) {
        fprintf(stderr, "could not open %s\n", argv[1]);
        return 1;
    }
    start = raw_start(gz);

/* create index */
    ret = inflate_index(gz, start, &index, &number, &data, &length, 0);
    if (ret != Z_OK) {
        fprintf(stderr, "indexing error %d\n", ret);
        return 1;
    }

/* set up raw inflate state for accessing entry points */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    ret = inflateInit2(&strm, -15);
    if (ret != Z_OK) {
        free(data);
        free(index);
        fprintf(stderr, "memory error %d\n", ret);
        return 1;
    }

/* test index in reverse order */
    n = number;
    while (n) {
        n--;

/* go to entry point n */
        ret = inflate_entry(&strm, gz, start, index, n, data, 0);
        if (ret != Z_OK) {
            (void) inflateEnd(&strm);
            free(data);
            free(index);
            fprintf(stderr, "entry error %d\n", ret);
            return 1;
        }

/* decompress CHUNK output bytes from here, or what's left */
        strm.avail_out = sizeof(out);
        strm.next_out = out;
        do {
            ret = fread(in, 1, sizeof(in), gz);
            if (ret < 0) {
                (void) inflateEnd(&strm);
                free(data);
                free(index);
                fprintf(stderr, "read error %d\n", errno);
                return 1;
            }
            strm.avail_in = (unsigned) ret;
            strm.next_in = in;
            ret = inflate(&strm, Z_NO_FLUSH);
            assert(ret != Z_STREAM_ERROR);
            if (ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
                (void) inflateEnd(&strm);
                free(data);
                free(index);
                fprintf(stderr, "decompression error %d\n", ret);
                return 1;
            }
        } while (strm.avail_out != 0 && ret != Z_STREAM_END);

/* verify the decompressed data */
        if ((n == number - 1 &&
             sizeof(out) - strm.avail_out != length - n * CHUNK) ||
            (n < number - 1 && strm.avail_out != 0)) {
            (void) inflateEnd(&strm);
            free(data);
            free(index);
            fprintf(stderr, "decompression shortfall at entry %lu\n", n);
            return 1;
        }
        if (memcmp(out, data + n * CHUNK, sizeof(out) - strm.avail_out) != 0) {
            (void) inflateEnd(&strm);
            free(data);
            free(index);
            fprintf(stderr, "compare error for entry %lu\n", n);
            return 1;
        }
    }

/* clean up */
    (void) inflateEnd(&strm);
    free(data);
    free(index);
    fprintf(stderr, "%lu entry points generated and successfully tested\n",
            number);
    return 0;
}
