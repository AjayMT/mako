
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

As of now, the compiler is just under 1500 lines of code. It only outputs 32-bit x86 machine code formatted as an ELF executable.

## Differences with C
nanoc is not a strict subset of C, as there are some small semantic differences between the two languages:
- Operators do not take precedence over each other; they are applied sequentially from right to left. For example, the expression `2 * 3 + 4 - 2` will evaluate to 10.
- "Pointer arithmetic" does not exist: pointers are simply integers, so adding 1 to an `int*` will increment it by 1 instead of 4 (or whatever `sizeof(int)` is).
- There is no explicit type casting (or any type checking at all), all types are cast implicitly.
- Boolean operators always evaluate both of their operands. For example, `0` will always be evaluated in the expression `1 || 0`, and `1` will always be evaluated in the expression `0 && 1`.

## Purpose, Goals and TODOs
nanoc is meant to be a small, self-contained (no dependencies) easily portable compiler for a usable subset of C. I wrote it because I wanted to write and compile code on my hobby [operating system](https://github.com/AjayMT/mako) without having to port a [big](https://gcc.gnu.org/) toolchain or even a [small (but not small enough)](https://bellard.org/tcc/) toolchain. Since many hobby operating systems run on x86 and read ELF executables, I hope this project proves to be useful for other OSdev enthusiasts as well.

Most existing "little C compiler" projects output some form of assembly. Because writing an x86 assembler is a difficult (and not very interesting) task, I decided to make nanoc output x86 machine code directly: nanoc produces a small subset of x86 instructions (for which I have hard-coded all of the opcodes) and places them in an ELF executable file. The result of this is a compiler that does not depend on an assembler, linker or any other tool.

nanoc is a work in progress. As of now, it compiles a single C file to a single executable without performing any "linking". My eventual goal is to have it link a C file with a statically compiled archive (like a libc.a), so it can be used to compile programs that depend on a library.

nanoc also produces very inefficient code. This is not desirable, but I would not sacrifice portability or too much simplicity for code efficiency.

TODOs to be addressed in the short-term:
- nanoc does not perform any relocations. Right now, the only consequence of this is that forward-declared functions do not work as expected.
- Not all escaped characters inside string or character literals are escaped correctly.
- The multiplication operator does not handle overflow correctly when multiplying `char`s
- Documentation and code quality (of course)

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
nanoc program.c
```

This will produce an executable file called `a.out`. nanoc does not (yet) link multiple files.
