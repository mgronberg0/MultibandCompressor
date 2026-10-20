# Project Name
TARGET = MultibandCompressor

USE_DAISYSP_LGPL = 1

# Sources
SRC_DIR = src
CPP_SOURCES = MultibandCompressor.cpp 
CPP_SOURCES += \
$(SRC_DIR)/butterworth.cpp \
$(SRC_DIR)/crossover.cpp \
$(SRC_DIR)/compressor.cpp

# Library Locations
LIBDAISY_DIR = ../../libDaisy/
DAISYSP_DIR = ../../DaisySP/

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

# newlib-nano's printf (pulled in by --specs=nano.specs above) has no
# floating-point format support by default -- %f/%.0f/etc. silently print
# nothing. UpdateDisplay() relies on float formatting for every number on
# the OLED, so pull the real implementation back in.
LDFLAGS += -Wl,-u,_printf_float