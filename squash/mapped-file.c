/* Copyright (c) 2015 The Squash Authors
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *   Evan Nemerson <evan@nemerson.com>
 */

#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200112L

#include "internal.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string.h>
#include <unistd.h>

bool
squash_mapped_file_init_full (SquashMappedFile* mapped, FILE* fp, size_t length, bool length_is_suggestion, bool writable) {
  assert (mapped != NULL);
  assert (fp != NULL);

  if (mapped->data != MAP_FAILED)
    munmap (mapped->data - mapped->window_offset, mapped->length + mapped->window_offset);

  int fd = fileno (fp);
  if (fd == -1)
    return false;

  int ires;
  struct stat fp_stat;

  ires = fstat (fd, &fp_stat);
  if (ires == -1 || !S_ISREG(fp_stat.st_mode) || (!writable && fp_stat.st_size == 0))
    return false;

  off_t offset = ftello (fp);
  if (offset < 0)
    return false;

  if (writable) {
    ires = ftruncate (fd, offset + (off_t) length);
    if (ires == -1)
      return false;
  } else {
    const size_t remaining = fp_stat.st_size - (size_t) offset;
    if (remaining > 0) {
      if (length == 0 || (length > remaining && length_is_suggestion)) {
        length = remaining;
      } else if (length > remaining) {
        return false;
      }
    } else {
      return false;
    }
  }
  mapped->length = length;

  const size_t page_size = squash_get_page_size ();
  mapped->window_offset = (size_t) offset % page_size;
  mapped->map_length = length + mapped->window_offset;

  if (writable)
    mapped->data = mmap (NULL, mapped->map_length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset - mapped->window_offset);
  else
    mapped->data = mmap (NULL, mapped->map_length, PROT_READ, MAP_SHARED, fd, offset - mapped->window_offset);

  if (mapped->data == MAP_FAILED)
    return false;

  mapped->data += mapped->window_offset;
  mapped->fp = fp;
  mapped->writable = writable;

  return true;
}

bool
squash_mapped_file_init (SquashMappedFile* mapped, FILE* fp, size_t length, bool writable) {
  return squash_mapped_file_init_full (mapped, fp, length, false, writable);
}

void
squash_mapped_file_destroy (SquashMappedFile* mapped, bool success) {
  if (mapped->data != MAP_FAILED) {
    munmap (mapped->data - mapped->window_offset, mapped->length + mapped->window_offset);
    mapped->data = MAP_FAILED;

    if (success) {
      fseeko (mapped->fp, mapped->length, SEEK_CUR);
      if (mapped->writable) {
        ftruncate (fileno (mapped->fp), ftello (mapped->fp));
      }
    }
  }
}
