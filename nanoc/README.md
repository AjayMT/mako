
# nanoc
nanoc is a tiny subset of C and a tiny compiler that targets 32-bit x86 machines.

## Tiny?
The only types are:
- `int` (32-bit signed integer)
- `char` (8-bit signed integer)
- `void` (only to be used as a function return type or pointee type)
- pointers to the above types and to pointer types, which are functionally equivalent
  to 32-bit unsigned integers

Notably, there are no structs, unions, enums or arrays.

The only keywords are: `int`, `char`, `void`, `if`, `else`, `while`, `continue`, `break`, `return`.

There are many syntactic limitations:
- Declarations and assignments cannot both be made in a single statement. For example, `int a = 12;` is not valid but `int a; a = 12;` is.
- Only a single declaration can be made in a single statement. `int a; int b;` is valid but `int a, b;` is not.
- There is no operator precedence (see "Differences with C")
- The `++` and `--` operators are always prefix operators. `++a` is valid, but `a++` is not.
- There is no array indexing syntax. `*(a + 1)` is valid but `a[1]` is not.
- There is no unary `-` operator. `0 - a` is valid but `-a` is not.
- There is no preprocessor.

As of now, the compiler is just under 1800 lines of code. It only outputs 32-bit x86 machine code formatted as an ELF executable.

## Differences with C
nanoc is not a strict subset of C, as there are some small semantic differences between the two languages:
- Operators do not take precedence over each other; the order in which they are applied must be specified explicitly with parentheses. For example, `1 + 2 * 3` is not a valid expression but `1 + (2 * 3)` is.
- "Pointer arithmetic" does not exist: pointers are simply integers, so adding 1 to an `int*` will increment it by 1 instead of 4 (or whatever `sizeof(int)` is).
- There is no explicit type casting (or any type checking at all), all types are cast implicitly.
- Boolean operators always evaluate both of their operands. For example, `0` will always be evaluated in the expression `1 || 0`, and `1` will always be evaluated in the expression `0 && 1`.

## Purpose, Goals and TODOs
nanoc is meant to be a small, self-contained and easily portable compiler for a usable subset of C. I wrote it because I wanted to write and compile code on my hobby [operating system](https://github.com/AjayMT/mako) without having to port a [big](https://gcc.gnu.org/) toolchain. Since many hobby operating systems run on x86 and read ELF executables, I hope this project proves to be useful for other OSdev enthusiasts as well.

Most existing "little C compiler" projects output some form of assembly. Because writing an x86 assembler is a difficult (and not very interesting) task, I decided to make nanoc output x86 machine code directly rather than assembly text. nanoc is also capable of linking source code with a statically compiled archive (like a [libnanoc.a](https://github.com/AjayMT/mako/tree/master/src/libnanoc)) to produce an ELF executable that can use syscalls and library functions and do useful things.

As of now, nanoc produces very inefficient code. This is not desirable, but I would not sacrifice portability or too much simplicity for code efficiency. It is also a very rudimentary linker -- in particular, the way nanoc combines `.data`, `.rodata` and `.bss` sections and handles global variables is questionable at best and non-functional at worst.

TODO.md lists TODOs to be addressed in the short-term.

## Build and Usage
To compile nanoc:
```
make
```

To use an OS-specific toolchain, simply define the `CC` variable:
```
CC=i686-pc-myos-gcc make
```

nanoc depends on a few libc functions: some simple ones from `string.h`, malloc+realloc, fopen+fread+fwrite, printf and atoi.

If you are having trouble porting nanoc to your operating system, please reach out to me! I am happy to help. Feel free to raise an issue on this repository or send me an [email](mailto:ajaymt2@illinois.edu).

To use nanoc:
```
nanoc <filename> [<archive>]
```

For example:
```
nanoc program.c
```
or
```
nanoc program.c /usr/lib/libnanoc.a
```

This will compile `program.c` and produce an executable file called `a.out`. If an archive file is specified, nanoc will link it with `program.c` so the program can use functions defined in the archive.
