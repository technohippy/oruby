/************************************************

  ainfo.c -

  created at: Thu Mar 31 12:21:29 JST 1994

  Copyright (C) 1993-2007 Yukihiro Matsumoto

************************************************/

#include "rubysocket.h"

#if defined(INET6) && (defined(LOOKUP_ORDER_HACK_INET) || defined(LOOKUP_ORDER_HACK_INET6))
#define LOOKUP_ORDERS (sizeof(lookup_order_table) / sizeof(lookup_order_table[0]))
static const int lookup_order_table[] = {
#if defined(LOOKUP_ORDER_HACK_INET)
    PF_INET, PF_INET6, PF_UNSPEC,
#elif defined(LOOKUP_ORDER_HACK_INET6)
    PF_INET6, PF_INET, PF_UNSPEC,
#else
    /* should not happen */
#endif
};

static int
ruby_getaddrinfo(const char *nodename, const char *servname,
		 const struct addrinfo *hints, struct addrinfo **res)
{
    struct addrinfo tmp_hints;
    int i, af, error;

    if (hints->ai_family != PF_UNSPEC) {
	return getaddrinfo(nodename, servname, hints, res);
    }

    for (i = 0; i < LOOKUP_ORDERS; i++) {
	af = lookup_order_table[i];
	MEMCPY(&tmp_hints, hints, struct addrinfo, 1);
	tmp_hints.ai_family = af;
	error = getaddrinfo(nodename, servname, &tmp_hints, res);
	if (error) {
	    if (tmp_hints.ai_family == PF_UNSPEC) {
		break;
	    }
	}
	else {
	    break;
	}
    }

    return error;
}
#define getaddrinfo(node,serv,hints,res) ruby_getaddrinfo((node),(serv),(hints),(res))
#endif

#if defined(_AIX)
static int
ruby_getaddrinfo__aix(const char *nodename, const char *servname,
		      struct addrinfo *hints, struct addrinfo **res)
{
    int error = getaddrinfo(nodename, servname, hints, res);
    struct addrinfo *r;
    if (error)
	return error;
    for (r = *res; r != NULL; r = r->ai_next) {
	if (r->ai_addr->sa_family == 0)
	    r->ai_addr->sa_family = r->ai_family;
	if (r->ai_addr->sa_len == 0)
	    r->ai_addr->sa_len = r->ai_addrlen;
    }
    return 0;
}
#undef getaddrinfo
#define getaddrinfo(node,serv,hints,res) ruby_getaddrinfo__aix((node),(serv),(hints),(res))
static int
ruby_getnameinfo__aix(const struct sockaddr *sa, size_t salen,
		      char *host, size_t hostlen,
		      char *serv, size_t servlen, int flags)
{
    struct sockaddr_in6 *sa6;
    u_int32_t *a6;

    if (sa->sa_family == AF_INET6) {
	sa6 = (struct sockaddr_in6 *)sa;
	a6 = sa6->sin6_addr.u6_addr.u6_addr32;

	if (a6[0] == 0 && a6[1] == 0 && a6[2] == 0 && a6[3] == 0) {
	    strncpy(host, "::", hostlen);
	    snprintf(serv, servlen, "%d", sa6->sin6_port);
	    return 0;
	}
    }
    return getnameinfo(sa, salen, host, hostlen, serv, servlen, flags);
}
#undef getnameinfo
#define getnameinfo(sa, salen, host, hostlen, serv, servlen, flags) \
            ruby_getnameinfo__aix((sa), (salen), (host), (hostlen), (serv), (servlen), (flags))
#endif

#ifndef GETADDRINFO_EMU
struct getaddrinfo_arg
{
    const char *node;
    const char *service;
    const struct addrinfo *hints;
    struct addrinfo **res;
};

static VALUE
nogvl_getaddrinfo(void *arg)
{
    struct getaddrinfo_arg *ptr = arg;
    return getaddrinfo(ptr->node, ptr->service,
                       ptr->hints, ptr->res);
}
#endif

int
rb_getaddrinfo(const char *node, const char *service,
               const struct addrinfo *hints,
               struct addrinfo **res)
{
#ifdef GETADDRINFO_EMU
    return getaddrinfo(node, service, hints, res);
#else
    struct getaddrinfo_arg arg;
    int ret;
    arg.node = node;
    arg.service = service;
    arg.hints = hints;
    arg.res = res;
    ret = BLOCKING_REGION(nogvl_getaddrinfo, &arg);
    return ret;
#endif
}

#ifndef GETADDRINFO_EMU
struct getnameinfo_arg
{
    const struct sockaddr *sa;
    socklen_t salen;
    char *host;
    size_t hostlen;
    char *serv;
    size_t servlen;
    int flags;
};

static VALUE
nogvl_getnameinfo(void *arg)
{
    struct getnameinfo_arg *ptr = arg;
    return getnameinfo(ptr->sa, ptr->salen,
                       ptr->host, ptr->hostlen,
                       ptr->serv, ptr->servlen,
                       ptr->flags);
}
#endif

int
rb_getnameinfo(const struct sockaddr *sa, socklen_t salen,
           char *host, size_t hostlen,
           char *serv, size_t servlen, int flags)
{
#ifdef GETADDRINFO_EMU
    return getnameinfo(sa, salen, host, hostlen, serv, servlen, flags);
#else
    struct getnameinfo_arg arg;
    int ret;
    arg.sa = sa;
    arg.salen = salen;
    arg.host = host;
    arg.hostlen = hostlen;
    arg.serv = serv;
    arg.servlen = servlen;
    arg.flags = flags;
    ret = BLOCKING_REGION(nogvl_getnameinfo, &arg);
    return ret;
#endif
}

static void
make_ipaddr0(struct sockaddr *addr, char *buf, size_t len)
{
    int error;

    error = rb_getnameinfo(addr, SA_LEN(addr), buf, len, NULL, 0, NI_NUMERICHOST);
    if (error) {
        raise_socket_error("getnameinfo", error);
    }
}

VALUE
make_ipaddr(struct sockaddr *addr)
{
    char buf[1024];

    make_ipaddr0(addr, buf, sizeof(buf));
    return rb_str_new2(buf);
}

static void
make_inetaddr(long host, char *buf, size_t len)
{
    struct sockaddr_in sin;

    MEMZERO(&sin, struct sockaddr_in, 1);
    sin.sin_family = AF_INET;
    SET_SIN_LEN(&sin, sizeof(sin));
    sin.sin_addr.s_addr = host;
    make_ipaddr0((struct sockaddr*)&sin, buf, len);
}

static int
str_isnumber(const char *p)
{
    char *ep;

    if (!p || *p == '\0')
       return 0;
    ep = NULL;
    (void)STRTOUL(p, &ep, 10);
    if (ep && *ep == '\0')
       return 1;
    else
       return 0;
}

static char*
host_str(VALUE host, char *hbuf, size_t len, int *flags_ptr)
{
    if (NIL_P(host)) {
        return NULL;
    }
    else if (rb_obj_is_kind_of(host, rb_cInteger)) {
        unsigned long i = NUM2ULONG(host);

        make_inetaddr(htonl(i), hbuf, len);
        if (flags_ptr) *flags_ptr |= AI_NUMERICHOST;
        return hbuf;
    }
    else {
        char *name;

        SafeStringValue(host);
        name = RSTRING_PTR(host);
        if (!name || *name == 0 || (name[0] == '<' && strcmp(name, "<any>") == 0)) {
            make_inetaddr(INADDR_ANY, hbuf, len);
            if (flags_ptr) *flags_ptr |= AI_NUMERICHOST;
        }
        else if (name[0] == '<' && strcmp(name, "<broadcast>") == 0) {
            make_inetaddr(INADDR_BROADCAST, hbuf, len);
            if (flags_ptr) *flags_ptr |= AI_NUMERICHOST;
        }
        else if (strlen(name) >= len) {
            rb_raise(rb_eArgError, "hostname too long (%"PRIuSIZE")",
                strlen(name));
        }
        else {
            strcpy(hbuf, name);
        }
        return hbuf;
    }
}

static char*
port_str(VALUE port, char *pbuf, size_t len, int *flags_ptr)
{
    if (NIL_P(port)) {
        return 0;
    }
    else if (FIXNUM_P(port)) {
        snprintf(pbuf, len, "%ld", FIX2LONG(port));
#ifdef AI_NUMERICSERV
        if (flags_ptr) *flags_ptr |= AI_NUMERICSERV;
#endif
        return pbuf;
    }
    else {
        char *serv;

        SafeStringValue(port);
        serv = RSTRING_PTR(port);
        if (strlen(serv) >= len) {
            rb_raise(rb_eArgError, "service name too long (%"PRIuSIZE")",
                strlen(serv));
        }
        strcpy(pbuf, serv);
        return pbuf;
    }
}

struct addrinfo*
sock_getaddrinfo(VALUE host, VALUE port, struct addrinfo *hints, int socktype_hack)
{
    struct addrinfo* res = NULL;
    char *hostp, *portp;
    int error;
    char hbuf[NI_MAXHOST], pbuf[NI_MAXSERV];
    int additional_flags = 0;

    hostp = host_str(host, hbuf, sizeof(hbuf), &additional_flags);
    portp = port_str(port, pbuf, sizeof(pbuf), &additional_flags);

    if (socktype_hack && hints->ai_socktype == 0 && str_isnumber(portp)) {
       hints->ai_socktype = SOCK_DGRAM;
    }
    hints->ai_flags |= additional_flags;

    error = rb_getaddrinfo(hostp, portp, hints, &res);
    if (error) {
        if (hostp && hostp[strlen(hostp)-1] == '\n') {
            rb_raise(rb_eSocket, "newline at the end of hostname");
        }
        raise_socket_error("getaddrinfo", error);
    }

#if defined(__APPLE__) && defined(__MACH__)
    {
        struct addrinfo *r;
        r = res;
        while (r) {
            if (! r->ai_socktype) r->ai_socktype = hints->ai_socktype;
            if (! r->ai_protocol) {
                if (r->ai_socktype == SOCK_DGRAM) {
                    r->ai_protocol = IPPROTO_UDP;
                }
                else if (r->ai_socktype == SOCK_STREAM) {
                    r->ai_protocol = IPPROTO_TCP;
                }
            }
            r = r->ai_next;
        }
    }
#endif
    return res;
}

struct addrinfo*
sock_addrinfo(VALUE host, VALUE port, int socktype, int flags)
{
    struct addrinfo hints;

    MEMZERO(&hints, struct addrinfo, 1);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = socktype;
    hints.ai_flags = flags;
    return sock_getaddrinfo(host, port, &hints, 1);
}

VALUE
ipaddr(struct sockaddr *sockaddr, int norevlookup)
{
    VALUE family, port, addr1, addr2;
    VALUE ary;
    int error;
    char hbuf[1024], pbuf[1024];
    ID id;

    id = intern_family(sockaddr->sa_family);
    if (id) {
        family = rb_str_dup(rb_id2str(id));
    }
    else {
        sprintf(pbuf, "unknown:%d", sockaddr->sa_family);
        family = rb_str_new2(pbuf);
    }

    addr1 = Qnil;
    if (!norevlookup) {
        error = rb_getnameinfo(sockaddr, SA_LEN(sockaddr), hbuf, sizeof(hbuf),
                               NULL, 0, 0);
        if (! error) {
            addr1 = rb_str_new2(hbuf);
        }
    }
    error = rb_getnameinfo(sockaddr, SA_LEN(sockaddr), hbuf, sizeof(hbuf),
                           pbuf, sizeof(pbuf), NI_NUMERICHOST | NI_NUMERICSERV);
    if (error) {
        raise_socket_error("getnameinfo", error);
    }
    addr2 = rb_str_new2(hbuf);
    if (addr1 == Qnil) {
        addr1 = addr2;
    }
    port = INT2FIX(atoi(pbuf));
    ary = rb_ary_new3(4, family, port, addr1, addr2);

    return ary;
}

#ifdef HAVE_SYS_UN_H
const char*
unixpath(struct sockaddr_un *sockaddr, socklen_t len)
{
    if (sockaddr->sun_path < (char*)sockaddr + len)
        return sockaddr->sun_path;
    else
        return "";
}

VALUE
unixaddr(struct sockaddr_un *sockaddr, socklen_t len)
{
    return rb_assoc_new(rb_str_new2("AF_UNIX"),
                        rb_str_new2(unixpath(sockaddr, len)));
}
#endif

struct hostent_arg {
    VALUE host;
    struct addrinfo* addr;
    VALUE (*ipaddr)(struct sockaddr*, size_t);
};

static VALUE
make_hostent_internal(struct hostent_arg *arg)
{
    VALUE host = arg->host;
    struct addrinfo* addr = arg->addr;
    VALUE (*ipaddr)(struct sockaddr*, size_t) = arg->ipaddr;

    struct addrinfo *ai;
    struct hostent *h;
    VALUE ary, names;
    char **pch;
    const char* hostp;
    char hbuf[NI_MAXHOST];

    ary = rb_ary_new();
    if (addr->ai_canonname) {
        hostp = addr->ai_canonname;
    }
    else {
        hostp = host_str(host, hbuf, sizeof(hbuf), NULL);
    }
    rb_ary_push(ary, rb_str_new2(hostp));

    if (addr->ai_canonname && (h = gethostbyname(addr->ai_canonname))) {
        names = rb_ary_new();
        if (h->h_aliases != NULL) {
            for (pch = h->h_aliases; *pch; pch++) {
                rb_ary_push(names, rb_str_new2(*pch));
            }
        }
    }
    else {
        names = rb_ary_new2(0);
    }
    rb_ary_push(ary, names);
    rb_ary_push(ary, INT2NUM(addr->ai_family));
    for (ai = addr; ai; ai = ai->ai_next) {
        rb_ary_push(ary, (*ipaddr)(ai->ai_addr, ai->ai_addrlen));
    }

    return ary;
}

VALUE
make_hostent(VALUE host, struct addrinfo *addr, VALUE (*ipaddr)(struct sockaddr *, size_t))
{
    struct hostent_arg arg;

    arg.host = host;
    arg.addr = addr;
    arg.ipaddr = ipaddr;
    return rb_ensure(make_hostent_internal, (VALUE)&arg,
                     RUBY_METHOD_FUNC(freeaddrinfo), (VALUE)addr);
}

typedef struct {
    VALUE inspectname;
    VALUE canonname;
    int pfamily;
    int socktype;
    int protocol;
    size_t sockaddr_len;
    struct sockaddr_storage addr;
} rb_addrinfo_t;

static void
addrinfo_mark(rb_addrinfo_t *rai)
{
    if (rai) {
        rb_gc_mark(rai->inspectname);
        rb_gc_mark(rai->canonname);
    }
}

static void
addrinfo_free(rb_addrinfo_t *rai)
{
    xfree(rai);
}

static VALUE
addrinfo_s_allocate(VALUE klass)
{
    return Data_Wrap_Struct(klass, addrinfo_mark, addrinfo_free, 0);
}

#define IS_ADDRINFO(obj) (RDATA(obj)->dmark == (RUBY_DATA_FUNC)addrinfo_mark)
static rb_addrinfo_t *
check_addrinfo(VALUE self)
{
    Check_Type(self, RUBY_T_DATA);
    if (!IS_ADDRINFO(self)) {
        rb_raise(rb_eTypeError, "wrong argument type %s (expected AddrInfo)",
                 rb_class2name(CLASS_OF(self)));
    }
    return DATA_PTR(self);
}

static rb_addrinfo_t *
get_addrinfo(VALUE self)
{
    rb_addrinfo_t *rai = check_addrinfo(self);

    if (!rai) {
        rb_raise(rb_eTypeError, "uninitialized socket address");
    }
    return rai;
}


static rb_addrinfo_t *
alloc_addrinfo()
{
    rb_addrinfo_t *rai = ALLOC(rb_addrinfo_t);
    memset(rai, 0, sizeof(rb_addrinfo_t));
    rai->inspectname = Qnil;
    rai->canonname = Qnil;
    return rai;
}

static void
init_addrinfo(rb_addrinfo_t *rai, struct sockaddr *sa, size_t len,
              int pfamily, int socktype, int protocol,
              VALUE canonname, VALUE inspectname)
{
    if (sizeof(rai->addr) < len)
        rb_raise(rb_eArgError, "sockaddr string too big");
    memcpy((void *)&rai->addr, (void *)sa, len);
    rai->sockaddr_len = len;

    rai->pfamily = pfamily;
    rai->socktype = socktype;
    rai->protocol = protocol;
    rai->canonname = canonname;
    rai->inspectname = inspectname;
}

static VALUE
addrinfo_new(struct sockaddr *addr, socklen_t len,
             int family, int socktype, int protocol,
             VALUE canonname, VALUE inspectname)
{
    VALUE a;
    rb_addrinfo_t *rai;

    a = addrinfo_s_allocate(rb_cAddrInfo);
    DATA_PTR(a) = rai = alloc_addrinfo();
    init_addrinfo(rai, addr, len, family, socktype, protocol, canonname, inspectname);
    return a;
}

static struct addrinfo *
call_getaddrinfo(VALUE node, VALUE service,
                 VALUE family, VALUE socktype, VALUE protocol, VALUE flags,
                 int socktype_hack)
{
    struct addrinfo hints, *res;

    MEMZERO(&hints, struct addrinfo, 1);
    hints.ai_family = NIL_P(family) ? PF_UNSPEC : family_arg(family);

    if (!NIL_P(socktype)) {
	hints.ai_socktype = socktype_arg(socktype);
    }
    if (!NIL_P(protocol)) {
	hints.ai_protocol = NUM2INT(protocol);
    }
    if (!NIL_P(flags)) {
	hints.ai_flags = NUM2INT(flags);
    }
    res = sock_getaddrinfo(node, service, &hints, socktype_hack);

    if (res == NULL)
	rb_raise(rb_eSocket, "host not found");
    return res;
}

static VALUE make_inspectname(VALUE node, VALUE service, struct addrinfo *res);

static void
init_addrinfo_getaddrinfo(rb_addrinfo_t *rai, VALUE node, VALUE service,
                          VALUE family, VALUE socktype, VALUE protocol, VALUE flags,
                          VALUE inspectnode, VALUE inspectservice)
{
    struct addrinfo *res = call_getaddrinfo(node, service, family, socktype, protocol, flags, 1);
    VALUE canonname;
    VALUE inspectname = rb_str_equal(node, inspectnode) ? Qnil : make_inspectname(inspectnode, inspectservice, res);

    canonname = Qnil;
    if (res->ai_canonname) {
        canonname = rb_tainted_str_new_cstr(res->ai_canonname);
        OBJ_FREEZE(canonname);
    }

    init_addrinfo(rai, res->ai_addr, res->ai_addrlen,
                  NUM2INT(family), NUM2INT(socktype), NUM2INT(protocol),
                  canonname, inspectname);

    freeaddrinfo(res);
}

static VALUE
make_inspectname(VALUE node, VALUE service, struct addrinfo *res)
{
    VALUE inspectname = Qnil;

    if (res) {
        char hbuf[NI_MAXHOST], pbuf[NI_MAXSERV];
        int ret;
        ret = rb_getnameinfo(res->ai_addr, res->ai_addrlen, hbuf,
                             sizeof(hbuf), pbuf, sizeof(pbuf),
                             NI_NUMERICHOST|NI_NUMERICSERV);
        if (ret == 0) {
            if (TYPE(node) == T_STRING && strcmp(hbuf, RSTRING_PTR(node)) == 0)
                node = Qnil;
            if (TYPE(service) == T_STRING && strcmp(pbuf, RSTRING_PTR(service)) == 0)
                service = Qnil;
            else if (TYPE(service) == T_FIXNUM && atoi(pbuf) == FIX2INT(service))
                service = Qnil;
        }
    }

    if (TYPE(node) == T_STRING) {
        inspectname = rb_str_dup(node);
    }
    if (TYPE(service) == T_STRING) {
        if (NIL_P(inspectname))
            inspectname = rb_sprintf(":%s", StringValueCStr(service));
        else
            rb_str_catf(inspectname, ":%s", StringValueCStr(service));
    }
    else if (TYPE(service) == T_FIXNUM && FIX2INT(service) != 0)
    {
        if (NIL_P(inspectname))
            inspectname = rb_sprintf(":%d", FIX2INT(service));
        else
            rb_str_catf(inspectname, ":%d", FIX2INT(service));
    }
    if (!NIL_P(inspectname)) {
        OBJ_INFECT(inspectname, node);
        OBJ_INFECT(inspectname, service);
        OBJ_FREEZE(inspectname);
    }
    return inspectname;
}

static VALUE
addrinfo_firstonly_new(VALUE node, VALUE service, VALUE family, VALUE socktype, VALUE protocol, VALUE flags)
{
    VALUE ret;
    VALUE canonname;
    VALUE inspectname;

    struct addrinfo *res = call_getaddrinfo(node, service, family, socktype, protocol, flags, 0);

    inspectname = make_inspectname(node, service, res);

    canonname = Qnil;
    if (res->ai_canonname) {
        canonname = rb_tainted_str_new_cstr(res->ai_canonname);
        OBJ_FREEZE(canonname);
    }

    ret = addrinfo_new(res->ai_addr, res->ai_addrlen,
                       res->ai_family, res->ai_socktype, res->ai_protocol,
                       canonname, inspectname);

    freeaddrinfo(res);
    return ret;
}

static VALUE
addrinfo_list_new(VALUE node, VALUE service, VALUE family, VALUE socktype, VALUE protocol, VALUE flags)
{
    VALUE ret;
    struct addrinfo *r;
    VALUE inspectname;

    struct addrinfo *res = call_getaddrinfo(node, service, family, socktype, protocol, flags, 0);

    inspectname = make_inspectname(node, service, res);

    ret = rb_ary_new();
    for (r = res; r; r = r->ai_next) {
        VALUE addr;
        VALUE canonname = Qnil;

        if (r->ai_canonname) {
            canonname = rb_tainted_str_new_cstr(r->ai_canonname);
            OBJ_FREEZE(canonname);
        }

        addr = addrinfo_new(r->ai_addr, r->ai_addrlen,
                            r->ai_family, r->ai_socktype, r->ai_protocol,
                            canonname, inspectname);

        rb_ary_push(ret, addr);
    }

    freeaddrinfo(res);
    return ret;
}


#ifdef HAVE_SYS_UN_H
static void
init_unix_addrinfo(rb_addrinfo_t *rai, VALUE path)
{
    struct sockaddr_un un;

    StringValue(path);

    if (sizeof(un.sun_path) <= RSTRING_LEN(path))
        rb_raise(rb_eArgError, "too long unix socket path (max: %dbytes)",
            (int)sizeof(un.sun_path)-1);

    MEMZERO(&un, struct sockaddr_un, 1);

    un.sun_family = AF_UNIX;
    memcpy((void*)&un.sun_path, RSTRING_PTR(path), RSTRING_LEN(path));
   
    init_addrinfo(rai, (struct sockaddr *)&un, sizeof(un), AF_UNIX, SOCK_STREAM, 0, Qnil, Qnil);
}
#endif

/*
 * call-seq:
 *   AddrInfo.new(sockaddr)                             => addrinfo
 *   AddrInfo.new(sockaddr, family)                     => addrinfo
 *   AddrInfo.new(sockaddr, family, socktype)           => addrinfo
 *   AddrInfo.new(sockaddr, family, socktype, protocol) => addrinfo
 *
 * returns a new instance of AddrInfo.
 * It the instnace contains sockaddr, family, socktype, protocol.
 * sockaddr means struct sockaddr which can be used for connect(2), etc.
 * family, socktype and protocol are integers which is used for arguments of socket(2).
 *
 * sockaddr is specified as an array or a string.
 * The array should be compatible to the value of IPSocket#addr or UNIXSocket#addr.
 * The string should be struct sockaddr as generated by
 * Socket.sockaddr_in or Socket.unpack_sockaddr_un.
 *
 * sockaddr examples:
 * - ["AF_INET", 46102, "localhost.localdomain", "127.0.0.1"] 
 * - ["AF_INET6", 42304, "ip6-localhost", "::1"] 
 * - ["AF_UNIX", "/tmp/sock"] 
 * - Socket.sockaddr_in("smtp", "2001:DB8::1")
 * - Socket.sockaddr_in(80, "172.18.22.42")
 * - Socket.sockaddr_in(80, "www.ruby-lang.org")
 * - Socket.sockaddr_un("/tmp/sock")
 *
 * In an AF_INET/AF_INET6 sockaddr array, the 4th element,
 * numeric IP address, is used to construct socket address in the AddrInfo instance.
 * The 3rd element, textual host name, is also recorded but only used for AddrInfo#inspect.
 *
 * family is specified as an integer to specify the protocol family such as Socket::PF_INET.
 * It can be a symbol or a string which is the constant name
 * with or without PF_ prefix such as :INET, :INET6, :UNIX, "PF_INET", etc.
 * If ommitted, PF_UNSPEC is assumed.
 *
 * socktype is specified as an integer to specify the socket type such as Socket::SOCK_STREAM.
 * It can be a symbol or a string which is the constant name
 * with or without SOCK_ prefix such as :STREAM, :DGRAM, :RAW, "SOCK_STREAM", etc.
 * If ommitted, 0 is assumed.
 *
 * protocol is specified as an integer to specify the protocol such as Socket::IPPROTO_TCP.
 * It must be an integer, unlike family and socktype.
 * If ommitted, 0 is assumed.
 * Note that 0 is reasonable value for most protocols, except raw socket.
 *
 */
static VALUE
addrinfo_initialize(int argc, VALUE *argv, VALUE self)
{
    rb_addrinfo_t *rai;
    VALUE sockaddr_arg, sockaddr_ary, pfamily, socktype, protocol;
    int i_pfamily, i_socktype, i_protocol;
    struct sockaddr *sockaddr_ptr;
    size_t sockaddr_len;
    VALUE canonname = Qnil, inspectname = Qnil;

    if (check_addrinfo(self))
        rb_raise(rb_eTypeError, "already initialized socket address");
    DATA_PTR(self) = rai = alloc_addrinfo();

    rb_scan_args(argc, argv, "13", &sockaddr_arg, &pfamily, &socktype, &protocol);

    i_pfamily = NIL_P(pfamily) ? PF_UNSPEC : family_arg(pfamily);
    i_socktype = NIL_P(socktype) ? 0 : socktype_arg(socktype);
    i_protocol = NIL_P(protocol) ? 0 : NUM2INT(protocol);

    sockaddr_ary = rb_check_array_type(sockaddr_arg);
    if (!NIL_P(sockaddr_ary)) {
        VALUE afamily = rb_ary_entry(sockaddr_ary, 0);
        int af;
        StringValue(afamily);
        if (family_to_int(RSTRING_PTR(afamily), RSTRING_LEN(afamily), &af) == -1)
	    rb_raise(rb_eSocket, "unknown address family: %s", StringValueCStr(afamily));
        switch (af) {
          case AF_INET: /* ["AF_INET", 46102, "localhost.localdomain", "127.0.0.1"] */
#ifdef INET6
          case AF_INET6: /* ["AF_INET6", 42304, "ip6-localhost", "::1"] */
#endif
          {
            VALUE service = rb_ary_entry(sockaddr_ary, 1);
            VALUE nodename = rb_ary_entry(sockaddr_ary, 2);
            VALUE numericnode = rb_ary_entry(sockaddr_ary, 3);
            int flags;

            service = INT2NUM(NUM2INT(service));
            if (!NIL_P(nodename))
                StringValue(nodename);
            StringValue(numericnode);
            flags = AI_NUMERICHOST;
#ifdef AI_NUMERICSERV
            flags |= AI_NUMERICSERV;
#endif

            init_addrinfo_getaddrinfo(rai, numericnode, service,
                    INT2NUM(i_pfamily ? i_pfamily : af), INT2NUM(i_socktype), INT2NUM(i_protocol),
                    INT2NUM(flags),
                    nodename, service);
            break;
          }

#ifdef HAVE_SYS_UN_H
          case AF_UNIX: /* ["AF_UNIX", "/tmp/sock"] */
          {
            VALUE path = rb_ary_entry(sockaddr_ary, 1);
            StringValue(path);
            init_unix_addrinfo(rai, path);
            break;
          }
#endif

          default:
            rb_raise(rb_eSocket, "unexpected address family");
        }
    }
    else {
        StringValue(sockaddr_arg);
        sockaddr_ptr = (struct sockaddr *)RSTRING_PTR(sockaddr_arg);
        sockaddr_len = RSTRING_LEN(sockaddr_arg);
        init_addrinfo(rai, sockaddr_ptr, sockaddr_len,
                      i_pfamily, i_socktype, i_protocol,
                      canonname, inspectname);
    }

    return self;
}

static int
get_afamily(struct sockaddr *addr, socklen_t len)
{
    if ((char*)&addr->sa_family + sizeof(addr->sa_family) - (char*)addr <= len)
        return addr->sa_family;
    else
        return AF_UNSPEC;
}

static int
ai_get_afamily(rb_addrinfo_t *rai)
{
    return get_afamily((struct sockaddr *)&rai->addr, rai->sockaddr_len);
}

/*
 * call-seq:
 *   addrinfo.inspect => string
 *
 * returns a string which shows addrinfo in human-readable form.
 *
 *   AddrInfo.tcp("localhost", 80).inspect #=> "#<AddrInfo: 127.0.0.1:80 TCP (localhost:80)>"
 *   AddrInfo.unix("/tmp/sock").inspect    #=> "#<AddrInfo: /tmp/sock SOCK_STREAM>"
 *
 */
static VALUE
addrinfo_inspect(VALUE self)
{
    rb_addrinfo_t *rai = get_addrinfo(self);
    int internet_p;
    VALUE ret;

    ret = rb_sprintf("#<%s: ", rb_obj_classname(self));

    if (rai->sockaddr_len == 0) {
        rb_str_cat2(ret, "empty-sockaddr");
    }
    else if (rai->sockaddr_len < ((char*)&rai->addr.ss_family + sizeof(rai->addr.ss_family)) - (char*)&rai->addr)
        rb_str_cat2(ret, "too-short-sockaddr");
    else {
        switch (rai->addr.ss_family) {
          case AF_INET:
          {
            struct sockaddr_in *addr;
            int port;
            if (rai->sockaddr_len < sizeof(struct sockaddr_in)) {
                rb_str_cat2(ret, "too-short-AF_INET-sockaddr");
            }
            else {
                addr = (struct sockaddr_in *)&rai->addr;
                rb_str_catf(ret, "%d.%d.%d.%d",
                            ((unsigned char*)&addr->sin_addr)[0],
                            ((unsigned char*)&addr->sin_addr)[1],
                            ((unsigned char*)&addr->sin_addr)[2],
                            ((unsigned char*)&addr->sin_addr)[3]);
                port = ntohs(addr->sin_port);
                if (port)
                    rb_str_catf(ret, ":%d", port);
                if (sizeof(struct sockaddr_in) < rai->sockaddr_len)
                    rb_str_catf(ret, "(sockaddr %d bytes too long)", (int)(rai->sockaddr_len - sizeof(struct sockaddr_in)));
            }
            break;
          }

#ifdef INET6
          case AF_INET6:
          {
            struct sockaddr_in6 *addr;
            char hbuf[1024];
            int port;
            int error;
            if (rai->sockaddr_len < sizeof(struct sockaddr_in6)) {
                rb_str_cat2(ret, "too-short-AF_INET6-sockaddr");
            }
            else {
                addr = (struct sockaddr_in6 *)&rai->addr;
                /* use getnameinfo for scope_id.
                 * RFC 4007: IPv6 Scoped Address Architecture
                 * draft-ietf-ipv6-scope-api-00.txt: Scoped Address Extensions to the IPv6 Basic Socket API
                 */
                error = getnameinfo((struct sockaddr *)&rai->addr, rai->sockaddr_len,
                                    hbuf, sizeof(hbuf), NULL, 0,
                                    NI_NUMERICHOST|NI_NUMERICSERV);
                if (error) {
                    raise_socket_error("getnameinfo", error);
                }
                if (addr->sin6_port == 0) {
                    rb_str_cat2(ret, hbuf);
                }
                else {
                    port = ntohs(addr->sin6_port);
                    rb_str_catf(ret, "[%s]:%d", hbuf, port);
                }
                if (sizeof(struct sockaddr_in6) < rai->sockaddr_len)
                    rb_str_catf(ret, "(sockaddr %d bytes too long)", (int)(rai->sockaddr_len - sizeof(struct sockaddr_in6)));
            }
            break;
          }
#endif

#ifdef HAVE_SYS_UN_H
          case AF_UNIX:
          {
            struct sockaddr_un *addr = (struct sockaddr_un *)&rai->addr;
            char *p, *s, *t, *e;
            s = addr->sun_path;
            e = (char*)addr + rai->sockaddr_len;
            if (e < s)
                rb_str_cat2(ret, "too-short-AF_UNIX-sockaddr");
            else if (s == e)
                rb_str_cat2(ret, "empty-path-AF_UNIX-sockaddr");
            else {
                int printable_only = 1;
                p = s;
                while (p < e && *p != '\0') {
                    printable_only = printable_only && ISPRINT(*p) && !ISSPACE(*p);
                    p++;
                }
                t = p;
                while (p < e && *p == '\0')
                    p++;
                if (printable_only && /* only printable, no space */
                    t < e && /* NUL terminated */
                    p == e) { /* no data after NUL */
		    if (s == t)
			rb_str_cat2(ret, "empty-path-AF_UNIX-sockaddr");
		    else if (s[0] == '/') /* absolute path */
			rb_str_cat2(ret, s);
		    else
                        rb_str_catf(ret, "AF_UNIX %s", s);
                }
                else {
                    rb_str_cat2(ret, "AF_UNIX");
                    e = (char *)addr->sun_path + sizeof(addr->sun_path);
                    while (s < e && *(e-1) == '\0')
                        e--;
                    while (s < e)
                        rb_str_catf(ret, ":%02x", (unsigned char)*s++);
                }
                if (addr->sun_path + sizeof(addr->sun_path) < (char*)&rai->addr + rai->sockaddr_len)
                    rb_str_catf(ret, "(sockaddr %d bytes too long)",
                            (int)(rai->sockaddr_len - (addr->sun_path + sizeof(addr->sun_path) - (char*)&rai->addr)));
            }
            break;
          }
#endif

          default:
          {
            ID id = intern_family(rai->addr.ss_family);
            if (id == 0)
                rb_str_catf(ret, "unknown address family %d", rai->addr.ss_family);
            else
                rb_str_catf(ret, "%s address format unknown", rb_id2name(id));
            break;
          }
        }
    }

    if (rai->pfamily && ai_get_afamily(rai) != rai->pfamily) {
        ID id = intern_protocol_family(rai->pfamily);
        if (id)
            rb_str_catf(ret, " %s", rb_id2name(id));
        else
            rb_str_catf(ret, " PF_\?\?\?(%d)", rai->pfamily);
    }

    internet_p = rai->pfamily == PF_INET;
#ifdef INET6
    internet_p = internet_p || rai->pfamily == PF_INET6;
#endif
    if (internet_p && rai->socktype == SOCK_STREAM &&
        (rai->protocol == 0 || rai->protocol == IPPROTO_TCP)) {
        rb_str_cat2(ret, " TCP");
    }
    else if (internet_p && rai->socktype == SOCK_DGRAM &&
        (rai->protocol == 0 || rai->protocol == IPPROTO_UDP)) {
        rb_str_cat2(ret, " UDP");
    }
    else {
        if (rai->socktype) {
            ID id = intern_socktype(rai->socktype);
            if (id)
                rb_str_catf(ret, " %s", rb_id2name(id));
            else
                rb_str_catf(ret, " SOCK_\?\?\?(%d)", rai->socktype);
        }

        if (rai->protocol) {
            if (internet_p) {
                ID id = intern_ipproto(rai->protocol);
                if (id)
                    rb_str_catf(ret, " %s", rb_id2name(id));
                else
                    goto unknown_protocol;
            }
            else {
              unknown_protocol:
                rb_str_catf(ret, " UNKNOWN_PROTOCOL(%d)", rai->protocol);
            }
        }
    }

    if (!NIL_P(rai->canonname)) {
        VALUE name = rai->canonname;
        rb_str_catf(ret, " %s", StringValueCStr(name));
    }

    if (!NIL_P(rai->inspectname)) {
        VALUE name = rai->inspectname;
        rb_str_catf(ret, " (%s)", StringValueCStr(name));
    }

    rb_str_buf_cat2(ret, ">");
    return ret;
}

/*
 * call-seq:
 *   addrinfo.afamily => integer
 *
 * returns the address family as an integer.
 *
 *   AddrInfo.tcp("localhost", 80).afamily == Socket::AF_INET #=> true
 *
 */
static VALUE
addrinfo_afamily(VALUE self)
{
    rb_addrinfo_t *rai = get_addrinfo(self);
    return INT2NUM(ai_get_afamily(rai));
}

/*
 * call-seq:
 *   addrinfo.pfamily => integer
 *
 * returns the protocol family as an integer.
 *
 *   AddrInfo.tcp("localhost", 80).pfamily == Socket::PF_INET #=> true
 *
 */
static VALUE
addrinfo_pfamily(VALUE self)
{
    rb_addrinfo_t *rai = get_addrinfo(self);
    return INT2NUM(rai->pfamily);
}

/*
 * call-seq:
 *   addrinfo.socktype => integer
 *
 * returns the socket type as an integer.
 *
 *   AddrInfo.tcp("localhost", 80).socktype == Socket::SOCK_STREAM #=> true
 *
 */
static VALUE
addrinfo_socktype(VALUE self)
{
    rb_addrinfo_t *rai = get_addrinfo(self);
    return INT2NUM(rai->socktype);
}

/*
 * call-seq:
 *   addrinfo.protocol => integer
 *
 * returns the socket type as an integer.
 *
 *   AddrInfo.tcp("localhost", 80).protocol == Socket::IPPROTO_TCP #=> true
 *
 */
static VALUE
addrinfo_protocol(VALUE self)
{
    rb_addrinfo_t *rai = get_addrinfo(self);
    return INT2NUM(rai->protocol);
}

/*
 * call-seq:
 *   addrinfo.to_sockaddr => string
 *
 * returns the socket address as packed struct sockaddr string.
 *
 *   AddrInfo.tcp("localhost", 80).to_sockaddr                    
 *   #=> "\x02\x00\x00P\x7F\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00"
 *
 */
static VALUE
addrinfo_to_sockaddr(VALUE self)
{
    rb_addrinfo_t *rai = get_addrinfo(self);
    VALUE ret;
    ret = rb_str_new((char*)&rai->addr, rai->sockaddr_len);
    OBJ_INFECT(ret, self);
    return ret;
}

/*
 * call-seq:
 *   addrinfo.canonname => string or nil
 *
 * returns the canonical name as an string.
 *
 * nil is returned if no canonical name.
 *
 * The canonical name is set by AddrInfo.getaddrinfo when AI_CANONNAME is specified.
 *
 *   list = AddrInfo.getaddrinfo("www.ruby-lang.org", 80, :INET, :STREAM, nil, Socket::AI_CANONNAME)
 *   p list[0] #=> #<AddrInfo: 221.186.184.68:80 TCP carbon.ruby-lang.org (www.ruby-lang.org:80)>
 *   p list[0].canonname #=> "carbon.ruby-lang.org"
 *
 */
static VALUE
addrinfo_canonname(VALUE self)
{
    rb_addrinfo_t *rai = get_addrinfo(self);
    return rai->canonname;
}

#ifdef AF_INET6
# define IS_IP_FAMILY(af) ((af) == AF_INET || (af) == AF_INET6)
#else
# define IS_IP_FAMILY(af) ((af) == AF_INET)
#endif

/*
 * call-seq:
 *   addrinfo.ip? => true or false
 *
 * returns true if addrinfo is internet (IPv4/IPv6) address.
 * returns false otherwise.
 *
 *   AddrInfo.tcp("127.0.0.1", 80).ip? #=> true
 *   AddrInfo.tcp("::1", 80).ip?       #=> true
 *   AddrInfo.unix("/tmp/sock").ip?    #=> false
 *
 */
static VALUE
addrinfo_ip_p(VALUE self)
{
    rb_addrinfo_t *rai = get_addrinfo(self);
    int family = ai_get_afamily(rai);
    return IS_IP_FAMILY(family) ? Qtrue : Qfalse;
}

/*
 * call-seq:
 *   addrinfo.ipv4? => true or false
 *
 * returns true if addrinfo is IPv4 address.
 * returns false otherwise.
 *
 *   AddrInfo.tcp("127.0.0.1", 80).ipv4? #=> true
 *   AddrInfo.tcp("::1", 80).ipv4?       #=> false
 *   AddrInfo.unix("/tmp/sock").ipv4?    #=> false
 *
 */
static VALUE
addrinfo_ipv4_p(VALUE self)
{
    rb_addrinfo_t *rai = get_addrinfo(self);
    return ai_get_afamily(rai) == AF_INET ? Qtrue : Qfalse;
}

/*
 * call-seq:
 *   addrinfo.ipv6? => true or false
 *
 * returns true if addrinfo is IPv6 address.
 * returns false otherwise.
 *
 *   AddrInfo.tcp("127.0.0.1", 80).ipv6? #=> false
 *   AddrInfo.tcp("::1", 80).ipv6?       #=> true
 *   AddrInfo.unix("/tmp/sock").ipv6?    #=> false
 *
 */
static VALUE
addrinfo_ipv6_p(VALUE self)
{
#ifdef AF_INET6
    rb_addrinfo_t *rai = get_addrinfo(self);
    return ai_get_afamily(rai) == AF_INET6 ? Qtrue : Qfalse;
#else
    return Qfalse;
#endif
}

/*
 * call-seq:
 *   addrinfo.unix? => true or false
 *
 * returns true if addrinfo is UNIX address.
 * returns false otherwise.
 *
 *   AddrInfo.tcp("127.0.0.1", 80).unix? #=> false
 *   AddrInfo.tcp("::1", 80).unix?       #=> false
 *   AddrInfo.unix("/tmp/sock").unix?    #=> true
 *
 */
static VALUE
addrinfo_unix_p(VALUE self)
{
    rb_addrinfo_t *rai = get_addrinfo(self);
#ifdef AF_UNIX
    return ai_get_afamily(rai) == AF_UNIX ? Qtrue : Qfalse;
#else
    return Qfalse;
#endif
}

/*
 * call-seq:
 *   addrinfo.getnameinfo        => [nodename, service]
 *   addrinfo.getnameinfo(flags) => [nodename, service]
 *
 * returns nodename and service as a pair of strings.
 * This converts struct sockaddr in addrinfo to textual representation.
 *
 * flags should be bitwise OR of Socket::NI_??? constants.
 *
 *   AddrInfo.tcp("127.0.0.1", 80).getnameinfo #=> ["localhost", "www"]
 *
 *   AddrInfo.tcp("127.0.0.1", 80).getnameinfo(Socket::NI_NUMERICSERV)
 *   #=> ["localhost", "80"]
 */
static VALUE
addrinfo_getnameinfo(int argc, VALUE *argv, VALUE self)
{
    rb_addrinfo_t *rai = get_addrinfo(self);
    VALUE vflags;
    char hbuf[1024], pbuf[1024];
    int flags, error;

    rb_scan_args(argc, argv, "01", &vflags);

    flags = NIL_P(vflags) ? 0 : NUM2INT(vflags);

    if (rai->socktype == SOCK_DGRAM)
        flags |= NI_DGRAM;

    error = getnameinfo((struct sockaddr *)&rai->addr, rai->sockaddr_len,
                        hbuf, sizeof(hbuf), pbuf, sizeof(pbuf),
                        flags);
    if (error) {
        raise_socket_error("getnameinfo", error);
    }

    return rb_assoc_new(rb_str_new2(hbuf), rb_str_new2(pbuf));
}

/*
 * call-seq:
 *   addrinfo.ip_unpack => [addr, port]
 *
 * Returns the IP address and port number as 2-element array.
 *
 *   AddrInfo.tcp("127.0.0.1", 80).ip_unpack    #=> ["127.0.0.1", 80]
 *   AddrInfo.tcp("::1", 80).ip_unpack          #=> ["::1", 80]
 */
static VALUE
addrinfo_ip_unpack(VALUE self)
{
    rb_addrinfo_t *rai = get_addrinfo(self);
    int family = ai_get_afamily(rai);
    VALUE vflags;
    VALUE ret, portstr;

    if (!IS_IP_FAMILY(family))
	rb_raise(rb_eSocket, "need IPv4 or IPv6 address");

    vflags = INT2NUM(NI_NUMERICHOST|NI_NUMERICSERV);
    ret = addrinfo_getnameinfo(1, &vflags, self);
    portstr = rb_ary_entry(ret, 1);
    rb_ary_store(ret, 1, INT2NUM(atoi(StringValueCStr(portstr))));
    return ret;
}

#ifdef HAVE_SYS_UN_H
/*
 * call-seq:
 *   addrinfo.unix_path => path
 *
 * Returns the socket path as a string.
 *
 *   AddrInfo.unix("/tmp/sock").unix_path       #=> "/tmp/sock"
 */
static VALUE
addrinfo_unix_path(VALUE self)
{
    rb_addrinfo_t *rai = get_addrinfo(self);
    int family = ai_get_afamily(rai);
    struct sockaddr_un *addr;
    char *s, *e;

    if (family != AF_UNIX)
	rb_raise(rb_eSocket, "need AF_UNIX address");

    addr = (struct sockaddr_un *)&rai->addr;

    s = addr->sun_path;
    e = (char*)addr + rai->sockaddr_len;
    if (e < s)
        rb_raise(rb_eSocket, "too short AF_UNIX address");
    if (addr->sun_path + sizeof(addr->sun_path) < e)
        rb_raise(rb_eSocket, "too long AF_UNIX address");
    while (s < e && *(e-1) == '\0')
        e--;
    return rb_str_new(s, e-s);
}
#endif

/*
 * call-seq:
 *   AddrInfo.getaddrinfo(nodename, service, family, socktype, protocol, flags) => [addrinfo, ...]
 *   AddrInfo.getaddrinfo(nodename, service, family, socktype, protocol)        => [addrinfo, ...]
 *   AddrInfo.getaddrinfo(nodename, service, family, socktype)                  => [addrinfo, ...]
 *   AddrInfo.getaddrinfo(nodename, service, family)                            => [addrinfo, ...]
 *   AddrInfo.getaddrinfo(nodename, service)                                    => [addrinfo, ...]
 *
 * returns a list of addrinfo objects as an array.
 *
 * This method converts nodename (hostname) and service (port) to addrinfo.
 * Since the conversion is not unique, the result is a list of addrinfo objects.
 *
 * nodename or service can be nil if no conversion intended.
 *
 * family, socktype and protocol are hint for prefered protocol.
 * If the result will be used for a socket with SOCK_STREAM, 
 * SOCK_STREAM should be specified as socktype.
 * If so, AddrInfo.getaddrinfo returns addrinfo list appropriate for SOCK_STREAM.
 * If they are omitted or nil is given, the result is not restricted.
 * 
 * Similary, PF_INET6 as family restricts for IPv6.
 *
 * flags should be bitwise OR of Socket::AI_??? constants.
 *
 *   AddrInfo.getaddrinfo("www.kame.net", 80, nil, :STREAM)
 *   #=> [#<AddrInfo: 203.178.141.194:80 TCP (www.kame.net:80)>,
 *   #    #<AddrInfo: [2001:200:0:8002:203:47ff:fea5:3085]:80 TCP (www.kame.net:80)>]
 *
 */
static VALUE
addrinfo_s_getaddrinfo(int argc, VALUE *argv, VALUE self)
{
    VALUE node, service, family, socktype, protocol, flags;

    rb_scan_args(argc, argv, "24", &node, &service, &family, &socktype, &protocol, &flags);
    return addrinfo_list_new(node, service, family, socktype, protocol, flags);
}

/*
 * call-seq:
 *   AddrInfo.ip(host) => addrinfo
 *
 * returns an addrinfo object for IP address.
 *
 * The port, socktype, protocol of the result is filled by zero.
 * So, it is not appropriate to create a socket.
 *
 *   AddrInfo.ip("localhost") #=> #<AddrInfo: 127.0.0.1 (localhost)>
 */
static VALUE
addrinfo_s_ip(VALUE self, VALUE host)
{
    VALUE ret;
    rb_addrinfo_t *rai;
    ret = addrinfo_firstonly_new(host, Qnil,
            INT2NUM(PF_UNSPEC), INT2FIX(0), INT2FIX(0), INT2FIX(0));
    rai = get_addrinfo(ret);
    rai->socktype = 0;
    rai->protocol = 0;
    return ret;
}

/*
 * call-seq:
 *   AddrInfo.tcp(host, port) => addrinfo
 *
 * returns an addrinfo object for TCP address.
 *
 *   AddrInfo.tcp("localhost", "smtp") #=> #<AddrInfo: 127.0.0.1:25 TCP (localhost:smtp)>
 */
static VALUE
addrinfo_s_tcp(VALUE self, VALUE host, VALUE port)
{
    return addrinfo_firstonly_new(host, port,
            INT2NUM(PF_UNSPEC), INT2NUM(SOCK_STREAM), INT2NUM(IPPROTO_TCP), INT2FIX(0));
}

/*
 * call-seq:
 *   AddrInfo.udp(host, port) => addrinfo
 *
 * returns an addrinfo object for UDP address.
 *
 *   AddrInfo.udp("localhost", "daytime") #=> #<AddrInfo: 127.0.0.1:13 UDP (localhost:daytime)>
 */
static VALUE
addrinfo_s_udp(VALUE self, VALUE host, VALUE port)
{
    return addrinfo_firstonly_new(host, port,
            INT2NUM(PF_UNSPEC), INT2NUM(SOCK_DGRAM), INT2NUM(IPPROTO_UDP), INT2FIX(0));
}

#ifdef HAVE_SYS_UN_H

/*
 * call-seq:
 *   AddrInfo.udp(host, port) => addrinfo
 *
 * returns an addrinfo object for UNIX socket address.
 *
 *   AddrInfo.unix("/tmp/sock") #=> #<AddrInfo: /tmp/sock SOCK_STREAM>
 */
static VALUE
addrinfo_s_unix(VALUE self, VALUE path)
{
    VALUE addr;
    rb_addrinfo_t *rai;

    addr = addrinfo_s_allocate(rb_cAddrInfo);
    DATA_PTR(addr) = rai = alloc_addrinfo();
    init_unix_addrinfo(rai, path);
    OBJ_INFECT(addr, path);
    return addr;
}

#endif

VALUE
sockaddr_string_value(volatile VALUE *v)
{
    VALUE val = *v;
    if (TYPE(val) == RUBY_T_DATA && IS_ADDRINFO(val)) {
        *v = addrinfo_to_sockaddr(val);
    }
    StringValue(*v);
    return *v;
}

char *
sockaddr_string_value_ptr(volatile VALUE *v)
{
    sockaddr_string_value(v);
    return RSTRING_PTR(*v);
}

VALUE
fd_socket_addrinfo(int fd, struct sockaddr *addr, socklen_t len)
{
    int family;
    int socktype;
    int ret;
    socklen_t optlen = sizeof(socktype);

    /* assumes protocol family and address family are identical */
    family = get_afamily(addr, len);

    ret = getsockopt(fd, SOL_SOCKET, SO_TYPE, (void*)&socktype, &optlen);
    if (ret == -1) {
        rb_sys_fail("getsockopt(SO_TYPE)");
    }

    return addrinfo_new(addr, len, family, socktype, 0, Qnil, Qnil);
}

VALUE
io_socket_addrinfo(VALUE io, struct sockaddr *addr, socklen_t len)
{
    rb_io_t *fptr;

    switch (TYPE(io)) {
      case T_FIXNUM:
        return fd_socket_addrinfo(FIX2INT(io), addr, len);

      case T_BIGNUM:
        return fd_socket_addrinfo(NUM2INT(io), addr, len);

      case T_FILE:
        GetOpenFile(io, fptr);
        return fd_socket_addrinfo(fptr->fd, addr, len);

      default:
        rb_raise(rb_eTypeError, "neither IO nor file descriptor");
    }
}

/*
 * AddrInfo class
 */
void
Init_addrinfo(void)
{
    rb_cAddrInfo = rb_define_class("AddrInfo", rb_cData);
    rb_define_alloc_func(rb_cAddrInfo, addrinfo_s_allocate);
    rb_define_method(rb_cAddrInfo, "initialize", addrinfo_initialize, -1);
    rb_define_method(rb_cAddrInfo, "inspect", addrinfo_inspect, 0);
    rb_define_singleton_method(rb_cAddrInfo, "getaddrinfo", addrinfo_s_getaddrinfo, -1);
    rb_define_singleton_method(rb_cAddrInfo, "ip", addrinfo_s_ip, 1);
    rb_define_singleton_method(rb_cAddrInfo, "tcp", addrinfo_s_tcp, 2);
    rb_define_singleton_method(rb_cAddrInfo, "udp", addrinfo_s_udp, 2);
#ifdef HAVE_SYS_UN_H
    rb_define_singleton_method(rb_cAddrInfo, "unix", addrinfo_s_unix, 1);
#endif

    rb_define_method(rb_cAddrInfo, "afamily", addrinfo_afamily, 0);
    rb_define_method(rb_cAddrInfo, "pfamily", addrinfo_pfamily, 0);
    rb_define_method(rb_cAddrInfo, "socktype", addrinfo_socktype, 0);
    rb_define_method(rb_cAddrInfo, "protocol", addrinfo_protocol, 0);
    rb_define_method(rb_cAddrInfo, "canonname", addrinfo_canonname, 0);

    rb_define_method(rb_cAddrInfo, "ip?", addrinfo_ip_p, 0);
    rb_define_method(rb_cAddrInfo, "ip_unpack", addrinfo_ip_unpack, 0);
    rb_define_method(rb_cAddrInfo, "ipv4?", addrinfo_ipv4_p, 0);
    rb_define_method(rb_cAddrInfo, "ipv6?", addrinfo_ipv6_p, 0);
    rb_define_method(rb_cAddrInfo, "unix?", addrinfo_unix_p, 0);
#ifdef HAVE_SYS_UN_H
    rb_define_method(rb_cAddrInfo, "unix_path", addrinfo_unix_path, 0);
#endif

    rb_define_method(rb_cAddrInfo, "to_sockaddr", addrinfo_to_sockaddr, 0);

    rb_define_method(rb_cAddrInfo, "getnameinfo", addrinfo_getnameinfo, -1);
}