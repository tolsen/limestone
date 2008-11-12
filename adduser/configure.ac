#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT(adduser,unknown,[team@limebits.com])
AC_CONFIG_SRCDIR([adduser.c])
#AC_CONFIG_HEADER([config.h])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
NEON_LIBRARY

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([limits.h stdlib.h string.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST

# Checks for library functions.
AC_CHECK_FUNCS([strdup strtol])

AC_OUTPUT(Makefile)
