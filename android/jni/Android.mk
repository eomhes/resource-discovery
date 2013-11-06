
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := discovery_client

LOCAL_C_INCLUDES := .

LOCAL_SRC_FILES := ../../src/client.c

include $(BUILD_EXECUTABLE)
