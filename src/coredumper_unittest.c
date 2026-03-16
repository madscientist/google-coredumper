/* Copyright (c) 2005-2008, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ---
 * Author: Markus Gutschke, Carl Crous
 */

#define __STDC_LIMIT_MACROS
#include <assert.h>
#include <stdarg.h>
#include <bits/wordsize.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "google/coredumper.h"
#include "linuxthreads.h"

/* A simple structure to store memory segment information.                   */
struct MemorySegment {
  size_t filesz;
  size_t memsz;
};

#ifdef __GNUC__
#define ASSERT(_cond, _fmt, ...)                                        \
  if (!(_cond)) ut_assert(__FILE__, __LINE__, __PRETTY_FUNCTION__,      \
                          #_cond, _fmt, ##__VA_ARGS__)

static void ut_assert(const char *filenm, uint64_t lineno, const char *func,
                      const char *condition, const char *fmt, ...)
{
  FILE* out = stdout;
  va_list args;
  va_start(args, fmt);

  fprintf(out, "%s:%lu: %s: Assertion '%s' failed: ", filenm, lineno, func, condition);
  vfprintf(out, fmt, args);
  va_end(args);
  fputc('\n', out);
  abort();
}
#else
#define ASSERT(_cond, ...)  assert(_cond)
#endif

static void info(const char *fmt, ...)
{
  FILE* out = stdout;
  va_list args;

  fputs("|| ", out);
  va_start(args, fmt);
  vfprintf(out, fmt, args);
  va_end(args);
  fputc('\n', out);
}

/* A comparator function to compare to memory segments based on their memsz in
 * descending order. If two segments have the same memsz, their filesz values
 * are compared in ascending order.
 */
static int MemorySegmentCmp(const struct MemorySegment *seg1,
                            const struct MemorySegment *seg2) {
  if (seg1->memsz == seg2->memsz) {
    return seg2->filesz - seg1->filesz;
  }
  return seg1->memsz - seg2->memsz;
}

/* A large extra note.                                                       */
unsigned int large_extra_note[256];

/* Extra core notes.                                                         */
static struct CoredumperNote extra_notes[] = {
    {"GOOGLE", 0xdeadbeef, 16,
      (const void *)("abcdefghijklmnop")},
    {"FOOO", 0xf00ba7, 9,
      (const void *)("qwertyuio")},
    {"BAR", 0x0, 0, NULL},
    {"LARGE", 0x1234, sizeof(large_extra_note),
      (const void *)(large_extra_note)}
  };
static const int kExtraNotesCount = 4;

static const char* getEnvVar(const char *varname, const char *defval)
{
  const char *var = getenv(varname);
  return var && var[0] != '\0' ? var : defval;
}

/* Make assertion failures print more readable messages                      */
#undef strcmp
#undef strncmp
#undef strstr

/* Simple signal handler for dealing with timeouts.                          */
static jmp_buf jmpenv;
static void TimeOutHandler(int sig, siginfo_t *info, void *p) {
  siglongjmp(jmpenv, sig);
}

/* This is a really silly CPU hog, but we want to avoid creating a
 * core dump while we are executing code in libc. Depending on the
 * system environment, gdb might or might not make stack traces
 * available within libc, and that would make this unittest
 * non-deterministic.
 */
static volatile enum State { IDLE, RUNNING, DEAD } state1, state2;
static volatile unsigned int counter;
static void *Busy(void *arg) {
  volatile enum State *state = (volatile enum State *)arg;
  *state = RUNNING;
  while (*state == RUNNING) {
    counter++;
  }
  return 0;
}

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define ENDIAN_NOTE "little endian"
#else
#define ENDIAN_NOTE "big endian"
#endif


/* Open the core file with "readelf", and check that all the expected
 * entries can be found. We are not checking exact numeric values, as these
 * might differ between runs, and it seems overkill recomputing them here.
 */
static void CheckWithReadElf(FILE *input, FILE *output, const char *filename,
                             const char *suffix, const char *decompress,
                             const char *args) {
  static const char *msg[] = {
      " ELF",
      ENDIAN_NOTE,
      "UNIX - System V",
      "Core file",
      "There are no section", /* Different readelf versions show different text */

      "NOTE",
      /* The LLVM readelf does not print this line
      "No version information found in this file",
      */
      "NT_PRPSINFO",
#ifndef __mips__
      "NT_TASKSTRUCT",
#endif
      "NT_PRSTATUS", "NT_FPREGSET",
#ifdef THREADS
      "NT_PRSTATUS", "NT_FPREGSET",
      "NT_PRSTATUS", "NT_FPREGSET",
#endif
      "DONE", 0 };
  const char  **ptr;
  char buffer[4096];
  /* Some of the tests explicitly limit core file size, resulting in a
   * truncated core, and causing readelf to print errors on its stderr.
   * These errors can then intermix with expected readelf output, causing
   * the test to fail. To prevent this, ignore readelf stderr (using '2>&1'
   * does not suffice when stdout is fully buffered).
   */
  int  rc = fprintf(input,
                    "cat /proc/%d/maps &&"
                    " %s %s <\"%s%s\" >core.%d &&"
                    " %s -a core.%d 2>/dev/null;"
                    " rm -f core.%d;"
                    " (set +x; echo DONE)\n",
                    getpid(),
                    decompress, args, filename, suffix, getpid(),
                    getEnvVar("READELF", "readelf"), getpid(),
                    getpid());
  ASSERT(rc > 0, "rc = %d", rc);

  *buffer = '\000';
  for (ptr = msg; *ptr; ptr++) {
    do {
      char *line;
      ASSERT(strncmp(buffer, "DONE", 4), "buffer = %s", buffer);
      line = fgets(buffer, sizeof(buffer), output);
      ASSERT(line, "line = %s", line);
      fputs(buffer, stdout);
    } while (!strstr(buffer, *ptr));
    info("Found: %s", *ptr);
  }
  return;
}

/* Skips leading white space within a string */
static char *SkipLeadingWhiteSpace(char *line) {
  while (isspace(*line) && *line != '\0') {
    line++;
  }
  return line;
}

/* Converts a hex string to a size_t number. pos is set to the character after
 * the number in the string.
 */
static size_t hextosizet(char *str, char **pos) {
  size_t num = 0;
  size_t digit;
  if (str[0] == '0' && toupper(str[1]) == 'X') {
    str += 2;
  }
  while (isxdigit(*str)) {
    if (isdigit(*str)) {
      digit = *str - '0';
    } else {
      digit = 10 + toupper(*str) - 'A';
    }
    num = num * 16 + digit;
    str++;
  }
  *pos = str;
  return num;
}

/* Open a prioritized core file with "readelf", and check that the
 * prioritization was performed correctly.
 */
static void CheckPrioritizationWithReadElf(FILE *input, FILE *output,
                                           const char *filename) {
  char *line;
  char buffer[4096];
  int last_line_was_load;
  int  rc = fprintf(input, "%s -a %s; echo DONE\n",
                    getEnvVar("READELF", "readelf"), filename);
  const int kMaxMemorySegments = 256;
  struct MemorySegment memory_segments[kMaxMemorySegments];
  int memory_segment_count = 0;
  ASSERT(rc > 0, "rc = %d", rc);

  *buffer = '\000';

  /* Read the output from readelf and remember each writable memory segment's
   * size in memory and in the file.
   */
  last_line_was_load = 0;
  do {
    line = fgets(buffer, sizeof(buffer), output);
    if (line) {
      fputs(buffer, stdout);
      if (!strncmp(line, "DONE", 4)) {
        break;
      }
      line = SkipLeadingWhiteSpace(line);
      if (!strncmp(line, "LOAD", 4)) {
        last_line_was_load = 1;
      } else if (last_line_was_load) {
        last_line_was_load = 0;
        line = SkipLeadingWhiteSpace(line);
        ASSERT(memory_segment_count < kMaxMemorySegments,
               "count=%d max=%d", memory_segment_count, kMaxMemorySegments);
        memory_segments[memory_segment_count].filesz =
          hextosizet(line, &line);
        memory_segments[memory_segment_count].memsz =
          hextosizet(line, &line);
        line = SkipLeadingWhiteSpace(line);
        /* Line is now at the flags with the second character being 'W' or
         * ' '. We only want to add writable memory segments.
         */
        if (line[1] == 'W') {
          memory_segment_count++;
        }
      }
    }
  } while (line);
  qsort(memory_segments, sizeof(struct MemorySegment), memory_segment_count,
        (int (*)(const void*, const void*))MemorySegmentCmp);

  /* Once the memory segments are sorted according to their size in memory, the
   * difference between the memory size and the file size must be decreasing.
   * If it is not, this means a memory segment which wasn't the largest was
   * decreased in size before the largest one.
   */
  size_t last_size_difference = 0;
  int i;
  for (i = 0; i < memory_segment_count; ++i) {
    size_t current_size_difference =
      memory_segments[i].memsz - memory_segments[i].filesz;
    if (i > 0) {
      ASSERT(last_size_difference >= current_size_difference,
             "last=%lu current=%lu", last_size_difference, current_size_difference);
    }
    last_size_difference = current_size_difference;
  }

  return;
}


/* Open a core file which has extra notes with "readelf", and check that the
 * notes were written correctly.
 */
static void CheckExtraNotesWithReadElf(FILE *input, FILE *output,
                                       const char *filename) {
  const int kBufferSize = 4096;
  char *line;
  char buffer[kBufferSize];
  int note_index = 0;
  /* The sizes of the notes and their offset are relatively small, definitely
   * less than 2GB therefore the int data type is more than enough.
   */
  int offset = 0;
  int note_sizes[kExtraNotesCount];
  int note_sizes_to_description[kExtraNotesCount];
  int rc = fprintf(input, "%s -n %s; echo DONE\n",
                   getEnvVar("READELF", "readelf"), filename);

  ASSERT(rc > 0, "rc = %d", rc);

  *buffer = '\000';
  /* Read the output from readelf and check the values are correct.          */
  do {
    line = fgets(buffer, sizeof(buffer), output);
    if (!strncmp(line, "DONE", 4)) {
      line = 0;
    }
    if (line) {
      fputs(buffer, stdout);
      if (!offset) {
        int l = 0;
        if (!strncmp(line, "Displaying notes found at file offset ", 38)) {
          l = 38;
        } else if (!strncmp(line, "Notes at offset ", 16)) {
          l = 16;
        }
        if (l) {
          line += l;
          offset = hextosizet(line, &line);
          /* Skip the line which contains the table headings.                */
          char *o = fgets(buffer, sizeof(buffer), output);
          ASSERT(o != NULL, "no table heading");
        }
      } else if (line[0] != '+') {
        /* Get the name, its size and the description size.                  */
        line = SkipLeadingWhiteSpace(line);
        char *name = line;
        /* Ignore "description data" lines                                   */
        if (!strncmp(name, "description data:", 17)) {
          continue;
        }
        /* Our test names do not include spaces so this will work.           */
        while (!isspace(*line) && *line != '\0') {
          line++;
        }
        *line = '\0';
        line++;
        int name_size = strlen(name) + 1;

        line = SkipLeadingWhiteSpace(line);
        int description_size = hextosizet(line, &line);

        int note_size = 12;
        note_size += name_size;
        if (name_size % 4 != 0) {
          note_size += 4 - name_size % 4;
        }
        int note_size_to_description = note_size;
        note_size += description_size;
        if (description_size % 4 != 0) {
          note_size += 4 - description_size % 4;
        }

        ASSERT(note_index < kExtraNotesCount, "index=%d", note_index);
        struct CoredumperNote *note = &extra_notes[note_index];
        if (!strcmp(name, note->name)) {
          ASSERT(description_size == note->description_size,
                 "name=%s desc=%d note=%u",
                 name, description_size, note->description_size);
          line = SkipLeadingWhiteSpace(line);
          /* Expect readelf to not recognize our note types.                 */
          ASSERT(!strncmp(line, "Unknown note type: (", 20),
                 "name=%s line=%s", name, line);
          line += 20;

          unsigned int type = hextosizet(line, &line);
          ASSERT(type == note->type,
                 "name=%s type=%u note=%u", name, type, note->type);

          note_sizes[note_index] = note_size;
          note_sizes_to_description[note_index] = note_size_to_description;
          note_index++;
        } else if (note_index == 0) {
          /* The custom notes must follow the core notes.                    */
          ASSERT(!strcmp(name, "CORE") || !strcmp(name, "LINUX"),
                 "name=%s", name);
          offset += note_size;
        }
      }
    }
  } while (line);
  ASSERT(note_index == kExtraNotesCount,
         "index=%d count=%d", note_index, kExtraNotesCount);

  /* Check the note descriptions.                                            */
  FILE *fp = fopen(filename, "rb");
  ASSERT(fp, "open %s failed", filename);

  ASSERT(!fseek(fp, offset, SEEK_SET), "offset=%d", offset);
  int i;
  for (i = 0; i < kExtraNotesCount; ++i) {
    struct CoredumperNote *note = &extra_notes[i];
    ASSERT(fread(buffer, 1, note_sizes[i], fp) == note_sizes[i],
           "buffer=%s size=%d", buffer, note_sizes[i]);
    if (note->description_size > 0) {
      line = &buffer[note_sizes_to_description[i]];
      ASSERT(!strncmp(line, (const char*)note->description,
                      note->description_size),
             "line=%s descr=%s", line, note->description);
    }
  }

  ASSERT(!fclose(fp), "close file %s", filename);


  return;
}

/* Open the core dump with gdb, and check that the stack traces look
 * correct. Again, we are not checking for exact numeric values.
 *
 * We also extract the value of the "dummy" environment variable, and check
 * that it is correct.
 */
static void CheckWithGDB(FILE *input, FILE *output, const char *filename,
                         int *dummy, int cmp_parm) {
  volatile int cmp = cmp_parm;
  char out[4096], buffer[4096];
  char * volatile out_ptr = out;
  const char **ptr, *arg = "";
  struct sigaction sa;

#if defined(__i386__) || defined(__x86_64) || defined(__ARM_ARCH_3__) || \
    defined(__aarch64__) || defined(__mips__)
  /* If we have a platform-specific FRAME() macro, we expect the stack trace
   * to be unrolled all the way to WriteCoreDump().
   */
  #define DUMPFUNCTION " @ Thread * *CoreDump"
#else
  /* Otherwise, we the stack trace will start in ListAllProcessThreads.
   */
  #define DUMPFUNCTION " @ Thread * *ListAllProcessThreads"
#endif

  /* Messages that we are looking for. "@" is a special character that
   * matches a pattern in the output, which can later be used as input
   * to gdb. "*" is a glob wildcard character.
   */
  static const char *msg[] = { "Core was generated by",
                               DUMPFUNCTION,
                               "[Current thread is *",
                               "#* *CoreDump",
                               "#@ * TestCoreDump",
                               " TestCoreDump",
                               "$1 = ",
#ifdef THREADS
                               " Busy",
                               " @ Thread * Busy",
                               "#*  *CoreDump",
#endif
                               "DONE", 0 };

  /* Commands that we are sending to gdb. All occurrences of "@" will be
   * substituted with the pattern matching the corresponding "@" character
   * in the stream of messages received.
   */
  sprintf(out,
          "%s /proc/%d/exe \"%s\"; (set +x; echo DONE)\n"
          "info threads\n"
          "thread @\n"
          "bt 10\n"
          "up @\n"
          "print *(unsigned int *)0x%lx\n"
          "print %dU\n"
          "print %dU\n"
#ifdef THREADS
          "info threads\n"
          "thread @\n"
          "thread apply all bt 10\n"
#endif
          "quit\n",
          getEnvVar("GDB", "gdb"),
          getpid(), filename,
          (unsigned long)dummy, *dummy, cmp);

  /* Since we are interactively driving gdb, it is possible that we would
   * indefinitely have to wait for a matching message to appear (this is
   * different from the "readelf" case, which can just read until EOF).
   * So, we have to set up a time out handler.
   */
  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = TimeOutHandler;
  sa.sa_flags     = SA_RESTART|SA_SIGINFO;
  sigaction(SIGALRM, &sa, 0);

  if (setjmp(jmpenv)) {
    info("Time out!");
    abort();
  } else {
    *buffer = '\000';
    for (ptr = msg; *ptr; ptr++) {
      /* If there is any command that does not require a pattern read from
       * the message stream, output it now.
       */
      while (*out_ptr && *out_ptr != '@') {
        int rc = putc(*out_ptr++, input);
        ASSERT(rc >= 0, "rc = %d", rc);
      }
      fflush(input);
      for (;;) {
        char *line, *templ, scratch[256], isarg = 0;

        /* We should never try to read any more messages, after we have
         * already seen the final "DONE" message.
         */
        ASSERT(strncmp(buffer, "DONE", 4), "buffer=%s", buffer);

        /* Read the next message from gdb.                                   */
        alarm(20);
        line = fgets(buffer, sizeof(buffer), output);
        alarm(0);
        ASSERT(line, "buffer=%s", buffer);
        fputs(buffer, stdout);

        /* Extract the "$1 =" string, which we will compare later.           */
        if ((arg = strstr(buffer, "$1 = ")) != NULL) {
          cmp = atoi(arg + 5);
          arg = 0;
        }

        /* Try to match the current line against our templates.              */
        templ = strcpy(scratch, *ptr);
        for (;;) {
          /* Split the template in substring separated by "@" and "*" chars. */
          int  l = strcspn(templ, "*@");
          char c = templ[l];
          templ[l] = '\000';

          /* If we just passed a "@", remember pattern for later use.        */
          if (isarg) {
            arg = line;
            isarg = 0;
          }
          if (c == '@')
            isarg++;

          /* Check if substring of template matches somewhere in current line*/
          if ((line = strstr(line, templ)) != NULL) {
            /* Found a match. Remember arg, if any.                          */
            if (c != '@')
              *line = '\000';

            /* Advance to next pattern that needs matching.                  */
            line += strlen(templ);
          } else {
            /* No match. Break out of this loop, and read next line.         */
            templ[l] = c;
            arg = 0;
            break;
          }
          /* No more patterns. We have a successful match.                   */
          if (!c)
            goto found;
          templ[l] = c;
          templ += l + 1;
        }
      }
    found:
      /* Print matched pattern. Enter arg into command stream. Then loop.    */
      info("Found: %s", *ptr);
      if (arg && *out_ptr == '@') {
        /* We only want to match the very last word; drop leading tokens.    */
        int rc;
        char *last = strrchr(arg, ' ');
        if (last != NULL) arg = last + 1;

        /* Enter matched data into the command stream.                       */
        rc = fputs(arg, input);
        ASSERT(rc > 0, "rc=%d", rc);
        info(" (arg = \"%s\")", arg);
        arg = 0;
        out_ptr++;
      }
    }

    ASSERT(*dummy == cmp, "*dummy=%d cmp=%d", *dummy, cmp);
    info("Magic marker matches %d", *dummy);
  }
}


/* We can test both the WriteCoreDump() and the GetCoreDump() functions
 * with the same test cases. We just need to wrap the GetCoreDump()
 * family of functions with some code that emulates the WriteCoreDump()
 * functions.
 */
static int MyWriteCoreDumpWith(const struct CoreDumpParameters *params,
                               const char *file_name) {
  int                         rc = 0;
  int                         coreFd;
  int                         flags;
  size_t                      max_length = params->max_length;
  struct CoredumperCompressor *comp = NULL;
  struct CoreDumpParameters   new_params;

  if (!max_length)
    return 0;
  /* Remove limiting parameters.                                             */
  memcpy(&new_params, params, sizeof(struct CoreDumpParameters));
  SetCoreDumpParameter(&new_params, max_length, SIZE_MAX);
  flags = new_params.flags;
  flags &= ~(COREDUMPER_FLAG_LIMITED | COREDUMPER_FLAG_LIMITED_BY_PRIORITY);
  SetCoreDumpParameter(&new_params, flags, flags);
  coreFd = GetCoreDumpWith(&new_params);
  if (params->selected_compressor) {
    comp = *params->selected_compressor;
  }
  if (coreFd >= 0) {
    int writeFd;
    const char *suffix = "";

    if (comp != NULL && comp->compressor != NULL && comp->suffix != NULL)
      suffix = comp->suffix;

    /* scope */ {
      char extended_file_name[strlen(file_name) + strlen(suffix) + 1];
      strcat(strcpy(extended_file_name, file_name), suffix);
      writeFd = open(extended_file_name, O_WRONLY|O_CREAT|O_TRUNC, 0600);
    }
    if (writeFd >= 0) {
      char buffer[16384];
      ssize_t len;
      while (max_length > 0 &&
             ((len = read(coreFd, buffer,
                          sizeof(buffer) < max_length
                          ? sizeof(buffer) : max_length)) > 0 ||
              (len < 0 && errno == EINTR))) {
        char *ptr = buffer;
        while (len > 0) {
          int i;
          i = write(writeFd, ptr, len);
          if (i <= 0) {
            rc = -1;
            break;
          }
          ptr        += i;
          len        -= i;
          max_length -= i;
        }
      }
      close(writeFd);
    } else {
      rc = -1;
    }
    close(coreFd);
  } else {
    rc = -1;
  }
  return rc;
}

static int MyWriteCoreDump(const char *file_name) {
  struct CoreDumpParameters params;
  ClearCoreDumpParameters(&params);
  return MyWriteCoreDumpWith(&params, file_name);
}

static int MyWriteCoreDumpLimited(const char *file_name, size_t max_length) {
  struct CoreDumpParameters params;
  ClearCoreDumpParameters(&params);
  ASSERT(!SetCoreDumpLimited(&params, max_length),
         "flags=%d max=%lu", params.flags, max_length);
  return MyWriteCoreDumpWith(&params, file_name);
}

static int MyWriteCompressedCoreDump(
    const char *file_name, size_t max_length,
    const struct CoredumperCompressor compressors[],
    struct CoredumperCompressor **selected_compressor){
  struct CoreDumpParameters params;
  ClearCoreDumpParameters(&params);
  ASSERT(!SetCoreDumpLimited(&params, max_length),
         "flags=%d max=%lu", params.flags, max_length);
  ASSERT(!SetCoreDumpCompressed(&params, compressors, selected_compressor),
         "flags=%d", params.flags);
  return MyWriteCoreDumpWith(&params, file_name);
}

static int MyCallback(void *arg)
{
    int *count = arg;
    if (*count < 0) {
        return 1;
    }
    ++(*count);
    return 0;
}

/* Do not declare this function static, so that the compiler does not get
 * tempted to inline it. We want to be able to see some stack traces.
 */
void TestCoreDump() {
  static struct CoredumperCompressor my_compressor[] = {
  { "/NOSUCHDIR/NOSUCHFILE", 0,    0 },
  { 0,                       0,    0 }, /* Will be overwritten by test       */
  { 0,                       0,    0 } };

  int         loop, in[2], out[2], dummy, cmp, rc;
  pid_t       pid;
  FILE        *input, *output;
  pthread_t   thread;
  struct stat statBuf;
  struct CoredumperCompressor *compressor;
  struct CoreDumpParameters note_params;

  /* Make stdout unbuffered. We absolutely want to see all output, even
   * if the application aborted with an assertion failure.
   */
  setvbuf(stdout, NULL, _IONBF, 0);

  /* It is rather tricky to properly call fork() from within a multi-threaded
   * application. To simplify this problem, we fork and exec /bin/bash before
   * creating the first thread.
   */
  info("Forking /bin/bash process");
  rc = pipe(in);  ASSERT(!rc, "rc=%d", rc);
  rc = pipe(out); ASSERT(!rc, "rc=%d", rc);

  if ((pid = fork()) == 0) {
    int i, openmax;
    dup2(in[0],  0);
    dup2(out[1], 1);
    dup2(out[1], 2);
    openmax = sysconf(_SC_OPEN_MAX);
    for (i = 3; i < openmax; i++)
      close(i);
    fcntl(0, F_SETFD, 0);
    fcntl(1, F_SETFD, 0);
    fcntl(2, F_SETFD, 0);
    execl("/bin/bash", "bash", "-ex", NULL);
    _exit(1);
  }
  ASSERT(pid >= 0, "pid=%d", pid);
  ASSERT(!close(in[0]), "close in");
  ASSERT(!close(out[1]), "close out");
  input  = fdopen(in[1], "w");
  output = fdopen(out[0], "r");
  setvbuf(input, NULL, _IONBF, 0);
  setvbuf(output, NULL, _IONBF, 0);

  /* Create a random value in one of our auto variables; we will later look
   * for this value by inspecting the core file with gdb.
   */
  srand(time(0));
  dummy = random();
  cmp   = ~dummy;

  /* Start some threads that should show up in our core dump; this is
   * complicated by the fact that we do not want our threads to perform any
   * system calls. So, they are busy looping and checking a volatile
   * state variable, instead.
   */
  info("Starting threads");
  pthread_create(&thread, 0, Busy, (void *)&state1);
  pthread_create(&thread, 0, Busy, (void *)&state2);
  while (state1 != RUNNING || state2 != RUNNING) {
    usleep(100*1000);
  }

  const char *core_test = "core-test";
  const char *core_test_gz = "core-test.gz";
  for (loop = 0; loop < 2; loop++) {
    /* Prepare to create a core dump for the current process                 */
    info("loop %d: Writing core file to \"%s\"", loop, core_test);
    unlink(core_test);

    /* Check whether limits work correctly                                   */
    info("Check whether limits work correctly");
    rc = (loop ? MyWriteCoreDumpLimited : WriteCoreDumpLimited)(
           core_test, 0);
    ASSERT(!rc, "rc=%d", rc);
    ASSERT(stat(core_test, &statBuf) < 0, "stat=%s", core_test);
    rc = (loop ? MyWriteCoreDumpLimited : WriteCoreDumpLimited)(
           core_test, 256);
    ASSERT(!rc, "rc=%d", rc);
    ASSERT(!stat(core_test, &statBuf), "stat=%s", core_test);
    ASSERT(statBuf.st_size == 256, "size=%ld", statBuf.st_size);
    ASSERT(!unlink(core_test), "unlink=%s", core_test);

    /* Check whether prioritized limiting works correctly                    */
    if (loop) {
      info("Checking priority limited core files of size 0");
      rc = WriteCoreDumpLimitedByPriority(core_test, 0);
      ASSERT(!rc, "rc=%d", rc);
      ASSERT(stat(core_test, &statBuf) < 0, "stat=%s", core_test);
      info("Checking priority limited core files of size 256. "
           "This should truncate the header.");
      rc = WriteCoreDumpLimitedByPriority(core_test, 256);
      ASSERT(!rc, "rc=%d", rc);
      ASSERT(!stat(core_test, &statBuf), "stat=%s", core_test);
      ASSERT(statBuf.st_size == 256, "size=%ld", statBuf.st_size);
      info("Checking priority limited core files of size 60000. "
           "This will include a couple of complete segments as well as "
           "an incomplete segment.");
      rc = WriteCoreDumpLimitedByPriority(core_test, 60000);
      ASSERT(!rc, "rc=%d", rc);
      ASSERT(!stat(core_test, &statBuf), "stat=%s", core_test);
      ASSERT(statBuf.st_size == 60000, "size=%ld", statBuf.st_size);
      CheckWithReadElf(input, output, core_test, "", "cat", "");
      CheckPrioritizationWithReadElf(input, output, core_test);
      ASSERT(!unlink(core_test), "unlink=%s", core_test);
    }

    /* Check whether compression works                                       */
    info("Checking compressed core files");
    rc = (loop?MyWriteCompressedCoreDump:WriteCompressedCoreDump)
           (core_test, SIZE_MAX, COREDUMPER_GZIP_COMPRESSED,
            &compressor);
    ASSERT(!rc, "rc=%d loop=%d", rc, loop);
    ASSERT(compressor, "no compressor");
    ASSERT(strstr(compressor->compressor, "gzip"), "compressor=%s", compressor->compressor);
    ASSERT(!strcmp(compressor->suffix, ".gz"), "suffix=%s", compressor->suffix);
    CheckWithReadElf(input, output, core_test, compressor->suffix,
                     compressor->compressor, "-d");
    ASSERT(!unlink(core_test_gz), "unlink=%s", core_test_gz);

    /* Check wether fallback to uncompressed core files works                */
    info("Checking fallback to uncompressed core files");
    my_compressor[1].compressor = NULL; /* Disable uncompressed files        */
    rc = (loop?MyWriteCompressedCoreDump:WriteCompressedCoreDump)
           (core_test, SIZE_MAX, my_compressor, &compressor);
    ASSERT(rc, "rc=%d loop=%d", rc, loop);
    ASSERT(!compressor->compressor, "compressor");
    my_compressor[1].compressor = ""; /* Enable uncompressed files           */
    rc = (loop?MyWriteCompressedCoreDump:WriteCompressedCoreDump)
           (core_test, SIZE_MAX, my_compressor, &compressor);
    ASSERT(!rc, "rc=%d loop=%d", rc, loop);
    ASSERT(compressor->compressor, "no compressor");
    ASSERT(!*compressor->compressor, "empty compressor");
    CheckWithReadElf(input, output, core_test, "", "cat", "");
    ASSERT(!unlink(core_test), "unlink=%s", core_test);

    /* Check if additional notes are written correctly to the core file      */
    info("Checking extra notes in core files");
    ClearCoreDumpParameters(&note_params);
    ASSERT(!SetCoreDumpNotes(&note_params, extra_notes, kExtraNotesCount),
           "flags=%d", note_params.flags);
    ASSERT(!SetCoreDumpLimited(&note_params, 0x10000),
           "flags=%d", note_params.flags);
    rc = (loop?MyWriteCoreDumpWith:WriteCoreDumpWith)
          (&note_params, core_test);
    ASSERT(!rc, "rc=%d loop=%d", rc, loop);
    CheckWithReadElf(input, output, core_test, "", "cat", "");
    CheckExtraNotesWithReadElf(input, output, core_test);
    ASSERT(!unlink(core_test), "unlink=%s", core_test);

    /* Check callback function handling                                      */
    info("Checking callback functions");
    ClearCoreDumpParameters(&note_params);
    int count = 0;
    ASSERT(!SetCoreDumpCallback(&note_params, MyCallback, &count), "flags=%d", note_params.flags);
    ASSERT(!SetCoreDumpLimited(&note_params, 0x10000), "flags=%d", note_params.flags);
    rc = (loop?MyWriteCoreDumpWith:WriteCoreDumpWith)
          (&note_params, core_test);
    ASSERT(!rc, "rc=%d loop=%d", rc, loop);
    CheckWithReadElf(input, output, core_test, "", "cat", "");
    ASSERT(count == 1, "count=%d", count);
    ASSERT(!unlink(core_test), "unlink=%s", core_test);
    count = -1;
    rc = (loop?MyWriteCoreDumpWith:WriteCoreDumpWith)
          (&note_params, core_test);
    ASSERT(rc, "rc=%d loop=%d", rc, loop);
    ASSERT(count == -1, "count=%d", count);
    ASSERT(unlink(core_test) == -1 && errno == ENOENT,
           "unlink=%s errno=%d", core_test, errno);

    /* Create a full-size core file                                          */
    info("Checking uncompressed core files");
    rc = (loop?MyWriteCoreDump:WriteCoreDump)(core_test);
    ASSERT(!rc, "rc=%d loop=%d", rc, loop);
    CheckWithReadElf(input, output, core_test, "", "cat", "");
    CheckWithGDB(input, output, core_test, &dummy, cmp);

    unlink(core_test);
  }

  /* Stop our threads                                                        */
  info("Stopping threads");
  state1 = DEAD;
  state2 = DEAD;

  /* Kill bash process                                                       */
  kill(SIGTERM, pid);
  fclose(input);
  fclose(output);

  return;
}

int main(int argc, char *argv[]) {
  static int bloat[1024*1024];
  int i;

  /* This unittest parses the output from "readelf" and "gdb" in order to
   * verify that the core files look correct. And unfortunately, some of
   * the messages for these programs have been localized, so the unittest
   * cannot always find the text that it is looking for.
   * Let's just force everything back to English:
   */
  putenv(strdup("LANGUAGE=C"));
  putenv(strdup("LC_ALL=C"));

  /* Make our RSS a little bigger, so that we can test codepaths that do not
   * trigger for very small core files. Also, make sure that this data is
   * not easily compressible nor in a read-only memory segment.
   */
  for (i = 0; i < sizeof(bloat)/sizeof(int); i++) {
    bloat[i] = rand();
  }

  TestCoreDump();
  puts("PASS");
  return 0;
}
