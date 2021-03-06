/**********************************************************************

  rubyio.h -

  $Author$
  created at: Fri Nov 12 16:47:09 JST 1993

  Copyright (C) 1993-2007 Yukihiro Matsumoto

**********************************************************************/

#ifndef RUBY_IO_H
#define RUBY_IO_H 1

#if defined(__cplusplus)
extern "C" {
#if 0
} /* satisfy cc-mode */
#endif
#endif

#include <stdio.h>
#include <errno.h>
#include "ruby/encoding.h"

#if defined(HAVE_STDIO_EXT_H)
#include <stdio_ext.h>
#endif

typedef struct rb_io_t {
    int fd;                     /* file descriptor */
    FILE *stdio_file;		/* stdio ptr for read/write if available */
    int mode;			/* mode flags: FMODE_XXXs */
    rb_pid_t pid;		/* child's pid (for pipes) */
    int lineno;			/* number of lines read */
    VALUE pathv;		/* pathname for file */
    void (*finalize)(struct rb_io_t*,int); /* finalize proc */

    char *wbuf;                 /* wbuf_off + wbuf_len <= wbuf_capa */
    int wbuf_off;
    int wbuf_len;
    int wbuf_capa;

    char *rbuf;                 /* rbuf_off + rbuf_len <= rbuf_capa */
    int rbuf_off;
    int rbuf_len;
    int rbuf_capa;

    VALUE tied_io_for_writing;

    /*
     * enc  enc2 read action                      write action
     * NULL NULL force_encoding(default_external) write the byte sequence of str
     * e1   NULL force_encoding(e1)               convert str.encoding to e1
     * e1   e2   convert from e2 to e1            convert str.encoding to e2
     */
    struct rb_io_enc_t {
        rb_encoding *enc;
        rb_encoding *enc2;
        int ecflags;
        VALUE ecopts;
    } encs;

    rb_econv_t *readconv;
    char *cbuf;                /* cbuf_off + cbuf_len <= cbuf_capa */
    int cbuf_off;
    int cbuf_len;
    int cbuf_capa;

    rb_econv_t *writeconv;
    VALUE writeconv_asciicompat;
    int writeconv_pre_ecflags;
    VALUE writeconv_pre_ecopts;
    int writeconv_initialized;

    VALUE write_lock;
} rb_io_t;

#define HAVE_RB_IO_T 1

#define FMODE_READABLE              0x00000001
#define FMODE_WRITABLE              0x00000002
#define FMODE_READWRITE             (FMODE_READABLE|FMODE_WRITABLE)
#define FMODE_BINMODE               0x00000004
#define FMODE_SYNC                  0x00000008
#define FMODE_TTY                   0x00000010
#define FMODE_DUPLEX                0x00000020
#define FMODE_APPEND                0x00000040
#define FMODE_CREATE                0x00000080
/* #define FMODE_NOREVLOOKUP        0x00000100 */
#define FMODE_WSPLIT                0x00000200
#define FMODE_WSPLIT_INITIALIZED    0x00000400
#define FMODE_TRUNC                 0x00000800
#define FMODE_TEXTMODE              0x00001000
#define FMODE_EOF                   0x00002000
/* #define FMODE_PREP               0x00010000 */
#define FMODE_STRIP_BOM             0x00100000

#define GetOpenFile(obj,fp) rb_io_check_closed((fp) = RFILE(rb_io_taint_check(obj))->fptr)

#define MakeOpenFile(obj, fp) do {\
    if (RFILE(obj)->fptr) {\
	rb_io_close(obj);\
	free(RFILE(obj)->fptr);\
	RFILE(obj)->fptr = 0;\
    }\
    fp = 0;\
    fp = RFILE(obj)->fptr = ALLOC(rb_io_t);\
    fp->fd = -1;\
    fp->stdio_file = NULL;\
    fp->mode = 0;\
    fp->pid = 0;\
    fp->lineno = 0;\
    fp->pathv = Qnil;\
    fp->finalize = 0;\
    fp->wbuf = NULL;\
    fp->wbuf_off = 0;\
    fp->wbuf_len = 0;\
    fp->wbuf_capa = 0;\
    fp->rbuf = NULL;\
    fp->rbuf_off = 0;\
    fp->rbuf_len = 0;\
    fp->rbuf_capa = 0;\
    fp->readconv = NULL;\
    fp->cbuf = NULL;\
    fp->cbuf_off = 0;\
    fp->cbuf_len = 0;\
    fp->cbuf_capa = 0;\
    fp->writeconv = NULL;\
    fp->writeconv_asciicompat = Qnil;\
    fp->writeconv_pre_ecflags = 0;\
    fp->writeconv_pre_ecopts = Qnil;\
    fp->writeconv_initialized = 0;\
    fp->tied_io_for_writing = 0;\
    fp->encs.enc = NULL;\
    fp->encs.enc2 = NULL;\
    fp->encs.ecflags = 0;\
    fp->encs.ecopts = Qnil;\
    fp->write_lock = 0;\
} while (0)

FILE *rb_io_stdio_file(rb_io_t *fptr);

FILE *rb_fdopen(int, const char*);
int rb_io_modestr_fmode(const char *modestr);
int rb_io_modestr_oflags(const char *modestr);
int rb_io_oflags_fmode(int oflags);
void rb_io_check_writable(rb_io_t*);
void rb_io_check_readable(rb_io_t*);
int rb_io_fptr_finalize(rb_io_t*);
void rb_io_synchronized(rb_io_t*);
void rb_io_check_initialized(rb_io_t*);
void rb_io_check_closed(rb_io_t*);
int rb_io_wait_readable(int);
int rb_io_wait_writable(int);
void rb_io_set_nonblock(rb_io_t *fptr);

/* compatibility for ruby 1.8 and older */
#define rb_io_mode_flags(modestr) rb_io_modestr_fmode(modestr)
#define rb_io_modenum_flags(oflags) rb_io_oflags_fmode(oflags)

VALUE rb_io_taint_check(VALUE);
NORETURN(void rb_eof_error(void));

void rb_io_read_check(rb_io_t*);
int rb_io_read_pending(rb_io_t*);
void rb_read_check(FILE*);

#if defined(__cplusplus)
#if 0
{ /* satisfy cc-mode */
#endif
}  /* extern "C" { */
#endif

#endif /* RUBY_IO_H */
