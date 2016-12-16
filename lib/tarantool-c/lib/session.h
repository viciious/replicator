#ifndef TB_SESSION_H_
#define TB_SESSION_H_

/*
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
*/

#ifdef __cplusplus
extern "C" {
#endif

/*
 * tarantool v1.6 network session
*/

#include <sys/time.h>

struct tbbuf {
	size_t off;
	size_t top;
	size_t size;
	char *buf;
};

struct tbses {
	char *host;
	int port;
	int connected;
	struct timeval tmc;
	int sbuf;
	int rbuf;
	int fd;
	struct timeval tmr;
	struct timeval tms;
	int noblock;
	int errno_;
	struct tbbuf s, r;
};

enum tbsesopt {
	TB_HOST,
	TB_PORT,
	TB_CONNECTTM,
	TB_SENDBUF,
	TB_READBUF,
	TB_RECVTM,
	TB_SENDTM,
	TB_NOBLOCK
};

int tb_sesinit(struct tbses*);
int tb_sesfree(struct tbses*);
int tb_sesset(struct tbses*, enum tbsesopt, ...);
int tb_sesconnect(struct tbses*);
int tb_sesclose(struct tbses*);
int tb_sessync(struct tbses*);
ssize_t tb_sessend(struct tbses*, char*, size_t);
ssize_t tb_sesrecv(struct tbses*, char*, size_t, int strict);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
