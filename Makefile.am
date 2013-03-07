bin_PROGRAMS = \
  substring-frequencies \
  substring-collect-documents \
  substring-tag-rfc822

AM_CFLAGS = -Wall -g -O3
AM_CXXFLAGS = -Wall -g -O3

substring_frequencies_SOURCES = \
  main.cc \
  libdivsufsort/divsufsort.c \
  libdivsufsort/sssort.c \
  libdivsufsort/trsort.c \
  libdivsufsort/utils.c \
  libdivsufsort/lfs.h \
  libdivsufsort/divsufsort.h

substring_collect_documents_SOURCES = collect.c

substring_tag_rfc822_SOURCES = tag-rfc822.c
