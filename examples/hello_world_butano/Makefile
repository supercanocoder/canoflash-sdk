#----------------------------------------------------------------------------
# CanoFlash Net example built with Butano.
#
# Set LIBBUTANO to the butano/butano directory of your Butano checkout, either
# by editing the line below or by exporting it in your environment:
#
#     LIBBUTANO=/path/to/butano/butano make
#
# Butano: https://github.com/GValiente/butano
#----------------------------------------------------------------------------
TARGET      	:=  $(notdir $(CURDIR))
BUILD       	:=  build
LIBBUTANO   	?=  ../../../../butano/butano/butano
PYTHON      	:=  python3

# The SDK: one include directory and one .c file. That is the whole
# integration; everything else here is ordinary Butano boilerplate.
SDK         	:=  ../..
SOURCES     	:=  src $(SDK)/src
INCLUDES    	:=  $(SDK)/include

DATA        	:=
GRAPHICS    	:=  graphics
AUDIO       	:=
DMGAUDIO    	:=
ROMTITLE    	:=  CFHELLO
ROMCODE     	:=  CFHW

USERFLAGS   	:=
USERCXXFLAGS	:=
USERASFLAGS 	:=
USERLDFLAGS 	:=
USERLIBDIRS 	:=
USERLIBS    	:=
DEFAULTLIBS 	:=  yes
STACKTRACE  	:=
USERBUILD   	:=
EXTTOOL     	:=

#----------------------------------------------------------------------------
ifeq ($(strip $(DEVKITARM)),)
$(error "DEVKITARM is not set. Install devkitPro and export DEVKITARM.")
endif

ifndef LIBBUTANOABS
	export LIBBUTANOABS	:=	$(realpath $(LIBBUTANO))
endif

ifeq ($(strip $(LIBBUTANOABS)),)
$(error "LIBBUTANO does not point at a Butano checkout: $(LIBBUTANO)")
endif

include $(LIBBUTANOABS)/butano.mak
