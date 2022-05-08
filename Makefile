#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := CoffeeCtrlEsp

idf_component_register(	SRCS "webserver.cpp"
												INCLUDE_DIRS "."
												EMBED_FILES "data/favicon-32x32.png" "data/index.html" "data/upload_script.html")

include $(IDF_PATH)/make/project.mk