all:
	gcc -Wall -Wextra -g bptree.c main.c -o output

clean:
	rm -f output *.idx *.dat
