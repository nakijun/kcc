#!/bin/sh

out=/tmp/$$.out
err=/tmp/$$.err

trap "rm -f $out $err" EXIT INT QUIT HUP

case $# in
0)
	echo "usage: update.sh test ..." >&2
	exit 1
	;;
*)
	for i
	do
		../cc1 -I./ -w $i  >$out 2>$err
		(echo '/^error/+;/^output/-c'
		cat $err
		printf "\n.\n"
		echo '/^output/+;/^\*\//-c'
		cat $out
		printf "\n.\nw\n") | ed -s $i
	done
	;;
esac
