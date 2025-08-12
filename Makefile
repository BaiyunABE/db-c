all:
	gcc -Wall -Wextra -g bptree.c main.c -o main

clean:
	rm -f main *.idx *.dat
