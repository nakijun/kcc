
#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../inc/sizes.h"
#include "../inc/cc.h"
#include "cc1.h"

#ifndef PREFIX
#define PREFIX "/usr/include/local"
#endif

static char *argp, *macroname;
static unsigned arglen;
static Symbol *symline, *symfile;
static unsigned char ifstatus[NR_COND];
static int ninclude;
static char **dirinclude;

unsigned cppctx;
int disexpand;

Symbol *
defmacro(char *s)
{
	char *p, *q;
	Symbol *sym;

	if ((p = strchr(s, '=')) != NULL) {
		*p++='\0';
		q = xmalloc(strlen(p) + 4);
		sprintf(q, "-1#%s", p);
		p = q;
	}
	sym = install(NS_CPP, lookup(NS_CPP, s));
	sym->u.s = p;
	return sym;
}

void
icpp(void)
{
	static char sdate[17], stime[14];
	struct tm *tm;
	time_t t;
	static char **bp, *list[] = {
		"__STDC__",
		"__STDC_HOSTED__",
		"__SCC__",
		NULL
	};
	static struct keyword keys[] = {
		{"define", DEFINE, DEFINE},
		{"include", INCLUDE, INCLUDE},
		{"line", LINE, LINE},
		{"ifdef", IFDEF, IFDEF},
		{"if", IF, IF},
		{"elif", ELIF, ELIF},
		{"else", ELSE, ELSE},
		{"ifndef", IFNDEF, IFNDEF},
		{"endif", ENDIF, ENDIF},
		{"undef", UNDEF, UNDEF},
		{"pragma", PRAGMA, PRAGMA},
		{"error", ERROR, ERROR},
		{NULL, 0, 0}
	};

	t = time(NULL);
	tm = localtime(&t);
	strftime(sdate, sizeof(sdate), "-1#\"%b %d %Y\"", tm);
	strftime(stime, sizeof(stime), "-1#\"%H:%M:%S\"", tm);
	defmacro("__DATE__")->u.s = sdate;
	defmacro("__TIME__")->u.s = stime;

	defmacro("__STDC_VERSION__")->u.s = "-1#199409L";
	symline = defmacro("__LINE__");
	symfile = defmacro("__FILE__");

	for (bp = list; *bp; ++bp)
		defmacro(*bp)->u.s = "-1#1";
	keywords(keys, NS_CPPCLAUSES);
}

static void
nextcpp(void)
{
	next();
	if (yytoken == EOFTOK)
		error("unterminated argument list invoking macro \"%s\"",
		      macroname);
	if (yylen + 1 > arglen)
		error("argument overflow invoking macro \"%s\"",
		      macroname);
	if (yytoken == IDEN)
		yylval.sym->flags |= ISUSED;
	memcpy(argp, yytext, yylen);
	argp += yylen;
	*argp++ = ' ';
	arglen -= yylen + 1;
}

static void
paren(void)
{
	for (;;) {
		nextcpp();
		switch (yytoken) {
		case ')':
			return;
		case '(':
			paren();
			break;
		}
	}
}

static void
parameter(void)
{
	for (;;) {
		nextcpp();
		switch (yytoken) {
		case ')':
		case ',':
			argp -= 3;  /* remove " , "  or " ) "*/
			*argp++ = '\0';
			return;
		case '(':
			paren();
			break;
		}
	}
}

static int
parsepars(char *buffer, char **listp, int nargs)
{
	int n;

	if (nargs == -1)
		return -1;
	if (ahead() != '(' && nargs > 0)
		return 0;

	disexpand = 1;
	next();
	n = 0;
	argp = buffer;
	arglen = INPUTSIZ;
	if (yytoken != ')') {
		do {
			*listp++ = argp;
			parameter();
		} while (++n < NR_MACROARG && yytoken == ',');
	}
	if (yytoken != ')')
		error("incorrect macro function alike invocation");
	disexpand = 0;

	if (n == NR_MACROARG)
		error("too much parameters in macro \"%s\"", macroname);
	if (n != nargs) {
		error("macro \"%s\" passed %d arguments, but it takes %d",
		      macroname, n, nargs);
	}

	return 1;
}

/* FIXME: characters in the definition break the macro definition */
static size_t
copymacro(char *buffer, char *s, size_t bufsiz, char *arglist[])
{
	char prevc, c, *p, *arg, *bp = buffer;
	size_t size;

	for (prevc = '\0'; c = *s; prevc = c, ++s) {
		if (c != '@') {
			switch (c) {
			case '$':
				while (bp[-1] == ' ')
					--bp, ++bufsiz;
				while (s[1] == ' ')
					++s;
			case '#':
				continue;
			case '\"':
				for (p = s; *++s != '"'; )
					/* nothing */;
				size = s - p + 1;
				if (size > bufsiz)
					goto expansion_too_long;
				memcpy(bp, p, size);
				bufsiz -= size;
				bp += size;
				continue;
			// case '\'';
			}
			if (bufsiz-- == 0)
				goto expansion_too_long;
			*bp++ = c;
		} else {
			if (prevc == '#')
				bufsiz -= 2;
			arg = arglist[atoi(++s)];
			size = strlen(arg);
			if (size > bufsiz)
				goto expansion_too_long;
			if (prevc == '#')
				*bp++ = '"';
			memcpy(bp, arg, size);
			bp += size;
			if (prevc == '#')
				*bp++ = '"';
			bufsiz -= size;
			s += 2;
		}
	}
	*bp = '\0';

	return bp - buffer;

expansion_too_long:
	error("expansion of macro \"%s\" is too long", macroname);
}

#define BUFSIZE ((INPUTSIZ > FILENAME_MAX+2) ? INPUTSIZ : FILENAME_MAX+2)
bool
expand(char *begin, Symbol *sym)
{
	size_t total, elen, rlen, llen, ilen;
	int n;
	char *s = sym->u.s;
	char *arglist[NR_MACROARG], arguments[INPUTSIZ], buffer[BUFSIZE];

	macroname = sym->name;
	if ((sym->flags & ISDECLARED) == 0) {
		if (namespace == NS_CPP && !strcmp(sym->name, "defined"))
			return 0;  /* we found a 'defined in an #if */
		/*
		 * This case happens in #if were macro not defined must
		 * be expanded to 0
		 */
		buffer[0] = '0';
		buffer[1] = '\0';
		elen = 1;
		goto substitute;
	}
	if (sym == symfile) {
		elen = sprintf(buffer, "\"%s\" ", input->fname);
		goto substitute;
	}
	if (sym == symline) {
		elen = sprintf(buffer, "%d ", input->nline);
		goto substitute;
	}

	if (!parsepars(arguments, arglist, atoi(s)))
		return 0;
	for (n = 0; n < atoi(s); ++n)
		DBG("MACRO par%d:%s", n, arglist[n]);

	elen = copymacro(buffer, s+3, INPUTSIZ-1, arglist);

substitute:
	DBG("MACRO '%s' expanded to :'%s'", macroname, buffer);
	rlen = strlen(input->p);      /* rigth length */
	llen = begin - input->line;   /* left length */
	ilen = input->p - begin;      /* invocation length */
	total = llen + elen + rlen;

	if (total >= LINESIZ)
		error("macro expansion too long");

	/* cut macro invocation */
	memmove(begin, begin + ilen, rlen);

	/* paste macro expansion */
	memmove(begin + elen, begin, rlen);
	memcpy(begin, buffer, elen);
	input->line[total] = '\0';

	input->p = input->begin = begin;

	if (!(sym->flags & ISDECLARED))
		killsym(sym);

	return 1;
}
#undef BUFSIZE

static int
getpars(Symbol *args[NR_MACROARG])
{
	int n = -1;
	Symbol *sym;

	if (!accept('('))
		return n;
	++n;
	if (accept(')'))
		return n;

	do {
		if (n == NR_MACROARG) {
			cpperror("too much parameters in macro");
			return NR_MACROARG;
		}
		if (yytoken != IDEN) {
			cpperror("macro arguments must be identifiers");
			return NR_MACROARG;
		}
		sym = install(NS_IDEN, yylval.sym);
		sym->flags |= ISUSED;
		args[n++] = sym;
		next();
	} while (accept(','));
	expect(')');

	return n;
}

static bool
getdefs(Symbol *args[NR_MACROARG], int nargs, char *bp, size_t bufsiz)
{
	Symbol **argp;
	size_t len;
	int prevc = 0, ispar;

	if (yytoken == '$') {
		cpperror("'##' cannot appear at either end of a macro expansion");
		return 0;
	}

	for (;;) {
		ispar = 0;
		if (yytoken == IDEN && nargs >= 0) {
			for (argp = args; argp < &args[nargs]; ++argp) {
				if (*argp == yylval.sym)
					break;
			}
			if (argp != &args[nargs]) {
				sprintf(yytext, "@%02d@", (int) (argp - args));
				ispar = 1;
			}
		}
		if (prevc == '#' && !ispar) {
			cpperror("'#' is not followed by a macro parameter");
			return 0;
		}
		if (yytoken == EOFTOK)
			break;

		if ((len = strlen(yytext)) >= bufsiz) {
			cpperror("too long macro");
			return 0;
		}
		if (yytoken == '$') {
			*bp++ = '$';
			 --bufsiz;
		} else {
			memcpy(bp, yytext, len);
			bp += len;
			bufsiz -= len;
		}
		if ((prevc  = yytoken) != '#')
			*bp++ = ' ';
		next();
	}
	*bp = '\0';
	return 1;
}

static void
define(void)
{
	Symbol *sym,*args[NR_MACROARG];
	char buff[LINESIZ+1];
	int n;

	if (cppoff)
		return;

	namespace = NS_CPP;
	next();

	if (yytoken != IDEN) {
		cpperror("macro names must be identifiers");
		return;
	}
	sym = yylval.sym;
	if (sym->flags & ISDECLARED) {
		warn("'%s' redefined", yytext);
		free(sym->u.s);
	} else {
		sym = install(NS_CPP, sym);
		sym->flags |= ISDECLARED|ISSTRING;
	}

	namespace = NS_IDEN;       /* Avoid polution in NS_CPP */
	next();
	if ((n = getpars(args)) == NR_MACROARG)
		goto delete;
	sprintf(buff, "%02d#", n);
	if (!getdefs(args, n, buff+3, LINESIZ-3))
		goto delete;
	sym->u.s = xstrdup(buff);
	DBG("MACRO '%s' defined as '%s'", sym->name, buff);
	return;

delete:
	killsym(sym);
}

void
incdir(char *dir)
{
	if (!dir || *dir == '\0')
		die("incorrect -I flag");
	++ninclude;
	dirinclude = xrealloc(dirinclude, sizeof(*dirinclude) * ninclude);
	dirinclude[ninclude-1] = dir;
}

static bool
includefile(char *dir, char *file, size_t filelen)
{
	size_t dirlen;
	char path[FILENAME_MAX];

	if (!dir) {
		dirlen = 0;
		if (filelen > FILENAME_MAX-1)
			return 0;
	} else {
		dirlen = strlen(dir);
		if (dirlen + filelen > FILENAME_MAX-2)
			return 0;
		memcpy(path, dir, dirlen);
		if (dir[dirlen-1] != '/')
			path[dirlen++] = '/';
	}
	memcpy(path+dirlen, file, filelen);
	path[dirlen + filelen] = '\0';

	return addinput(path);
}

static void
include(void)
{
	char *file, *p, **bp;
	size_t filelen;
	static char *sysinclude[] = {
		PREFIX"/include/",
		PREFIX"/local/include/",
		NULL
	};
	int n;

	if (cppoff)
		return;

	namespace = NS_IDEN;
	next();

	switch (*yytext) {
	case '<':
		if ((p = strchr(input->begin, '>')) == NULL || p == yytext + 1)
			goto bad_include;
		*p = '\0';
		file = input->begin;
		filelen = strlen(file);
		input->begin = input->p = p+1;
		break;
	case '"':
		if ((p = strchr(yytext + 1, '"')) == NULL || p == yytext + 1)
			goto bad_include;
		*p = '\0';
		file = yytext+1;
		filelen = strlen(file);
		if (includefile(NULL, file, filelen))
			goto its_done;
		break;
	default:
		goto bad_include;
	}

	n = ninclude;
	for (bp = dirinclude; n--; ++bp) {
		if (includefile(*bp, file, filelen))
			goto its_done;
	}
	for (bp = sysinclude; *bp; ++bp) {
		if (includefile(*bp, file, filelen))
			goto its_done;
	}
	cpperror("included file '%s' not found", file);

its_done:
	next();
	return;

bad_include:
	cpperror("#include expects \"FILENAME\" or <FILENAME>");
	return;
}

static void
line(void)
{
	long n;
	char *endp;

	if (cppoff)
		return;

	next();
	n = strtol(yytext, &endp, 10);
	if (n <= 0 || n > USHRT_MAX || *endp != '\0') {
		cpperror("first parameter of #line is not a positive integer");
		return;
	}

	next();
	if (yytoken == EOFTOK)
		goto set_line;

	if (*yytext != '\"' || yylen == 1) {
		cpperror("second parameter of #line is not a valid filename");
		return;
	}

	free(input->fname);
	input->fname = xstrdup(yylval.sym->u.s);
	next();

set_line:
	input->nline = n;
}

static void
pragma(void)
{
	if (cppoff)
		return;
	*input->p = '\0';
	next();
}

static void
usererr(void)
{
	if (cppoff)
		return;
	cpperror("#error %s", input->p);
	*input->p = '\0';
	next();
}

static void
ifclause(int negate, int isifdef)
{
	Symbol *sym;
	unsigned n;
	int status;
	Node *expr;

	if (cppctx == NR_COND-1)
		error("too much nesting levels of conditional inclusion");

	n = cppctx++;
	namespace = NS_CPP;
	next();

	if (isifdef) {
		if (yytoken != IDEN) {
			cpperror("no macro name given in #%s directive",
			         (negate) ? "ifndef" : "ifdef");
			return;
		}
		sym = yylval.sym;
		next();
		status = (sym->flags & ISDECLARED) != 0;
		if (!status)
			killsym(sym);
	} else {
		/* TODO: catch recovery here */
		if ((expr = iconstexpr()) == NULL) {
			cpperror("parameter of #if is not an integer constant expression");
			return;
		}
		status = expr->sym->u.i != 0;
	}

	if (negate)
		status = !status;
	if ((ifstatus[n] = status) == 0)
		++cppoff;
}

static void
cppif(void)
{
	disexpand = 0;
	ifclause(0, 0);
}

static void
ifdef(void)
{
	ifclause(0, 1);
}

static void
ifndef(void)
{
	ifclause(1, 1);
}

static void
elseclause(void)
{
	int status;

	if (cppctx == 0)
		error("#else without #ifdef/ifndef");

	status = (ifstatus[cppctx-1] ^= 1);
	cppoff += (status) ? -1 : 1;
}

static void
cppelse(void)
{
	elseclause();
	next();
}

static void
elif(void)
{
	elseclause();
	--cppctx;
	cppif();
}

static void
endif(void)
{
	if (cppctx == 0)
		error("#endif without #if");
	if (!ifstatus[--cppctx])
		--cppoff;
	next();
}

static void
undef(void)
{
	if (cppoff)
		return;

	namespace = NS_CPP;
	next();
	if (yytoken != IDEN) {
		error("no macro name given in #undef directive");
		return;
	}
	killsym(yylval.sym);
	next();
}

bool
cpp(void)
{
	static struct {
		uint8_t token;
		void (*fun)(void);
	} *bp, clauses [] = {
		{DEFINE, define},
		{INCLUDE, include},
		{LINE, line},
		{IFDEF, ifdef},
		{IF, cppif},
		{ELIF, elif},
		{IFNDEF, ifndef},
		{ELSE, cppelse},
		{ENDIF, endif},
		{UNDEF, undef},
		{PRAGMA, pragma},
		{ERROR, usererr},
		{0, NULL}
	};
	int ns;

	if (*input->p != '#')
		return 0;
	++input->p;

	disexpand = 1;
	lexmode = CPPMODE;
	ns = namespace;
	namespace = NS_CPPCLAUSES;
	next();
	namespace = NS_IDEN;

	for (bp = clauses; bp->token && bp->token != yytoken; ++bp)
		/* nothing */;
	if (!bp->token)
		error("incorrect preprocessor directive");

	pushctx();              /* create a new context to avoid polish */
	(*bp->fun)();           /* the current context, and to get all  */
	popctx();               /* the symbols freed at the  end        */

	if (yytoken != EOFTOK && !cppoff)
		errorp("trailing characters after preprocessor directive");

	disexpand = 0;
	lexmode = CCMODE;
	namespace = ns;

	return 1;
}

void
outcpp(void)
{
	char c, *s, *t;

	for (next(); yytoken != EOFTOK; next()) {
		if (yytoken != CONSTANT || *yytext != '"') {
			printf("%s ", yytext);
			continue;
		}
		for (s = yytext; c = *s; ++s) {
			switch (c) {
			case '\n':
				t = "\\n";
				goto print_str;
			case '\v':
				t = "\\v";
				goto print_str;
			case '\b':
				t = "\\b";
				goto print_str;
			case '\t':
				t = "\\t";
				goto print_str;
			case '\a':
				t = "\\a";
			print_str:
				fputs(t, stdout);
				break;
			case '\\':
				putchar('\\');
			default:
				if (!isprint(c))
					printf("\\x%x", c);
				else
					putchar(c);
				break;
			}
		}
		putchar(' ');
	}
	putchar('\n');
}

