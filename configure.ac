AC_INIT(substring-frequencies,1.0.0)
AM_INIT_AUTOMAKE([-Wall -Werror foreign])

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_PROG_CC
AC_PROG_CXX
AC_PROG_INSTALL
AC_PROG_MAKE_SET

AC_SUBST(PACKAGES_CFLAGS)
AC_SUBST(PACKAGES_LIBS)

AC_CHECK_HEADER([divsufsort.h], [], [AC_MSG_ERROR([libdivsufsort is required.  Get it at http://code.google.com/p/libdivsufsort/])])

AC_OUTPUT(Makefile)
