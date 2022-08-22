#include <ctype.h>
#include <math.h>
#include <stdio.h>

static char buff[200];

int main()
{
	int i, j;
	char *p;
	double v;

	for(i=0; i<128; i++){
		if(i%8==0) printf("\n   ");
		fgets(buff, 200, stdin);
		p=buff;
		for(j=0; j<4; j++){
			while(isspace(*p)) p++;
			p+=7;
			v=atof(p);
			p++;
			while(!isspace(*p)) p++;
			v*=1024.0;
			if(0<v) v+=0.5;
			if(v<0) v-=0.5;
			if(j==0) printf(" %5d,", (int)v);
		}
	}
	return 0;
}

