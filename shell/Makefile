CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c99 -D_WIN32_WINNT=0x0600
LDFLAGS = -lkernel32 -luser32
TARGET  = myShell.exe
SRCDIR  = src
SRCS    = $(SRCDIR)/main.c \
          $(SRCDIR)/globals.c \
          $(SRCDIR)/parser.c \
          $(SRCDIR)/process.c \
          $(SRCDIR)/process_list.c \
          $(SRCDIR)/builtins.c \
          $(SRCDIR)/line_editor.c \
          $(SRCDIR)/shell_utils.c

OBJS    = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(SRCDIR)/%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -I$(SRCDIR) -c -o $@ $<

clean:
	-del /Q $(SRCDIR)\*.o $(TARGET) 2>NUL

test: $(TARGET)
	powershell -NoProfile -ExecutionPolicy Bypass -File tests\regression.ps1
	python tests\ctrl_c_test.py

.PHONY: all clean test
