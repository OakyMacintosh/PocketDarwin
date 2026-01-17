# WriterSc
Scripting Language for General Purpose low level programming without compilers.

## Main purpose
This language was developed with OCMobile in mind
so it works as a driver language, instaed of using
EFI binaries, OpenCore Mobile uses .wscd files, which
describes the driver and implements its functionality.

## Characteristics
The language is C-like with some little keyword and
identation changes. And it's meant to be C-compatible
in syntax, so drivers (through the IOKit API or a custom api)
can be easily made of the programmer was just coding it in
C language.