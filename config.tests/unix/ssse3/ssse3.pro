SOURCES = ssse3.cpp
CONFIG -= x11 qt
mac:CONFIG -= app_bundle
isEmpty(QMAKE_CFLAGS_SSSE3):error("This compiler does not support SSSE3")
else:QMAKE_CXXFLAGS += $$QMAKE_CFLAGS_SSSE3
