#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../inc/cc.h"
#include "../inc/sizes.h"
#include "cc1.h"

#define XCHG(lp, rp, np) (np = lp, lp = rp, rp = np)

Node *expr(void);

bool
cmpnode(Node *np, TUINT val)
{
	Symbol *sym;
	Type *tp;
	TUINT mask, nodeval;

	if (!np || !np->constant)
		return 0;
	sym = np->sym;
	tp = sym->type;

	switch (tp->op) {
	case PTR:
	case INT:
		mask = (val > 1) ? ones(np->type->size) : -1;
		nodeval = (tp->sign) ? sym->u.i : sym->u.u;
		return (nodeval & mask) == (val & mask);
	case FLOAT:
		return sym->u.f == val;
	}
	return 0;
}

bool
isnodecmp(int op)
{
	switch (op) {
	case OEQ:
	case ONE:
	case OLT:
	case OGE:
	case OLE:
	case OGT:
		return 1;
	default:
		return 0;
	}
}

static Node *
promote(Node *np)
{
	Type *tp;
	Node *new;
	unsigned r;
	struct limits *lim, *ilim;

	tp = np->type;

	switch (tp->op) {
	case ENUM:
	case INT:
		if (tp->n.rank >= inttype->n.rank)
			return np;
		lim = getlimits(tp);
		ilim = getlimits(inttype);
		tp = (lim->max.i <= ilim->max.i) ? inttype : uinttype;
		break;
	case FLOAT:
		/* TODO: Add support for C99 float math */
		tp = doubletype;
		break;
	default:
		abort();
	}
	if ((new = convert(np, tp, 1)) != NULL)
		return new;
	return np;
}

static void
arithconv(Node **p1, Node **p2)
{
	int n, to = 0, s1, s2;
	unsigned r1, r2;
	Type *tp1, *tp2;
	Node *np1, *np2;
	struct limits *lp1, *lp2;

	np1 = promote(*p1);
	np2 = promote(*p2);

	tp1 = np1->type;
	tp2 = np2->type;

	if (tp1 == tp2)
		goto set_p1_p2;

	s1 = tp1->sign, r1 = tp1->n.rank, lp1 = getlimits(tp1);
	s2 = tp2->sign, r2 = tp2->n.rank, lp2 = getlimits(tp2);

	if (s1 == s2 || tp1->op == FLOAT || tp2->op == FLOAT) {
		to = r1 - r2;
	} else if (!s1) {
		if (r1 >= r2 || lp1->max.i >= lp2->max.i)
			to = 1;
		else
			to = -1;
	} else {
		if (r2 >= r1 || lp2->max.i >= lp1->max.i)
			to = -1;
		else
			to = 1;
	}

	if (to > 0)
		np2 = convert(np2, tp1, 1);
	else if (to < 0)
		np1 = convert(np1, tp2, 1);
		
set_p1_p2:
	*p1 = np1;
	*p2 = np2;
}

static int
null(Node *np)
{
	if (!np->constant || np->type != pvoidtype)
		return 0;
	return cmpnode(np, 0);
}

static Node *
chkternary(Node *yes, Node *no)
{
	yes = decay(yes);
	no = decay(no);

	/*
	 * FIXME:
	 * We are ignoring type qualifiers here,
	 * but the standard has strong rules about this.
	 * take a look to 6.5.15
	 */

	if (!eqtype(yes->type, no->type)) {
		if (yes->type->arith && no->type->arith) {
			arithconv(&yes, &no);
		} else if (yes->type->op != PTR && no->type->op != PTR) {
			goto wrong_type;
		} else {
			/* convert integer 0 to NULL */
			if (yes->type->integer && cmpnode(yes, 0))
				yes = convert(yes, pvoidtype, 0);
			if (no->type->integer && cmpnode(no, 0))
				no = convert(no, pvoidtype, 0);
			/*
			 * At this point the type of both should be
			 * a pointer to something, or we have don't
			 * compatible types
			 */
			if (yes->type->op != PTR || no->type->op != PTR)
				goto wrong_type;
			/*
			 * If we have a null pointer constant then
			 * convert to the another type
			 */
			if (null(yes))
				yes = convert(yes, no->type, 0);
			if (null(no))
				no = convert(no, yes->type, 0);

			if (!eqtype(yes->type, no->type))
				goto wrong_type;
		}
	}
	return node(OCOLON, yes->type, yes, no);

wrong_type:
	errorp("type mismatch in conditional expression");
	freetree(yes);
	freetree(no);
	return constnode(zero);
}

static void
chklvalue(Node *np)
{
	if (!np->lvalue)
		error("lvalue required in operation");
	if (np->type == voidtype)
		error("invalid use of void expression");
}

Node *
decay(Node *np)
{
	Type *tp = np->type;

	switch (tp->op) {
	case ARY:
		tp = tp->type;
		if (np->op == OPTR) {
			Node *new = np->left;
			free(np);
			new->type = mktype(tp, PTR, 0, NULL);
			return new;
		}
	case FTN:
		np = node(OADDR, mktype(tp, PTR, 0, NULL), np, NULL);
	default:
		return np;
	}
}

static Node *
integerop(char op, Node *lp, Node *rp)
{
	if (!lp->type->integer || !rp->type->integer)
		error("operator requires integer operands");
	arithconv(&lp, &rp);
	return simplify(op, lp->type, lp, rp);
}

static Node *
integeruop(char op, Node *np)
{
	if (!np->type->integer)
		error("unary operator requires integer operand");
	np = promote(np);
	if (op == OCPL && np->op == OCPL)
		return np->left;
	return simplify(op, np->type, np, NULL);
}

static Node *
numericaluop(char op, Node *np)
{
	if (!np->type->arith)
		error("unary operator requires numerical operand");
	np = promote(np);
	if (op == ONEG && np->op == ONEG)
		return np->left;
	if (op == OADD)
		return np;
	return simplify(op, np->type, np, NULL);
}

Node *
convert(Node *np, Type *newtp, char iscast)
{
	Type *oldtp = np->type;

	if (eqtype(newtp, oldtp))
		return np;

	switch (oldtp->op) {
	case ENUM:
	case INT:
	case FLOAT:
		switch (newtp->op) {
		case PTR:
			if (oldtp->op == FLOAT || !cmpnode(np, 0) && !iscast)
				return NULL;
			/* PASSTHROUGH */
		case INT:
		case FLOAT:
		case ENUM:
			break;
		default:
			return NULL;
		}
		break;
	case PTR:
		switch (newtp->op) {
		case ENUM:
		case INT:
		case VOID:
			if (!iscast)
				return NULL;
			break;
		case PTR:
			if (iscast ||
			    newtp == pvoidtype || oldtp == pvoidtype) {
				np->type = newtp;
				return np;
			}
		default:
			return NULL;
		}
	default:
		return NULL;
	}
	return castcode(np, newtp);
}

static Node *
parithmetic(char op, Node *lp, Node *rp)
{
	Type *tp;
	Node *size, *np;

	if (lp->type->op != PTR)
		XCHG(lp, rp, np);

	tp = lp->type;
	size = sizeofnode(tp->type);

	if (op == OSUB && BTYPE(rp) == PTR) {
		if (tp != rp->type)
			goto incorrect;
		lp = node(OSUB, inttype, lp, rp);
		return node(ODIV, inttype, lp, size);
	}
	if (!rp->type->integer)
		goto incorrect;

	rp = convert(promote(rp), sizettype, 0);
	rp = simplify(OMUL, sizettype, rp, size);
	rp = convert(rp, tp, 1);

	return simplify(OADD, tp, lp, rp);

incorrect:
	errorp("incorrect arithmetic operands");
	return node(OADD, tp, lp, rp);
}

static Node *
arithmetic(char op, Node *lp, Node *rp)
{
	Type *ltp = lp->type, *rtp = rp->type;

	if (ltp->arith && rtp->arith) {
		arithconv(&lp, &rp);
	} else if ((ltp->op == PTR || rtp->op == PTR) &&
	           (op == OADD || op == OSUB)) {
		return parithmetic(op, rp, lp);
	} else if (op != OINC && op != ODEC) {
		errorp("incorrect arithmetic operands");
	}
	return simplify(op, lp->type, lp, rp);
}

static Node *
pcompare(char op, Node *lp, Node *rp)
{
	Node *np;
	int err = 0;

	if (lp->type->integer)
		XCHG(lp, rp, np);

	if (rp->type->integer) {
		if (!cmpnode(rp, 0))
			err = 1;
		rp = convert(rp, pvoidtype, 1);
	} else if (rp->type->op == PTR) {
		if (!eqtype(lp->type, rp->type))
			err = 1;
	} else {
		err = 1;
	}
	if (err)
		errorp("incompatibles type in comparision");
	return simplify(op, inttype, lp, rp);
}

static Node *
compare(char op, Node *lp, Node *rp)
{
	Type *ltp, *rtp;

	lp = decay(lp);
	rp = decay(rp);

	ltp = lp->type;
	rtp = rp->type;

	if (ltp->op == PTR || rtp->op == PTR) {
		return pcompare(op, rp, lp);
	} else if (ltp->arith && rtp->arith) {
		arithconv(&lp, &rp);
		return simplify(op, inttype, lp, rp);
	} else {
		errorp("incompatibles type in comparision");
		freetree(lp);
		freetree(rp);
		return constnode(zero);
	}
}

int
negop(int op)
{
	switch (op) {
	case OAND: return OOR;
	case OOR:  return OAND;
	case OEQ:  return ONE;
	case ONE:  return OEQ;
	case OLT:  return OGE;
	case OGE:  return OLT;
	case OLE:  return OGT;
	case OGT:  return OLE;
	}
	return op;
}

Node *
negate(Node *np)
{
	np->op = negop(np->op);
	return np;
}

static Node *
exp2cond(Node *np, char neg)
{
	np = decay(np);
	if (np->type->aggreg) {
		errorp("used struct/union type value where scalar is required");
		np = constnode(zero);
	}
	if (isnodecmp(np->op))
		return (neg) ? negate(np) : np;
	return compare((neg) ?  OEQ : ONE, np, constnode(zero));
}

static Node *
logic(char op, Node *lp, Node *rp)
{
	lp = exp2cond(lp, 0);
	rp = exp2cond(rp, 0);
	return simplify(op, inttype, lp, rp);
}

static Node *
field(Node *np)
{
	Symbol *sym;

	namespace = np->type->ns;
	next();
	namespace = NS_IDEN;

	sym = yylval.sym;
	if (yytoken != IDEN)
		unexpected();
	next();

	if (!np->type->aggreg) {
		errorp("request for member '%s' in something not a structure or union",
		      yylval.sym->name);
		goto free_np;
	}
	if ((sym->flags & ISDECLARED) == 0) {
		errorp("incorrect field in struct/union");
		goto free_np;
	}
	np = node(OFIELD, sym->type, np, varnode(sym));
	np->lvalue = 1;
	return np;

free_np:
	freetree(np);
	return constnode(zero);
}

static Node *
content(char op, Node *np)
{
	np = decay(np);
	switch (BTYPE(np)) {
	case ARY:
	case FTN:
	case PTR:
		if (np->op == OADDR) {
			Node *new = np->left;
			new->type = np->type->type;
			free(np);
			np = new;
		} else {
			np = node(op, np->type->type, np, NULL);
		}
		np->lvalue = 1;
		return np;
	default:
		error("invalid argument of memory indirection");
	}
}

static Node *
array(Node *lp, Node *rp)
{
	Type *tp;
	Node *np;

	if (!lp->type->integer && !rp->type->integer)
		error("array subscript is not an integer");
	np = arithmetic(OADD, decay(lp), decay(rp));
	tp = np->type;
	if (tp->op != PTR)
		errorp("subscripted value is neither array nor pointer");
	return content(OPTR, np);
}

static Node *
assignop(char op, Node *lp, Node *rp)
{
	if ((rp = convert(decay(rp), lp->type, 0)) == NULL) {
		errorp((op == OINIT) ?
		        "incorrect initiliazer" :
		        "incompatible types when assigning");
		return lp;
	}

	return node(op, lp->type, lp, rp);
}

static Node *
incdec(Node *np, char op)
{
	Type *tp = np->type;
	Node *inc;

	chklvalue(np);

	if (!tp->defined) {
		errorp("invalid use of undefined type");
		return np;
	} else if (tp->arith) {
		inc = constnode(one);
	} else if (tp->op == PTR) {
		inc = sizeofnode(tp->type);
	} else {
		errorp("wrong type argument to increment or decrement");
		return np;
	}
	return arithmetic(op, np, inc);
}

static Node *
address(char op, Node *np)
{
	if (BTYPE(np) != FTN) {
		chklvalue(np);
		if (np->symbol && (np->sym->flags & ISREGISTER))
			errorp("address of register variable '%s' requested", yytext);
		if (np->op == OPTR) {
			Node *new = np->left;
			free(np);
			return new;
		}
	}
	return node(op, mktype(np->type, PTR, 0, NULL), np, NULL);
}

static Node *
negation(char op, Node *np)
{
	np = decay(np);
	if (!np->type->arith && np->type->op != PTR) {
		errorp("invalid argument of unary '!'");
		freetree(np);
		return constnode(zero);
	}
	return exp2cond(np, 1);
}

static Symbol *
notdefined(Symbol *sym)
{
	int isdef;

	if (namespace == NS_CPP && !strcmp(sym->name, "defined")) {
		disexpand = 1;
		next();
		expect('(');
		sym = yylval.sym;
		expect(IDEN);
		expect(')');

		isdef = (sym->flags & ISDECLARED) != 0;
		sym = newsym(NS_IDEN);
		sym->type = inttype;
		sym->flags |= ISCONSTANT;
		sym->u.i = isdef;
		disexpand = 0;
		return sym;
	}
	errorp("'%s' undeclared", yytext);
	sym->type = inttype;
	return install(sym->ns, yylval.sym);
}

/*************************************************************
 * grammar functions                                         *
 *************************************************************/
static Node *
primary(void)
{
	Node *np;
	Symbol *sym;

	sym = yylval.sym;
	switch (yytoken) {
	case CONSTANT:
		np = constnode(sym);
		next();
		break;
	case IDEN:
		if ((sym->flags & ISDECLARED) == 0)
			sym = notdefined(sym);
		if (sym->flags & ISCONSTANT) {
			np = constnode(sym);
			break;
		}
		sym->flags |= ISUSED;
		np = varnode(sym);
		next();
		break;
	default:
		unexpected();
	}
	return np;
}

static Node *assign(void);

static Node *
arguments(Node *np)
{
	int toomany, n;
	Node *par = NULL, *arg;
	Type *argtype, **targs, *tp = np->type, *rettype;

	if (tp->op == PTR && tp->type->op == FTN) {
		np = content(OPTR, np);
		tp = np->type;
	}
	if (tp->op != FTN) {
		targs = (Type *[]) {ellipsistype};
		n = 1;
		rettype = inttype;
		errorp("function or function pointer expected");
	} else {
		targs = tp->p.pars;
		n = tp->n.elem;
		rettype = tp->type;
	}

	expect('(');
	if (yytoken == ')')
		goto no_pars;
	toomany = 0;

	do {
		arg = decay(assign());
		argtype = *targs;
		if (argtype == ellipsistype) {
			n = 0;
			switch (arg->type->op) {
			case INT:
				arg = promote(arg);
				break;
			case FLOAT:
				if (arg->type == floattype)
					arg = convert(arg, doubletype, 1);
				break;
			}
			par = node(OPAR, arg->type, par, arg);
			continue;
		}
		if (--n < 0) {
			if (!toomany)
				errorp("too many arguments in function call");
			toomany = 1;
			continue;
		}
		++targs;
		if ((arg = convert(arg, argtype, 0)) != NULL) {
			par = node(OPAR, arg->type, par, arg);
			continue;
		}
		errorp("incompatible type for argument %d in function call",
		       tp->n.elem - n + 1);
	} while (accept(','));

no_pars:
	expect(')');
	if (n > 0 && *targs != ellipsistype)
		errorp("too few arguments in function call");

	return node(OCALL, rettype, np, par);
}

static Node *
postfix(Node *lp)
{
	Node *rp;

	if (!lp)
		lp = primary();
	for (;;) {
		switch (yytoken) {
		case '[':
			next();
			rp = expr();
			lp = array(lp, rp);
			expect(']');
			break;
		case DEC:
		case INC:
			lp = incdec(lp, (yytoken == INC) ? OINC : ODEC);
			next();
			break;
		case INDIR:
			lp = content(OPTR, lp);
		case '.':
			lp = field(lp);
			break;
		case '(':
			lp = arguments(lp);
			break;
		default:
			return lp;
		}
	}
}

static Node *unary(void);

static Type *
typeof(Node *np)
{
	Type *tp;

	if (np == NULL)
		unexpected();
	tp = np->type;
	freetree(np);
	return tp;
}

static Type *
sizeexp(void)
{
	Type *tp;

	expect('(');
	switch (yytoken) {
	case TYPE:
	case TYPEIDEN:
		tp = typename();
		break;
	default:
		tp = typeof(unary());
		break;
	}
	expect(')');
	return tp;
}

static Node *cast(void);

static Node *
unary(void)
{
	Node *(*fun)(char, Node *);
	char op;
	Type *tp;

	switch (yytoken) {
	case SIZEOF:
		next();
		tp = (yytoken == '(') ? sizeexp() : typeof(unary());
		if (!tp->defined)
			errorp("sizeof applied to an incomplete type");
		return sizeofnode(tp);
	case INC:
	case DEC:
		op = (yytoken == INC) ? OA_ADD : OA_SUB;
		next();
		return incdec(unary(), op);
	case '!': op = 0;     fun = negation;     break;
	case '+': op = OADD;  fun = numericaluop; break;
	case '-': op = ONEG;  fun = numericaluop; break;
	case '~': op = OCPL;  fun = integeruop;   break;
	case '&': op = OADDR; fun = address;      break;
	case '*': op = OPTR;  fun = content;      break;
	default:  return postfix(NULL);
	}

	next();
	return (*fun)(op, cast());
}

static Node *
cast(void)
{
	Node *lp, *rp;
	Type *tp;
	static int nested;

	if (!accept('('))
		return unary();

	switch (yytoken) {
	case TQUALIFIER:
	case TYPE:
		tp = typename();
		switch (tp->op) {
		case ARY:
			error("cast specify an array type");
		case FTN:
			error("cast specify a function type");
		default:
			expect(')');
			lp = cast();
			if ((rp = convert(lp,  tp, 1)) == NULL)
				error("bad type convertion requested");
			rp->lvalue = lp->lvalue;
		}
		break;
	default:
		if (nested == NR_SUBEXPR)
			error("too expressions nested by parentheses");
		++nested;
		rp = expr();
		--nested;
		expect(')');
		rp = postfix(rp);
		break;
	}

	return rp;
}

static Node *
mul(void)
{
	Node *np, *(*fun)(char, Node *, Node *);
	char op;

	np = cast();
	for (;;) {
		switch (yytoken) {
		case '*': op = OMUL; fun = arithmetic; break;
		case '/': op = ODIV; fun = arithmetic; break;
		case '%': op = OMOD; fun = integerop;  break;
		default: return np;
		}
		next();
		np = (*fun)(op, np, cast());
	}
}

static Node *
add(void)
{
	char op;
	Node *np;

	np = mul();
	for (;;) {
		switch (yytoken) {
		case '+': op = OADD; break;
		case '-': op = OSUB; break;
		default:  return np;
		}
		next();
		np = arithmetic(op, np, mul());
	}
}

static Node *
shift(void)
{
	char op;
	Node *np;

	np = add();
	for (;;) {
		switch (yytoken) {
		case SHL: op = OSHL; break;
		case SHR: op = OSHR; break;
		default:  return np;
		}
		next();
		np = integerop(op, np, add());
	}
}

static Node *
relational(void)
{
	char op;
	Node *np;

	np = shift();
	for (;;) {
		switch (yytoken) {
		case '<': op = OLT; break;
		case '>': op = OGT; break;
		case GE:  op = OGE; break;
		case LE:  op = OLE; break;
		default:  return np;
		}
		next();
		np = compare(op, np, shift());
	}
}

static Node *
eq(void)
{
	char op;
	Node *np;

	np = relational();
	for (;;) {
		switch (yytoken) {
		case EQ: op = OEQ; break;
		case NE: op = ONE; break;
		default: return np;
		}
		next();
		np = compare(op, np, relational());
	}
}

static Node *
bit_and(void)
{
	Node *np;

	np = eq();
	while (accept('&'))
		np = integerop(OBAND, np, eq());
	return np;
}

static Node *
bit_xor(void)
{
	Node *np;

	np = bit_and();
	while (accept('^'))
		np = integerop(OBXOR,  np, bit_and());
	return np;
}

static Node *
bit_or(void)
{
	Node *np;

	np = bit_xor();
	while (accept('|'))
		np = integerop(OBOR, np, bit_xor());
	return np;
}

static Node *
and(void)
{
	Node *np;

	np = bit_or();
	while (accept(AND))
		np = logic(OAND, np, bit_or());
	return np;
}

static Node *
or(void)
{
	Node *np;

	np = and();
	while (accept(OR))
		np = logic(OOR, np, and());
	return np;
}

static Node *
ternary(void)
{
	Node *cond;

	cond = or();
	while (accept('?')) {
		Node *ifyes, *ifno, *np;

		cond = exp2cond(cond, 0);
		ifyes = expr();
		expect(':');
		ifno = ternary();
		np = chkternary(ifyes, ifno);
		cond = simplify(OASK, np->type, cond, np);
	}
	return cond;
}

static Node *
assign(void)
{
	Node *np, *(*fun)(char , Node *, Node *);
	char op;

	np = ternary();
	for (;;) {
		switch (yytoken) {
		case '=':    op = OASSIGN; fun = assignop;   break;
		case MUL_EQ: op = OA_MUL;  fun = arithmetic; break;
		case DIV_EQ: op = OA_DIV;  fun = arithmetic; break;
		case MOD_EQ: op = OA_MOD;  fun = integerop;  break;
		case ADD_EQ: op = OA_ADD;  fun = arithmetic; break;
		case SUB_EQ: op = OA_SUB;  fun = arithmetic; break;
		case SHL_EQ: op = OA_SHL;  fun = integerop;  break;
		case SHR_EQ: op = OA_SHR;  fun = integerop;  break;
		case AND_EQ: op = OA_AND;  fun = integerop;  break;
		case XOR_EQ: op = OA_XOR;  fun = integerop;  break;
		case OR_EQ:  op = OA_OR;   fun = integerop;  break;
		default: return np;
		}
		chklvalue(np);
		next();
		np = (fun)(op, np, assign());
	}
}

Node *
constexpr(void)
{
	Node *np;

	np = ternary();
	if (!np->constant) {
		freetree(np);
		return NULL;
	}
	return np;
}

Node *
iconstexpr(void)
{
	Node *np;

	if ((np = constexpr()) == NULL)
		return NULL;

	if (np->type->op != INT) {
		freetree(np);
		return NULL;
	}

	return convert(np, inttype, 0);
}

Node *
expr(void)
{
	Node *lp, *rp;

	lp = assign();
	while (accept(',')) {
		rp = assign();
		lp = node(OCOMMA, rp->type, lp, rp);
	}

	return lp;
}

Node *
condexpr(void)
{
	Node *np;

	np = exp2cond(expr(), 0);
	if (np->constant)
		warn("conditional expression is constant");
	return np;
}

struct designator {
	TINT pos;
	struct designator *next;
};

static TINT
arydesig(Type *tp)
{
	TINT npos;
	Node *np;

	if (tp->op != ARY)
		errorp("array index in non-array initializer");
	next();
	np = iconstexpr();
	npos = np->sym->u.i;
	freetree(np);
	expect(']');
	return npos;
}

static TINT
fielddesig(Type *tp)
{
	TINT npos;
	int ons;
	Symbol *sym, **p;

	if (!tp->aggreg)
		errorp("field name not in record or union initializer");
	ons = namespace;
	namespace = tp->ns;
	next();
	namespace = ons;
	if (yytoken != IDEN)
		unexpected();
	sym = yylval.sym;
	if ((sym->flags & ISDECLARED) == 0) {
		errorp(" unknown field '%s' specified in initializer",
		      sym->name);
		return 0;
	}
	for (p = tp->p.fields; *p != sym; ++p)
		/* nothing */;
	return p - tp->p.fields;
}

static struct designator *
designation(Type *tp)
{
	struct designator *des = NULL, *d;
	TINT (*fun)(Type *);

	for (;;) {
		switch (yytoken) {
		case '[': fun = arydesig;   break;
		case '.': fun = fielddesig; break;
		default:
			if (des)
				expect('=');
			return des;
		}
		d = xmalloc(sizeof(*d));
		d->next = NULL;

		if (!des) {
			des = d;
		} else {
			des->next = d;
			des = d;
		}
		des->pos  = (*fun)(tp);
	}
}

static void
initlist(Symbol *sym, Type *tp)
{
	struct designator *des;
	int toomany = 0;
	TINT n;
	Type *newtp;

	for (n = 0; ; ++n) {
		if ((des = designation(tp)) == NULL) {
			des = xmalloc(sizeof(*des));
			des->pos = n;
		} else {
			n = des->pos;
		}
		switch (tp->op) {
		case ARY:
			if (tp->defined && n >= tp->n.elem) {
				if (!toomany)
					warn("excess elements in array initializer");
				toomany = 1;
				sym = NULL;
			}
			newtp = tp->type;
			break;
		case STRUCT:
			if (n >= tp->n.elem) {
				if (!toomany)
					warn("excess elements in struct initializer");
				toomany = 1;
				sym = NULL;
			} else {
				sym = tp->p.fields[n];
				newtp = sym->type;
			}
			break;
		default:
			newtp = tp;
			warn("braces around scalar initializer");
			if (n > 0) {
				if (!toomany)
					warn("excess elements in scalar initializer");
				toomany = 1;
				sym = NULL;
			}
			break;
		}
		initializer(sym, newtp, n);
		if (!accept(','))
			break;
	}
	expect('}');

	if (tp->op == ARY && !tp->defined) {
		tp->n.elem = n + 1;
		tp->defined = 1;
	}
}

void
initializer(Symbol *sym, Type *tp, int nelem)
{
	Node *np;
	int flags = sym->flags;

	if (tp->op == FTN)
		error("function '%s' is initialized like a variable", sym->name);

	if (accept('{')) {
		initlist(sym, tp);
		return;
	}
	np = assign();

	/* if !sym it means there are too much initializers */
	if (!sym)
		return;
	if (nelem >= 0)
		return;

	np = assignop(OINIT, varnode(sym), np);

	if (flags & ISDEFINED) {
		errorp("redeclaration of '%s'", sym->name);
	} else if ((flags & (ISGLOBAL|ISLOCAL|ISPRIVATE)) != 0) {
		if (!np->right->constant)
			errorp("initializer element is not constant");
		emit(OINIT, np);
		sym->flags |= ISDEFINED;
	} else if ((flags & (ISEXTERN|ISTYPEDEF)) != 0) {
		errorp("'%s' has both '%s' and initializer",
		       sym->name, (flags&ISEXTERN) ? "extern" : "typedef");
	} else {
		np->op = OASSIGN;
		emit(OEXPR, np);
	}
}
