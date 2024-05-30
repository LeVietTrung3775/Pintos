#include <stdio.h>
#include <syscall.h>
#include <stdlib.h>
int
main (int argc, char *argv[]) 
{

    if(argc!=5){
        printf("usage : %s [n1] [n2] [n3] [n4]\n",argv[0]);
        return -1;
    }

    int n1=atoi(argv[1]);
    int n2=atoi(argv[2]);
    int n3=atoi(argv[3]);
    int n4=atoi(argv[4]);

    int fib=fibonacci(n1);
    int max_4=max_of_four_int(n1,n2,n3,n4);
    printf("%d %d\n",fib,max_4);

    return 0;
}
