
/*
name: TEST027
description: Test of cpp stringizer
error:

output:
F2	I
G3	F2	main
{
\
A5	P	p
	A5	"68656C6C6F20697320626574746572207468616E20627965	'P	:P
	r	A5	@K	gI
}

*/

#define x(y) #y

int
main(void)
{
	char *p;
	p = x(hello)  " is better than bye";

	return *p;
}

