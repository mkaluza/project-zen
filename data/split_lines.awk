#!/bin/awk -f 
{
	if ($1 != mem) {
		print "";
		mem=$1;
	}
	print $0
}
