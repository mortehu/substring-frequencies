bin_PROGRAMS = \
  substring-frequencies \
  substring-collect-documents \
  substring-tag-rfc822
noinst_LIBRARIES = libsubstrings.a
check_PROGRAMS = \
  substrings_test

AM_CFLAGS = -Wall -g -O3
AM_CXXFLAGS = -Wall -g -O3

substring_frequencies_SOURCES = main.cc
substring_frequencies_LDADD = libsubstrings.a

libsubstrings_a_SOURCES = \
  substrings.cc \
  substrings.h \
  libdivsufsort/divsufsort.c \
  libdivsufsort/sssort.c \
  libdivsufsort/trsort.c \
  libdivsufsort/utils.c \
  libdivsufsort/lfs.h \
  libdivsufsort/divsufsort.h

substring_collect_documents_SOURCES = collect.c

substring_tag_rfc822_SOURCES = tag-rfc822.c

substrings_test_SOURCES = substrings_test.cc
substrings_test_LDADD = libsubstrings.a

TESTS = $(check_PROGRAMS)
