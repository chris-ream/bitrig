#	$OpenBSD: Makefile,v 1.14 2014/01/11 03:35:57 deraadt Exp $

.include <bsd.own.mk>
LIB=    stubs
SRCS=	db.c sha1.c sha2.c getgrent.c getpwent.c \
	ethers.c \
	sscanf.c vfprintf.c vfscanf.c
#	mbrtowc_sb.c sscanf.c vfprintf.c vfscanf.c
SRCS+=	asr.c			\
	asr_utils.c		\
	getaddrinfo.c		\
	getaddrinfo_async.c	\
	gethostnamadr.c		\
	gethostnamadr_async.c	\
	getnameinfo.c		\
	getnameinfo_async.c	\
	getnetnamadr.c		\
	getnetnamadr_async.c	\
	getrrsetbyname.c	\
	getrrsetbyname_async.c	\
	res_debug.c		\
	res_init.c		\
	res_mkquery.c		\
	res_query.c		\
	res_search_async.c	\
	res_send.c		\
	res_send_async.c	\
	sethostent.c

NOPIC=	Yes
NOPROFILE=Yes

CFLAGS+=-D__DBINTERFACE_PRIVATE -DSHA2_SMALL -UYP -I${LIBCSRCDIR}/include \
	-DNO_LOG_BAD_DNS_RESPONSES -I${LIBCSRCDIR}/locale \
	-I${LIBCSRCDIR}/gdtoa -I${LIBCSRCDIR}/arch/${MACHINE_CPU}/gdtoa \
	-I${LIBCSRCDIR}/stdio -I${LIBCSRCDIR}/ -DFLOATING_POINT
CFLAGS+=-I${LIBCSRCDIR}/asr -DASRNODEBUG

LIBCSRCDIR=${.CURDIR}/../../../lib/libc

.PATH: ${.CURDIR}/../../../lib/libc/hash
.PATH: ${.CURDIR}/../../../lib/libc/gen
.PATH: ${.CURDIR}/../../../lib/libc/asr
.PATH: ${.CURDIR}/../../../lib/libc/net
.PATH: ${.CURDIR}/../../../lib/libc/stdio

.include <bsd.lib.mk>
