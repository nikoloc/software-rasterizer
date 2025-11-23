#ifndef READER_H
#define READER_H

// this is a simple reader interface for reading a text file line by line, suitable for usual text parsing needs it
// depends on the `dynamic_string.h` header, so make sure you include it beforehand

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

struct reader *
reader_create(char *path);

bool
reader_read_line(struct reader *reader, string_t *dest);

void
reader_destroy(struct reader *reader);

#endif

#ifdef READER_IMPLEMENTATION

#ifndef READER_BUFFER_SIZE
#define READER_BUFFER_SIZE 256
#endif

struct reader {
    int fd;

    ssize_t len;
    char buffer[READER_BUFFER_SIZE];
    char *cur;
};

static inline bool
_read_next(struct reader *reader) {
    ssize_t n = read(reader->fd, reader->buffer, READER_BUFFER_SIZE);
    reader->cur = reader->buffer;
    reader->len = n;

    return n > 0;
}

struct reader *
reader_create(char *path) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if(fd < 0) {
        goto err;
    }

    struct reader *reader = calloc(1, sizeof(*reader));
    if(!reader) {
        goto err_reader;
    }

    reader->fd = fd;

    // read initial data
    if(!_read_next(reader)) {
        goto err_read;
    }

    return reader;

err_read:
    free(reader);
err_reader:
    close(fd);
err:
    return NULL;
}

bool
reader_read_line(struct reader *reader, string_t *dest) {
    if(reader->len <= 0) {
        return false;
    }

    // reset the string because the caller may be reusing it
    dest->len = 0;

    while(1) {
        char *p = reader->cur;
        char *end = reader->buffer + reader->len;

        while(p != end) {
            if(*p == '\n') {
                reader->cur = p + 1;
                if(reader->cur >= end) {
                    _read_next(reader);
                }

                // handle windows newlines: if the previous was \r remove it; note: this is easier done like this, then
                // if we check it in advance, since the \r and \n characters could be in separate read chunks, making
                // the logic messy
                if(dest->len > 0 && dest->data[dest->len - 1] == '\r') {
                    dest->len--;
                }

                return true;
            }

            string_append(dest, *p);
            p++;
        }

        if(!_read_next(reader)) {
            // returning last line of the file
            return dest->len > 0;
        }
    }
}

void
reader_destroy(struct reader *reader) {
    close(reader->fd);
    free(reader);
}

#endif
