
#include <stddef.h>
#include <stdint.h>

#include <cc.h>
#include "cc2.h"


#include <stdio.h>

static Node *
optcasts(Node *np, Type *tp)
{
	if (!np)
		return NULL;

repeat:
	switch (np->op) {
	case OCAST:
		/* TODO: be careful with the sign */
		if (np->type->c_int && np->type->size >= tp->size) {
			np = np->left;
			goto repeat;
		}
		break;
	case OASSIG:
		tp = np->type;
		break;
	default:
		np->type = tp;
	}

	np->left = optcasts(np->left, tp);
	np->right = optcasts(np->right, tp);
	return np;			
}

Node *
optimize(Node *np)
{
	np = optcasts(np, np->type);
	return np;
}
