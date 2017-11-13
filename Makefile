.PHONY: all
all: rtt2pty

rtt2pty: main.o
	$(CC) $? -ldl -o $@

clean:
	rm -f rtt2pty main.o
