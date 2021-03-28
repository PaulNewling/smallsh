# @File - Makefile
# @Original MakeFile provided by Trinity Lundgren <lundgret@oregonstate.edu>
# @Modified by - Paul Newling newlingp@oregonstate.edu
# @Description - Makefile for CS 344 Assignment 3: Smallsh

# Project name
project = smallsh

# Compiler
CXX = gcc

# Source files
sources = main.c

# Create objects from source files
objects = $(sources:.c=.o)

# Output executable
EXE = $(project)

# Compiler flags
CFLAGS = -std=gnu99 -g


#Valgrind options
VOPT = --tool=memcheck --leak-check=full --show-leak-kinds=all --track-origins=yes

# Phony targets
.PHONY: default debug  clean zip

# Default behavior: clean, compile, run
default: clean $(EXE) # Debug is toggled off for submission

# Debug: pass to valgrind to check for memory leaks
debug: $(EXE)
	valgrind $(VOPT) ./$(EXE)

# '$@' refers to tag, '$^' to dependency
$(EXE) : $(objects)
	$(CXX) $(CFLAGS) $^ -o $@

# Create .o files from corresponding .c files
%.o: %.c
	$(CXX) $(CFLAGS) -c $^

# Create a zip archive of the project files for submission
zip:
	zip -D newlingp_program3.zip *.c *.h Makefile README.txt p3testscript

clean:
	rm -r -f *.o *.zip junk junk2 $(EXE)