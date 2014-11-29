BEGIN {
	FS="|";
	getline;
	print $0;
	printf("---");
	for(i=1; i<NF; i++) {
		printf("|---");
	}
	printf("\n");
}
{
	print $0
}
