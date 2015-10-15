This program takes two files as inputs, and prints the strings that occur at
least twice in the first input, together with the number of times they occur in
the second input.

The user can provide thresholds so that only strings that occur at least X
times in input 1 are printed, and only strings that occur at most Y times in
input 2 are printed.

Example run:

    $ ./substring-frequencies --threshold-count=0 \
      --threshold=2.5 --skip-prefixes \
      /usr/share/common-licenses/GPL-3 \
      /usr/share/common-licenses/GPL-2
    3.178   23      0       Corresponding
    3.296   26      0       Source
    2.890   17      0        a covered
    3.584   35      0        conve
    3.638   37      0       red w
    2.773   15      0       rial
    3.258   25      0       uct
    2.565   12      0       the object code
    2.674   28      1       he work
    2.639   13      0       k i
    2.833   16      0       aga
    3.157   46      1       vey
    2.708   14      0       d work,
    2.944   18      0       onal
    2.565   12      0       e covered
    2.565   12      0       er a
    2.773   15      0       eying
    2.708   14      0       gate
    2.833   16      0       mate
    2.996   19      0       produc
    2.773   15      0       onal
    2.833   16      0       teri

Building:

    $ ./configure
    $ make
    # make install
