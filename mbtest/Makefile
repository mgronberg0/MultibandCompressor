# Project Name
TARGET = mbtest

# Sources
SRC_DIR = src
CPP_SOURCES = mbtest.cpp 
CPP_SOURCES += \
$(SRC_DIR)/butterworth.cpp \
$(SRC_DIR)/sbcompressor.cpp

C_INCLUDES += -I…/…/DaisySP/DaisySP-LGPL/Source
USE_DAISYSP_LGPL = 1

# Library Locations
LIBDAISY_DIR = ../../libDaisy
DAISYSP_DIR = ../../DaisySP

# Include Paths
INCLUDES = -I$(LIBDAISY_DIR)/core \
           -I$(LIBDAISY_DIR)/drivers \
           -I$(DAISYSP_DIR)/modules \
           -I$(DAISYSP_DIR)/Source \
		   -I$(DAISYSP_DIR)/Source/Utility

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile