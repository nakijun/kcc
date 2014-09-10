
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cc.h>
#include <sizes.h>

#include "cc2.h"

#define MAXLINE 200
#define NR_STACKSIZ 32
#define NR_NODEPOOL 128
#define NR_EXPRESSIONS 64

enum {
	LOCAL, GLOBAL, PARAMETER
};

static Symbol *curfun;
static Node *stack[NR_STACKSIZ], **stackp = stack;
static Node *listexp[NR_EXPRESSIONS], **listp = listexp;
static Node nodepool[NR_NODEPOOL], *newp = nodepool;


Type l_int8 = {
	.size = 1,
	.align = 2,
	.sign = 1,
	.c_int = 1,
};

Type l_int16 = {
	.size = 2,
	.align = 2,
	.sign = 1,
	.c_int = 1,
};

Type l_int32 = {
	.size = 4,
	.align = 4,
	.sign = 1,
	.c_int = 1,
};

Type l_int64 = {
	.size = 8,
	.align = 8,
	.sign = 1,
	.c_int = 1,
};

Type l_uint8 = {
	.size = 1,
	.align = 2,
	.c_int = 1,
};

Type l_uint16 = {
	.size = 2,
	.align = 2,
	.c_int = 1,
};

Type l_uint32 = {
	.size = 4,
	.align = 4,
	.c_int = 1,
};

Type l_uint64 = {
	.size = 8,
	.align = 8,
	.c_int = 1,
};

static Symbol *
parameter(char *num)
{
	static Symbol tbl[NR_FUNPARAM];
	unsigned i = atoi(num);

	if (i >= NR_FUNPARAM)
		error(EPARNUM);
	return &tbl[i];
}

static Symbol *
local(char *num)
{
	static Symbol tbl[NR_INT_IDENT];
	unsigned i = atoi(num);

	if (i >= NR_INT_IDENT)
		error(EINTNUM);
	return &tbl[i];
}

static Symbol *
global(char *num)
{
	static Symbol tbl[NR_EXT_IDENT];
	unsigned i = atoi(num);

	if (i >= NR_EXT_IDENT)
		error(EEXTNUM);

	return &tbl[i];
}

static Node *
newnode(void)
{
	if (newp == &nodepool[NR_NODEPOOL])
		error(ENODEOV);
	return newp++;
}

static void
push(Node *np)
{
	if (stackp == &stack[NR_STACKSIZ])
		error(ESTACKO);
	*stackp++ = np;
}

static Node *
pop(void)
{
	if (stackp == stack)
		error(ESTACKU);
	return *--stackp;
}

static Type *
gettype(char *type)
{
	switch (type[0]) {
	case L_INT8:
		return &l_int8;
	case L_INT16:
		return &l_int16;
	case L_INT32:
		return &l_int32;
	case L_INT64:
		return &l_int64;
	case L_UINT8:
		return &l_uint8;
	case L_UINT16:
		return &l_uint16;
	case L_UINT32:
		return &l_uint32;
	case L_UINT64:
		return &l_uint64;
	case 'F':
		return NULL;
	default:
		error(ETYPERR);
	}
}

static Symbol *
symbol(uint8_t t, char *token)
{
	static Symbol *(*tbl[3])(char *)= {
		[LOCAL] = local,
		[GLOBAL] = global,
		[PARAMETER] = parameter
	};
	return (*tbl[t])(token+1);
}

static void
variable(uint8_t t, char *token)
{
	Node *np = newnode();
	Symbol *sym = symbol(t, token);

	np->u.sym = sym;
	np->op = sym->u.v.sclass;
	np->type = sym->u.v.type;
	np->left = np->right = NULL;
	push(np);
}

static void
localvar(char *token)
{
	variable(LOCAL, token);
}

static void
globvar(char *token)
{
	variable(GLOBAL, token);
}

static void
paramvar(char *token)
{
	variable(PARAMETER, token);
}

static void
immediate(char *token)
{
	Node *np = newnode();

	np->op = CONST;
	np->type = gettype(token+1);
	np->u.imm = atoi(token+2);
	np->left = np->right = NULL;
	push(np);
}

static void
unary(char *token)
{
	Node *np = newnode();

	np->right = NULL;
	np->left = pop();
	np->type = gettype(token+1);
	np->op = token[0];
	push(np);
}

static void
operator(char *token)
{
	Node *np = newnode();

	np->right = pop();
	np->left = pop();
	np->type = gettype(token+1);
	np->op = token[0];
	push(np);
}

static void
label(char *token)
{
	Node *np = newnode();

	np->left = np->right = NULL;
	np->op = LABEL;
	np->u.sym = local(token);
	push(np);
}

static void
increment(char *token)
{
	Node *np = newnode();

	np->right = pop();
	np->left = pop();
	np->type = gettype(token+2);
	np->op = token[0];
	switch (np->subop = token[1]) {
	case '-': case '+':
		push(np);
		break;
	default:
		error(ESYNTAX);
	}
}

static void
assignment(char *token)
{
	Node *np = newnode();

	np->right = pop();
	np->left = pop();
	np->op = *token;
	switch (*++token) {
	case OADD: case OSUB: case OINC:  case OMOD: case ODIV:
	case OSHL: case OSHR: case OBAND: case OBOR: case OBXOR:
		np->subop = *++token;
	default:
		np->type = gettype(token);
		break;
	}
	push(np);
}

static void (*optbl[])(char *) = {
	['+'] = operator,
	['%'] = operator,
	['-'] = operator,
	['*'] = operator,
	['/'] = operator,
	['l'] = operator,
	['r'] = operator,
	['&'] = operator,
	['|'] = operator,
	['^'] = operator,
	[':'] = assignment,
	[';'] = increment,
	['Y'] = globvar,
	['A'] = localvar,
	['T'] = localvar,
	['G'] = globvar,
	['P'] = paramvar,
	['L'] = label,
	['#'] = immediate,
	['@'] = unary,
	['a'] = unary,
	['<'] = operator,
	['>'] = operator,
	[']'] = operator,
	['['] = operator,
	['='] = operator,
	['!'] = unary,
	['y'] = NULL,
	['j'] = NULL,
	['o'] = operator,
	['_'] = unary,
	['~'] = unary,
	[','] = operator,
	['\177'] = NULL
};

static void
expression(char *token)
{
	Node *np;
	void (*fun)(char *);

	if (!curfun)
		error(ESYNTAX);

	do {
		if ((fun = optbl[token[0]]) == NULL)
			error(ESYNTAX);
		(*fun)(token);
	} while (token = strtok(NULL, "\t"));

	np = pop();
	if (stackp != stack)
		error(EEXPBAL);
	if (listp == &listexp[NR_EXPRESSIONS])
		error(EEXPROV);
	*listp++ = np;
}

static void
deflabel(char *token)
{
	Symbol *sym;

	if (!curfun)
		error(ESYNTAX);
	sym = local(token);
	sym->type = LABEL;
	sym->u.l.addr = listp - listexp;
}

static void
endfunction(char *token)
{
	listp = NULL;
	genstack(curfun);
	genaddable(listexp);
	cgen(curfun, listexp);
	curfun = NULL;
}

static Symbol *
declaration(uint8_t t, char class, char *token)
{
	Symbol *sym = symbol(t, token);
	char *s;

	if (t == LOCAL && !curfun)
		error(ESYNTAX);
	if (sym->name)
		free(sym->name);
	memset(sym, 0, sizeof(*sym));
	sym->type = VAR;
	sym->u.v.sclass = class;

	if ((s = strtok(NULL, "\t")) == NULL)
		error(ESYNTAX);
	sym->u.v.type = gettype(s);
	if ((s = strtok(NULL, "\t")) != NULL)
		sym->name = xstrdup(s);

	return sym;
}

static void
globdcl(char *token)
{
	Symbol *sym = declaration(GLOBAL, MEM, token);

	switch (token[0]) {
	case 'X':
		sym->extrn = 1;
		break;
	case 'G':
		sym->public = 1;
		break;
	}

	if (sym->u.v.type != NULL)
		return;

	if (curfun)
		error(ESYNTAX);

	sym->type = FUN;
	sym->u.f.vars = NULL;
	sym->u.f.pars = NULL;
	curfun = sym;
	listp = listexp;
	newp = nodepool;
}

static void
paramdcl(char *token)
{
	Symbol *sym = declaration(PARAMETER, AUTO, token);
	sym->next = curfun->u.f.pars;
	curfun->u.f.pars = sym;
}

static void
localdcl(char *token)
{
	Symbol *sym = declaration(LOCAL, token[0], token);
	sym->next = curfun->u.f.vars;
	curfun->u.f.vars = sym;
}

void
parse(void)
{
	void (*fun)(char *tok);
	uint8_t len;
	int c;
	char line[MAXLINE];

	for (;;) {
		switch (c = getchar()) {
		case 'L':
			fun = deflabel;
			break;
		case '\t':
			fun = expression;
			break;
		case 'S':
			/* struct */
			break;
		case 'P':
			fun = paramdcl;
			break;
		case 'A': case 'R': case 'T':
			fun = localdcl;
			break;
		case 'Y': case 'G':
			fun = globdcl;
			break;
		case '}':
			fun = endfunction;
			break;
		case EOF:
			goto found_eof;
		default:
			goto syntax_error;
		}

		ungetc(c, stdin);
		if (!fgets(line, sizeof(line), stdin))
			break;
		len = strlen(line);
		if (line[len-1] != '\n')
			error(ELNLINE);
		line[len-1] = '\0';
		(*fun)(strtok(line, "\t"));
	}

found_eof:
	if (ferror(stdin))
		error(EFERROR, strerror(errno));
	return;

syntax_error:
	error(ESYNTAX);
}
